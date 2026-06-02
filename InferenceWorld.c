/*
#   Ghost[World:InferenceWorld Domain:GGUF_Inference Purpose:Resident_Model_Execution_Bridge
#   Owns:resident_model_context_and_chat_generation Accepts:model_path|prompt|max_tokens Returns:response_text|chat_status|health_snapshot
#   Requires:llama_cpp_bridge_runtime Exposes:boot|chat|health|cleanup
#   State:model_path|model|ctx|vocab|booted|max_ctx|last_response|last_message Health:inference_status_snapshot Tags:world,inference,bridge,resident,chat ]
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "llama.h"

#define INFERENCEWORLD_PATH 1024
#define INFERENCEWORLD_TEXT 4096

typedef struct InferenceWorld_State {
    char model_path[INFERENCEWORLD_PATH];
    struct llama_model * model;
    struct llama_context * ctx;
    const struct llama_vocab * vocab;
    int n_gpu_layers;
    int n_ctx;
    int n_threads;
    int is_booted;
    int health_code;
    char last_message[256];
    char last_response[INFERENCEWORLD_TEXT];
} InferenceWorld_State;

static const char * InferenceWorld_model_name(const char * model_path) {
    const char * slash;
    if (!model_path || model_path[0] == '\0') {
        return "unknown.gguf";
    }
    slash = strrchr(model_path, '/');
    return slash ? slash + 1 : model_path;
}

InferenceWorld_State * InferenceWorld_create(void) {
    InferenceWorld_State * self = (InferenceWorld_State *)calloc(1, sizeof(InferenceWorld_State));
    return self;
}

static void InferenceWorld_log_errors_only(enum ggml_log_level level, const char * text, void * user_data) {
    (void)user_data;
    if (level >= GGML_LOG_LEVEL_ERROR && text) {
        fputs(text, stderr);
    }
}

static int InferenceWorld_env_to_int(const char * name, int fallback_value) {
    const char * raw = getenv(name);
    char * end;
    long parsed;
    if (!raw || raw[0] == '\0') {
        return fallback_value;
    }
    parsed = strtol(raw, &end, 10);
    if (end == raw) {
        return fallback_value;
    }
    return (int)parsed;
}

static int InferenceWorld_detect_thread_count(void) {
    long detected = sysconf(_SC_NPROCESSORS_ONLN);
    if (detected <= 0) {
        return 8;
    }
    if (detected > 32) {
        return 32;
    }
    return (int)detected;
}

static void InferenceWorld_set_status(InferenceWorld_State * self, int health_code, const char * message) {
    if (!self) {
        return;
    }
    self->health_code = health_code;
    if (message) {
        snprintf(self->last_message, sizeof(self->last_message), "%s", message);
    }
}

static int InferenceWorld_append_piece(char * text, size_t text_size, size_t * used, const char * piece, size_t piece_len) {
    if (!text || !used || !piece) {
        return -1;
    }
    if (*used + piece_len + 1 > text_size) {
        return -1;
    }
    memcpy(text + *used, piece, piece_len);
    *used += piece_len;
    text[*used] = '\0';
    return 0;
}

static int InferenceWorld_format_prompt(
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

int InferenceWorld_boot(InferenceWorld_State * self, const char * model_path) {
    struct llama_model_params model_params;
    struct llama_context_params ctx_params;

    if (!self || !model_path || model_path[0] == '\0') {
        return -1;
    }

    memset(self, 0, sizeof(*self));
    snprintf(self->model_path, sizeof(self->model_path), "%s", model_path);
    self->n_gpu_layers = InferenceWorld_env_to_int("GGUF_CHAT_NGL", 99);
    self->n_ctx = InferenceWorld_env_to_int("GGUF_CHAT_CTX", 1024);
    self->n_threads = InferenceWorld_env_to_int("GGUF_CHAT_THREADS", InferenceWorld_detect_thread_count());

    llama_log_set(InferenceWorld_log_errors_only, NULL);
    ggml_backend_load_all();
    llama_backend_init();

    model_params = llama_model_default_params();
    model_params.n_gpu_layers = self->n_gpu_layers;
    model_params.use_mmap = true;
    model_params.use_mlock = false;

    self->model = llama_model_load_from_file(model_path, model_params);
    if (!self->model) {
        InferenceWorld_set_status(self, 0, "failed to load model");
        return -1;
    }

    self->vocab = llama_model_get_vocab(self->model);
    ctx_params = llama_context_default_params();
    ctx_params.n_ctx = (uint32_t)self->n_ctx;
    /* FIX: n_batch was set to n_ctx causing double memory allocation
     * Batch only needs to handle the initial prompt, not full context */
    ctx_params.n_batch = (uint32_t)(self->n_ctx < 512 ? self->n_ctx : 512);
    ctx_params.n_threads = self->n_threads;
    ctx_params.n_threads_batch = self->n_threads;

    self->ctx = llama_init_from_model(self->model, ctx_params);
    if (!self->ctx) {
        InferenceWorld_set_status(self, 0, "failed to create context");
        llama_model_free(self->model);
        self->model = NULL;
        llama_backend_free();
        return -1;
    }

    self->is_booted = 1;
    InferenceWorld_set_status(self, 100, "InferenceWorld booted");
    return 0;
}

const char * InferenceWorld_chat(InferenceWorld_State * self, const char * prompt_text, int max_tokens) {
    struct llama_sampler * sampler;
    char * prompt = NULL;
    int32_t prompt_token_count;
    llama_token * prompt_tokens = NULL;
    struct llama_batch batch;
    int rc;
    int token_index;
    size_t used = 0;

    if (!self || !self->is_booted || !prompt_text || prompt_text[0] == '\0') {
        return NULL;
    }

    if (max_tokens <= 0) {
        max_tokens = 96;
    }

    self->last_response[0] = '\0';
    llama_memory_clear(llama_get_memory(self->ctx), true);

    sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    if (InferenceWorld_format_prompt(self->model, prompt_text, &prompt) != 0) {
        InferenceWorld_set_status(self, 0, "failed to format prompt");
        llama_sampler_free(sampler);
        return NULL;
    }

    prompt_token_count = -llama_tokenize(self->vocab, prompt, (int32_t)strlen(prompt), NULL, 0, true, true);
    if (prompt_token_count <= 0) {
        InferenceWorld_set_status(self, 0, "failed to measure prompt token count");
        free(prompt);
        llama_sampler_free(sampler);
        return NULL;
    }

    prompt_tokens = (llama_token *)malloc(sizeof(llama_token) * (size_t)prompt_token_count);
    if (!prompt_tokens) {
        InferenceWorld_set_status(self, 0, "failed to allocate prompt token buffer");
        free(prompt);
        llama_sampler_free(sampler);
        return NULL;
    }

    rc = llama_tokenize(self->vocab, prompt, (int32_t)strlen(prompt), prompt_tokens, prompt_token_count, true, true);
    if (rc < 0) {
        InferenceWorld_set_status(self, 0, "failed to tokenize prompt");
        free(prompt_tokens);
        free(prompt);
        llama_sampler_free(sampler);
        return NULL;
    }

    batch = llama_batch_get_one(prompt_tokens, prompt_token_count);
    for (token_index = 0; token_index < max_tokens; ++token_index) {
        char piece_buf[256];
        int piece_len;
        llama_token token_id;

        if (llama_memory_seq_pos_max(llama_get_memory(self->ctx), 0) + batch.n_tokens > (llama_pos)llama_n_ctx(self->ctx)) {
            InferenceWorld_set_status(self, 0, "context size exceeded");
            break;
        }

        rc = llama_decode(self->ctx, batch);
        if (rc != 0) {
            InferenceWorld_set_status(self, 0, "llama_decode failed");
            break;
        }

        token_id = llama_sampler_sample(sampler, self->ctx, -1);
        if (llama_vocab_is_eog(self->vocab, token_id)) {
            break;
        }

        piece_len = llama_token_to_piece(self->vocab, token_id, piece_buf, (int32_t)sizeof(piece_buf), 0, true);
        if (piece_len < 0) {
            InferenceWorld_set_status(self, 0, "failed to convert token to text");
            break;
        }

        if (InferenceWorld_append_piece(self->last_response, sizeof(self->last_response), &used, piece_buf, (size_t)piece_len) != 0) {
            InferenceWorld_set_status(self, 0, "response buffer full");
            break;
        }

        batch = llama_batch_get_one(&token_id, 1);
    }

    if (self->last_response[0] != '\0') {
        InferenceWorld_set_status(self, 100, "chat response ready");
    }

    free(prompt_tokens);
    free(prompt);
    llama_sampler_free(sampler);
    return self->last_response;
}

const char * InferenceWorld_health(InferenceWorld_State * self) {
    static char snapshot[512];
    if (!self) {
        return "InferenceWorld missing";
    }
    snprintf(
        snapshot,
        sizeof(snapshot),
        "{"
        "\"health_code\":%d,"
        "\"message\":\"%s\","
        "\"model_path\":\"%s\","
        "\"booted\":%d,"
        "\"n_gpu_layers\":%d,"
        "\"n_ctx\":%d,"
        "\"n_threads\":%d"
        "}",
        self->health_code,
        self->last_message,
        self->model_path,
        self->is_booted,
        self->n_gpu_layers,
        self->n_ctx,
        self->n_threads
    );
    return snapshot;
}

const char * InferenceWorld_manifest(InferenceWorld_State * self) {
    static char snapshot[1024];
    const char * model_name;
    if (!self) {
        return "InferenceWorld missing";
    }
    model_name = InferenceWorld_model_name(self->model_path);
    snprintf(
        snapshot,
        sizeof(snapshot),
        "{"
        "\"model_name\":\"%s\","
        "\"model_path\":\"%s\","
        "\"booted\":%d,"
        "\"health_code\":%d,"
        "\"message\":\"%s\","
        "\"n_gpu_layers\":%d,"
        "\"n_ctx\":%d,"
        "\"n_threads\":%d"
        "}",
        model_name,
        self->model_path,
        self->is_booted,
        self->health_code,
        self->last_message,
        self->n_gpu_layers,
        self->n_ctx,
        self->n_threads
    );
    return snapshot;
}

void InferenceWorld_cleanup(InferenceWorld_State * self) {
    if (!self) {
        return;
    }
    if (self->ctx) {
        llama_free(self->ctx);
        self->ctx = NULL;
    }
    if (self->model) {
        llama_model_free(self->model);
        self->model = NULL;
    }
    if (self->is_booted) {
        llama_backend_free();
    }
    self->is_booted = 0;
}

void InferenceWorld_destroy(InferenceWorld_State * self) {
    if (!self) {
        return;
    }
    InferenceWorld_cleanup(self);
    free(self);
}
