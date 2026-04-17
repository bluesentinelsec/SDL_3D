#!/usr/bin/env bash

set -euo pipefail

files=()

while IFS= read -r file; do
    files+=("${file}")
done < <(git ls-files --cached --others --exclude-standard -- '*.c' '*.h')

if [ "${#files[@]}" -eq 0 ]; then
    echo "No C source files tracked by git."
    exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
