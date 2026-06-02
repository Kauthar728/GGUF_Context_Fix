#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "Testing OutputWorld.c compilation (syntax only)..."

# Compile OutputWorld.c alone to check for syntax errors
clang -c -O2 \
    "${ROOT}/OutputWorld.c" \
    -o /tmp/outputworld_test.o \
    -lsqlite3 2>&1

if [ $? -eq 0 ]; then
    echo "OutputWorld.c: SYNTAX OK"
    rm -f /tmp/outputworld_test.o
else
    echo "OutputWorld.c: COMPILE FAILED"
    exit 1
fi
