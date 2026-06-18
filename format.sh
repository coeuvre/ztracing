#!/bin/bash
set -e

# Get the directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

# Get changed files using jj diff --summary
# Filter for lines starting with M (Modified) or A (Added)
# and extract the file path (second column).
# Then filter for C/C++ source and header extensions.
files=$(jj diff --summary | awk '/^[MA] / {print $2}' | grep -E '\.(c|cc|cpp|h|hh|hpp)$' || true)

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
