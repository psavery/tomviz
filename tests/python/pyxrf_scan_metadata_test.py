"""The PyXRF dialog's scan table: which scans a range lists."""
import importlib.util
from pathlib import Path

import h5py
import pytest

# Loaded from its file: importing the tomviz.pyxrf package pulls in the
# PyXRF stack, which this module does not need
_path = (Path(__file__).parents[2] / 'tomviz' / 'python' / 'tomviz' /
         'pyxrf' / 'scan_metadata.py')
_spec = importlib.util.spec_from_file_location('scan_metadata', _path)
scan_metadata = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(scan_metadata)


# Only an empty range lists the scans in the directory; a range that
# names no scans (a typo, say) lists nothing rather than every file
@pytest.mark.parametrize('scan_range, expected', [
    ('', [1, 2]),
    ('  ', [1, 2]),
    ('abc', []),
    ('5:1', []),
])
def test_only_an_empty_range_lists_the_scans_present(tmp_path, scan_range,
                                                     expected):
    for scan_id in (1, 2):
        with h5py.File(tmp_path / f'scan2D_{scan_id}.h5', 'w') as f:
            md = f.create_group('xrfmap/scan_metadata')
            md.attrs['scan_id'] = scan_id
            md.attrs['param_theta'] = 0.0

    rows = scan_metadata.read_scan_metadata(str(tmp_path), scan_range)
    assert [row['scan_id'] for row in rows] == expected


# Files are cached by modification time and size, so a rewritten file
# (a scan downloaded again, say) is read again
def test_a_rewritten_file_is_read_again(tmp_path: Path) -> None:
    path = tmp_path / 'scan2D_1.h5'
    for theta in (10.0, 20.0):
        with h5py.File(path, 'w') as f:
            md = f.create_group('xrfmap/scan_metadata')
            md.attrs['scan_id'] = 1
            md.attrs['param_theta'] = theta
            md.attrs['note'] = 'x' * int(theta)

        rows = scan_metadata.read_scan_metadata(str(tmp_path), '1')
        assert rows[0]['theta'] == theta
