#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
python3 "${ROOT}/GGUF_Chat_Window.py"
