def transform(dataset, second_dataset, operation=0, normalize_first=False):
    """Combine two datasets arithmetically, voxel by voxel.

    The primary dataset's active array is combined with the second
    dataset's active array (subtract, add, multiply, or divide) and the
    result replaces the primary's active array. With normalization on,
    each array is first divided by its own mean intensity, so datasets
    on different intensity scales (e.g. two elements, or two
    reconstructions) can be compared directly.
    """
    import numpy as np

    primary = dataset.active_scalars
    secondary = second_dataset.active_scalars

    if primary is None or secondary is None:
        raise RuntimeError('Both datasets must have an active scalar array')

    if primary.shape != secondary.shape:
        raise RuntimeError(
            'Datasets must have the same shape: '
            f'{tuple(primary.shape)} vs {tuple(secondary.shape)}')

    a = np.asarray(primary, dtype=np.float64)
    b = np.asarray(secondary, dtype=np.float64)

    if normalize_first:
        a_mean = a.mean()
        b_mean = b.mean()
        if a_mean == 0 or b_mean == 0:
            raise RuntimeError(
                'Cannot normalize: one of the datasets has zero mean')
        a = a / a_mean
        b = b / b_mean

    if operation == 0:
        result = a - b
    elif operation == 1:
        result = a + b
    elif operation == 2:
        result = a * b
    else:
        # Divide: voxels where the divisor is zero become zero rather
        # than inf/nan, so the result stays renderable.
        result = np.divide(a, b, out=np.zeros_like(a), where=b != 0)

    dataset.active_scalars = np.asfortranarray(result.astype(np.float32))
