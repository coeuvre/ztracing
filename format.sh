#!/bin/bash
set -e

# Get the directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

# Get changed files using jj diff git headers
# This correctly captures modified, added, and renamed files by looking
# at the destination paths (+++ b/ lines).
files=$(jj diff --git | grep -E '^\+\+\+ ' | awk '{print $2}' | cut -c 3- | grep -E '\.(c|cc|cpp|h|hh|hpp)$' || true)

if [ -z "$files" ]; then
  echo "No changed C/C++ files to format."
  exit 0
fi

echo "Formatting changed C/C++ files..."
for file in $files; do
  if [ -f "$file" ]; then
    echo "  Formatting $file..."
    clang-format -i "$file"
  fi
done

echo "Done!"
