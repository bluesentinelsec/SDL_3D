#!/usr/bin/env bash
# Invoked by the Android CI job (reactivecircus/android-emulator-runner)
# *after* the emulator has booted. The action executes each line of its
# `script:` input as a separate `sh -c`, so we keep all control flow here
# in a single script file and invoke it from the workflow as one command.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build/android}"
DEVICE_DIR="/data/local/tmp"

adb wait-for-device

# SDL3 may build as a shared library; push it next to the tests if so.
SDL_SO="$(find "$BUILD_DIR" -name 'libSDL3.so' | head -n 1 || true)"
if [ -n "$SDL_SO" ]; then
    adb push "$SDL_SO" "$DEVICE_DIR/libSDL3.so"
fi

# The NDK's libc++_shared.so is only needed when ANDROID_STL=c++_shared,
# but pushing it is cheap insurance and matches the Vigil reference.
LIBCXX="$(find "$ANDROID_NDK_HOME/toolchains" \
    -name 'libc++_shared.so' \
    -path '*/x86_64-linux-android/*' | head -n 1 || true)"
if [ -n "$LIBCXX" ]; then
    adb push "$LIBCXX" "$DEVICE_DIR/libc++_shared.so"
fi

failures=0
for t in "$BUILD_DIR"/tests/sdl3d_*_test; do
    [ -f "$t" ] || continue
    name="$(basename "$t")"
    echo "::group::$name"
    adb push "$t" "$DEVICE_DIR/$name"
    adb shell chmod 755 "$DEVICE_DIR/$name"
    if ! adb shell "cd $DEVICE_DIR && LD_LIBRARY_PATH=$DEVICE_DIR SDL_VIDEO_DRIVER=offscreen ./$name"; then
        failures=$((failures + 1))
        echo "::error::$name failed"
    fi
    echo "::endgroup::"
done

if [ "$failures" -ne 0 ]; then
    echo "$failures test binary(ies) failed"
    exit 1
fi
