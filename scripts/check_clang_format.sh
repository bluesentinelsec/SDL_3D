#!/usr/bin/env bash

set -euo pipefail

files=()

while IFS= read -r file; do
    if [ -f "${file}" ]; then
        files+=("${file}")
    fi
done < <(git ls-files --cached --others --exclude-standard -- '*.c' '*.cc' '*.cpp' '*.h' '*.hh' '*.hpp' ':!:vendor/**')

if [ "${#files[@]}" -eq 0 ]; then
    echo "No source files tracked by git."
    exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
