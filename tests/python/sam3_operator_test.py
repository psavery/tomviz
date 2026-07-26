import importlib.util
import json
import os

import numpy as np
import pytest

_PYTHON_DIR = os.path.join(
    os.path.dirname(__file__), '..', '..', 'tomviz', 'python')


def _load_module():
    path = os.path.join(_PYTHON_DIR, 'SAM3Segment3D.py')
    spec = importlib.util.spec_from_file_location('SAM3Segment3D', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_json_description():
    path = os.path.join(_PYTHON_DIR, 'SAM3Segment3D.json')
    with open(path) as f:
        desc = json.load(f)

    assert desc['name'] == 'SAM3Segment3D'
    assert desc['externalOnly'] is True
    assert desc['externalCompatible'] is True
    assert 'tomviz_pipeline_env' not in desc

    names = [p['name'] for p in desc['parameters']]
    assert 'text_prompt' in names
    assert 'vote_threshold' in names
    assert 'checkpoint_path' in names


def test_module_imports_without_sam3():
    # Heavy imports (torch, sam3, PIL, scipy) live inside transform() or
    # the helpers, so loading the module must succeed in the application
    # environment.
    module = _load_module()
    assert hasattr(module, 'SAM3Segment3D')


def test_transform_raises_install_instructions_without_sam3():
    try:
        import sam3  # noqa: F401
        pytest.skip('sam3 is installed; ImportError path not reachable')
    except ImportError:
        pass

    module = _load_module()
    op = module.SAM3Segment3D.__new__(module.SAM3Segment3D)

    class Progress:
        maximum = 0
        value = 0
        message = ''

    op.progress = Progress()

    with pytest.raises(ImportError) as excinfo:
        op.transform(dataset=None)

    text = str(excinfo.value)
    assert 'conda env create' in text
    assert 'sam3-tomviz-cuda.yml' in text
    assert 'Execution' in text


def test_vote():
    module = _load_module()
    a = np.zeros((2, 2, 2), dtype=bool)
    b = np.zeros((2, 2, 2), dtype=bool)
    c = np.zeros((2, 2, 2), dtype=bool)
    a[0, 0, 0] = b[0, 0, 0] = c[0, 0, 0] = True  # 3 votes
    a[1, 1, 1] = b[1, 1, 1] = True               # 2 votes
    c[0, 1, 0] = True                            # 1 vote

    out = module._vote({'xy': a, 'xz': b, 'yz': c}, threshold=2)
    assert out.dtype == np.uint8
    assert out[0, 0, 0] == 1
    assert out[1, 1, 1] == 1
    assert out[0, 1, 0] == 0


def test_stitch_instances():
    scipy = pytest.importorskip('scipy')  # noqa: F841
    module = _load_module()

    binary = np.zeros((12, 12, 12), dtype=np.uint8)
    binary[1:5, 1:5, 1:5] = 1    # 64 voxels, kept
    binary[9:11, 9:11, 9:11] = 1  # 8 voxels, filtered out

    labels = module._stitch_instances(binary, min_voxels=20)
    assert labels.dtype == np.int32
    assert labels.max() == 1
    assert labels[2, 2, 2] == 1
    assert labels[9, 9, 9] == 0


def test_norm_to_uint8():
    module = _load_module()
    vol = np.linspace(0.0, 1.0, 1000, dtype=np.float32).reshape(10, 10, 10)
    u8 = module._norm_to_uint8(vol)
    assert u8.dtype == np.uint8
    assert u8.min() == 0
    assert u8.max() == 255
    # Constant volumes must not divide by zero.
    flat = module._norm_to_uint8(np.ones((4, 4, 4), dtype=np.float32))
    assert flat.max() == 0
