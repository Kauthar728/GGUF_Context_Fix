#!/bin/bash
# Test compilation of VBSTYLE GGUF Context Fix files

echo "Testing VBSTYLE C files..."
echo "=========================="
echo ""

# Create mock llama.h for syntax checking
cat > /tmp/llama.h << 'EOF'
#ifndef MOCK_LLAMA_H
#define MOCK_LLAMA_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef int llama_log_level;
enum ggml_log_level { GGML_LOG_LEVEL_NONE = 0, GGML_LOG_LEVEL_INFO = 1, GGML_LOG_LEVEL_ERROR = 2 };
typedef struct llama_model llama_model;
typedef struct llama_context llama_context;
typedef struct llama_vocab llama_vocab;
typedef struct llama_sampler llama_sampler;
typedef struct llama_chat_message { const char * role; const char * content; } llama_chat_message;
typedef struct llama_batch { int n_tokens; } llama_batch;
typedef int llama_pos;
typedef int llama_token;
typedef int llama_seq_id;
typedef struct llama_memory llama_memory;

#define LLAMA_DEFAULT_SEED 0

struct llama_model_params { int n_gpu_layers; bool use_mmap; bool use_mlock; };
struct llama_context_params { uint32_t n_ctx; uint32_t n_batch; int n_threads; int n_threads_batch; };

static inline void llama_log_set(void (*fn)(enum ggml_log_level, const char*, void*), void* user) {}
static inline void ggml_backend_load_all(void) {}
static inline void llama_backend_init(void) {}
static inline void llama_backend_free(void) {}
static inline struct llama_model_params llama_model_default_params(void) { struct llama_model_params p = {0}; return p; }
static inline struct llama_context_params llama_context_default_params(void) { struct llama_context_params p = {0}; return p; }
static inline llama_model* llama_model_load_from_file(const char* path, struct llama_model_params p) { return NULL; }
static inline void llama_model_free(llama_model* m) {}
static inline const llama_vocab* llama_model_get_vocab(llama_model* m) { return NULL; }
static inline const char* llama_model_chat_template(const llama_model* m, const char* n) { return NULL; }
static inline llama_context* llama_init_from_model(llama_model* m, struct llama_context_params p) { return NULL; }
static inline void llama_free(llama_context* c) {}
static inline int llama_tokenize(const llama_vocab* v, const char* t, int l, llama_token* o, int s, bool a, bool b) { return 0; }
static inline int llama_token_to_piece(const llama_vocab* v, llama_token t, char* b, int s, int o, bool sp) { return 0; }
static inline bool llama_vocab_is_eog(const llama_vocab* v, llama_token t) { return false; }
static inline llama_sampler* llama_sampler_chain_default_params(void) { return NULL; }
static inline llama_sampler* llama_sampler_chain_init(llama_sampler* p) { return NULL; }
static inline void llama_sampler_chain_add(llama_sampler* s, llama_sampler* a) {}
static inline void llama_sampler_free(llama_sampler* s) {}
static inline llama_token llama_sampler_sample(llama_sampler* s, llama_context* c, int i) { return 0; }
static inline llama_batch llama_batch_get_one(llama_token* t, int n) { llama_batch b = {n}; return b; }
static inline int llama_decode(llama_context* c, llama_batch b) { return 0; }
static inline llama_memory* llama_get_memory(llama_context* c) { return NULL; }
static inline void llama_memory_clear(llama_memory* m, bool s) {}
static inline int llama_memory_seq_pos_max(llama_memory* m, llama_seq_id s) { return 0; }
static inline int llama_n_ctx(llama_context* c) { return 0; }
static inline llama_sampler* llama_sampler_init_min_p(float p, int m) { return NULL; }
static inline llama_sampler* llama_sampler_init_temp(float t) { return NULL; }
static inline llama_sampler* llama_sampler_init_dist(int s) { return NULL; }
static inline int32_t llama_chat_apply_template(const char* tmpl, llama_chat_message* m, size_t n, bool a, char* b, int32_t s) { return 0; }

#endif
EOF

# Test InferenceWorld.c
echo -n "Testing InferenceWorld.c... "
if clang -c -fsyntax-only -I/tmp InferenceWorld.c -o /dev/null 2>&1 | grep -q error; then
    echo "✗ FAILED"
    clang -c -fsyntax-only -I/tmp InferenceWorld.c 2>&1 | head -20
    exit 1
else
    echo "✓ PASSED"
fi

# Test MemUnit.c
echo -n "Testing MemUnit.c... "
if clang -c -fsyntax-only MemUnit.c -o /dev/null 2>&1 | grep -q error; then
    echo "✗ FAILED"
    clang -c -fsyntax-only MemUnit.c 2>&1 | head -20
    exit 1
else
    echo "✓ PASSED"
fi

# Test LMDBWorld.c
echo -n "Testing LMDBWorld.c... "
if clang -c -fsyntax-only -I/Users/waynephilliplundall/testbed/lmdb-40d3741b7d40ba4c75cb91dd9987ce692d376d71/libraries/liblmdb LMDBWorld.c -o /dev/null 2>&1 | grep -q error; then
    echo "✗ FAILED"
    clang -c -fsyntax-only -I/Users/waynephilliplundall/testbed/lmdb-40d3741b7d40ba4c75cb91dd9987ce692d376d71/libraries/liblmdb LMDBWorld.c 2>&1 | head -20
    exit 1
else
    echo "✓ PASSED"
fi

# Test Main_Driver.c
echo -n "Testing Main_Driver.c... "
if clang -c -fsyntax-only Main_Driver.c -o /dev/null 2>&1 | grep -q error; then
    echo "✗ FAILED"
    clang -c -fsyntax-only Main_Driver.c 2>&1 | head -20
    exit 1
else
    echo "✓ PASSED"
fi

echo ""
echo "=========================="
echo "All VBSTYLE files compile!"
echo ""
echo "Files in folder:"
ls -la *.c *.md *.sh 2>/dev/null || ls -la
echo ""

# Cleanup
rm -f /tmp/llama.h
