import math
from pathlib import Path
import sys

import numpy as np

PathLike = Path | str


def list_ptycho_sids(ptycho_dir: PathLike) -> list[int]:
    ptycho_dir = Path(ptycho_dir)
    if not ptycho_dir.is_dir():
        # It either doesn't exist or it's not a directory
        return []

    sids = []
    for x in ptycho_dir.iterdir():
        if not (x.name.startswith('S') and x.is_dir()):
            continue
        try:
            sids.append(int(x.name[1:]))
        except ValueError:
            continue

    return sorted(sids)


def gather_sid_info(ptycho_dir: PathLike, sid: int) -> dict:
    ptycho_dir = Path(ptycho_dir)
    sid_dir = ptycho_dir / f'S{sid}'

    versions = []
    if sid_dir.is_dir():
        versions = sorted(x.name for x in sid_dir.iterdir() if x.is_dir())

    if not versions:
        # There will be an error, but at least put in 't1'
        versions = ['t1']

    angles = []
    errors = []
    for version in versions:
        angle = load_angle_from_sid(sid, version, ptycho_dir)
        angles.append(angle)
        errors.append(validate_sid(sid, version, ptycho_dir, angle=angle))

    return {
        'versions': versions,
        'angles': angles,
        'errors': errors,
    }


def gather_ptycho_info(ptycho_dir: PathLike) -> dict:
    sid_list = list_ptycho_sids(ptycho_dir)

    version_list = []
    angle_list = []
    error_list = []
    for sid in sid_list:
        info = gather_sid_info(ptycho_dir, sid)
        version_list.append(info['versions'])
        angle_list.append(info['angles'])
        error_list.append(info['errors'])

    return {
        'sid_list': sid_list,
        'version_list': version_list,
        'angle_list': angle_list,
        'error_list': error_list,
    }


def validate_sid(sid: int, version: str, ptycho_dir: PathLike,
                 angle: float | None = None) -> str:
    # Validate the sid and version, that it contains the data and angles.
    # If it is valid, the returned string will be empty. Otherwise, the
    # returned string will contain the error message as to what is not
    # valid.
    # An already-loaded angle may be passed in to avoid reading it again.
    f_path = find_ptycho_file(sid, version, 'object', ptycho_dir)
    if f_path is None:
        return 'Ptycho data missing'

    g_path = find_ptycho_file(sid, version, 'probe', ptycho_dir)
    if g_path is None:
        return 'Probe data missing'

    if angle is None:
        angle = load_angle_from_sid(sid, version, ptycho_dir)
    if np.isnan(angle):
        return 'Angle not found'

    return ''


def find_ptycho_file(sid: int, version: str, type_str: str,
                     ptycho_dir: PathLike) -> Path | None:
    # type_str is `ptycho` or `probe`
    ptycho_dir = Path(ptycho_dir)
    dir_path = ptycho_dir / f'S{sid}/{version}/recon_data'
    base_str = f'recon_{sid}_{version}_{type_str}'
    # Prefer `_ave.npy` if available, then `.npy`
    suffix_to_try = [
        '_ave.npy',
        '.npy',
    ]
    for suffix in suffix_to_try:
        path = dir_path / f'{base_str}{suffix}'
        if path.exists():
            return path

    # If those didn't exist, just try to grab anything that
    # matches `{base_str}*.npy`
    paths = list(dir_path.glob(f'{base_str}*.npy'))
    if paths:
        return paths[0].resolve()

    # Didn't find any matches
    return None


def load_angle_from_sid(sid: int, version: str,
                        ptycho_dir: PathLike) -> float:
    path = locate_ptycho_hyan_file(sid, version, ptycho_dir)
    if path is None:
        return math.nan

    return fetch_angle_from_ptycho_hyan_file(path)


# Files with these suffixes cannot be the small text config that
# carries the angle. Skipping them avoids reading large binary arrays
# (potentially over a network file system) during a directory scan.
NON_CONFIG_SUFFIXES = {
    '.npy', '.npz', '.h5', '.hdf5', '.nxs', '.tif', '.tiff',
    '.png', '.jpg', '.jpeg', '.mat',
}

# The config is a small text file; never read more than this much of
# any candidate while searching for it.
MAX_CONFIG_READ_BYTES = 1024 * 1024


def locate_ptycho_hyan_file(sid: int, version: str,
                            ptycho_dir: PathLike) -> str | None:
    recon_data_dir = Path(ptycho_dir) / f'S{sid}' / version / 'recon_data'
    matches = list(recon_data_dir.glob(f'{sid}_{version}*'))

    for match in matches:
        if match.suffix.lower() in NON_CONFIG_SUFFIXES:
            continue
        try:
            with open(match.resolve(), 'r', encoding='utf-8',
                      errors='replace') as rf:
                if 'angle =' in rf.read(MAX_CONFIG_READ_BYTES):
                    return match.resolve()
        except Exception:
            # Move on to the next one
            continue

    return None


def get_use_and_versions_from_csv(csv_path: str) -> dict:
    data = np.genfromtxt(csv_path, delimiter=',', names=True, dtype=None)

    sids = []
    try:
        sids = data['Scan_ID'].tolist()
    except Exception:
        print('"Scan_ID" column not found in CSV file', file=sys.stderr)

    use = []
    try:
        use = data['Use'].tolist()
        # Convert to boolean
        use = [x in (1, '1', 'x') for x in use]
    except Exception:
        print('"Use" column not found in CSV file', file=sys.stderr)

    versions = []
    try:
        versions = data['Version'].tolist()
    except Exception:
        print('"Version" column not found in CSV file', file=sys.stderr)

    return {
        'sids': sids,
        'use': use,
        'versions': versions,
    }


def filter_sid_list(sid_list: list[int], filter_string: str) -> list[int]:
    if not filter_string.strip():
        # All SIDs are valid
        return list(sid_list)

    # Either a comma-delimited list or numpy slicing
    sid_strings = filter_string.split(',')
    valid_sids = []
    for this_str in sid_strings:
        if ':' in this_str:
            this_slice = slice(
                *(int(s) if s else None for s in this_str.split(':'))
            )
            # If there was no stop specified, go to the end of the sid_list
            if this_slice.stop is None:
                this_slice = slice(this_slice.start, max(sid_list) + 1,
                                   this_slice.step)
            else:
                # Unlike numpy, we want to be inclusive of the last number
                this_slice = slice(this_slice.start, this_slice.stop + 1,
                                   this_slice.step)

            valid_sids += np.r_[this_slice].tolist()
        else:
            valid_sids.append(int(this_str))

    return [sid for sid in sid_list if sid in valid_sids]


def fetch_angle_from_ptycho_hyan_file(filepath: PathLike) -> float | None:
    with open(filepath, 'r', encoding='utf-8') as rf:
        for line in rf:
            line = line.lstrip()
            if line.startswith('angle = '):
                angle = float(line.split('=')[1].strip())
                return angle

    # Angle was not found
    return math.nan


def fetch_pixel_sizes_from_ptycho_hyan_file(
    filepath: PathLike,
) -> tuple[float, float] | None:
    print(f'Obtaining pixel sizes from config file: {filepath}')
    vars_required = [
        'lambda_nm', 'z_m', 'nx', 'ny', 'ccd_pixel_um'
    ]
    alternatives = {
        'nx': 'x_arr_size',
        'ny': 'y_arr_size',
    }
    vars_requested = vars_required + list(alternatives.values())
    results = {}
    try:
        with open(filepath, 'r', encoding='utf-8') as rf:
            for line in rf:
                if '=' not in line:
                    continue

                lhs = line.split('=')[0].strip()
                if lhs in vars_requested:
                    value = float(line.split('=', 1)[1].strip())
                    results[lhs] = value
    except Exception as e:
        print('Failed to fetch pixel sizes with error:', e, file=sys.stderr)
        return None

    # Add alternatives if they are present
    for name in vars_required:
        if name not in results and name in alternatives:
            # Check the alt_name
            alt_name = alternatives[name]
            if alt_name in results:
                # Convert it
                results[name] = results.pop(alt_name)

    missing = [x for x in vars_required if x not in results]
    if missing:
        print(
            'Failed to fetch pixel sizes. Some required variables '
            f'were not found: {missing}'
        )
        return None

    # Now compute them. They can both use the same numerator
    numerator = (
        results['lambda_nm'] * results['z_m'] * 1e6 / results['ccd_pixel_um']
    )

    x_pixel_size = numerator / results['nx']
    y_pixel_size = numerator / results['ny']

    return x_pixel_size, y_pixel_size
