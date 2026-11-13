"""Read scan metadata from PyXRF HDF5 files for the dialog scan table."""
from __future__ import annotations

import logging
import os

import h5py

logger = logging.getLogger(__name__)


def _expand_scan_range(scan_range: str) -> list[int]:
    if not scan_range or not scan_range.strip():
        return []
    ids: set[int] = set()
    for part in scan_range.split(','):
        part = part.strip()
        if not part:
            continue
        try:
            if ':' in part:
                pieces = part.split(':')
                start = int(pieces[0])
                stop = int(pieces[1])
                stride = int(pieces[2]) if len(pieces) > 2 else 1
                if stride == 0:
                    continue
                ids.update(range(start, stop + 1, stride))
            else:
                ids.add(int(part))
        except (ValueError, IndexError):
            # Skip malformed segments rather than aborting the whole parse.
            continue
    return sorted(ids)


def _scan_ids_present(working_directory: str) -> list[int]:
    """Scan IDs for the ``scan2D_<id>.h5`` files in the directory."""
    import glob
    import re

    ids = []
    pattern = re.compile(r'^scan2D_(\d+)\.h5$')
    for path in glob.glob(os.path.join(working_directory, 'scan2D_*.h5')):
        match = pattern.match(os.path.basename(path))
        if match:
            ids.append(int(match.group(1)))
    return sorted(ids)


def expected_scan_ids(working_directory: str,
                      scan_range: str = '') -> list[int]:
    """The scan IDs the table lists, found without opening any file.

    If *scan_range* is empty, these are the ``scan2D_<id>.h5`` files
    already present in the directory, so a directory of downloaded scans
    shows its contents without requiring the range to be typed first
    (matching the ptycho dialog). A range that is given but names no
    scans (a typo, say) lists nothing rather than every file.
    """
    if scan_range and scan_range.strip():
        return _expand_scan_range(scan_range)
    return _scan_ids_present(working_directory)


# Scan metadata by file path, kept with the file's modification time and
# size, so reopening the dialog or changing the filter rereads only the
# files that changed. Opening files on network storage is the slow part.
_cache: dict[str, tuple[tuple[int, int], dict]] = {}


def _open_for_reading(path: str) -> h5py.File:
    # HDF5's file locking is slow on network file systems, and it refuses
    # a file that the batch downloader still has open for writing
    try:
        return h5py.File(path, 'r', locking=False)
    except TypeError:
        # h5py older than 3.5 has no locking argument
        return h5py.File(path, 'r')


def _read_one(path: str) -> dict:
    stat = os.stat(path)
    key = (stat.st_mtime_ns, stat.st_size)
    cached = _cache.get(path)
    if cached is not None and cached[0] == key:
        return cached[1]

    with _open_for_reading(path) as f:
        md = f['xrfmap/scan_metadata']
        scan_id = int(md.attrs['scan_id'])
        theta = float(md.attrs['param_theta'])
        theta_units = md.attrs.get('param_theta_units', 'deg')
        if theta_units == 'mdeg':
            theta /= 1000.0
        theta = round(theta, 3)
        status = str(md.attrs.get('scan_exit_status', ''))
    row = {
        'scan_id': scan_id,
        'theta': theta,
        'status': status if status else 'success',
        'filename': os.path.basename(path),
    }
    _cache[path] = (key, row)
    return row


def read_scan_metadata(working_directory: str,
                       scan_range: str = '') -> list[dict]:
    """Return a list of scan metadata dicts from HDF5 files in *working_directory*.

    Each dict has keys: scan_id, theta, status, filename.

    Any ID in the range without an HDF5 file gets a ``"missing"`` status
    entry, and a file that exists but cannot be read gets a ``"fail"`` entry.
    The list is sorted by scan ID. See ``expected_scan_ids`` for what an
    empty *scan_range* lists.
    """
    wd = working_directory

    expected_ids = expected_scan_ids(wd, scan_range)
    if not expected_ids:
        return []

    # Only read the files for the scan IDs the user asked for, so out-of-range
    # (and possibly unreadable) files are never opened.
    found: dict[int, dict] = {}
    failed_files: dict[int, str] = {}
    for sid in expected_ids:
        path = os.path.join(wd, f'scan2D_{sid}.h5')
        if not os.path.isfile(path):
            continue
        try:
            row = _read_one(path)
            found[row['scan_id']] = row
        except Exception as e:
            failed_files[sid] = os.path.basename(path)
            logger.warning('Failed to read scan %s from %s: %s: %s', sid, path,
                           type(e).__name__, e)

    results = []
    for sid in expected_ids:
        if sid in found:
            results.append(found[sid])
        elif sid in failed_files:
            results.append({
                'scan_id': sid,
                'theta': 0.0,
                'status': 'fail',
                'filename': failed_files[sid],
            })
        else:
            results.append({
                'scan_id': sid,
                'theta': 0.0,
                'status': 'missing',
                'filename': '',
            })

    results.sort(key=lambda r: r['scan_id'])
    return results
