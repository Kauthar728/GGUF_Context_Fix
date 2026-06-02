#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "llama.h"

static void log_errors_only(enum ggml_log_level level, const char * text, void * user_data) {
    (void)user_data;
    if (level >= GGML_LOG_LEVEL_ERROR && text) {
        fputs(text, stderr);
    }
}

static void print_usage(const char * argv0) {
    fprintf(stderr, "usage: %s <model.gguf> <prompt> [max_tokens]\n", argv0);
}

static int env_to_int(const char * name, int fallback_value) {
    const char * raw;
    char * end;
    long parsed;

    raw = getenv(name);
    if (!raw || raw[0] == '\0') {
        return fallback_value;
    }

    parsed = strtol(raw, &end, 10);
    if (end == raw) {
        return fallback_value;
    }

    return (int)parsed;
}

static int detect_thread_count(void) {
    long detected = sysconf(_SC_NPROCESSORS_ONLN);
    if (detected <= 0) {
        return 8;
    }
    if (detected > 32) {
        return 32;
    }
    return (int)detected;
}

static int append_piece(char ** text, size_t * used, size_t * capacity, const char * piece, size_t piece_len) {
    size_t required;
    char * grown;

    if (!text || !used || !capacity || !piece) {
        return -1;
    }

    required = *used + piece_len + 1;
    if (required > *capacity) {
        size_t next_capacity = *capacity ? *capacity : 256;
        while (next_capacity < required) {
            next_capacity *= 2;
        }
        grown = (char *)realloc(*text, next_capacity);
        if (!grown) {
            return -1;
        }
        *text = grown;
        *capacity = next_capacity;
    }

    memcpy(*text + *used, piece, piece_len);
    *used += piece_len;
    (*text)[*used] = '\0';
    return 0;
}

static int format_prompt(
    const struct llama_model * model,
    const char * user_prompt,
    char ** out_prompt
) {
    const char * tmpl;
    struct llama_chat_message msg;
    int32_t size_needed;
    char * buffer;
    int written;
    size_t fallback_size;

    if (!model || !user_prompt || !out_prompt) {
        return -1;
    }

    tmpl = llama_model_chat_template(model, NULL);
    if (!tmpl || tmpl[0] == '\0') {
        fallback_size = strlen(user_prompt) + 64;
        buffer = (char *)malloc(fallback_size);
        if (!buffer) {
            return -1;
        }
        snprintf(buffer, fallback_size, "User: %s\nAssistant:", user_prompt);
        *out_prompt = buffer;
        return 0;
    }

    msg.role = "user";
    msg.content = user_prompt;

    size_needed = llama_chat_apply_template(tmpl, &msg, 1, true, NULL, 0);
    if (size_needed <= 0) {
        return -1;
    }

    buffer = (char *)malloc((size_t)size_needed + 1);
    if (!buffer) {
        return -1;
    }

    written = llama_chat_apply_template(tmpl, &msg, 1, true, buffer, size_needed + 1);
    if (written < 0) {
        free(buffer);
        return -1;
    }

    buffer[written] = '\0';
    *out_prompt = buffer;
    return 0;
}

int main(int argc, char ** argv) {
    const char * model_path;
    const char * user_prompt;
    int max_tokens;
    int n_gpu_layers;
    int n_ctx;
    int n_threads;
    struct llama_model_params model_params;
    struct llama_context_params ctx_params;
    struct llama_sampler * sampler;
    struct llama_model * model;
    struct llama_context * ctx;
    const struct llama_vocab * vocab;
    char * prompt;
    int32_t prompt_token_count;
    llama_token * prompt_tokens;
    struct llama_batch batch;
    char * response;
    size_t response_used;
    size_t response_capacity;
    int token_index;
    int rc;

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    model_path = argv[1];
    user_prompt = argv[2];
    max_tokens = argc > 3 ? atoi(argv[3]) : 96;
    if (max_tokens <= 0) {
        max_tokens = 96;
    }

    n_gpu_layers = env_to_int("GGUF_CHAT_NGL", 99);
    n_ctx = env_to_int("GGUF_CHAT_CTX", 1024);
    n_threads = env_to_int("GGUF_CHAT_THREADS", detect_thread_count());
    if (n_ctx <= 0) {
        n_ctx = 1024;
    }
    if (n_threads <= 0) {
        n_threads = detect_thread_count();
    }

    llama_log_set(log_errors_only, NULL);
    ggml_backend_load_all();
    llama_backend_init();

    model_params = llama_model_default_params();
    model_params.n_gpu_layers = n_gpu_layers;
    model_params.use_mmap = true;
    model_params.use_mlock = false;

    model = llama_model_load_from_file(model_path, model_params);
    if (!model) {
        fprintf(stderr, "failed to load model: %s\n", model_path);
        llama_backend_free();
        return 1;
    }

    vocab = llama_model_get_vocab(model);
    ctx_params = llama_context_default_params();
    ctx_params.n_ctx = (uint32_t)n_ctx;
    /* FIX: n_batch was set to n_ctx causing double memory allocation
     * Batch only needs to handle the initial prompt, not full context */
    ctx_params.n_batch = (uint32_t)(n_ctx < 512 ? n_ctx : 512);
    ctx_params.n_threads = n_threads;
    ctx_params.n_threads_batch = n_threads;

    ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        fprintf(stderr, "failed to create context\n");
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    prompt = NULL;
    if (format_prompt(model, user_prompt, &prompt) != 0) {
        fprintf(stderr, "failed to format chat prompt\n");
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    prompt_token_count = -llama_tokenize(vocab, prompt, (int32_t)strlen(prompt), NULL, 0, true, true);
    if (prompt_token_count <= 0) {
        fprintf(stderr, "failed to measure prompt token count\n");
        free(prompt);
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    prompt_tokens = (llama_token *)malloc(sizeof(llama_token) * (size_t)prompt_token_count);
    if (!prompt_tokens) {
        fprintf(stderr, "failed to allocate prompt token buffer\n");
        free(prompt);
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    rc = llama_tokenize(vocab, prompt, (int32_t)strlen(prompt), prompt_tokens, prompt_token_count, true, true);
    if (rc < 0) {
        fprintf(stderr, "failed to tokenize prompt\n");
        free(prompt_tokens);
        free(prompt);
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        llama_backend_free();
        return 1;
    }

    batch = llama_batch_get_one(prompt_tokens, prompt_token_count);
    response = NULL;
    response_used = 0;
    response_capacity = 0;

    printf("model: %s\n", model_path);
    printf("prompt: %s\n", user_prompt);
    printf("runtime: n_gpu_layers=%d n_ctx=%d n_threads=%d\n", n_gpu_layers, n_ctx, n_threads);
    printf("response:\n");

    for (token_index = 0; token_index < max_tokens; ++token_index) {
        char piece_buf[256];
        int piece_len;
        llama_token token_id;

        if (llama_memory_seq_pos_max(llama_get_memory(ctx), 0) + batch.n_tokens > (llama_pos)llama_n_ctx(ctx)) {
            fprintf(stderr, "\ncontext size exceeded\n");
            break;
        }

        rc = llama_decode(ctx, batch);
        if (rc != 0) {
            fprintf(stderr, "\nllama_decode failed: %d\n", rc);
            break;
        }

        token_id = llama_sampler_sample(sampler, ctx, -1);
        if (llama_vocab_is_eog(vocab, token_id)) {
            break;
        }

        piece_len = llama_token_to_piece(vocab, token_id, piece_buf, (int32_t)sizeof(piece_buf), 0, true);
        if (piece_len < 0) {
            fprintf(stderr, "\nfailed to convert token to text\n");
            break;
        }

        if (append_piece(&response, &response_used, &response_capacity, piece_buf, (size_t)piece_len) != 0) {
            fprintf(stderr, "\nfailed to grow response buffer\n");
            break;
        }

        fwrite(piece_buf, 1, (size_t)piece_len, stdout);
        fflush(stdout);

        batch = llama_batch_get_one(&token_id, 1);
    }

    printf("\n");

    free(response);
    free(prompt_tokens);
    free(prompt);
    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return 0;
}
