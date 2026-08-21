###############################################################################
# This source file is part of the Tomviz project, https://tomviz.org/.
# It is released under the 3-Clause BSD License, see "LICENSE".
###############################################################################
"""Tests for the schema-v2 Python node runtime — the CLI side of
``tomviz.pipeline.transforms.python_transform.PythonTransform`` and
``tomviz.pipeline.sources.python_source.PythonSource`` — plus the
abstract Dataset helpers (``apply_to_each_scalar_array``,
``empty_copy``) used heavily by v2 operator authors.

The C++ side of the same code paths is covered by the
PipelinePythonTest gtest cases (``PythonTransformV2``, etc.); this
file mirrors them on the Python runtime so the two sides don't drift.
"""

import json

import numpy as np
import pytest

from tomviz.external_dataset import Dataset, LegacyDataset
from tomviz.pipeline import PortData, register_builtins
from tomviz.pipeline.sources.python_source import PythonSource
from tomviz.pipeline.transforms.python_transform import PythonTransform


@pytest.fixture(autouse=True)
def _builtins():
    register_builtins()


def _multi_array_dataset():
    """Build an external Dataset with two scalar arrays so the
    apply_to_each / filter / active-preserve paths can all be
    exercised against the same fixture."""
    ds = Dataset({'a': np.array([1.0, 2.0, 3.0]),
                  'b': np.array([10.0, 20.0, 30.0])}, active='a')
    ds.spacing = (0.5, 0.5, 0.5)
    ds.tilt_axis = 1
    return ds


# ============================================================
# Dataset.apply_to_each_scalar_array (return-new + filter)
# ============================================================


def test_apply_to_each_scalar_array_returns_new_dataset():
    src = _multi_array_dataset()
    out = src.apply_to_each_scalar_array(lambda a: a * 2)

    assert out is not src
    # Source untouched
    np.testing.assert_array_equal(src.arrays['a'], [1.0, 2.0, 3.0])
    np.testing.assert_array_equal(src.arrays['b'], [10.0, 20.0, 30.0])
    # Output transformed
    np.testing.assert_array_equal(out.arrays['a'], [2.0, 4.0, 6.0])
    np.testing.assert_array_equal(out.arrays['b'], [20.0, 40.0, 60.0])


def test_apply_to_each_scalar_array_preserves_metadata():
    src = _multi_array_dataset()
    out = src.apply_to_each_scalar_array(lambda a: a + 1)

    assert tuple(out.spacing) == (0.5, 0.5, 0.5)
    assert out.tilt_axis == 1
    assert out.active_name == 'a'
    assert sorted(out.scalars_names) == ['a', 'b']


def test_apply_to_each_scalar_array_accepts_in_place_mutation():
    src = _multi_array_dataset()

    def in_place(arr):
        arr += 100
        return arr

    out = src.apply_to_each_scalar_array(in_place)
    np.testing.assert_array_equal(out.arrays['a'], [101.0, 102.0, 103.0])


def test_apply_to_each_scalar_array_filters_on_none():
    src = _multi_array_dataset()
    # Drop 'b' by returning None for it.
    out = src.apply_to_each_scalar_array(
        lambda a: None if a[0] == 10.0 else a * 2)

    assert out.scalars_names == ['a']
    np.testing.assert_array_equal(out.arrays['a'], [2.0, 4.0, 6.0])
    assert out.active_name == 'a'


def test_apply_to_each_scalar_array_filters_active_falls_back():
    """When fn filters out the active array, the next remaining
    scalar should become the new active."""
    src = _multi_array_dataset()
    out = src.apply_to_each_scalar_array(
        lambda a: None if a[0] == 1.0 else a)

    assert out.scalars_names == ['b']
    assert out.active_name == 'b'


def test_apply_to_each_scalar_array_filters_all_returns_empty():
    src = _multi_array_dataset()
    out = src.apply_to_each_scalar_array(lambda a: None)

    assert out.scalars_names == []
    # Source untouched
    assert sorted(src.scalars_names) == ['a', 'b']


def test_apply_to_each_scalar_array_preserves_concrete_type():
    """LegacyDataset in → LegacyDataset out (so chained v1 operators
    keep create_child_dataset access)."""
    src = LegacyDataset({'a': np.array([1.0])}, active='a')
    out = src.apply_to_each_scalar_array(lambda a: a * 2)
    assert isinstance(out, LegacyDataset)


# ============================================================
# Dataset.empty_copy
# ============================================================


def test_empty_copy_preserves_metadata_drops_arrays():
    src = _multi_array_dataset()
    out = src.empty_copy()

    assert out is not src
    assert out.scalars_names == []
    assert tuple(out.spacing) == (0.5, 0.5, 0.5)
    assert out.tilt_axis == 1
    # Source untouched
    assert sorted(src.scalars_names) == ['a', 'b']


def test_empty_copy_preserves_concrete_type():
    src = LegacyDataset({'a': np.array([1.0])}, active='a')
    out = src.empty_copy()
    assert isinstance(out, LegacyDataset)


# ============================================================
# PythonTransform CLI runtime: end-to-end execute
# ============================================================


def _multiply_v2_description(persistent=True):
    return json.dumps({
        'schemaVersion': 2,
        'name': 'MultiplyBy',
        'label': 'Multiply By',
        'inputs':  [{'name': 'volume', 'type': 'ImageData'}],
        'outputs': [{'name': 'volume', 'type': 'ImageData',
                     'persistent': persistent}],
        'parameters': [{'name': 'factor', 'type': 'double',
                        'default': 1.0}],
    })


_MULTIPLY_V2_SCRIPT = """
import tomviz.nodes


class MultiplyBy(tomviz.nodes.TransformNode):
    def transform(self, inputs, factor=1.0):
        ds = inputs["volume"]
        return {"volume": ds.apply_to_each_scalar_array(lambda a: a * factor)}
"""


def test_python_transform_v2_executes_end_to_end():
    transform = PythonTransform()
    transform.set_json_description(_multiply_v2_description())
    transform.script = _MULTIPLY_V2_SCRIPT
    transform._backend.parameters['factor'] = 3.0

    src = Dataset({'a': np.array([1.0, 2.0, 3.0])}, active='a')
    inputs = {'volume': PortData(src, 'ImageData')}

    outputs = transform.transform(inputs)
    assert 'volume' in outputs
    out_ds = outputs['volume'].payload
    np.testing.assert_array_equal(out_ds.arrays['a'], [3.0, 6.0, 9.0])
    # Apply-helper return-new: source unchanged.
    np.testing.assert_array_equal(src.arrays['a'], [1.0, 2.0, 3.0])


def test_python_transform_v2_label_and_ports_from_json():
    """Setting the JSON description should populate the host's label
    and create the input/output ports."""
    transform = PythonTransform()
    transform.set_json_description(_multiply_v2_description())

    assert transform.label == 'Multiply By'
    assert [p.name for p in transform.input_ports()] == ['volume']
    assert [p.name for p in transform.output_ports()] == ['volume']


def test_python_transform_v2_none_return_signals_failure():
    """A transform that returns None should produce no outputs — the
    backend collapses non-dict (including None) to an empty result."""
    transform = PythonTransform()
    transform.set_json_description(_multiply_v2_description())
    transform.script = """
import tomviz.nodes

class Refuse(tomviz.nodes.TransformNode):
    def transform(self, inputs, factor=1.0):
        return  # None — signals "no output produced"
"""

    src = Dataset({'a': np.array([1.0])}, active='a')
    inputs = {'volume': PortData(src, 'ImageData')}
    outputs = transform.transform(inputs)
    assert outputs == {}


def test_python_transform_v2_exception_signals_failure():
    """A transform that raises should be caught and produce no
    outputs (logged at the runtime)."""
    transform = PythonTransform()
    transform.set_json_description(_multiply_v2_description())
    transform.script = """
import tomviz.nodes

class Boom(tomviz.nodes.TransformNode):
    def transform(self, inputs, factor=1.0):
        raise RuntimeError("intentional")
"""

    src = Dataset({'a': np.array([1.0])}, active='a')
    inputs = {'volume': PortData(src, 'ImageData')}
    outputs = transform.transform(inputs)
    assert outputs == {}


def test_python_transform_v2_supports_cancel_flag_from_json():
    transform = PythonTransform()
    transform.set_json_description(json.dumps({
        'schemaVersion': 2,
        'name': 'Demo',
        'inputs':  [{'name': 'volume', 'type': 'ImageData'}],
        'outputs': [{'name': 'volume', 'type': 'ImageData'}],
        'supportsCancel': True,
        'supportsComplete': True,
    }))
    assert transform._backend.supports_cancel is True
    assert transform._backend.supports_complete is True


# ============================================================
# PythonSource CLI runtime: end-to-end produce
# ============================================================


def _constant_source_description():
    return json.dumps({
        'schemaVersion': 2,
        'name': 'ConstantSource',
        'label': 'Constant Source',
        'inputs': [],
        'outputs': [{'name': 'volume', 'type': 'ImageData',
                     'persistent': True}],
        'parameters': [{'name': 'value', 'type': 'double',
                        'default': 0.0}],
    })


_CONSTANT_SOURCE_SCRIPT = """
import numpy as np
import tomviz.nodes
from tomviz.external_dataset import Dataset


class ConstantSource(tomviz.nodes.SourceNode):
    def produce(self, value=0.0):
        arr = np.full((2, 2, 2), value, dtype=np.float32)
        return {"volume": Dataset({"Scalars": arr}, active="Scalars")}
"""


def test_python_source_v2_executes_end_to_end():
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = _CONSTANT_SOURCE_SCRIPT
    source._backend.parameters['value'] = 7.0

    assert source.execute() is True
    port = source.output_port('volume')
    assert port.has_data()
    out_ds = port.data().payload
    assert out_ds.scalars('Scalars').shape == (2, 2, 2)
    np.testing.assert_array_equal(
        out_ds.scalars('Scalars'),
        np.full((2, 2, 2), 7.0, dtype=np.float32))


def test_python_source_v2_none_return_fails_execute():
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = """
import tomviz.nodes

class Refuse(tomviz.nodes.SourceNode):
    def produce(self, value=0.0):
        return  # None — no output produced
"""
    # outputs declared but produce() returned None → execute() returns
    # False (no outputs produced for declared ports).
    assert source.execute() is False
    assert not source.output_port('volume').has_data()


# ============================================================
# Schema validation
# ============================================================


def test_python_transform_v2_persistent_default_false():
    """When the JSON omits `persistent` for an output, the schema-v2
    convention is `false` (transient) — the backend must call
    setPersistent(false) explicitly."""
    transform = PythonTransform()
    transform.set_json_description(json.dumps({
        'schemaVersion': 2,
        'name': 'Demo',
        'inputs':  [{'name': 'volume', 'type': 'ImageData'}],
        'outputs': [{'name': 'volume', 'type': 'ImageData'}],  # no persistent
    }))
    assert transform.output_port('volume').persistent is False


def test_python_transform_v2_persistent_explicit_true():
    transform = PythonTransform()
    transform.set_json_description(json.dumps({
        'schemaVersion': 2,
        'name': 'Demo',
        'inputs':  [{'name': 'volume', 'type': 'ImageData'}],
        'outputs': [{'name': 'volume', 'type': 'ImageData',
                     'persistent': True}],
    }))
    assert transform.output_port('volume').persistent is True


# ============================================================
# Periodic execution: self.state + should_auto_execute
# ============================================================


_COUNTING_SOURCE_SCRIPT = """
import numpy as np
import tomviz.nodes
from tomviz.external_dataset import Dataset


class CountingSource(tomviz.nodes.SourceNode):
    def produce(self, value=0.0):
        self.state['runs'] = self.state.get('runs', 0) + 1
        arr = np.full((2, 2, 2), value, dtype=np.float32)
        return {"volume": Dataset({"Scalars": arr}, active="Scalars")}
"""


_WATCHER_SCRIPT = """
import tomviz.nodes


class Watcher(tomviz.nodes.SourceNode):
    def produce(self, value=0.0):
        return None

    def should_auto_execute(self, value=0.0):
        if value != 7.5:
            raise ValueError('parameters not forwarded')
        polls = self.state.get('polls', 0) + 1
        self.state['polls'] = polls
        return polls >= 2
"""


def test_v2_state_persists_across_runs():
    """A fresh user instance runs each execution, so the counter only
    grows if self.state round-trips through the host node."""
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = _COUNTING_SOURCE_SCRIPT

    assert source.execute() is True
    assert source.user_state == {'runs': 1}
    assert source.execute() is True
    assert source.user_state == {'runs': 2}


def test_should_auto_execute_default_false():
    """Scripts without the hook fall back to the base class's False."""
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = _CONSTANT_SOURCE_SCRIPT

    assert source._backend.run_should_auto_execute(source) is False
    assert source.user_state == {}


def test_should_auto_execute_hook_and_state():
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = _WATCHER_SCRIPT
    source._backend.parameters['value'] = 7.5

    # First poll answers no, but its state mutation is kept.
    assert source._backend.run_should_auto_execute(source) is False
    assert source.user_state == {'polls': 1}

    # Second poll sees the previous state and answers yes.
    assert source._backend.run_should_auto_execute(source) is True
    assert source.user_state == {'polls': 2}


def test_should_auto_execute_exception_answers_false():
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = _WATCHER_SCRIPT
    # Default value=0.0 trips the hook's parameter check.

    assert source._backend.run_should_auto_execute(source) is False


def test_should_auto_execute_rebound_state_is_harvested():
    """Rebinding self.state (rather than mutating it) must land too."""
    source = PythonSource()
    source.set_json_description(_constant_source_description())
    source.script = """
import tomviz.nodes

class Rebinder(tomviz.nodes.SourceNode):
    def produce(self, value=0.0):
        return None

    def should_auto_execute(self, value=0.0):
        self.state = {'fresh': True}
        return False
"""
    assert source._backend.run_should_auto_execute(source) is False
    assert source.user_state == {'fresh': True}


def _watcher_state_file(tmp_path):
    state = {
        'schemaVersion': 2,
        'pipeline': {
            'nextNodeId': 2,
            'nodes': [{
                'id': 1,
                'type': 'source.python',
                'label': 'Watcher',
                'description': _constant_source_description(),
                'script': _WATCHER_SCRIPT,
                'arguments': {'value': 7.5},
            }],
            'links': [],
        },
    }
    path = tmp_path / 'state.tvsm'
    path.write_text(json.dumps(state))
    return path


def test_check_auto_execute_round_trip(tmp_path):
    """The CLI check mode the external executor drives: evaluate the
    hook without executing, write the verdict and the updated state
    bags, and accept a prior bag via node_state_file."""
    from tomviz.pipeline.runner import check_auto_execute

    state_path = _watcher_state_file(tmp_path)

    out1 = tmp_path / 'out1'
    assert check_auto_execute(state_path, out1, 1) is False
    verdict = json.loads((out1 / 'auto_execute.json').read_text())
    assert verdict == {'shouldExecute': False}
    bags = json.loads((out1 / 'node_state.json').read_text())
    assert bags == {'nodes': {'1': {'polls': 1}}}

    # Feeding the first poll's state back makes the second answer yes —
    # exactly the round trip the parent app performs between polls.
    out2 = tmp_path / 'out2'
    assert check_auto_execute(
        state_path, out2, 1,
        node_state_file=str(out1 / 'node_state.json')) is True
    bags2 = json.loads((out2 / 'node_state.json').read_text())
    assert bags2 == {'nodes': {'1': {'polls': 2}}}


def test_check_auto_execute_unknown_node_answers_false(tmp_path):
    from tomviz.pipeline.runner import check_auto_execute

    state_path = _watcher_state_file(tmp_path)
    out = tmp_path / 'out'
    assert check_auto_execute(state_path, out, 99) is False
    verdict = json.loads((out / 'auto_execute.json').read_text())
    assert verdict == {'shouldExecute': False}


def test_run_writes_node_state_file(tmp_path):
    """`run --node-state`: bags are installed before the run and the
    updated bags are written back next to the outputs."""
    from tomviz.pipeline.runner import run

    state = {
        'schemaVersion': 2,
        'pipeline': {
            'nextNodeId': 2,
            'nodes': [{
                'id': 1,
                'type': 'source.python',
                'label': 'Counter',
                'description': _constant_source_description(),
                'script': _COUNTING_SOURCE_SCRIPT,
                'arguments': {'value': 1.0},
            }],
            'links': [],
        },
    }
    state_path = tmp_path / 'state.tvsm'
    state_path.write_text(json.dumps(state))

    in_bag = tmp_path / 'node_state_in.json'
    in_bag.write_text(json.dumps({'nodes': {'1': {'runs': 41}}}))

    out = tmp_path / 'out'
    run(state_path, out, output_format='port',
        node_state_file=str(in_bag))

    bags = json.loads((out / 'node_state.json').read_text())
    assert bags == {'nodes': {'1': {'runs': 42}}}
