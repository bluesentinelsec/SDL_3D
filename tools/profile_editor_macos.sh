#!/bin/zsh

set -euo pipefail

script_dir=${0:A:h}
repo_root=${script_dir:h}
editor_bin=${SLAYER3D_EDITOR_BIN:-"${repo_root}/build/editor-profile/slayer3d_editor"}
timestamp=$(date '+%Y%m%d-%H%M%S')
artifact_dir=${SLAYER3D_PROFILE_DIR:-"${repo_root}/build/profiles/editor-${timestamp}"}
log_path="${artifact_dir}/editor.log"
trace_path="${artifact_dir}/time-profile.trace"
metadata_path="${artifact_dir}/metadata.txt"

if [[ $(uname -s) != Darwin ]]; then
    print -u2 "profile_editor_macos.sh requires macOS"
    exit 1
fi
if ! command -v xcrun >/dev/null 2>&1; then
    print -u2 "xcrun is required; install the Xcode command-line tools"
    exit 1
fi
if [[ ! -x ${editor_bin} ]]; then
    print -u2 "instrumented editor not found: ${editor_bin}"
    print -u2 "build it with: cmake --preset editor-profile && cmake --build --preset editor-profile"
    exit 1
fi

mkdir -p "${artifact_dir}"
{
    print "started_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    print "repository=${repo_root}"
    print "branch=$(git -C "${repo_root}" branch --show-current)"
    print "commit=$(git -C "${repo_root}" rev-parse HEAD)"
    print "editor=${editor_bin}"
    print "arguments=${(q)@}"
    print "host=$(uname -a)"
    print "xctrace=$(xcrun xctrace version 2>&1 | tr '\n' ' ')"
} >"${metadata_path}"

print "Recording editor profile in ${artifact_dir}"
print "Drive the editor normally, then close it to finalize the recording."

SLAYER3D_PROFILE_FRAMES=1 "${editor_bin}" --window-mode windowed "$@" >"${log_path}" 2>&1 &
editor_pid=$!

cleanup() {
    if kill -0 ${editor_pid} 2>/dev/null; then
        kill -TERM ${editor_pid} 2>/dev/null || true
    fi
}
trap cleanup INT TERM EXIT

sleep 1
trace_status=0
if kill -0 ${editor_pid} 2>/dev/null; then
    xcrun xctrace record --template 'Time Profiler' --output "${trace_path}" --attach ${editor_pid} || trace_status=$?
else
    trace_status=1
fi

editor_status=0
wait ${editor_pid} || editor_status=$?
trap - INT TERM EXIT
{
    print "finished_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    print "editor_exit_status=${editor_status}"
    print "xctrace_exit_status=${trace_status}"
} >>"${metadata_path}"

print "Profile complete:"
print "  Log:   ${log_path}"
if [[ -d ${trace_path} ]]; then
    print "  Trace: ${trace_path}"
else
    print -u2 "  Trace capture failed; inspect ${metadata_path} and the terminal output"
fi
print "  Meta:  ${metadata_path}"

if (( editor_status != 0 )); then
    exit ${editor_status}
fi
exit ${trace_status}
