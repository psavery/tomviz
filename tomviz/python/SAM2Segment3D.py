import tomviz.operators


_MODEL_VARIANTS = {
    0: ("configs/sam2.1/sam2.1_hiera_t.yaml",  "sam2.1_hiera_tiny.pt"),
    1: ("configs/sam2.1/sam2.1_hiera_s.yaml",  "sam2.1_hiera_small.pt"),
    2: ("configs/sam2.1/sam2.1_hiera_b+.yaml", "sam2.1_hiera_base_plus.pt"),
    3: ("configs/sam2.1/sam2.1_hiera_l.yaml",  "sam2.1_hiera_large.pt"),
}

_PROPAGATE = {0: "both", 1: "forward", 2: "backward"}

_DEVICE_CHOICE = {0: "auto", 1: "gpu", 2: "cpu"}

_PROMPT_MODE = {0: "point", 1: "auto_mask"}

_Z_AXIS_CHOICE = {0: 0, 1: 1, 2: 2}

_ENV_URL_BASE = ("https://raw.githubusercontent.com/OpenChemistry/tomviz/"
                 "master/tomviz/python/environments/")

_INSTALL_INSTRUCTIONS = """\
The SAM 2 Segmentation operator runs in a separate conda environment that
provides PyTorch and SAM 2. It cannot run inside the Tomviz application
environment. To set it up:

1. Download the environment file for your hardware:
     CPU / Apple Silicon:  {base}sam2-tomviz-cpu.yml
     NVIDIA GPU (CUDA):    {base}sam2-tomviz-cuda.yml
   (These files are also installed with Tomviz under
   share/tomviz/environments/.)

2. Create the environment:
     conda env create -f sam2-tomviz-cpu.yml

3. In the operator's properties dialog, open the "Execution" tab and
   browse to the newly created environment (e.g.
   ~/miniconda3/envs/sam2-tomviz).

See the "Machine Learning Segmentation" page in the Tomviz documentation
for full instructions, including model checkpoint download.
""".format(base=_ENV_URL_BASE)


def _auto_seed_mask(slice_2d, invert=False):
    """Build an initial binary mask from a 2D slice via Otsu + cleanup.

    Steps: Otsu threshold, morphological opening to drop specks, keep only
    the largest connected component. Returns a bool array with the same
    shape as the input. Raises if nothing above threshold survives.
    """
    import numpy as np
    from skimage.filters import threshold_otsu
    from skimage.measure import label
    from skimage.morphology import disk, opening

    arr = slice_2d.astype(np.float32, copy=False)
    thr = threshold_otsu(arr)
    m = arr < thr if invert else arr > thr
    if not m.any():
        raise RuntimeError(
            "Otsu auto-mask produced an empty seed mask on the seed slice; "
            "try a different slice or set prompt_mode=Point instead.")

    radius = max(1, int(round(min(arr.shape) * 0.005)))
    m = opening(m, disk(radius))

    lab = label(m, connectivity=2)
    if lab.max() == 0:
        raise RuntimeError(
            "Otsu auto-mask cleanup removed all components on the seed "
            "slice; try a different slice or set prompt_mode=Point.")
    sizes = np.bincount(lab.ravel())
    sizes[0] = 0  # ignore background
    keep = int(sizes.argmax())
    return lab == keep


def _cleanup_mask(mask, volume, seed, trim_fraction, keep_seed_component):
    """Suppress tracker drift in a propagated mask.

    SAM 2's video predictor can reattach to other bright objects after
    the seeded object ends, leaving disconnected phantom regions. Trim
    the mask to voxels above trim_fraction of the volume's bright
    reference value (99.9th percentile), then keep only the connected
    component containing the seed (or the largest component if the
    seed voxel is not in the mask, as with an auto-mask prompt).
    """
    import numpy as np
    from scipy import ndimage as ndi

    cleaned = mask.astype(bool)
    if trim_fraction > 0:
        ref = float(np.percentile(volume, 99.9))
        cleaned &= volume > trim_fraction * ref
    if keep_seed_component:
        labels, count = ndi.label(cleaned, structure=np.ones((3, 3, 3)))
        if count > 0:
            target = labels[seed]
            if target == 0:
                sizes = np.bincount(labels.ravel())
                sizes[0] = 0
                target = int(sizes.argmax())
            cleaned = labels == target
    return cleaned.astype(np.uint8)


def _pick_device(choice):
    """Resolve 'auto' | 'gpu' | 'cpu' to a torch.device.

    'gpu' prefers MPS (Apple) over CUDA and errors if neither is available.
    'auto' falls back to CPU silently.
    """
    import torch
    has_mps = torch.backends.mps.is_available()
    has_cuda = torch.cuda.is_available()
    if choice == "cpu":
        return torch.device("cpu")
    if choice == "gpu":
        if has_mps:
            return torch.device("mps")
        if has_cuda:
            return torch.device("cuda")
        raise RuntimeError(
            "Device 'gpu' requested but no MPS or CUDA backend is available")
    if has_mps:
        return torch.device("mps")
    if has_cuda:
        return torch.device("cuda")
    return torch.device("cpu")


class SAM2Segment3D(tomviz.operators.CancelableOperator):

    def transform(self, dataset,
                  seed_x=0, seed_y=0, seed_z=0,
                  prompt_mode=0,
                  invert_contrast=False,
                  z_axis=0,
                  propagate=0,
                  model_size=2,
                  device=0,
                  checkpoint_dir="",
                  trim_fraction=0.1,
                  keep_seed_component=True):
        """Segment a 3D volume with SAM 2.

        The Z-axis is treated as the video time axis; SAM 2's video
        predictor propagates the mask through the volume from a seed slice.
        The seed prompt on that slice is either a single positive click at
        (seed_x, seed_y)  [prompt_mode=Point]  or an Otsu-thresholded
        binary mask of the slice  [prompt_mode=Auto Mask]. Returns the
        segmentation as a uint8 label map in the active scalars.
        """
        import os
        import tempfile
        import numpy as np

        self.progress.maximum = 100
        self.progress.value = 0
        self.progress.message = "Loading modules"

        # Must be set before torch is first imported (inside _pick_device).
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        try:
            from sam2.build_sam import build_sam2_video_predictor
        except ImportError as exc:
            raise ImportError(_INSTALL_INSTRUCTIONS) from exc
        from PIL import Image

        if model_size not in _MODEL_VARIANTS:
            raise ValueError("Unknown model_size index %r" % (model_size,))
        cfg, ckpt_name = _MODEL_VARIANTS[model_size]

        direction = _PROPAGATE.get(propagate, "both")

        if not checkpoint_dir:
            checkpoint_dir = os.path.expanduser("~/.tomviz/sam2/checkpoints")
        ckpt_path = os.path.join(checkpoint_dir, ckpt_name)
        if not os.path.isfile(ckpt_path):
            raise FileNotFoundError(
                "SAM 2 checkpoint not found: %s\n"
                "Download it from "
                "https://dl.fbaipublicfiles.com/segment_anything_2/092824/%s"
                % (ckpt_path, ckpt_name))

        torch_device = _pick_device(_DEVICE_CHOICE.get(device, "auto"))

        raw_volume = dataset.active_scalars
        if raw_volume is None or raw_volume.ndim != 3:
            raise ValueError("SAM 2 3D operator requires a 3D volume")

        # Move the user-specified z_axis to position 2 so the rest of the
        # operator can assume a (nx, ny, nz) layout. We transpose the output
        # mask back at the end so the dataset keeps its original axis order.
        z_axis_int = _Z_AXIS_CHOICE.get(z_axis, 0)
        volume = np.moveaxis(raw_volume, z_axis_int, 2)
        print("SAM2: input shape=%s, z_axis=%d -> working shape=%s"
              % (raw_volume.shape, z_axis_int, volume.shape))
        nx, ny, nz = volume.shape

        # -1 (or anything negative) means "auto center / middle slice".
        seed_x = nx // 2 if seed_x < 0 else int(np.clip(seed_x, 0, nx - 1))
        seed_y = ny // 2 if seed_y < 0 else int(np.clip(seed_y, 0, ny - 1))
        seed_z = nz // 2 if seed_z < 0 else int(np.clip(seed_z, 0, nz - 1))
        print("SAM2: seeds -> x=%d y=%d z=%d (of %d,%d,%d)"
              % (seed_x, seed_y, seed_z, nx, ny, nz))

        vmin = float(volume.min())
        vmax = float(volume.max())
        scale = 255.0 / (vmax - vmin) if vmax > vmin else 0.0

        with tempfile.TemporaryDirectory(prefix="tomviz_sam2_") as tmp:
            self.progress.message = "Writing %d frames" % nz
            for z in range(nz):
                if self.canceled:
                    return None
                # Tomviz slice is (nx, ny); PIL/SAM 2 want (H, W) = (ny, nx).
                u8 = ((volume[:, :, z] - vmin) * scale) \
                    .clip(0, 255).astype(np.uint8).T
                Image.fromarray(u8).convert("RGB").save(
                    os.path.join(tmp, "%06d.jpg" % z), quality=95)
                self.progress.value = int(20 * (z + 1) / nz)

            self.progress.message = \
                "Building SAM 2 predictor on %s" % torch_device
            predictor = build_sam2_video_predictor(
                cfg, ckpt_path, device=str(torch_device))
            self.progress.value = 25

            self.progress.message = "Initializing video state"
            init_kwargs = dict(video_path=tmp,
                               offload_video_to_cpu=True,
                               offload_state_to_cpu=True)
            try:
                state = predictor.init_state(**init_kwargs)
            except TypeError:
                state = predictor.init_state(video_path=tmp)
            self.progress.value = 30

            mode = _PROMPT_MODE.get(prompt_mode, "point")
            if mode == "auto_mask":
                self.progress.message = \
                    "Auto-masking seed slice (Otsu + cleanup)"
                # volume[:, :, seed_z] has shape (nx, ny). SAM 2 wants a
                # mask shaped like the video frame = (ny, nx), so transpose.
                seed_mask = _auto_seed_mask(
                    volume[:, :, seed_z], invert=bool(invert_contrast)).T
                predictor.add_new_mask(
                    inference_state=state,
                    frame_idx=seed_z,
                    obj_id=1,
                    mask=seed_mask,
                )
            else:
                points = np.array([[seed_x, seed_y]], dtype=np.float32)
                labels = np.array([1], dtype=np.int32)
                predictor.add_new_points_or_box(
                    inference_state=state,
                    frame_idx=seed_z,
                    obj_id=1,
                    points=points,
                    labels=labels,
                )

            out = np.zeros((nx, ny, nz), dtype=np.uint8)

            runs = []
            expected = 0
            if direction in ("forward", "both"):
                runs.append(False)
                expected += nz - seed_z
            if direction in ("backward", "both"):
                runs.append(True)
                expected += seed_z + 1
            if not runs:
                runs.append(False)
                expected = 1
            expected = max(expected, 1)
            done = 0

            for reverse in runs:
                for frame_idx, _obj_ids, mask_logits in \
                        predictor.propagate_in_video(state, reverse=reverse):
                    if self.canceled:
                        return None
                    mask = (mask_logits[0, 0] > 0.0).detach().cpu().numpy()
                    out[:, :, frame_idx] = mask.T.astype(np.uint8)
                    done += 1
                    self.progress.value = 30 + int(70 * done / expected)
                    self.progress.message = \
                        "Propagating (reverse=%s): slice %d" \
                        % (reverse, frame_idx)

        if trim_fraction > 0 or keep_seed_component:
            self.progress.message = "Cleaning up mask"
            out = _cleanup_mask(out, volume, (seed_x, seed_y, seed_z),
                                trim_fraction, bool(keep_seed_component))

        self.progress.message = "Storing label map"
        # Transpose the output mask back to the dataset's original axis
        # order before assigning it to active_scalars.
        dataset.active_scalars = np.moveaxis(out, 2, z_axis_int)
        self.progress.value = 100
        return None
