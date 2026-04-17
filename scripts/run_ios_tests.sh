#!/usr/bin/env bash
# Creates a fresh iOS simulator, installs the pre-built test .app bundle,
# launches it, and tails both the app console and unified logs for the
# sentinel line emitted by tests/ios_main.cpp.
set -euo pipefail

APP="${APP:-}"
APP_NAME="${APP_NAME:-sdl3d_ios_tests.app}"
BUILD_DIR="${BUILD_DIR:-build/ios/iphonesimulator}"
BUNDLE_ID="${BUNDLE_ID:-com.sdl3d.tests}"
LOG_SUBSYSTEM="${LOG_SUBSYSTEM:-com.sdl3d.tests}"
SENTINEL="${SENTINEL:-SDL3D_TEST_RESULT}"
SIM_NAME="${SIM_NAME:-SDL3D Tests}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-240}"

SIM_UDID=""
SIM_DEVICE_NAME=""
CREATED_SIM=0
LAUNCH_PID=""
STREAM_PID=""
LOGFILE="$(mktemp)"

cleanup() {
    if [ -n "$LAUNCH_PID" ]; then
        kill "$LAUNCH_PID" 2>/dev/null || true
        wait "$LAUNCH_PID" 2>/dev/null || true
    fi
    if [ -n "$STREAM_PID" ]; then
        kill "$STREAM_PID" 2>/dev/null || true
        wait "$STREAM_PID" 2>/dev/null || true
    fi
    if [ -n "$SIM_UDID" ]; then
        xcrun simctl shutdown "$SIM_UDID" >/dev/null 2>&1 || true
        if [ "$CREATED_SIM" -eq 1 ]; then
            xcrun simctl delete "$SIM_UDID" >/dev/null 2>&1 || true
        fi
    fi
    rm -f "$LOGFILE"
}
trap cleanup EXIT

if [ -z "$APP" ]; then
    APP="$(find "$BUILD_DIR" -type d -name "$APP_NAME" | head -n 1 || true)"
fi

if [ -z "$APP" ] || [ ! -d "$APP" ]; then
    echo "iOS app bundle not found. Looked for $APP_NAME under $BUILD_DIR" >&2
    find "$BUILD_DIR" -maxdepth 4 -type d -name "*.app" -print 2>/dev/null || true
    exit 2
fi

RUNTIME_ID="${SIM_RUNTIME_ID:-}"
if [ -z "$RUNTIME_ID" ]; then
    RUNTIME_ID="$(
        xcrun simctl list runtimes | sed -En \
            '/^[[:space:]]*iOS /{/unavailable/d;s/.* - (com\.apple\.CoreSimulator\.SimRuntime\.iOS[-A-Za-z0-9]+).*/\1/p;q;}'
    )"
fi

RUNTIME_NAME="${SIM_RUNTIME_NAME:-}"
if [ -z "$RUNTIME_NAME" ]; then
    RUNTIME_NAME="$(
        xcrun simctl list runtimes | awk -v id="$RUNTIME_ID" '
            index($0, id) {
                line = $0
                sub(/[[:space:]]+\(.*/, "", line)
                sub(/^[[:space:]]*/, "", line)
                print line
                exit
            }
        '
    )"
fi

DEVICE_TYPE_ID="${SIM_DEVICE_TYPE_ID:-}"
if [ -z "$DEVICE_TYPE_ID" ]; then
    for device_name in "iPhone 17" "iPhone 16" "iPhone 15" "iPhone 14"; do
        DEVICE_TYPE_ID="$(
            xcrun simctl list devicetypes | awk -v want="$device_name" '
                index($0, want " (") == 1 {
                    line = $0
                    sub(/^.*\(/, "", line)
                    sub(/\)$/, "", line)
                    print line
                    exit
                }
            '
        )"
        if [ -n "$DEVICE_TYPE_ID" ]; then
            break
        fi
    done
fi

if [ -z "$RUNTIME_ID" ] || [ -z "$DEVICE_TYPE_ID" ]; then
    echo "Unable to determine an available iOS simulator runtime/device type" >&2
    xcrun simctl list runtimes
    xcrun simctl list devicetypes
    exit 2
fi

xcrun simctl delete unavailable >/dev/null 2>&1 || true

echo "Using runtime: $RUNTIME_ID"
SIM_UDID="$(
    xcrun simctl list devices available | awk -v runtime="$RUNTIME_NAME" '
        $0 == "-- " runtime " --" {
            in_runtime = 1
            next
        }
        /^-- / {
            in_runtime = 0
        }
        in_runtime && /^[[:space:]]*iPhone / {
            line = $0
            sub(/^[[:space:]]*/, "", line)
            name = line
            sub(/[[:space:]]+\(.*/, "", name)
            if (match(line, /\(([0-9A-F-]+)\)/)) {
                print substr(line, RSTART + 1, RLENGTH - 2)
                exit
            }
        }
    '
)"

if [ -n "$SIM_UDID" ]; then
    SIM_DEVICE_NAME="$(
        xcrun simctl list devices available | awk -v runtime="$RUNTIME_NAME" '
            $0 == "-- " runtime " --" {
                in_runtime = 1
                next
            }
            /^-- / {
                in_runtime = 0
            }
            in_runtime && /^[[:space:]]*iPhone / {
                line = $0
                sub(/^[[:space:]]*/, "", line)
                sub(/[[:space:]]+\(.*/, "", line)
                print line
                exit
            }
        '
    )"
    echo "Using existing simulator: ${SIM_DEVICE_NAME:-$SIM_UDID}"
else
    echo "Using device type: $DEVICE_TYPE_ID"
    SIM_UDID="$(xcrun simctl create "$SIM_NAME" "$DEVICE_TYPE_ID" "$RUNTIME_ID")"
    CREATED_SIM=1
fi

xcrun simctl boot "$SIM_UDID" >/dev/null
xcrun simctl bootstatus "$SIM_UDID" -b

# Capture the unified log stream in the background; the app's stdout/stderr
# is collected separately by simctl launch --console-pty and appended to the
# same logfile so CI shows both the gtest transcript and our os_log lines.
log stream \
    --style compact \
    --level debug \
    --predicate "subsystem == \"$LOG_SUBSYSTEM\"" >"$LOGFILE" 2>&1 &
STREAM_PID=$!

xcrun simctl install "$SIM_UDID" "$APP"
xcrun simctl launch --console-pty "$SIM_UDID" "$BUNDLE_ID" >>"$LOGFILE" 2>&1 &
LAUNCH_PID=$!

deadline=$((SECONDS + TIMEOUT_SECONDS))
rc=""
while [ -z "$rc" ] && [ $SECONDS -lt $deadline ]; do
    if grep -q "$SENTINEL:" "$LOGFILE" 2>/dev/null; then
        rc="$(grep "$SENTINEL:" "$LOGFILE" | tail -n 1 | sed -E "s/.*${SENTINEL}: ([0-9]+).*/\\1/")"
        break
    fi
    sleep 2
done

kill "$LAUNCH_PID" 2>/dev/null || true
wait "$LAUNCH_PID" 2>/dev/null || true
kill "$STREAM_PID" 2>/dev/null || true
wait "$STREAM_PID" 2>/dev/null || true
LAUNCH_PID=""
STREAM_PID=""

echo "----- iOS test log -----"
cat "$LOGFILE"
echo "----- end iOS test log -----"

if [ -z "$rc" ]; then
    echo "Test run did not report $SENTINEL within ${TIMEOUT_SECONDS}s" >&2
    exit 1
fi

echo "GoogleTest exit code: $rc"
exit "$rc"
