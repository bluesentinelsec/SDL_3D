#!/usr/bin/env bash
# Invoked from inside reactivecircus/android-emulator-runner after the
# emulator is booted. Installs the pre-built APK, launches the SDLActivity
# that hosts our GoogleTest binary, and tails logcat for a sentinel line
# emitted by tests/android_main.cpp.
set -euo pipefail

APK="${APK:-tests/android/app/build/outputs/apk/debug/app-debug.apk}"
PKG="${PKG:-com.sdl3d.tests}"
ACTIVITY="${ACTIVITY:-com.sdl3d.tests.TestActivity}"
SENTINEL="SDL3D_TEST_RESULT"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-180}"

if [ ! -f "$APK" ]; then
    echo "APK not found at $APK" >&2
    exit 2
fi

adb wait-for-device
adb install -r -g "$APK"

# Start with an empty logcat buffer so we only see test output from this run.
adb logcat -c

adb shell am start -W -n "$PKG/$ACTIVITY" >/dev/null

# Stream the SDL / stdout / stderr / our sentinel tags to the job log and
# watch for the sentinel line. Backgrounded so we can enforce a timeout.
LOGFILE="$(mktemp)"
trap 'rm -f "$LOGFILE"' EXIT

adb logcat -v brief SDL3D_TEST:I SDL:V SDL/APP:V stdout:V stderr:V AndroidRuntime:E *:S > "$LOGFILE" &
LOGCAT_PID=$!

deadline=$(( SECONDS + TIMEOUT_SECONDS ))
rc=""
while [ -z "$rc" ] && [ $SECONDS -lt $deadline ]; do
    if grep -q "$SENTINEL:" "$LOGFILE" 2>/dev/null; then
        rc=$(grep "$SENTINEL:" "$LOGFILE" | tail -n 1 | sed -E "s/.*${SENTINEL}: ([0-9]+).*/\1/")
        break
    fi
    # Quit early if the process crashed.
    if grep -q "FATAL EXCEPTION" "$LOGFILE" 2>/dev/null; then
        break
    fi
    sleep 2
done

kill "$LOGCAT_PID" 2>/dev/null || true
wait "$LOGCAT_PID" 2>/dev/null || true

echo "----- logcat tail -----"
cat "$LOGFILE"
echo "----- end logcat -----"

if [ -z "$rc" ]; then
    echo "Test run did not report $SENTINEL within ${TIMEOUT_SECONDS}s"
    exit 1
fi

echo "GoogleTest exit code: $rc"
exit "$rc"
