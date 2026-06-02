/*
#   Ghost[World:OutputWorld Domain:Output_Delivery Purpose:Truth_Delivery_Only
#   Owns:target_selection|target_validation|output_delivery|delivery_status|error_capture|ramdb_writeback
#   Accepts:target_type|target_path|format|payload Returns:delivery_status|structured_result|error_snapshot
#   Requires:MemUnit_connection_for_ramdb_writeback Exposes:boot|health|deliver|cleanup|destroy
#   State:target|format|payload|status|last_error|health_code|delivery_count|last_delivery Health:delivery_status_snapshot
#   Tags:world,output,delivery,screen,file,json,csv,markdown,log,ramdb,truth ]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sqlite3.h>

#define OUTPUTWORLD_PATH 4096
#define OUTPUTWORLD_PAYLOAD 65536
#define OUTPUTWORLD_MESSAGE 1024
#define OUTPUTWORLD_MAX_TARGETS 8

/*##section##[OutputWorld|StateContainer]*/
typedef struct OutputWorld_DeliveryResult {
    int status;
    char target[256];
    char path_or_sink[OUTPUTWORLD_PATH];
    long bytes_or_count;
    char message[OUTPUTWORLD_MESSAGE];
} OutputWorld_DeliveryResult;

typedef struct OutputWorld_DeliveryError {
    int status;
    char target[256];
    char reason[256];
    char message[OUTPUTWORLD_MESSAGE];
} OutputWorld_DeliveryError;

typedef struct OutputWorld_State {
    char current_target[256];
    char current_format[64];
    char current_path[OUTPUTWORLD_PATH];
    char current_payload[OUTPUTWORLD_PAYLOAD];
    int last_status;
    int health_code;
    int delivery_count;
    long total_bytes_delivered;
    char last_message[OUTPUTWORLD_MESSAGE];
    OutputWorld_DeliveryResult last_result;
    OutputWorld_DeliveryError last_error;
    sqlite3 * ramdb;
    int is_booted;
} OutputWorld_State;

/*##section##[OutputWorld|TargetValidation]*/
static bool OutputWorld_validate_target_screen(const char * path) {
    (void)path;
    return true;
}

static bool OutputWorld_validate_target_txt(const char * path) {
    if (!path || path[0] == '\0') return false;
    return true;
}

static bool OutputWorld_validate_target_json(const char * path) {
    if (!path || path[0] == '\0') return false;
    return true;
}

static bool OutputWorld_validate_target_csv(const char * path) {
    if (!path || path[0] == '\0') return false;
    return true;
}

static bool OutputWorld_validate_target_markdown(const char * path) {
    if (!path || path[0] == '\0') return false;
    return true;
}

static bool OutputWorld_validate_target_log(const char * path) {
    if (!path || path[0] == '\0') return false;
    return true;
}

static bool OutputWorld_validate_target_ramdb(const char * path) {
    (void)path;
    return true;
}

static bool OutputWorld_validate_target(const char * target_type, const char * target_path) {
    if (!target_type || target_type[0] == '\0') return false;
    
    if (strcmp(target_type, "screen") == 0) {
        return OutputWorld_validate_target_screen(target_path);
    }
    if (strcmp(target_type, "txt") == 0) {
        return OutputWorld_validate_target_txt(target_path);
    }
    if (strcmp(target_type, "json") == 0) {
        return OutputWorld_validate_target_json(target_path);
    }
    if (strcmp(target_type, "csv") == 0) {
        return OutputWorld_validate_target_csv(target_path);
    }
    if (strcmp(target_type, "markdown") == 0) {
        return OutputWorld_validate_target_markdown(target_path);
    }
    if (strcmp(target_type, "log") == 0) {
        return OutputWorld_validate_target_log(target_path);
    }
    if (strcmp(target_type, "ramdb") == 0) {
        return OutputWorld_validate_target_ramdb(target_path);
    }
    return false;
}

/*##section##[OutputWorld|DeliveryFunctions]*/
static int OutputWorld_deliver_screen(OutputWorld_State * self, const char * payload) {
    size_t len;
    if (!self || !payload) return -1;
    
    len = strlen(payload);
    printf("%s", payload);
    if (len > 0 && payload[len - 1] != '\n') {
        printf("\n");
    }
    fflush(stdout);
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "screen");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "stdout");
    self->last_result.bytes_or_count = (long)len;
    snprintf(self->last_result.message, sizeof(self->last_result.message), "delivered to screen");
    
    self->delivery_count++;
    self->total_bytes_delivered += (long)len;
    return 0;
}

static int OutputWorld_deliver_txt(OutputWorld_State * self, const char * path, const char * payload) {
    FILE * fp;
    size_t written;
    size_t len;
    if (!self || !path || !payload) return -1;
    
    fp = fopen(path, "w");
    if (!fp) {
        self->last_error.status = -1;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "txt");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "fopen_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    
    len = strlen(payload);
    written = fwrite(payload, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        self->last_error.status = -2;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "txt");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "write_incomplete");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "wrote %zu of %zu bytes", written, len);
        return -1;
    }
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "txt");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "%s", path);
    self->last_result.bytes_or_count = (long)written;
    snprintf(self->last_result.message, sizeof(self->last_result.message), "written to txt file");
    
    self->delivery_count++;
    self->total_bytes_delivered += (long)written;
    return 0;
}

static int OutputWorld_deliver_json(OutputWorld_State * self, const char * path, const char * payload) {
    FILE * fp;
    size_t written;
    size_t len;
    if (!self || !path || !payload) return -1;
    
    fp = fopen(path, "w");
    if (!fp) {
        self->last_error.status = -1;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "json");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "fopen_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    
    len = strlen(payload);
    written = fwrite(payload, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        self->last_error.status = -2;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "json");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "write_incomplete");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "wrote %zu of %zu bytes", written, len);
        return -1;
    }
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "json");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "%s", path);
    self->last_result.bytes_or_count = (long)written;
    snprintf(self->last_result.message, sizeof(self->last_result.message), "written to json file");
    
    self->delivery_count++;
    self->total_bytes_delivered += (long)written;
    return 0;
}

static int OutputWorld_deliver_csv(OutputWorld_State * self, const char * path, const char * payload) {
    FILE * fp;
    size_t written;
    size_t len;
    if (!self || !path || !payload) return -1;
    
    fp = fopen(path, "w");
    if (!fp) {
        self->last_error.status = -1;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "csv");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "fopen_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    
    len = strlen(payload);
    written = fwrite(payload, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        self->last_error.status = -2;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "csv");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "write_incomplete");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "wrote %zu of %zu bytes", written, len);
        return -1;
    }
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "csv");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "%s", path);
    self->last_result.bytes_or_count = (long)written;
    snprintf(self->last_result.message, sizeof(self->last_result.message), "written to csv file");
    
    self->delivery_count++;
    self->total_bytes_delivered += (long)written;
    return 0;
}

static int OutputWorld_deliver_markdown(OutputWorld_State * self, const char * path, const char * payload) {
    FILE * fp;
    size_t written;
    size_t len;
    if (!self || !path || !payload) return -1;
    
    fp = fopen(path, "w");
    if (!fp) {
        self->last_error.status = -1;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "markdown");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "fopen_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    
    len = strlen(payload);
    written = fwrite(payload, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        self->last_error.status = -2;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "markdown");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "write_incomplete");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "wrote %zu of %zu bytes", written, len);
        return -1;
    }
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "markdown");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "%s", path);
    self->last_result.bytes_or_count = (long)written;
    snprintf(self->last_result.message, sizeof(self->last_result.message), "written to markdown file");
    
    self->delivery_count++;
    self->total_bytes_delivered += (long)written;
    return 0;
}

static int OutputWorld_deliver_ramdb(OutputWorld_State * self, const char * payload) {
    sqlite3_stmt * stmt = NULL;
    int rc;
    if (!self || !payload) return -1;
    
    if (!self->ramdb) {
        self->last_error.status = -3;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "ramdb");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "no_ramdb_connection");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "ramdb not connected");
        return -1;
    }
    
    rc = sqlite3_prepare_v2(
        self->ramdb,
        "INSERT INTO output_delivery(target,content,timestamp) VALUES('ramdb',?1,unixepoch())",
        -1,
        &stmt,
        NULL
    );
    if (rc != SQLITE_OK) {
        self->last_error.status = -4;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "ramdb");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "prepare_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "%s", sqlite3_errmsg(self->ramdb));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, payload, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        self->last_error.status = -5;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "ramdb");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "insert_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "%s", sqlite3_errmsg(self->ramdb));
        return -1;
    }
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "ramdb");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "ramdb:output_delivery");
    self->last_result.bytes_or_count = (long)strlen(payload);
    snprintf(self->last_result.message, sizeof(self->last_result.message), "stored to ramdb output_delivery");
    
    self->delivery_count++;
    self->total_bytes_delivered += self->last_result.bytes_or_count;
    return 0;
}

static int OutputWorld_deliver_log(OutputWorld_State * self, const char * path, const char * payload) {
    FILE * fp;
    time_t now;
    struct tm * tm_info;
    char timestamp[64];
    size_t written;
    size_t len;
    if (!self || !path || !payload) return -1;
    
    fp = fopen(path, "a");
    if (!fp) {
        self->last_error.status = -1;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "log");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "fopen_failed");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    
    now = time(NULL);
    tm_info = localtime(&now);
    snprintf(timestamp, sizeof(timestamp), "[%04d-%02d-%02d %02d:%02d:%02d] ",
        tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    fwrite(timestamp, 1, strlen(timestamp), fp);
    len = strlen(payload);
    written = fwrite(payload, 1, len, fp);
    fwrite("\n", 1, 1, fp);
    fclose(fp);
    
    self->last_result.status = 0;
    snprintf(self->last_result.target, sizeof(self->last_result.target), "log");
    snprintf(self->last_result.path_or_sink, sizeof(self->last_result.path_or_sink), "%s", path);
    self->last_result.bytes_or_count = (long)(written + strlen(timestamp) + 1);
    snprintf(self->last_result.message, sizeof(self->last_result.message), "appended to log file");
    
    self->delivery_count++;
    self->total_bytes_delivered += self->last_result.bytes_or_count;
    return 0;
}

/*##method##[OutputWorld|create|allocation|constructor]*/
/*{[Input:none][Output:OutputWorld_State_ptr][Side_effects:allocates_state][Pattern:param_validate_execute]}*/
OutputWorld_State * OutputWorld_create(void) {
    OutputWorld_State * self = (OutputWorld_State *)calloc(1, sizeof(OutputWorld_State));
    return self;
}

/*##method##[OutputWorld|boot|lifecycle|initialization]*/
/*{[Input:config][Output:status][Side_effects:initializes_state|optionally_connects_ramdb][Pattern:param_validate_execute]}*/
int OutputWorld_boot(OutputWorld_State * self, const char * config) {
    sqlite3 * external_db = NULL;
    if (!self) return -1;
    
    memset(self, 0, sizeof(*self));
    self->health_code = 100;
    snprintf(self->last_message, sizeof(self->last_message), "OutputWorld booted");
    
    if (config) {
        const char * ramdb_ptr = strstr(config, "ramdb=");
        if (ramdb_ptr) {
            ramdb_ptr += 6;
            if (strncmp(ramdb_ptr, "external:", 9) == 0) {
                /* External ramdb pointer passed - use it */
                sscanf(ramdb_ptr + 9, "%p", &external_db);
                self->ramdb = external_db;
            }
        }
    }
    
    self->is_booted = 1;
    return 0;
}

/*##method##[OutputWorld|set_ramdb|connection|external_db]*/
/*{[Input:sqlite3_ptr][Output:status][Side_effects:sets_ramdb_connection][Pattern:param_validate_execute]}*/
int OutputWorld_set_ramdb(OutputWorld_State * self, sqlite3 * db) {
    if (!self) return -1;
    self->ramdb = db;
    return 0;
}

/*##method##[OutputWorld|deliver|core|output_delivery]*/
/*{[Input:target_type|target_path|payload][Output:status][Side_effects:delivers_to_target|records_result][Pattern:param_validate_execute]}*/
int OutputWorld_deliver(OutputWorld_State * self, const char * target_type, const char * target_path, const char * payload) {
    int rc;
    if (!self || !self->is_booted) return -1;
    if (!target_type || !payload) return -1;
    
    if (!OutputWorld_validate_target(target_type, target_path)) {
        self->last_error.status = -10;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "%s", target_type ? target_type : "unknown");
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "invalid_target");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "target validation failed");
        return -1;
    }
    
    snprintf(self->current_target, sizeof(self->current_target), "%s", target_type);
    snprintf(self->current_path, sizeof(self->current_path), "%s", target_path ? target_path : "");
    if (strlen(payload) < sizeof(self->current_payload)) {
        snprintf(self->current_payload, sizeof(self->current_payload), "%s", payload);
    } else {
        snprintf(self->current_payload, sizeof(self->current_payload), "%.*s...", (int)(sizeof(self->current_payload) - 4), payload);
    }
    
    if (strcmp(target_type, "screen") == 0) {
        rc = OutputWorld_deliver_screen(self, payload);
    } else if (strcmp(target_type, "txt") == 0) {
        rc = OutputWorld_deliver_txt(self, target_path, payload);
    } else if (strcmp(target_type, "json") == 0) {
        rc = OutputWorld_deliver_json(self, target_path, payload);
    } else if (strcmp(target_type, "csv") == 0) {
        rc = OutputWorld_deliver_csv(self, target_path, payload);
    } else if (strcmp(target_type, "markdown") == 0) {
        rc = OutputWorld_deliver_markdown(self, target_path, payload);
    } else if (strcmp(target_type, "ramdb") == 0) {
        rc = OutputWorld_deliver_ramdb(self, payload);
    } else if (strcmp(target_type, "log") == 0) {
        rc = OutputWorld_deliver_log(self, target_path, payload);
    } else {
        self->last_error.status = -11;
        snprintf(self->last_error.target, sizeof(self->last_error.target), "%s", target_type);
        snprintf(self->last_error.reason, sizeof(self->last_error.reason), "unknown_target");
        snprintf(self->last_error.message, sizeof(self->last_error.message), "target type not supported in phase 1");
        return -1;
    }
    
    self->last_status = rc;
    if (rc == 0) {
        self->health_code = 100;
        snprintf(self->last_message, sizeof(self->last_message), "delivered to %s", target_type);
    } else {
        self->health_code = 50;
        snprintf(self->last_message, sizeof(self->last_message), "delivery failed: %s", self->last_error.reason);
    }
    
    return rc;
}

/* Forward declarations */
const char * OutputWorld_health(OutputWorld_State * self);

/*##method##[OutputWorld|execute_text|world_runtime|compat_surface]*/
/*{[Input:action|payload|number_arg][Output:text][Side_effects:delivers_output][Pattern:param_validate_execute]}*/
const char * OutputWorld_execute_text(OutputWorld_State * self, const char * action, const char * payload, int number_arg) {
    static char result_buffer[OUTPUTWORLD_PAYLOAD];
    int rc;
    char target_type[64];
    char target_path[OUTPUTWORLD_PATH];
    char * content = NULL;
    const char * split;
    (void)number_arg;
    
    if (!self || !action) return NULL;
    
    if (strcmp(action, "deliver") == 0) {
        if (!payload) return NULL;
        
        /* Parse: target_type|target_path|content */
        split = strchr(payload, '|');
        if (!split) {
            /* Try space separator for simpler usage */
            split = strchr(payload, ' ');
            if (!split) return NULL;
        }
        snprintf(target_type, sizeof(target_type), "%.*s", (int)(split - payload), payload);
        
        split++;
        if (strncmp(split, "path=", 5) == 0) {
            split += 5;
            content = strchr(split, '|');
            if (!content) {
                content = strchr(split, ' ');
            }
            if (content) {
                snprintf(target_path, sizeof(target_path), "%.*s", (int)(content - split), split);
                content++;
            } else {
                snprintf(target_path, sizeof(target_path), "%s", split);
                content = "";
            }
        } else {
            snprintf(target_path, sizeof(target_path), "");
            content = (char *)split;
        }
        
        rc = OutputWorld_deliver(self, target_type, target_path[0] ? target_path : NULL, content);
        
        snprintf(result_buffer, sizeof(result_buffer),
            "{[OutputWorld|Delivery][Status:%d][Target:%s][Bytes:%ld][Message:%s]}",
            self->last_result.status,
            self->last_result.target,
            self->last_result.bytes_or_count,
            self->last_result.message);
        return result_buffer;
    }
    
    if (strcmp(action, "health") == 0) {
        return OutputWorld_health(self);
    }
    
    return NULL;
}

/*##method##[OutputWorld|health|status_query]*/
/*{[Input:none][Output:string][Side_effects:none][Pattern:param_validate_execute]}*/
const char * OutputWorld_health(OutputWorld_State * self) {
    static char snapshot[2048];
    if (!self) return "OutputWorld missing";
    
    snprintf(
        snapshot,
        sizeof(snapshot),
        "{[OutputWorld|Health][HealthCode:%d][Booted:%d][Deliveries:%d][TotalBytes:%ld]"
        "[LastTarget:%s][LastPath:%s][LastStatus:%d][LastMessage:%s]"
        "[LastResult:Status=%d,Target=%s,Bytes=%ld][LastError:Status=%d,Reason=%s]}",
        self->health_code,
        self->is_booted,
        self->delivery_count,
        self->total_bytes_delivered,
        self->current_target,
        self->current_path,
        self->last_status,
        self->last_message,
        self->last_result.status,
        self->last_result.target,
        self->last_result.bytes_or_count,
        self->last_error.status,
        self->last_error.reason
    );
    return snapshot;
}

/*##method##[OutputWorld|cleanup|lifecycle|destruction]*/
/*{[Input:none][Output:none][Side_effects:clears_state][Pattern:param_validate_execute]}*/
void OutputWorld_cleanup(OutputWorld_State * self) {
    if (!self) return;
    self->is_booted = 0;
    self->ramdb = NULL;
    snprintf(self->last_message, sizeof(self->last_message), "OutputWorld cleaned up");
}

/*##method##[OutputWorld|destroy|allocation|destructor]*/
/*{[Input:OutputWorld_State_ptr][Output:none][Side_effects:cleanup_and_free][Pattern:param_validate_execute]}*/
void OutputWorld_destroy(OutputWorld_State * self) {
    if (!self) return;
    OutputWorld_cleanup(self);
    free(self);
}
