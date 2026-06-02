/*
#   Ghost[Driver:Main_Driver Domain:EntryPoint Purpose:Resident_GGUF_Chat_Shell
#   Owns:nothing_hands_immediately_to_MemUnit_and_InferenceWorld Accepts:argv|argc Returns:exit_code
#   Requires:MemUnit|InferenceWorld Exposes:main
#   State:none Health:pass_through Tags:driver,entrypoint,thin_shell,chat,repl ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct MemUnit_State MemUnit_State;
typedef struct InferenceWorld_State InferenceWorld_State;
typedef struct MemUnit_Result {
    char * value;
    char * error;
    int status;
    int count;
} MemUnit_Result;

extern MemUnit_State * MemUnit_create(void);
extern int MemUnit_boot(MemUnit_State * self, const char * config);
extern int MemUnit_seed_defaults(MemUnit_State * self);
extern const char * MemUnit_cache_retrieve(MemUnit_State * self, const char * key);
extern const char * MemUnit_search_text(MemUnit_State * self, const char * type, const char * value);
extern const char * MemUnit_assemble_text(MemUnit_State * self, const char * seed, int radius, const char * mode);
extern MemUnit_Result MemUnit_bind_world_runtime(
    MemUnit_State * self,
    const char * world_name,
    const char * authority,
    const char * config,
    const char * deps,
    void * world_state,
    int (*boot_fn)(void *, const char *),
    const char * (*chat_fn)(void *, const char *, int),
    const char * (*health_fn)(void *),
    const char * (*exec_text_fn)(void *, const char *, const char *, int),
    void (*cleanup_fn)(void *)
);
extern const char * MemUnit_execute_text(MemUnit_State * self, const char * world_name, const char * action, const char * text_payload, int number_arg);
extern char * MemUnit_health(MemUnit_State * self);
extern void MemUnit_destroy(MemUnit_State * self);

extern InferenceWorld_State * InferenceWorld_create(void);
extern int InferenceWorld_boot(InferenceWorld_State * self, const char * model_path);
extern const char * InferenceWorld_chat(InferenceWorld_State * self, const char * prompt_text, int max_tokens);
extern const char * InferenceWorld_health(InferenceWorld_State * self);
extern const char * InferenceWorld_manifest(InferenceWorld_State * self);
extern void InferenceWorld_destroy(InferenceWorld_State * self);

typedef struct LMDBWorld_State LMDBWorld_State;
extern LMDBWorld_State * LMDBWorld_create(void);
extern int LMDBWorld_boot(LMDBWorld_State * self, const char * config);
extern const char * LMDBWorld_execute_text(LMDBWorld_State * self, const char * action, const char * payload, int number_arg);
extern const char * LMDBWorld_health(LMDBWorld_State * self);
extern void LMDBWorld_destroy(LMDBWorld_State * self);

static int Main_Driver_world_boot(void * world_state, const char * model_path) {
    return InferenceWorld_boot((InferenceWorld_State *)world_state, model_path);
}

static const char * Main_Driver_world_chat(void * world_state, const char * prompt_text, int max_tokens) {
    return InferenceWorld_chat((InferenceWorld_State *)world_state, prompt_text, max_tokens);
}

static const char * Main_Driver_world_health(void * world_state) {
    return InferenceWorld_health((InferenceWorld_State *)world_state);
}

static void Main_Driver_world_cleanup(void * world_state) {
    InferenceWorld_destroy((InferenceWorld_State *)world_state);
}

static int Main_Driver_lmdb_boot(void * world_state, const char * config) {
    return LMDBWorld_boot((LMDBWorld_State *)world_state, config);
}

static const char * Main_Driver_lmdb_health(void * world_state) {
    return LMDBWorld_health((LMDBWorld_State *)world_state);
}

static const char * Main_Driver_lmdb_exec(void * world_state, const char * action, const char * payload, int number_arg) {
    return LMDBWorld_execute_text((LMDBWorld_State *)world_state, action, payload, number_arg);
}

static void Main_Driver_lmdb_cleanup(void * world_state) {
    LMDBWorld_destroy((LMDBWorld_State *)world_state);
}

static void Main_Driver_print_usage(const char * argv0) {
    fprintf(stderr, "usage: %s <model.gguf> [prompt ...]\n", argv0);
    fprintf(stderr, "   or: %s <model.gguf> --shell\n", argv0);
}

static const char * Main_Driver_basename(const char * path) {
    const char * slash;
    if (!path || path[0] == '\0') {
        return "model.gguf";
    }
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int Main_Driver_ensure_dir(const char * path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static const char * Main_Driver_detect_family(const char * model_name) {
    if (!model_name) {
        return "unknown";
    }
    if (strstr(model_name, "Falcon") || strstr(model_name, "falcon")) {
        return "Falcon";
    }
    return "GGUF";
}

static void Main_Driver_store_lmdb_pair(MemUnit_State * memunit, char * payload, size_t payload_size, const char * key, const char * value) {
    if (!memunit || !payload || payload_size == 0 || !key || !value) {
        return;
    }
    snprintf(payload, payload_size, "%s\t%s", key, value);
    MemUnit_execute_text(memunit, "LMDBWorld", "put", payload, 0);
}

static int Main_Driver_write_roommate_manifest(const char * manifest_path, const char * manifest_text) {
    FILE * fp;
    if (!manifest_path || !manifest_text) {
        return -1;
    }
    fp = fopen(manifest_path, "w");
    if (!fp) {
        return -1;
    }
    fputs(manifest_text, fp);
    fclose(fp);
    return 0;
}

static void Main_Driver_store_roommate_manifest(MemUnit_State * memunit, InferenceWorld_State * world, const char * model_path) {
    char payload[8192];
    char room_dir[1024];
    char hotcache_path[1024];
    char manifest_path[1024];
    char manifest_text[2048];
    const char * model_name;
    const char * family;
    const char * manifest_json;

    if (!memunit || !world || !model_path) {
        return;
    }

    model_name = Main_Driver_basename(model_path);
    family = Main_Driver_detect_family(model_name);
    snprintf(room_dir, sizeof(room_dir), "%s", "/Users/waynephilliplundall/testbed/GGUF_Context_Fix/model_room");
    snprintf(hotcache_path, sizeof(hotcache_path), "%s/%s.hotcache", room_dir, model_name);
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.manifest.json", room_dir, model_name);
    if (Main_Driver_ensure_dir(room_dir) != 0) {
        return;
    }

    manifest_json = InferenceWorld_manifest(world);
    snprintf(
        manifest_text,
        sizeof(manifest_text),
        "{\n"
        "  \"room\": \"GGUF_Context_Fix\",\n"
        "  \"family\": \"%s\",\n"
        "  \"model_name\": \"%s\",\n"
        "  \"model_path\": \"%s\",\n"
        "  \"hotcache_path\": \"%s\",\n"
        "  \"runtime_lmdb_path\": \"/Users/waynephilliplundall/testbed/GGUF_Context_Fix/runtime_lmdb\",\n"
        "  \"resident_manifest\": %s\n"
        "}\n",
        family,
        model_name,
        model_path,
        hotcache_path,
        manifest_json ? manifest_json : "{}"
    );
    Main_Driver_write_roommate_manifest(manifest_path, manifest_text);

    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_path", model_path);
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_name", model_name);
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_family", family);
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_room", "GGUF_Context_Fix");
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_hotcache_path", hotcache_path);
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_manifest_path", manifest_path);
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_runtime_lmdb_path", "/Users/waynephilliplundall/testbed/GGUF_Context_Fix/runtime_lmdb");
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_status", "roommate_booted");
    Main_Driver_store_lmdb_pair(memunit, payload, sizeof(payload), "resident_model_manifest", manifest_text);
}

static void Main_Driver_join_prompt(int argc, char ** argv, int start_index, char * out, size_t out_size) {
    int index;
    size_t used = 0;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    for (index = start_index; index < argc; ++index) {
        int written = snprintf(out + used, out_size - used, "%s%s", used ? " " : "", argv[index]);
        if (written < 0 || (size_t)written >= out_size - used) break;
        used += (size_t)written;
    }
}

static int Main_Driver_run_shell(MemUnit_State * memunit, InferenceWorld_State * world) {
    char line[2048];
    char chat_response[8192];
    (void)world;
    printf("GGUF Context Fix shell ready\n");
    printf("Commands: /health /mem /roommate /search <type> <value> /assemble <seed> [radius] [mode] /cache <key> /lmdb <get key|put key value|list prefix> /quit\n");
    for (;;) {
        const char * response;
        printf("chat> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0) break;
        if (strcmp(line, "/health") == 0) {
            printf("Inference: %s\n", MemUnit_execute_text(memunit, "InferenceWorld", "health", "", 0));
            continue;
        }
        if (strcmp(line, "/mem") == 0) {
            printf("MemUnit: %s\n", MemUnit_health(memunit));
            continue;
        }
        if (strcmp(line, "/roommate") == 0) {
            const char * roommate = MemUnit_execute_text(memunit, "LMDBWorld", "get", "resident_model_manifest", 0);
            printf("%s\n", roommate ? roommate : "{[Error:resident_model_missing]}");
            continue;
        }
        if (strncmp(line, "/search ", 8) == 0) {
            char type[128];
            char value[512];
            if (sscanf(line + 8, "%127s %511[^\n]", type, value) == 2) {
                const char * found = MemUnit_search_text(memunit, type, value);
                printf("%s\n", found ? found : "{[Error:search_failed]}");
            } else {
                printf("usage: /search <verb|authority|magk|bracket_sig> <value>\n");
            }
            continue;
        }
        if (strncmp(line, "/assemble ", 10) == 0) {
            char seed[128];
            char mode[128];
            int radius = 2;
            int parsed = sscanf(line + 10, "%127s %d %127s", seed, &radius, mode);
            const char * packet = MemUnit_assemble_text(memunit, seed, radius, parsed == 3 ? mode : "related");
            printf("%s\n", packet ? packet : "{[Error:assemble_failed]}");
            continue;
        }
        if (strncmp(line, "/cache ", 7) == 0) {
            const char * cached = MemUnit_cache_retrieve(memunit, line + 7);
            printf("%s\n", cached ? cached : "{[Error:not_found]}");
            continue;
        }
        if (strncmp(line, "/lmdb ", 6) == 0) {
            char action[16];
            char key[256];
            char value[1024];
            char payload[1400];
            int parsed = sscanf(line + 6, "%15s %255s %1023[^\n]", action, key, value);
            if (parsed >= 2 && strcmp(action, "get") == 0) {
                printf("%s\n", MemUnit_execute_text(memunit, "LMDBWorld", "get", key, 0));
                continue;
            }
            if (parsed >= 2 && strcmp(action, "list") == 0) {
                printf("%s\n", MemUnit_execute_text(memunit, "LMDBWorld", "list", key, 0));
                continue;
            }
            if (parsed == 3 && strcmp(action, "put") == 0) {
                snprintf(payload, sizeof(payload), "%s\t%s", key, value);
                printf("%s\n", MemUnit_execute_text(memunit, "LMDBWorld", "put", payload, 0));
                continue;
            }
            printf("usage: /lmdb get <key> | /lmdb put <key> <value> | /lmdb list <prefix>\n");
            continue;
        }

        response = MemUnit_execute_text(memunit, "InferenceWorld", "chat", line, 96);
        if (!response || strncmp(response, "{[Error:", 8) == 0) {
            printf("ERROR: %s\n", response ? response : MemUnit_health(memunit));
            continue;
        }
        snprintf(chat_response, sizeof(chat_response), "%s", response);
        {
            char lmdb_payload[8192];
            snprintf(lmdb_payload, sizeof(lmdb_payload), "last_prompt\t%s", line);
            MemUnit_execute_text(memunit, "LMDBWorld", "put", lmdb_payload, 0);
            snprintf(lmdb_payload, sizeof(lmdb_payload), "last_response\t%s", chat_response);
            MemUnit_execute_text(memunit, "LMDBWorld", "put", lmdb_payload, 0);
        }
        printf("%s\n", chat_response);
    }
    return 0;
}

/*##bracket##[entry_point|main|ignition]*/
int main(int argc, char ** argv) {
    MemUnit_State * memunit;
    InferenceWorld_State * world;
    LMDBWorld_State * lmdb_world;
    MemUnit_Result bind_result;
    const char * model_path;
    char prompt[4096];
    char chat_response[8192];
    char lmdb_payload[8192];
    const char * response;

    if (argc < 2) {
        Main_Driver_print_usage(argv[0]);
        return 1;
    }

    model_path = argv[1];
    memunit = MemUnit_create();
    world = InferenceWorld_create();
    lmdb_world = LMDBWorld_create();
    if (!memunit || !world || !lmdb_world) {
        fprintf(stderr, "allocation failed\n");
        MemUnit_destroy(memunit);
        InferenceWorld_destroy(world);
        LMDBWorld_destroy(lmdb_world);
        return 1;
    }

    if (MemUnit_boot(memunit, NULL) != 0) {
        fprintf(stderr, "MemUnit boot failed\n");
        MemUnit_destroy(memunit);
        InferenceWorld_destroy(world);
        return 1;
    }
    MemUnit_seed_defaults(memunit);

    bind_result = MemUnit_bind_world_runtime(
        memunit,
        "InferenceWorld",
        "GGUF_Inference",
        "resident=1",
        "llama_cpp_bridge_runtime",
        world,
        Main_Driver_world_boot,
        Main_Driver_world_chat,
        Main_Driver_world_health,
        NULL,
        Main_Driver_world_cleanup
    );
    if (bind_result.status != 0) {
        fprintf(stderr, "MemUnit bind failed\n");
        free(bind_result.value);
        free(bind_result.error);
        InferenceWorld_destroy(world);
        LMDBWorld_destroy(lmdb_world);
        MemUnit_destroy(memunit);
        return 1;
    }
    free(bind_result.value);
    free(bind_result.error);

    bind_result = MemUnit_bind_world_runtime(
        memunit,
        "LMDBWorld",
        "Persistent_Runtime_Store",
        "path=/Users/waynephilliplundall/testbed/GGUF_Context_Fix/runtime_lmdb;map_size=67108864",
        "LMDB_library",
        lmdb_world,
        Main_Driver_lmdb_boot,
        NULL,
        Main_Driver_lmdb_health,
        Main_Driver_lmdb_exec,
        Main_Driver_lmdb_cleanup
    );
    if (bind_result.status != 0) {
        fprintf(stderr, "MemUnit LMDB bind failed\n");
        free(bind_result.value);
        free(bind_result.error);
        MemUnit_destroy(memunit);
        return 1;
    }
    free(bind_result.value);
    free(bind_result.error);

    response = MemUnit_execute_text(memunit, "InferenceWorld", "boot", model_path, 0);
    if (!response || strncmp(response, "{[Error:", 8) == 0) {
        fprintf(stderr, "InferenceWorld boot failed: %s\n", response ? response : MemUnit_health(memunit));
        MemUnit_destroy(memunit);
        return 1;
    }

    response = MemUnit_execute_text(memunit, "LMDBWorld", "boot", "path=/Users/waynephilliplundall/testbed/GGUF_Context_Fix/runtime_lmdb;map_size=67108864", 0);
    if (!response || strncmp(response, "{[Error:", 8) == 0) {
        fprintf(stderr, "LMDBWorld boot failed: %s\n", response ? response : MemUnit_health(memunit));
        MemUnit_destroy(memunit);
        return 1;
    }

    snprintf(lmdb_payload, sizeof(lmdb_payload), "model_path\t%s", model_path);
    MemUnit_execute_text(memunit, "LMDBWorld", "put", lmdb_payload, 0);
    Main_Driver_store_roommate_manifest(memunit, world, model_path);

    if (argc == 2 || (argc >= 3 && strcmp(argv[2], "--shell") == 0)) {
        int rc = Main_Driver_run_shell(memunit, world);
        printf("MemUnit: %s\n", MemUnit_health(memunit));
        MemUnit_destroy(memunit);
        return rc;
    }

    Main_Driver_join_prompt(argc, argv, 2, prompt, sizeof(prompt));
    response = MemUnit_execute_text(memunit, "InferenceWorld", "chat", prompt, 96);
    if (!response || strncmp(response, "{[Error:", 8) == 0) {
        fprintf(stderr, "chat failed: %s\n", response ? response : MemUnit_health(memunit));
        MemUnit_destroy(memunit);
        return 1;
    }
    snprintf(chat_response, sizeof(chat_response), "%s", response);

    snprintf(lmdb_payload, sizeof(lmdb_payload), "last_prompt\t%s", prompt);
    MemUnit_execute_text(memunit, "LMDBWorld", "put", lmdb_payload, 0);
    snprintf(lmdb_payload, sizeof(lmdb_payload), "last_response\t%s", chat_response);
    MemUnit_execute_text(memunit, "LMDBWorld", "put", lmdb_payload, 0);

    printf("%s\n", chat_response);
    printf("Inference: %s\n", MemUnit_execute_text(memunit, "InferenceWorld", "health", "", 0));
    printf("LMDB: %s\n", MemUnit_execute_text(memunit, "LMDBWorld", "health", "", 0));
    printf("MemUnit: %s\n", MemUnit_health(memunit));

    MemUnit_destroy(memunit);
    return 0;
}
