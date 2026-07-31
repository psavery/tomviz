import importlib.util
import json
import os

import numpy as np
import pytest

_PYTHON_DIR = os.path.join(
    os.path.dirname(__file__), '..', '..', 'tomviz', 'python')


def _load_module():
    path = os.path.join(_PYTHON_DIR, 'SAM2Segment3D.py')
    spec = importlib.util.spec_from_file_location('SAM2Segment3D', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_json_description():
    path = os.path.join(_PYTHON_DIR, 'SAM2Segment3D.json')
    with open(path) as f:
        desc = json.load(f)

    assert desc['name'] == 'SAM2Segment3D'
    assert desc['externalOnly'] is True
    assert desc['externalCompatible'] is True
    # The description must not carry a machine-specific env path.
    assert 'tomviz_pipeline_env' not in desc

    names = [p['name'] for p in desc['parameters']]
    assert 'prompt_mode' in names
    assert 'checkpoint_dir' in names


def test_module_imports_without_sam2():
    # Heavy imports (torch, sam2, PIL, skimage) live inside transform(),
    # so loading the module must succeed in the application environment.
    module = _load_module()
    assert hasattr(module, 'SAM2Segment3D')


def test_transform_raises_install_instructions_without_sam2():
    pytest.importorskip('numpy')
    try:
        import sam2  # noqa: F401
        pytest.skip('sam2 is installed; ImportError path not reachable')
    except ImportError:
        pass

    module = _load_module()
    op = module.SAM2Segment3D.__new__(module.SAM2Segment3D)

    class Progress:
        maximum = 0
        value = 0
        message = ''

    op.progress = Progress()

    with pytest.raises(ImportError) as excinfo:
        op.transform(dataset=None)

    # The error must tell the user how to set up the environment.
    text = str(excinfo.value)
    assert 'conda env create' in text
    assert 'sam2-tomviz-cpu.yml' in text
    assert 'Execution' in text


def test_auto_seed_mask():
    skimage = pytest.importorskip('skimage')  # noqa: F841
    module = _load_module()

    img = np.zeros((64, 64), dtype=np.float32)
    img[8:24, 8:24] = 100.0    # large bright blob
    img[40:44, 40:44] = 100.0  # small bright blob

    mask = module._auto_seed_mask(img)
    assert mask.dtype == bool
    assert mask.shape == img.shape
    # Only the largest component survives.
    assert mask[10, 10]
    assert not mask[41, 41]

    # Inverted contrast selects the (dark) background instead.
    inv = module._auto_seed_mask(img, invert=True)
    assert inv[32, 32]
    assert not inv[10, 10]


def test_cleanup_mask():
    scipy = pytest.importorskip('scipy')  # noqa: F841
    module = _load_module()

    vol = np.zeros((40, 40, 40), dtype=np.float32)
    vol[5:15, 5:15, 5:15] = 200.0    # the clicked particle
    vol[25:35, 25:35, 25:35] = 200.0  # a neighbor the tracker drifted onto

    mask = np.zeros_like(vol, dtype=np.uint8)
    mask[4:16, 4:16, 4:16] = 1    # particle + a 1-voxel halo
    mask[25:35, 25:35, 25:35] = 1  # drift region
    mask[15:25, 10, 10] = 1        # thin dark bridge from drift

    seed = (10, 10, 10)
    out = module._cleanup_mask(mask, vol, seed,
                               trim_fraction=0.1, keep_seed_component=True)
    assert out.dtype == np.uint8
    # Only the seed-connected bright region survives.
    assert out[10, 10, 10] == 1
    assert out[30, 30, 30] == 0
    assert out[20, 10, 10] == 0   # bridge trimmed (below intensity floor)
    assert out.sum() == 10 * 10 * 10

    # Seed outside the mask (auto-mask prompts): fall back to the
    # largest component instead of dropping everything.
    out2 = module._cleanup_mask(mask, vol, (0, 0, 0),
                                trim_fraction=0.1, keep_seed_component=True)
    assert out2.sum() == 10 * 10 * 10

    # Disabled cleanup passes the mask through untouched.
    out3 = module._cleanup_mask(mask, vol, seed,
                                trim_fraction=0.0, keep_seed_component=False)
    assert (out3 == mask).all()
