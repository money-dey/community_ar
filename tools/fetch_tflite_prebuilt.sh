#!/usr/bin/env bash
# =============================================================================
# Community AR — Vendor PREBUILT TensorFlow Lite (no Bazel required)
#
# Run from the project root:  bash tools/fetch_tflite_prebuilt.sh
#
# Downloads the official TFLite runtime + GPU-delegate AARs from Maven Central
# (an AAR is a zip), and vendors:
#   - C API + GPU delegate headers  → third_party/tensorflow-lite/include/
#   - per-ABI .so files             → third_party/tensorflow-lite/lib/<ABI>/
#                                     (for the CMake link step)
#   - the same .so files            → android/src/main/jniLibs/<ABI>/
#                                     (so Gradle packages them into the APK)
#
# Why prebuilt: the from-source Bazel build (tools/fetch_tflite.sh) is only
# needed for the experimental GL-interop tensor binding
# (TfLiteGpuDelegateBindGlTextureToTensor), which the prebuilt binaries do NOT
# export (verified with llvm-nm against 2.16.1). The prebuilt path still gives
# the full C API + GPU-accelerated inference; tflite_backend.cpp stages tensor
# I/O through CPU at the boundaries in this mode. See
# docs/AR_INTEGRATION_SPEC.md (WP-A) and DETAILED_CHANGELOG.md for the
# trade-off analysis.
#
# Version pin: 2.16.1 — the last org.tensorflow release that ships a real AAR
# on Maven Central (2.17+ are empty POM redirects to com.google.ai.edge.litert).
#
# Windows/Git-Bash note: on schannel CRYPT_E_NO_REVOCATION_CHECK curl errors,
# see the workaround note in tools/fetch_models.sh (ssl-no-revoke via CURL_HOME).
# =============================================================================

set -euo pipefail

TFLITE_VERSION="2.16.1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/tensorflow-lite"
JNILIBS="$ROOT/android/src/main/jniLibs"
TMP="$DEST/_aar_tmp"

MAVEN="https://repo1.maven.org/maven2/org/tensorflow"
RT_AAR="$MAVEN/tensorflow-lite/$TFLITE_VERSION/tensorflow-lite-$TFLITE_VERSION.aar"
GPU_AAR="$MAVEN/tensorflow-lite-gpu/$TFLITE_VERSION/tensorflow-lite-gpu-$TFLITE_VERSION.aar"

ABIS=(arm64-v8a armeabi-v7a x86 x86_64)

echo "==> Vendoring prebuilt TFLite $TFLITE_VERSION into $DEST"
rm -rf "$TMP"
mkdir -p "$TMP/rt" "$TMP/gpu" "$DEST/include"

echo "==> Downloading runtime AAR"
curl -fL --retry 3 -o "$TMP/rt.aar" "$RT_AAR"
echo "==> Downloading GPU delegate AAR"
curl -fL --retry 3 -o "$TMP/gpu.aar" "$GPU_AAR"

unzip -o -q "$TMP/rt.aar"  -d "$TMP/rt"
unzip -o -q "$TMP/gpu.aar" -d "$TMP/gpu"

echo "==> Vendoring headers"
cp -r "$TMP/rt/headers/." "$DEST/include/"
cp -r "$TMP/gpu/headers/." "$DEST/include/"

# The AAR's header set is incomplete: tensorflow/lite/core/c/c_api.h includes
# registration_external.h, which the AAR omits. Fetch the straggler(s) from the
# matching source tag so the C API actually compiles.
echo "==> Fetching headers the AAR omits"
RAW="https://raw.githubusercontent.com/tensorflow/tensorflow/v$TFLITE_VERSION"
EXTRA_HEADERS=(
  tensorflow/lite/core/c/registration_external.h
)
for h in "${EXTRA_HEADERS[@]}"; do
  mkdir -p "$DEST/include/$(dirname "$h")"
  curl -fL --retry 3 -o "$DEST/include/$h" "$RAW/$h"
done

echo "==> Vendoring libraries (link copies + APK-packaging copies)"
for abi in "${ABIS[@]}"; do
  mkdir -p "$DEST/lib/$abi" "$JNILIBS/$abi"
  cp "$TMP/rt/jni/$abi/libtensorflowlite_jni.so"      "$DEST/lib/$abi/"
  cp "$TMP/gpu/jni/$abi/libtensorflowlite_gpu_jni.so" "$DEST/lib/$abi/"
  cp "$TMP/rt/jni/$abi/libtensorflowlite_jni.so"      "$JNILIBS/$abi/"
  cp "$TMP/gpu/jni/$abi/libtensorflowlite_gpu_jni.so" "$JNILIBS/$abi/"
done

rm -rf "$TMP"

echo
echo "==> Vendored layout:"
find "$DEST/lib" "$JNILIBS" -name "*.so" | sed "s|$ROOT/||"
echo
echo "==> Headers: $(find "$DEST/include" -name '*.h' | wc -l) files"
echo "==> Done. CMake will detect this layout automatically (see CMakeLists.txt)."
echo "    NOTE: this is the CPU-staged-I/O mode. For zero-copy GL interop,"
echo "    use the from-source build (tools/fetch_tflite.sh + Bazel) instead."
