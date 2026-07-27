import tomviz.operators


_ENV_URL_BASE = ("https://raw.githubusercontent.com/OpenChemistry/tomviz/"
                 "master/tomviz/python/environments/")

_INSTALL_INSTRUCTIONS = """\
The SAM 3 Segmentation operator runs in a separate conda environment that
provides PyTorch and SAM 3. It cannot run inside the Tomviz application
environment, and it requires an NVIDIA GPU (CUDA) with at least 8 GB
of memory. To set it up:

1. Download the environment file:
     {base}sam3-tomviz-cuda.yml
   (Also installed with Tomviz under share/tomviz/environments/.)

2. Create the environment:
     conda env create -f sam3-tomviz-cuda.yml

3. In the operator's properties dialog, open the "Execution" tab and
   browse to the newly created environment (e.g.
   ~/miniconda3/envs/sam3-tomviz).

See the "Machine Learning Segmentation" page in the Tomviz documentation
for full instructions, including model checkpoint download.
""".format(base=_ENV_URL_BASE)

_CHECKPOINT_HELP = (
    "SAM 3 checkpoint not found: %s\n"
    "Download sam3.pt from https://huggingface.co/facebook/sam3 "
    "(a free HuggingFace account and acceptance of Meta's SAM License "
    "are required; the file is about 3.5 GB).")

# Percentile window used to normalize the volume to uint8.
_NORM_LO_PCT, _NORM_HI_PCT = 1.0, 99.9

_AXES = {"xy": 0, "xz": 1, "yz": 2}


def _norm_to_uint8(vol):
    import numpy as np
    lo, hi = np.percentile(vol, (_NORM_LO_PCT, _NORM_HI_PCT))
    return np.clip(
        255 * (vol - lo) / max(hi - lo, 1e-12), 0, 255).astype(np.uint8)


def _make_rgb_frame(vol_u8, axis, i):
    """Pack slices i-1, i, i+1 along `axis` into an RGB image.

    Gives the (2D) model a little through-plane context per frame.
    """
    import numpy as np
    from PIL import Image
    n = vol_u8.shape[axis]
    r = np.take(vol_u8, max(0, i - 1), axis=axis)
    g = np.take(vol_u8, i, axis=axis)
    b = np.take(vol_u8, min(n - 1, i + 1), axis=axis)
    return Image.fromarray(np.stack([r, g, b], axis=-1), "RGB")


def _load_sam3_weights(model, checkpoint_path):
    """Load a SAM 3 checkpoint into the image model.

    Handles both the official packed release checkpoint (keys prefixed
    "detector." / "tracker.") and fine-tuned training checkpoints that
    store the image-model state dict directly, optionally wrapped in a
    {"model": ...} training-state dict. sam3's bundled loader only
    understands the packed format and silently continues with random
    weights on anything else, so we load here and raise instead.
    """
    import torch
    ckpt = torch.load(checkpoint_path, map_location="cpu",
                      weights_only=True, mmap=True)
    if isinstance(ckpt, dict) and isinstance(ckpt.get("model"), dict):
        ckpt = ckpt["model"]
    if any(k.startswith("detector.") for k in ckpt):
        ckpt = {k.replace("detector.", ""): v
                for k, v in ckpt.items() if "detector" in k}
    missing, _unexpected = model.load_state_dict(ckpt, strict=False)
    if missing:
        raise RuntimeError(
            "SAM 3 checkpoint %s does not match the SAM 3 image model: "
            "%d parameter(s) missing (e.g. %s). Use the official sam3.pt "
            "or a fine-tuned checkpoint of the SAM 3 image model."
            % (checkpoint_path, len(missing), missing[0]))


def _predict_frame(processor, img, text_prompt):
    import numpy as np
    import torch
    state = processor.set_image(img)
    processor.reset_all_prompts(state)
    state = processor.set_text_prompt(prompt=text_prompt, state=state)
    out = np.zeros((img.height, img.width), dtype=bool)
    if "masks" not in state or state["masks"] is None:
        return out
    pm = state["masks"]
    if torch.is_tensor(pm):
        pm = pm.cpu().numpy()
    for j in range(pm.shape[0]):
        m = pm[j]
        while m.ndim > 2:
            m = m[0] if m.shape[0] == 1 else m.any(axis=0)
        out |= m.astype(bool)
    return out


def _vote(per_axis, threshold):
    import numpy as np
    stack = np.stack(list(per_axis.values()), axis=0).astype(np.uint8)
    return (stack.sum(axis=0) >= threshold).astype(np.uint8)


def _stitch_instances(binary_vol, min_voxels):
    """3D connected-component labeling with a minimum-size filter."""
    import numpy as np
    from scipy import ndimage as ndi
    lbl, _ = ndi.label(binary_vol, structure=np.ones((3, 3, 3)))
    sizes = np.bincount(lbl.ravel())
    keep = sizes >= min_voxels
    keep[0] = False
    remap = np.zeros_like(sizes)
    remap[keep] = np.arange(1, keep.sum() + 1)
    return remap[lbl].astype(np.int32)


class SAM3Segment3D(tomviz.operators.CancelableOperator):

    def transform(self, dataset,
                  text_prompt="bright lines",
                  vote_threshold=1,
                  min_component_voxels=200,
                  confidence_threshold=0.3,
                  checkpoint_path=""):
        """Segment a 3D volume with SAM 3 using a text prompt.

        Each slice along all three axes is segmented independently with
        the text-prompted SAM 3 image model; per-axis masks are combined
        by majority voting and stitched into a 3D instance label map
        (int32, 0 = background) via connected-component labeling.
        Requires CUDA.
        """
        import os
        import numpy as np

        self.progress.maximum = 100
        self.progress.value = 0
        self.progress.message = "Loading modules"

        try:
            from sam3 import build_sam3_image_model
            from sam3.model.sam3_image_processor import Sam3Processor
        except ImportError as exc:
            raise ImportError(_INSTALL_INSTRUCTIONS) from exc
        import torch

        if not torch.cuda.is_available():
            raise RuntimeError(
                "The SAM 3 operator requires an NVIDIA GPU (CUDA), which "
                "is not available in the selected Python environment. Run "
                "it on a CUDA-capable machine, or use the SAM 2 operator "
                "for CPU/Apple Silicon segmentation.")

        if not checkpoint_path:
            checkpoint_path = os.path.expanduser(
                "~/.tomviz/sam3/checkpoints/sam3.pt")
        if not os.path.isfile(checkpoint_path):
            raise FileNotFoundError(_CHECKPOINT_HELP % checkpoint_path)

        volume = dataset.active_scalars
        if volume is None or volume.ndim != 3:
            raise ValueError("SAM 3 3D operator requires a 3D volume")

        vote_threshold = int(np.clip(vote_threshold, 1, 3))

        self.progress.message = "Normalizing volume"
        vol_u8 = _norm_to_uint8(volume.astype(np.float32, copy=False))
        self.progress.value = 5

        self.progress.message = "Loading SAM 3 model (CUDA)"
        torch.backends.cuda.matmul.allow_tf32 = True
        model = build_sam3_image_model(
            checkpoint_path=None, load_from_HF=False,
            enable_segmentation=True, device="cuda", eval_mode=True)
        _load_sam3_weights(model, str(checkpoint_path))
        processor = Sam3Processor(
            model, confidence_threshold=float(confidence_threshold))
        self.progress.value = 15

        total_frames = sum(vol_u8.shape[a] for a in _AXES.values())
        done = 0
        per_axis = {}
        with torch.autocast("cuda", dtype=torch.bfloat16):
            for name, axis in _AXES.items():
                pred = np.zeros(vol_u8.shape, dtype=np.uint8)
                for i in range(vol_u8.shape[axis]):
                    if self.canceled:
                        return None
                    mask = _predict_frame(
                        processor, _make_rgb_frame(vol_u8, axis, i),
                        text_prompt)
                    sl = [slice(None)] * 3
                    sl[axis] = i
                    pred[tuple(sl)] = mask.astype(np.uint8)
                    done += 1
                    self.progress.value = 15 + int(75 * done / total_frames)
                    self.progress.message = \
                        "Segmenting [%s]: slice %d of %d" \
                        % (name, i + 1, vol_u8.shape[axis])
                per_axis[name] = pred

        self.progress.message = \
            "Voting across axes (threshold %d)" % vote_threshold
        binary = _vote(per_axis, vote_threshold)
        self.progress.value = 92

        self.progress.message = "Labeling connected components"
        labels = _stitch_instances(binary, int(min_component_voxels))
        print("SAM3: %d instances after size filter (min %d voxels)"
              % (int(labels.max()), int(min_component_voxels)))

        dataset.active_scalars = labels
        self.progress.value = 100
        return None
