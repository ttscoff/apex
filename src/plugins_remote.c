#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Simple structures for remote plugin directory entries */

typedef struct apex_remote_plugin {
    char *id;
    char *title;
    char *description;
    char *author;
    char *homepage;
    char *repo;
    struct apex_remote_plugin *next;
} apex_remote_plugin;

typedef struct apex_remote_plugin_list {
    apex_remote_plugin *head;
} apex_remote_plugin_list;

void apex_remote_free_plugins(apex_remote_plugin_list *list) {
    if (!list) return;
    apex_remote_plugin *p = list->head;
    while (p) {
        apex_remote_plugin *next = p->next;
        free(p->id);
        free(p->title);
        free(p->description);
        free(p->author);
        free(p->homepage);
        free(p->repo);
        free(p);
        p = next;
    }
    free(list);
}

/* Fetch JSON from a URL using curl. Returns malloc'd buffer or NULL. */
static char *apex_remote_fetch_json(const char *url) {
    if (!url) return NULL;
    /* Use curl -fsSL to fail on HTTP errors and be quiet except for data. */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -fsSL \"%s\"", url);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "Error: failed to run curl. Is it installed?\n");
        return NULL;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = nb;
        }
    }
    buf[len] = '\0';
    int rc = pclose(fp);
    if (rc != 0) {
        /* curl failed; treat as error */
        free(buf);
        fprintf(stderr, "Error: curl exited with status %d while fetching plugin directory.\n", rc);
        return NULL;
    }
    return buf;
}

/* Helper: trim leading/trailing whitespace */
static void apex_remote_trim(char *s) {
    if (!s) return;
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' ||
            s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

/* Very small JSON helper: extract string value for a key from an object snippet.
 * Assumes JSON is well-formed and keys/values are double-quoted.
 */
static char *apex_remote_extract_string(const char *obj, const char *key) {
    if (!obj || !key) return NULL;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(obj, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\"') return NULL;
    p++;
    const char *start = p;
    while (*p && *p != '\"') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
        } else {
            p++;
        }
    }
    if (*p != '\"') return NULL;
    size_t len = (size_t)(p - start);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

/* Parse a very small subset of JSON: { \"plugins\": [ { ... }, { ... } ] } */
static apex_remote_plugin_list *apex_remote_parse_directory(const char *json) {
    if (!json) return NULL;
    const char *plugins_key = "\"plugins\"";
    const char *p = strstr(json, plugins_key);
    if (!p) {
        fprintf(stderr, "Error: plugin directory JSON missing \"plugins\" key.\n");
        return NULL;
    }
    p = strchr(p, '[');
    if (!p) return NULL;
    p++; /* move past '[' */

    apex_remote_plugin_list *list = calloc(1, sizeof(apex_remote_plugin_list));
    if (!list) return NULL;

    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
        if (!*p || *p == ']') break;
        if (*p != '{') {
            break;
        }
        const char *obj_start = p;
        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            }
            p++;
        }
        if (depth != 0) {
            apex_remote_free_plugins(list);
            return NULL;
        }
        size_t obj_len = (size_t)(p - obj_start);
        char *obj = malloc(obj_len + 1);
        if (!obj) {
            apex_remote_free_plugins(list);
            return NULL;
        }
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        apex_remote_plugin *rp = calloc(1, sizeof(apex_remote_plugin));
        if (!rp) {
            free(obj);
            apex_remote_free_plugins(list);
            return NULL;
        }
        rp->id = apex_remote_extract_string(obj, "id");
        rp->title = apex_remote_extract_string(obj, "title");
        rp->description = apex_remote_extract_string(obj, "description");
        rp->author = apex_remote_extract_string(obj, "author");
        rp->homepage = apex_remote_extract_string(obj, "homepage");
        rp->repo = apex_remote_extract_string(obj, "repo");
        free(obj);

        if (!rp->id || !rp->repo) {
            /* id and repo are required for use; drop this entry */
            free(rp->id);
            free(rp->title);
            free(rp->description);
            free(rp->author);
            free(rp->homepage);
            free(rp->repo);
            free(rp);
            continue;
        }

        rp->next = list->head;
        list->head = rp;
    }

    return list;
}

/* Public helpers used by CLI */

apex_remote_plugin_list *apex_remote_fetch_directory(const char *url) {
    char *json = apex_remote_fetch_json(url);
    if (!json) return NULL;
    apex_remote_plugin_list *list = apex_remote_parse_directory(json);
    free(json);
    return list;
}

void apex_remote_print_plugins(apex_remote_plugin_list *list) {
    if (!list || !list->head) {
        fprintf(stderr, "No plugins found in remote directory.\n");
        return;
    }
    for (apex_remote_plugin *p = list->head; p; p = p->next) {
        const char *title = p->title ? p->title : p->id;
        const char *author = p->author ? p->author : "";
        printf("%-20s - %s", p->id, title);
        if (author && *author) {
            printf("  (author: %s)", author);
        }
        printf("\n");
        if (p->description && *p->description) {
            printf("    %s\n", p->description);
        }
        if (p->homepage && *p->homepage) {
            printf("    homepage: %s\n", p->homepage);
        } else if (p->repo && *p->repo) {
            printf("    repo: %s\n", p->repo);
        }
    }
}

apex_remote_plugin *apex_remote_find_plugin(apex_remote_plugin_list *list, const char *id) {
    if (!list || !id) return NULL;
    for (apex_remote_plugin *p = list->head; p; p = p->next) {
        if (p->id && strcmp(p->id, id) == 0) {
            return p;
        }
    }
    return NULL;
}

const char *apex_remote_plugin_repo(apex_remote_plugin *p) {
    if (!p) return NULL;
    return p->repo;
}

