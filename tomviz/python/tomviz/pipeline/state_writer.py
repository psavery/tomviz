###############################################################################
# This source file is part of the Tomviz project, https://tomviz.org/.
# It is released under the 3-Clause BSD License, see "LICENSE".
###############################################################################
"""Writers for tomviz state containers — the ``.tvh5`` format that
bundles a schema-v2 state JSON with per-port voxel data inside one
HDF5 file. Mirrors the C++ ``Tvh5Format::write`` so files produced
here can be loaded by the in-app pipeline as well as round-tripped
by :func:`tomviz.pipeline.state_io.load_state`."""

import copy
import json
import logging
from pathlib import Path

import h5py
import numpy as np

from tomviz.io_emd import _write_emd_node_into
from tomviz.pipeline.node import SinkNode


logger = logging.getLogger('tomviz')


def write_state_tvh5(target_path, state_json: dict, pipeline) -> None:
    """Write ``state_json`` plus every populated, non-sink output port
    into ``target_path`` as a ``.tvh5`` HDF5 container.

    For every node N with output port P that carries a Dataset payload
    (volume data), the voxels are written under ``/data/<N>/<P>/`` in
    the same EMD layout as a stand-alone ``.emd`` file, and a
    ``dataRef`` entry pointing at that group is stamped onto the
    matching port entry in the JSON before serialization.

    Tables and molecules are skipped with a warning — the tvh5 format
    only persists volume data, matching the C++ side's behavior. If
    the caller wants those leaves on disk, they should request
    ``output_format='state+port'`` so the per-port writers (CSV/XYZ)
    run alongside the tvh5 writer."""
    snapshot = copy.deepcopy(state_json)
    nodes_by_id = {entry['id']: entry for entry in
                   snapshot.get('pipeline', {}).get('nodes') or []
                   if 'id' in entry}

    target_path = Path(target_path)
    with h5py.File(target_path, 'w') as f:
        f.create_group('/data')
        for node in pipeline.nodes:
            if isinstance(node, SinkNode):
                continue
            entry = nodes_by_id.get(node.id)
            if entry is None:
                continue
            outputs = entry.setdefault('outputPorts', {})
            for port in node.output_ports():
                if not port.has_data():
                    continue
                payload = port.data().payload
                if not hasattr(payload, 'arrays'):
                    logger.warning(
                        'tvh5 writer: skipping non-volume port %s.%s '
                        '(port type %r) — only Dataset payloads are '
                        'persisted.',
                        node.label or type(node).__name__, port.name,
                        port.port_type)
                    continue
                node_group_path = f'/data/{node.id}'
                port_group_path = f'{node_group_path}/{port.name}'
                if node_group_path not in f:
                    f.create_group(node_group_path)
                port_group = f.create_group(port_group_path)
                _write_emd_node_into(port_group, payload)
                outputs.setdefault(port.name, {})['dataRef'] = {
                    'container': 'h5',
                    'path': port_group_path,
                }

        # Serialize the (now dataRef-stamped) JSON as an int8 (signed)
        # array at /tomviz_state. The C++ side reads with
        # H5ReadWrite::readData<char>, which strictly H5Tequal-checks
        # the storage type against H5T_STD_I8LE — using uint8 here
        # makes Tvh5Format::readState fall through to an empty state
        # and tomviz then mis-routes the file to LegacyStateLoader.
        # The bytes are identical either way; only the HDF5 type
        # label differs.
        state_bytes = json.dumps(snapshot).encode('utf-8')
        f.create_dataset('tomviz_state',
                         data=np.frombuffer(state_bytes, dtype='i1'))
