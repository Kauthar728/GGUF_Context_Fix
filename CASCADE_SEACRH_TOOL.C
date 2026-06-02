#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#include <pthread.h>

#define MAX_PATH 4096
#define MAX_FILES 200000
#define THREADS 4

typedef struct Node {
char path[MAX_PATH];
struct Node *next;
} Node;

Node *index_table[100003];

char query[512];
int use_glob = 0;
int search_inside = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

unsigned long hash(const char *s) {
unsigned long h = 5381;
while (*s) h = ((h << 5) + h) + *s++;
return h % 100003;
}

int already_indexed(const char *path) {
unsigned long h = hash(path);
Node *n = index_table[h];
while (n) {
if (strcmp(n->path, path) == 0) return 1;
n = n->next;
}
return 0;
}

void add_index(const char *path) {
unsigned long h = hash(path);

pthread_mutex_lock(&lock);
Node *n = index_table[h];
while (n) {
if (strcmp(n->path, path) == 0) {
pthread_mutex_unlock(&lock);
return;
}
n = n->next;
}

Node *new_node = (Node *)malloc(sizeof(Node));
strncpy(new_node->path, path, MAX_PATH);
new_node->next = index_table[h];
index_table[h] = new_node;
pthread_mutex_unlock(&lock);

}

int match_name(const char *name) {
if (use_glob) return fnmatch(query, name, 0) == 0;
return strncasecmp(name, query, strlen(query)) == 0;
}

int should_skip(const char *path) {
const char *skip_dirs[] = {
".git", "node_modules", ".venv", "venv", "env",
"__pycache__", ".DS_Store", "site-packages",
"target", "build", "dist", ".cache"
};
for (int i = 0; i < (int)(sizeof(skip_dirs)/sizeof(skip_dirs[0])); i++) {
if (strstr(path, skip_dirs[i])) return 1;
}
return 0;
}

int match_content(const char *path) {
if (!search_inside) return 1;

FILE *f = fopen(path, "r");
if (!f) return 0;
char buf[4096];
while (fgets(buf, sizeof(buf), f)) {
    if (strstr(buf, query)) {
        fclose(f);
        return 1;
    }
}
fclose(f);
return 0;

}

void scan_dir(const char *root) {
DIR *d = opendir(root);
if (!d) return;

struct dirent *e;
while ((e = readdir(d)) != NULL) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        continue;
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s/%s", root, e->d_name);
    if (should_skip(full)) continue;
    struct stat st;
    if (lstat(full, &st) == -1) continue;
    if (S_ISDIR(st.st_mode)) {
        scan_dir(full);
    } else if (S_ISREG(st.st_mode)) {
        add_index(full);
    }
}
closedir(d);

}

void *worker(void *arg) {
char *root = (char *)arg;
scan_dir(root);
return NULL;
}

void search() {
for (int i = 0; i < 100003; i++) {
Node *n = index_table[i];
while (n) {
char *name = strrchr(n->path, '/');
name = name ? name + 1 : (char *)n->path;

        if (match_name(name) && match_content(n->path)) {
            printf("%s\n", n->path);
        }
        n = n->next;
    }
}

}

int main(int argc, char **argv) {
if (argc < 3) {
printf("usage: search <path> <query> [--content]\n");
return 1;
}

strncpy(query, argv[2], sizeof(query));
if (strchr(query, '*') || strchr(query, '?'))
    use_glob = 1;
if (argc > 3 && strcmp(argv[3], "--content") == 0)
    search_inside = 1;
pthread_t t[THREADS];
for (int i = 0; i < THREADS; i++) {
    pthread_create(&t[i], NULL, worker, argv[1]);
}
for (int i = 0; i < THREADS; i++) {
    pthread_join(t[i], NULL);
}
search();
return 0;

}

