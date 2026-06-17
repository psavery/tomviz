from pathlib import Path

import h5py

from .scan_metadata import _expand_scan_range


def ic_names(working_directory, scan_range=''):
    """Return the scaler ("ic") names from an HDF5 file in *working_directory*.

    If *scan_range* is provided, only files whose scan IDs fall within the
    range are considered, so out-of-range (and possibly unreadable) files are
    never opened.  Any file that cannot be opened or that lacks the expected
    structure is skipped, and the next candidate is tried.  The names from the
    first readable file are returned, since the scaler names are the same
    across the scans in a series.

    This function never raises: if no readable file yields names, an empty
    list is returned.
    """
    wd = Path(working_directory)
    if not wd.is_dir():
        return []

    expected_ids = _expand_scan_range(scan_range) if scan_range else []
    if expected_ids:
        # Only look at the files the user actually asked for.
        candidates = [wd / f'scan2D_{sid}.h5' for sid in expected_ids]
        candidates = [f for f in candidates if f.is_file()]
    else:
        # No range given: consider every HDF5 file, in a deterministic order.
        candidates = sorted(x for x in wd.iterdir() if x.suffix == '.h5')

    for f in candidates:
        try:
            with h5py.File(f, 'r') as rf:
                names = rf["xrfmap"]["scalers"]["name"]
                return [x.decode() for x in names]
        except Exception:
            # Unreadable (e.g. permission denied) or unexpected structure -
            # skip it and try the next candidate rather than failing outright.
            continue

    return []
