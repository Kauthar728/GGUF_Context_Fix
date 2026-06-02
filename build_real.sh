#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT_BIN="${ROOT}/gguf_context_fix"
LLAMA_INCLUDE_DEFAULT="/Users/waynephilliplundall/Library/Python/3.13/lib/python/site-packages/include"
LLAMA_LIB_DEFAULT="/usr/local/lib/GlobalLibs"
LMDB_ROOT_DEFAULT="/Users/waynephilliplundall/testbed/lmdb-40d3741b7d40ba4c75cb91dd9987ce692d376d71/libraries/liblmdb"

LLAMA_INCLUDE_DIR="${LLAMA_INCLUDE_DIR:-$LLAMA_INCLUDE_DEFAULT}"
LLAMA_LIB_DIR="${LLAMA_LIB_DIR:-$LLAMA_LIB_DEFAULT}"
LMDB_ROOT_DIR="${LMDB_ROOT_DIR:-$LMDB_ROOT_DEFAULT}"

echo "Building GGUF Context Fix..."
echo "  include: ${LLAMA_INCLUDE_DIR}"
echo "  libs:    ${LLAMA_LIB_DIR}"
echo "  lmdb:    ${LMDB_ROOT_DIR}"
echo "  output:  ${OUT_BIN}"

clang -O2 \
    -I"${LLAMA_INCLUDE_DIR}" \
    -I"${LMDB_ROOT_DIR}" \
    -L"${LLAMA_LIB_DIR}" \
    -Wl,-rpath,"${LLAMA_LIB_DIR}" \
    -o "${OUT_BIN}" \
    "${ROOT}/Main_Driver.c" \
    "${ROOT}/MemUnit.c" \
    "${ROOT}/InferenceWorld.c" \
    "${ROOT}/LMDBWorld.c" \
    "${LMDB_ROOT_DIR}/liblmdb.a" \
    -lsqlite3 \
    -lllama \
    -lggml

echo "Build complete: ${OUT_BIN}"
echo "Run example:"
echo "  DYLD_LIBRARY_PATH=${LLAMA_LIB_DIR} ${OUT_BIN} /path/to/model.gguf \"Say hello in five words.\""
