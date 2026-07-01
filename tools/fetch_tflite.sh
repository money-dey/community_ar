#!/usr/bin/env bash
# =============================================================================
# Community AR — Vendor TensorFlow Lite for the Android native build
#
# Run from the project root:  bash tools/fetch_tflite.sh
#
# Populates third_party/tensorflow-lite/ with the layout the root CMakeLists.txt
# expects:
#
#   third_party/tensorflow-lite/
#   ├── include/                     # C API + GPU delegate headers
#   │   └── tensorflow/lite/...
#   └── lib/<ABI>/                   # prebuilt runtime libraries
#       ├── libtensorflowlite_c.so
#       └── libtensorflowlite_gpu_delegate.so
#
# WHY A SOURCE CHECKOUT (not just a Maven AAR):
#   native/core/ml/tflite_backend.cpp uses the GPU delegate's GL-interop API
#   (tensorflow/lite/delegates/gpu/gl/{gl_texture,gl_buffer,egl_environment}.h
#   and TfLiteGpuDelegateBindGl*ToTensor). Those headers/symbols are NOT in the
#   standard `org.tensorflow:tensorflow-lite` AAR — they live in the TF source
#   tree. This script fetches the full header set from source; the matching
#   runtime .so's must be BUILT from source (Bazel) — see below.
#
# This script automates the fetchable part (headers) and creates the lib/<ABI>
# directories. It prints the exact Bazel commands for the libraries, which
# require the TF build toolchain + Android NDK and are best run on your build
# machine.
# =============================================================================

set -euo pipefail

# ---- Config (override via env) ----------------------------------------------
TF_VERSION="${TF_VERSION:-v2.17.0}"          # pinned TensorFlow/LiteRT tag
ABIS="${ABIS:-arm64-v8a armeabi-v7a}"        # must match android/build.gradle.kts

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/tensorflow-lite"
INC="$DEST/include"

echo "==> TensorFlow Lite $TF_VERSION -> $DEST"
mkdir -p "$INC"

# ---- 1. Headers: sparse checkout of tensorflow/lite from source -------------
# The GL-interop headers only exist in source, so we copy the whole lite header
# tree (small once filtered to *.h).
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> Fetching headers from tensorflow/tensorflow@$TF_VERSION (sparse)"
git clone --depth 1 --branch "$TF_VERSION" --filter=blob:none --sparse \
  https://github.com/tensorflow/tensorflow.git "$TMP/tf"
git -C "$TMP/tf" sparse-checkout set tensorflow/lite

echo "==> Copying *.h into $INC (preserving tensorflow/lite/... paths)"
( cd "$TMP/tf" && find tensorflow/lite -name '*.h' -print0 \
    | while IFS= read -r -d '' h; do
        mkdir -p "$INC/$(dirname "$h")"
        cp "$h" "$INC/$h"
      done )

echo "==> Headers vendored."

# ---- 2. Libraries: build from source (Bazel) --------------------------------
# No official single prebuilt bundles libtensorflowlite_c.so + the GPU delegate
# with GL-interop enabled, so build them from the same TF checkout. This needs
# Bazel + the Android NDK configured (`./configure` in the TF tree). Example:
for ABI in $ABIS; do
  mkdir -p "$DEST/lib/$ABI"
done

cat <<EOF

============================================================================
HEADERS are vendored. LIBRARIES must be built from source (Bazel), once per
ABI, and copied into third_party/tensorflow-lite/lib/<ABI>/.

From a TensorFlow $TF_VERSION checkout (with ./configure pointed at your NDK):

  # arm64-v8a
  bazel build -c opt --config=android_arm64 \\
      //tensorflow/lite/c:tensorflowlite_c \\
      //tensorflow/lite/delegates/gpu:libtensorflowlite_gpu_delegate.so
  cp bazel-bin/tensorflow/lite/c/libtensorflowlite_c.so \\
     "$DEST/lib/arm64-v8a/"
  cp bazel-bin/tensorflow/lite/delegates/gpu/libtensorflowlite_gpu_delegate.so \\
     "$DEST/lib/arm64-v8a/"

  # armeabi-v7a: --config=android_arm  (repeat, copy into lib/armeabi-v7a/)

See third_party/tensorflow-lite/README.md for details and the GL-interop
caveat (verify TfLiteGpuDelegateBindGlTextureToTensor exists in your pinned
version — the texture-binding variant is not in every release).
============================================================================
EOF
