###############################################################################
# This source file is part of the Tomviz project, https://tomviz.org/.
# It is released under the 3-Clause BSD License, see "LICENSE".
###############################################################################
"""Per-transform parity tests for the trivial C++ transforms ported to
NumPy. Each test exercises the Python implementation against the
documented C++ semantics taken from tomviz/pipeline/transforms/*.cxx."""

import numpy as np
import pytest

from tomviz.external_dataset import Dataset
from tomviz.pipeline import PortData, register_builtins
from tomviz.pipeline.transforms.convert_to_float import (
    ConvertToFloatTransform,
)
from tomviz.pipeline.transforms.convert_to_volume import (
    ConvertToVolumeTransform,
)
from tomviz.pipeline.transforms.crop import CropTransform
from tomviz.pipeline.transforms.set_tilt_angles import (
    SetTiltAnglesTransform,
)
from tomviz.pipeline.transforms.threshold import ThresholdTransform
from tomviz.pipeline.transforms.transpose import TransposeDataTransform


@pytest.fixture(autouse=True)
def _builtins():
    register_builtins()


def _make_dataset(arr, name='ImageScalars'):
    ds = Dataset({name: arr}, name)
    ds.spacing = (1.0, 1.0, 1.0)
    return ds


def _run(transform, dataset, port_type='ImageData'):
    inputs = {'volume': PortData(dataset, port_type)}
    return transform.transform(inputs)


# ---- ConvertToFloat -----------------------------------------------------


def test_convert_to_float_casts_int_array():
    arr = np.arange(24, dtype=np.uint8).reshape((2, 3, 4))
    out = _run(ConvertToFloatTransform(), _make_dataset(arr))
    out_arr = out['output'].payload.active_scalars
    assert out_arr.dtype == np.float32
    np.testing.assert_array_equal(out_arr, arr.astype(np.float32))


# ---- ConvertToVolume ----------------------------------------------------


def test_convert_to_volume_strips_tilt_angles():
    arr = np.zeros((2, 2, 2), dtype=np.float32)
    ds = _make_dataset(arr)
    ds.tilt_angles = np.array([0.0, 1.0])
    ds.tilt_axis = 2
    out = _run(ConvertToVolumeTransform(), ds, port_type='TiltSeries')
    out_ds = out['output'].payload
    assert out_ds.tilt_angles is None
    assert out_ds.tilt_axis is None
    assert out['output'].port_type == 'Volume'


# ---- SetTiltAngles ------------------------------------------------------


def test_set_tilt_angles_expands_sparse_map():
    arr = np.zeros((2, 2, 4), dtype=np.float32)
    ds = _make_dataset(arr)
    t = SetTiltAnglesTransform()
    # As written by the C++ side: string keys, sparse coverage.
    t.deserialize({'angles': {'0': -10.0, '3': 20.0}})
    out = _run(t, ds)
    out_ds = out['output'].payload
    np.testing.assert_array_equal(out_ds.tilt_angles,
                                  np.array([-10.0, 0.0, 0.0, 20.0]))
    assert out['output'].port_type == 'TiltSeries'


# ---- TransposeData ------------------------------------------------------


def test_transpose_data_swaps_i_and_k_axes():
    arr = np.arange(24, dtype=np.float32).reshape((2, 3, 4))
    out = _run(TransposeDataTransform(), _make_dataset(arr))
    out_arr = out['output'].payload.active_scalars
    np.testing.assert_array_equal(out_arr, np.transpose(arr, (2, 1, 0)))


# ---- Crop ---------------------------------------------------------------


def test_crop_default_sentinel_returns_full_volume():
    arr = np.arange(24, dtype=np.float32).reshape((2, 3, 4))
    t = CropTransform()  # bounds left at INT_MIN sentinel
    out = _run(t, _make_dataset(arr))
    np.testing.assert_array_equal(out['output'].payload.active_scalars, arr)


def test_crop_inclusive_bounds():
    arr = np.arange(60, dtype=np.float32).reshape((3, 4, 5))
    t = CropTransform()
    # VTK extent is inclusive on both ends.
    t.deserialize({'bounds': [0, 1, 1, 2, 2, 4]})
    out = _run(t, _make_dataset(arr))
    np.testing.assert_array_equal(
        out['output'].payload.active_scalars,
        arr[0:2, 1:3, 2:5])


def test_crop_clamps_to_full_extent():
    arr = np.arange(24, dtype=np.float32).reshape((2, 3, 4))
    t = CropTransform()
    t.deserialize({'bounds': [-5, 10, -2, 100, 0, 99]})
    out = _run(t, _make_dataset(arr))
    np.testing.assert_array_equal(out['output'].payload.active_scalars, arr)


# ---- Threshold ----------------------------------------------------------


def test_legacy_python_transform_runs_real_operator():
    """LegacyPythonTransform must be able to load a JSON-described
    Python operator from disk, execute its `transform()`, and return
    the mutated dataset on its primary output port. We use the trivial
    AddConstant operator shipped in tomviz/python/."""
    from pathlib import Path

    from tomviz.pipeline.transforms.legacy_python import (
        LegacyPythonTransform,
    )

    op_dir = (Path(__file__).parent / '..' / '..' / 'tomviz'
              / 'python').resolve()
    desc = (op_dir / 'AddConstant.json').read_text()
    script = (op_dir / 'AddConstant.py').read_text()

    t = LegacyPythonTransform()
    t.deserialize({'description': desc, 'script': script,
                   'arguments': {'constant': 7.0}})

    arr = np.zeros((2, 2, 3), dtype=np.float32)
    ds = Dataset({'ImageScalars': arr}, 'ImageScalars')
    ds.spacing = (1.0, 1.0, 1.0)
    result = t.transform({'volume': PortData(ds, 'ImageData')})
    out_arr = result[t._primary_output_name].payload.active_scalars
    np.testing.assert_array_equal(out_arr, np.full_like(arr, 7.0))


def _run_rotate(arr, **arguments):
    """Run the real Rotate operator from tomviz/python/ over `arr`."""
    from pathlib import Path

    from tomviz.pipeline.transforms.legacy_python import (
        LegacyPythonTransform,
    )

    op_dir = (Path(__file__).parent / '..' / '..' / 'tomviz'
              / 'python').resolve()
    t = LegacyPythonTransform()
    t.deserialize({'description': (op_dir / 'Rotate3D.json').read_text(),
                   'script': (op_dir / 'Rotate3D.py').read_text(),
                   'arguments': arguments})
    ds = Dataset({'ImageScalars': arr}, 'ImageScalars')
    ds.spacing = (1.0, 1.0, 1.0)
    result = t.transform({'volume': PortData(ds, 'ImageData')})
    return result[t._primary_output_name].payload.active_scalars


def _slab_with_marker():
    # tomviz arrays are (nx, ny, nz). Put a marker near the top in y.
    arr = np.zeros((64, 64, 20), dtype=np.float32, order='F')
    arr[8:56, 8:56, 4:16] = 1.0
    arr[28:36, 48:54, 8:12] = 5.0
    return arr


def _marker_centre(arr):
    return np.argwhere(arr > 3.0).mean(axis=0)


def test_rotate_in_plane_without_expand_keeps_shape():
    """An alignment rotation about Z should turn the data within the image
    plane, leave the dimensions alone, and leave the marker at the top."""
    arr = _slab_with_marker()
    before = _marker_centre(arr)
    out = _run_rotate(arr, rotation_angle=3.0, rotation_axis=2, expand=False)

    assert out.shape == arr.shape
    after = _marker_centre(out)
    # Still near the top in y, and still in the same slices.
    assert after[1] > arr.shape[1] * 0.6
    assert abs(after[2] - before[2]) < 1.0


def test_rotate_expand_pads_the_perpendicular_axis():
    """Expanding grows the box to hold the rotated volume and zero-pads the
    corners, so the leading slices of the grown axis are nearly empty. This
    is why a small rotation can appear to blank the start of a dataset."""
    arr = _slab_with_marker()
    out = _run_rotate(arr, rotation_angle=3.0, rotation_axis=0, expand=True)

    # Rotating about x tips y into z, so z grows by about ny * sin(3 deg).
    assert out.shape[2] > arr.shape[2]
    filled = [(out[:, :, k] > 0.01).mean() for k in range(out.shape[2])]
    assert filled[0] < 0.5 * filled[out.shape[2] // 2]

    # Turning expansion off is what avoids that.
    kept = _run_rotate(arr, rotation_angle=3.0, rotation_axis=0, expand=False)
    assert kept.shape == arr.shape


def test_threshold_produces_binary_mask():
    arr = np.array([[[0, 1, 2], [3, 4, 5]]], dtype=np.float32)
    ds = _make_dataset(arr)
    t = ThresholdTransform()
    t.deserialize({'minValue': 1.5, 'maxValue': 3.5})
    out = _run(t, ds)
    mask = out['mask'].payload.active_scalars
    expected = ((arr >= 1.5) & (arr <= 3.5)).astype(np.uint8)
    np.testing.assert_array_equal(mask, expected)
    # The port is typed LabelMap, so the mask has to be integral for its
    # two states to be enumerable as labels.
    assert mask.dtype == np.uint8


# ---- CylindricalCrop (legacy operator) ------------------------------------


def _run_cylindrical_crop(
    arr: np.ndarray, **kwargs: float,
) -> np.ndarray:
    from pathlib import Path

    from tomviz.pipeline.transforms.legacy_python import (
        LegacyPythonTransform,
    )

    op_dir = (Path(__file__).parent / '..' / '..' / 'tomviz'
              / 'python').resolve()
    desc = (op_dir / 'CylindricalCrop.json').read_text()
    script = (op_dir / 'CylindricalCrop.py').read_text()

    t = LegacyPythonTransform()
    t.deserialize({'description': desc, 'script': script,
                   'arguments': kwargs})

    ds = Dataset({'ImageScalars': arr.copy()}, 'ImageScalars')
    ds.spacing = (1.0, 1.0, 1.0)
    result = t.transform({'volume': PortData(ds, 'ImageData')})
    return result[t._primary_output_name].payload.active_scalars


def test_cylindrical_crop_defaults_preserve_center() -> None:
    """With default params (center=volume center, radius=min(nx,ny)/2),
    the center voxel should always be preserved."""
    arr = np.ones((10, 10, 10), dtype=np.float32)
    out = _run_cylindrical_crop(arr)
    assert out[5, 5, 5] == 1.0


def test_cylindrical_crop_zeros_corners() -> None:
    """Corners of a cube should be outside the default cylinder."""
    arr = np.ones((10, 10, 10), dtype=np.float32)
    out = _run_cylindrical_crop(arr)
    assert out[0, 0, 5] == 0.0
    assert out[9, 9, 5] == 0.0
    assert out[0, 9, 5] == 0.0
    assert out[9, 0, 5] == 0.0


def test_cylindrical_crop_custom_fill_value() -> None:
    arr = np.ones((10, 10, 10), dtype=np.float32)
    out = _run_cylindrical_crop(arr, fill_value=-999.0)
    assert out[0, 0, 0] == -999.0


def test_cylindrical_crop_small_radius() -> None:
    """A very small radius should zero out almost everything."""
    arr = np.ones((10, 10, 10), dtype=np.float32)
    out = _run_cylindrical_crop(arr, radius=0.5)
    kept = np.count_nonzero(out)
    assert kept < arr.size * 0.05  # less than 5% preserved


def test_cylindrical_crop_large_radius_preserves_all() -> None:
    """A very large radius should keep everything."""
    arr = np.arange(1000, dtype=np.float32).reshape((10, 10, 10))
    out = _run_cylindrical_crop(arr, radius=100.0)
    np.testing.assert_array_equal(out, arr)


def test_cylindrical_crop_custom_axis() -> None:
    """With axis along X, the crop should be cylindrical around X."""
    arr = np.ones((10, 10, 10), dtype=np.float32)
    out = _run_cylindrical_crop(
        arr, axis_x=1.0, axis_y=0.0, axis_z=0.0, radius=2.0,
    )
    # Center of YZ face should be preserved
    assert out[0, 5, 5] == 1.0
    # Corner of YZ face should be zeroed
    assert out[0, 0, 0] == 0.0
