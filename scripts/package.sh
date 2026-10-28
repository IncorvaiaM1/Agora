#!/usr/bin/env bash
# Builds Final_Project.zip for Canvas submission from the committed tree.
# Uses git archive so build output and untracked files are never included.
# Usage: ./scripts/package.sh   (run from anywhere inside the repo)
set -euo pipefail

root="$(git rev-parse --show-toplevel)"
cd "$root"

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "warning: uncommitted changes are NOT included in the archive" >&2
fi

rm -f Final_Project.zip
git archive --format=zip --prefix=Final_Project/ -o Final_Project.zip HEAD
echo "wrote $root/Final_Project.zip ($(du -h Final_Project.zip | cut -f1))"
