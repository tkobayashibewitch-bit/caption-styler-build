#!/bin/bash
# Caption Styler build script (macOS)
# Usage:  ./build_mac.sh /path/to/AE_SDK
# Builds a universal (Intel + Apple silicon) CaptionStyler.plugin from the SDK's Skeleton sample.
set -euo pipefail
SDK="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

SKEL="$(find "$SDK" -type d -name Skeleton -path '*Examples*' | head -n 1)"
[ -n "$SKEL" ] || { echo "Skeleton sample not found under $SDK"; exit 1; }

PROJ="$(dirname "$SKEL")/CaptionStyler"
rm -rf "$PROJ"; cp -R "$SKEL" "$PROJ"

cp "$HERE/CaptionStyler.h"   "$PROJ/CaptionStyler.h"
cp "$HERE/CaptionStyler.cpp" "$PROJ/Skeleton.cpp"
echo '#include "CaptionStyler.h"' > "$PROJ/Skeleton.h"
echo '// not used'               > "$PROJ/Skeleton_Strings.cpp"
cp "$HERE/CaptionStylerPiPL.r" "$PROJ/SkeletonPiPL.r"

XPROJ="$(find "$PROJ/Mac" -maxdepth 1 -name '*.xcodeproj' | head -n 1)"
[ -n "$XPROJ" ] || { echo "Xcode project not found in $PROJ/Mac"; exit 1; }

OUT="$HERE/build_mac"; rm -rf "$OUT"; mkdir -p "$OUT"
# The SDK template only has a Debug configuration; turn on optimization explicitly.
xcodebuild -project "$XPROJ" -configuration Debug \
  ARCHS="x86_64 arm64" ONLY_ACTIVE_ARCH=NO MACOSX_DEPLOYMENT_TARGET=11.0 \
  GCC_OPTIMIZATION_LEVEL=s ENABLE_TESTABILITY=NO DEBUG_INFORMATION_FORMAT=dwarf \
  PRODUCT_BUNDLE_IDENTIFIER=com.tkb.CaptionStyler \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_REQUIRED=NO \
  CONFIGURATION_BUILD_DIR="$OUT/xc" build

PLUG="$(find "$OUT/xc" -maxdepth 1 -name '*.plugin' | head -n 1)"
[ -n "$PLUG" ] || { echo "Build finished but no .plugin was found"; exit 1; }
rm -rf "$OUT/CaptionStyler.plugin"
cp -R "$PLUG" "$OUT/CaptionStyler.plugin"
codesign --force --deep --sign - "$OUT/CaptionStyler.plugin"
lipo -info "$OUT/CaptionStyler.plugin/Contents/MacOS/"*
echo "OK: $OUT/CaptionStyler.plugin"
