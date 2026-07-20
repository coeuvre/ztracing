#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

mapfile -t changed_files < <(jj diff --name-only)

rust_files=()
cpp_files=()
for file in "${changed_files[@]}"; do
  # Deleted files are reported by jj but cannot be formatted.
  if [ ! -f "$file" ]; then
    continue
  fi
  if [[ "$file" == *.rs ]]; then
    rust_files+=("$file")
  elif [[ "$file" == *.c || "$file" == *.cc || "$file" == *.h ]]; then
    cpp_files+=("$file")
  fi
done

if [ "${#rust_files[@]}" -gt 0 ]; then
  echo "Formatting changed Rust files..."
  rustfmt --edition 2024 "${rust_files[@]}"
fi

if [ "${#cpp_files[@]}" -gt 0 ]; then
  echo "Formatting changed C++ files..."
  clang-format -i "${cpp_files[@]}"
fi

echo "Done!"
