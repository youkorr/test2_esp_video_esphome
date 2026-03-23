"""
ESP-DL YOLO11 ONNX Export Script
From: https://github.com/espressif/esp-dl/blob/master/models/coco_detect/models/export_onnx.py

Usage:
  pip install ultralytics onnx onnxsim
  python export_onnx.py

This exports yolo11n.pt to ONNX with output names box0/score0/box1/score1/box2/score2
which are required by esp-dl's yolo11PostProcessor.

After export, quantize with esp-dl tools to produce the .espdl file:
  See: https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_quantize_and_deploy_yolo11n.html
"""

from ultralytics import YOLO
from ultralytics.nn.modules import Detect, Attention
from ultralytics.engine.exporter import Exporter, try_export, arange_patch
from ultralytics.utils import LOGGER, __version__, colorstr
from ultralytics.utils.checks import check_requirements
from ultralytics.utils.torch_utils import get_latest_opset
import torch
import onnx


class ESP_Detect(Detect):
    def forward(self, x):
        """Returns predicted bounding boxes and class probabilities respectively."""
        box0 = self.cv2[0](x[0])
        score0 = self.cv3[0](x[0])

        box1 = self.cv2[1](x[1])
        score1 = self.cv3[1](x[1])

        box2 = self.cv2[2](x[2])
        score2 = self.cv3[2](x[2])

        return box0, score0, box1, score1, box2, score2


class ESP_Attention(Attention):
    def forward(self, x):
        B, C, H, W = x.shape
        N = H * W
        qkv = self.qkv(x)
        q, k, v = qkv.view(
            -1, self.num_heads, self.key_dim * 2 + self.head_dim, N
        ).split([self.key_dim, self.key_dim, self.head_dim], dim=2)
        attn = (q.transpose(-2, -1) @ k) * self.scale
        attn = attn.softmax(dim=-1)
        x = (v @ attn.transpose(-2, -1)).view(-1, C, H, W) + self.pe(
            v.reshape(-1, C, H, W)
        )
        x = self.proj(x)
        return x


class ESP_Detect_Exporter(Exporter):
    @try_export
    def export_onnx(self, prefix=colorstr("ONNX:")):
        """YOLO ONNX export with esp-dl compatible output names."""
        requirements = ["onnx>=1.14.0"]
        if self.args.simplify:
            requirements += [
                "onnxsim",
                "onnxruntime" + ("-gpu" if torch.cuda.is_available() else ""),
            ]
        check_requirements(requirements)

        opset_version = self.args.opset or get_latest_opset()
        LOGGER.info(
            f"\n{prefix} starting export with onnx {onnx.__version__} opset {opset_version}..."
        )
        f = str(self.file.with_suffix(".onnx"))
        output_names = ["box0", "score0", "box1", "score1", "box2", "score2"]
        dynamic = self.args.dynamic
        if dynamic:
            dynamic = {"images": {0: "batch"}}
            for name in output_names:
                dynamic[name] = {0: "batch"}

        with arange_patch(self.args):
            torch.onnx.export(
                self.model,
                self.im,
                f,
                verbose=False,
                opset_version=opset_version,
                do_constant_folding=False,
                input_names=["images"],
                output_names=output_names,
                dynamic_axes=dynamic or None,
            )
        model_onnx = onnx.load(f)

        if self.args.simplify:
            try:
                import onnxsim
                LOGGER.info(f"{prefix} simplifying with onnxsim {onnxsim.__version__}...")
                model_onnx, _ = onnxsim.simplify(model_onnx)
            except Exception as e:
                LOGGER.warning(f"{prefix} simplifier failure: {e}")

        for k, v in self.metadata.items():
            meta = model_onnx.metadata_props.add()
            meta.key, meta.value = k, str(v)

        onnx.save(model_onnx, f)
        return f, model_onnx


class ESP_YOLO(YOLO):
    def export(self, **kwargs):
        self._check_is_pytorch_model()
        custom = {
            "imgsz": self.model.args["imgsz"],
            "batch": 1,
            "data": None,
            "device": None,
            "verbose": False,
        }
        args = {**self.overrides, **custom, **kwargs, "mode": "export"}
        return ESP_Detect_Exporter(overrides=args, _callbacks=self.callbacks)(
            model=self.model
        )


if __name__ == "__main__":
    # Change imgsz to 320 for ESP32-P4 (less memory), or 640 for full resolution
    model = ESP_YOLO("yolo11n.pt")
    for m in model.modules():
        if isinstance(m, Attention):
            m.forward = ESP_Attention.forward.__get__(m)
        if isinstance(m, Detect):
            m.forward = ESP_Detect.forward.__get__(m)

    model.export(format="onnx", simplify=True, opset=13, dynamic=False, imgsz=320)
