def transform(dataset, centers='', centers_file='', radius=15.0, sigma=5.0,
              include_friedel_mates=True):
    """Keep only the reciprocal-space volumes around the given peaks.

    The dataset is Fourier transformed, multiplied by soft spherical
    windows centered on each peak (plus, optionally, their Friedel
    mates mirrored through the spectrum center), and transformed back;
    the real part is kept. This is the equivalent of selected-area
    electron diffraction: only the periodicities represented by the
    chosen peaks survive.

    Peak coordinates are voxel indices into the centered (fftshift-ed)
    spectrum, exactly as displayed by the "FFT (abs log)" operator: run
    that on a copy of the data, find each peak's (x, y, z) index, and
    enter one peak per line (or semicolon) as "x, y, z". A CSV file
    with one x,y,z row per peak (extra columns and header lines are
    ignored) can be used instead.
    """
    import numpy as np
    from scipy.special import erf

    array = dataset.active_scalars
    if array is None:
        raise RuntimeError('No data array found!')

    peaks = _parse_centers(centers, centers_file)
    if not peaks:
        raise RuntimeError(
            'No peak centers given. Enter one "x, y, z" per line, or '
            'select a CSV file with one x,y,z row per peak.')

    array = np.nan_to_num(np.asarray(array, dtype=np.float64))
    shape = array.shape
    for peak in peaks:
        if len(peak) != len(shape):
            raise RuntimeError(f'Peak {peak} does not have '
                               f'{len(shape)} coordinates')
        if any(c < 0 or c >= n for c, n in zip(peak, shape)):
            raise RuntimeError(f'Peak {peak} is outside the data '
                               f'extents {tuple(shape)}')

    # The spectrum is Hermitian-symmetric, so every peak has a mate
    # mirrored through the DC bin (n // 2 after fftshift). Peaks close
    # enough to the center that their own window already covers the
    # mate are not mirrored again.
    dc = [n // 2 for n in shape]
    if include_friedel_mates:
        mates = []
        for peak in peaks:
            mate = [2 * c - p for c, p in zip(dc, peak)]
            distance = np.sqrt(sum((m - p)**2 for m, p in zip(mate, peak)))
            if distance > radius:
                mates.append(mate)
        peaks = peaks + mates

    grids = np.meshgrid(*(np.arange(n, dtype=np.float64) for n in shape),
                        indexing='ij')
    window = np.zeros(shape)
    for peak in peaks:
        dist = np.sqrt(sum((g - c)**2 for g, c in zip(grids, peak)))
        window += 0.5 * (erf((dist + radius) / (np.sqrt(2) * sigma)) -
                         erf((dist - radius) / (np.sqrt(2) * sigma)))
    # Overlapping windows must not amplify the spectrum
    np.clip(window, 0.0, 1.0, out=window)

    spectrum = np.fft.fftshift(np.fft.fftn(array))
    result = np.real(np.fft.ifftn(np.fft.ifftshift(spectrum * window)))

    dataset.active_scalars = np.asfortranarray(result.astype(np.float32))


def _parse_centers(centers, centers_file):
    """Peak triplets from the text parameter and/or the CSV file."""
    rows = []
    if centers:
        for chunk in centers.replace(';', '\n').splitlines():
            rows.append(chunk)
    if centers_file:
        with open(centers_file, 'r', encoding='utf-8-sig',
                  errors='replace') as rf:
            rows.extend(rf.read().splitlines())

    peaks = []
    for row in rows:
        fields = row.replace(',', ' ').split()
        if len(fields) < 3:
            continue
        try:
            peaks.append([float(f) for f in fields[:3]])
        except ValueError:
            # Header or comment line
            continue
    return peaks
