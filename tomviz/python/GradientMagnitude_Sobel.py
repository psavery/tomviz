def transform(dataset):
    """Calculate 3D gradient magnitude using Sobel operator"""

    import numpy as np
    import scipy.ndimage

    array = dataset.active_scalars.astype(np.float32)

    # Transform the dataset
    result = np.empty_like(array)
    scipy.ndimage.generic_gradient_magnitude(
        array, scipy.ndimage.sobel, output=result)

    # Set the result as the new scalars.
    dataset.active_scalars = result
