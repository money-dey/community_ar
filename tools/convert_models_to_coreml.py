#!/usr/bin/env python3
"""
Community AR — Convert .tflite models to .mlmodelc for iOS Core ML.

Requirements (run on macOS):
    pip install coremltools tensorflow

Usage (from project root):
    python3 tools/convert_models_to_coreml.py

Outputs go to native/models/ alongside the .tflite source files.
"""
import os
import shutil
import subprocess
import sys

# coremltools and tensorflow imports — kept inside main() so the script can
# print a helpful error if they're missing without blowing up on import
def main():
    try:
        import coremltools as ct
        import tensorflow as tf
    except ImportError as e:
        print(f"Missing dependency: {e}")
        print("Install with:  pip install coremltools tensorflow")
        sys.exit(1)

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    models_dir = os.path.join(project_root, "native", "models")
    if not os.path.isdir(models_dir):
        print(f"Models dir not found: {models_dir}")
        print("Run tools/fetch_models.sh first to download the .tflite files.")
        sys.exit(1)

    # ─────────────────────────────────────────────────────────────────────
    # Models to convert. Tuples of (input_filename, compute_units).
    # compute_units controls Core ML's ANE/GPU/CPU routing.
    # ─────────────────────────────────────────────────────────────────────
    models = [
        ("face_detector.tflite",   "ALL"),    # ANE-eligible
        ("face_landmarker.tflite", "ALL"),    # ANE-eligible
        ("face_blendshapes.tflite","CPU_AND_GPU"),  # tiny model, ANE overhead not worth
        ("iris_landmark.tflite",   "ALL"),
        ("hair_segmenter.tflite",  "ALL"),
        ("selfie_segmenter.tflite","ALL"),
    ]

    for fname, compute_units in models:
        tflite_path = os.path.join(models_dir, fname)
        if not os.path.exists(tflite_path):
            print(f"  skip (not found): {fname}")
            continue

        base = os.path.splitext(fname)[0]
        mlmodel_path  = os.path.join(models_dir, base + ".mlmodel")
        mlmodelc_path = os.path.join(models_dir, base + ".mlmodelc")

        print(f"==> Converting {fname}")

        # ── Step 1: .tflite → .mlmodel ──
        # coremltools supports tflite directly since 6.0
        mlmodel = ct.convert(
            tflite_path,
            source="tensorflow",
            convert_to="mlprogram",   # newer format, smaller + ANE-friendly
            compute_precision=ct.precision.FLOAT16,
            compute_units=getattr(ct.ComputeUnit, compute_units),
            minimum_deployment_target=ct.target.iOS15,
        )
        mlmodel.save(mlmodel_path)

        # ── Step 2: .mlmodel → .mlmodelc (compiled bundle) ──
        # `xcrun coremlcompiler` produces the on-device-ready compiled form.
        if os.path.exists(mlmodelc_path):
            shutil.rmtree(mlmodelc_path)
        subprocess.check_call([
            "xcrun", "coremlcompiler", "compile",
            mlmodel_path,
            models_dir
        ])
        # `coremlcompiler compile` outputs `<base>.mlmodelc` directly to dest
        os.remove(mlmodel_path)   # we only need the compiled form for shipping

        size = sum(os.path.getsize(os.path.join(root, f))
                   for root, _, files in os.walk(mlmodelc_path)
                   for f in files) / (1024 * 1024)
        print(f"   → {base}.mlmodelc  ({size:.2f} MB)")

    print()
    print("==> Done. The iOS framework's build phase should copy *.mlmodelc")
    print("    bundles into the app's resource directory.")

if __name__ == "__main__":
    main()
