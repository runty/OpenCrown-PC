#!/usr/bin/env bash
# Public entry point; all generated output belongs on a mounted external disk.
set -euo pipefail
exec python3 "$(cd "$(dirname "$0")" && pwd)/bootstrap.py" "$@"
