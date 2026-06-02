# GGUF Context Fix - VBSTYLE

## Files

| File | Description | VBSTYLE Compliance |
|------|-------------|-------------------|
| `InferenceWorld.c` | VBSTYLE World class for resident model execution | Full Ghost header, bracket markers, boot\|chat\|cleanup lifecycle |
| `LMDBWorld.c` | VBSTYLE world for persistent LMDB runtime storage | Full Ghost header, standalone boot\|execute_text\|health\|cleanup lifecycle |
| `MemUnit.c` | In-memory orchestration surface for cache, reports, output queue, search, assembly, runtime bindings, session state, execution routes | MemUnit authority, SQLite RAM truth, VBSTYLE sections |
| `Main_Driver.c` | CLI driver + interactive shell | Thin driver, binds worlds into MemUnit, all runtime flows through MemUnit |
| `model_room/` | Resident-model room for Falcon manifest + local hotcache path ownership | Runtime-created roommate surface |
| `README.md` | This documentation | - |
| `test_compile.sh` | Build verification script | - |
| `build_real.sh` | Real build script for this machine | Uses tested llama include + GlobalLibs dylibs |
| `gguf_context_fix` | Built binary | Built and tested against Falcon GGUF |

## The Fix: Context Doubling Bug

`InferenceWorld.c` now properly caps `n_batch` at `512` instead of setting it equal to `n_ctx`:

```c
ctx_params.n_batch = (uint32_t)(self->n_ctx < 512 ? self->n_ctx : 512);
```

**Impact:** ~25% memory savings on batch buffer allocation.

## Runtime Shape

This folder now runs as:

`CLI/AI -> Main_Driver -> MemUnit -> World`

- `InferenceWorld` owns resident model boot and chat generation
- `LMDBWorld` owns persistent key/value runtime storage through LMDB
- `LMDBWorld` also records the resident Falcon manifest, room path, and local hotcache path
- `MemUnit` owns RAM-side SQLite truth:
  `worlds`, `verb_registry`, `cache`, `packets`, `bus_messages`, `gui_widgets`, `gui_layouts`, `gui_state`, `gui_actions`, `orchestrator_queue`, `reports`, `output_queue`, `session_state`, `runtime_counter`, `execution_route`
- `Main_Driver` stays thin and only binds the world and hands commands into `MemUnit`
- `Main_Driver` no longer chats the model directly; `MemUnit` is the execution authority
- `MemUnit` routes execution through `MemBus`, stores runtime truth in `MemDB`, and records report/output state separately

## VBSTYLE Structure

The core files follow VBSTYLE principles:

1. **Ghost header** at top - declares purpose, inputs, outputs, requirements
2. **Bracket markers** on every function - `# [role|behavior|domain]`
3. **Single authority** - one World per file
4. **Lifecycle methods** - `boot|chat|health|cleanup`
5. **Param-validate-execute** pattern in all methods
6. **No main()** in world files - separate driver handles CLI
7. **ResultT returns** - status codes and error messages

## Building

```bash
# Syntax verification
./test_compile.sh

# Real build on this machine
./build_real.sh

# One-shot
DYLD_LIBRARY_PATH=/usr/local/lib/GlobalLibs ./gguf_context_fix model.gguf "Hello, how are you?"

# Interactive shell
DYLD_LIBRARY_PATH=/usr/local/lib/GlobalLibs ./gguf_context_fix model.gguf --shell
```

Shell extras:

- `/roommate` prints the resident model manifest stored in LMDB
- `/lmdb list resident_` lists the roommate keys

## Test

```bash
./test_compile.sh
```

## Notes

This package currently uses:

- `libllama` + `libggml` for model execution
- in-RAM SQLite in `MemUnit`
- LMDB in `LMDBWorld`

## Verified

The following were verified:

1. `./test_compile.sh` passes
2. `./build_real.sh` builds the binary
3. `./gguf_context_fix` runs against Falcon
4. one-shot chat returns a real model response
5. `MemUnit` reports live cache/report/output/session/route state after execution
6. `LMDBWorld` is built into the package and stores runtime keys under `GGUF_Context_Fix/runtime_lmdb`
7. the resident model manifest is written under `GGUF_Context_Fix/model_room`

## Original Files

Original buggy sources were at:
- `/Users/Shared/Share_All/Cascade_Tools/VB_ai_Dec/Project_PropPanel/COMPLETE_RESTORATION/C/gguf_llama_chat.c`
- `/Users/Shared/Share_All/Cascade_Tools/VB_ai_Dec/Project_PropPanel/COMPLETE_RESTORATION/C/InferenceWorld.c`

These VBSTYLE versions are now at:
- `/Users/waynephilliplundall/testbed/GGUF_Context_Fix/`
