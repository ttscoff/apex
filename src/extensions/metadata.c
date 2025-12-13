/**
 * Metadata Extension for Apex
 * Implementation (Simplified version - metadata handled via preprocessing)
 */

#include "../../include/apex/apex.h"
#include "metadata.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <regex.h>
#ifdef _POSIX_C_SOURCE
/* strptime should be available if POSIX is defined */
#elif defined(__APPLE__) || defined(__linux__)
/* strptime is available on macOS and most Linux systems */
#define HAVE_STRPTIME 1
#elif defined(_GNU_SOURCE)
/* GNU extension */
#define HAVE_STRPTIME 1
#endif

/* For now, we'll handle metadata as a preprocessing step rather than a block type
 * This is simpler and matches how MultiMarkdown actually works */

/* Node type for metadata blocks */
cmark_node_type APEX_NODE_METADATA = CMARK_NODE_CUSTOM_BLOCK;

/* Transform structures */
typedef struct apex_transform {
    char *name;
    char *options;  /* NULL if no options */
    struct apex_transform *next;
} apex_transform;

/* Dynamic array for string arrays (used by split/join/slice) */
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} apex_string_array;

/**
 * Free metadata items list
 */
void apex_free_metadata(apex_metadata_item *metadata) {
    while (metadata) {
        apex_metadata_item *next = metadata->next;
        free(metadata->key);
        free(metadata->value);
        free(metadata);
        metadata = next;
    }
}

/**
 * Trim whitespace from both ends of a string (in-place)
 */
static char *trim_whitespace(char *str) {
    char *end;

    /* Trim leading space */
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';
    return str;
}

/**
 * Add a metadata item to the list
 */
static void add_metadata_item(apex_metadata_item **list, const char *key, const char *value) {
    if (!key || !value) return;  /* Don't add items with NULL key or value */

    apex_metadata_item *item = malloc(sizeof(apex_metadata_item));
    if (!item) return;

    item->key = strdup(key);
    item->value = strdup(value);

    /* If strdup failed, free the item and don't add it */
    if (!item->key || !item->value) {
        free(item->key);
        free(item->value);
        free(item);
        return;
    }

    item->next = *list;
    *list = item;
}

/**
 * Parse YAML front matter
 * Format: --- at start, key: value pairs, --- to close
 */
static apex_metadata_item *parse_yaml_metadata(const char *text, size_t *consumed) {
    apex_metadata_item *items = NULL;
    const char *line_start = text;
    const char *line_end;

    /* Skip opening --- */
    if (strncmp(text, "---", 3) != 0) return NULL;
    line_start = strchr(text + 3, '\n');
    if (!line_start) return NULL;
    line_start++;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        size_t len = line_end - line_start;
        char line[1024];

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';

        /* Check for closing --- or ... */
        char *trimmed = trim_whitespace(line);
        if (strcmp(trimmed, "---") == 0 || strcmp(trimmed, "...") == 0) {
            *consumed = (line_end + 1) - text;
            return items;
        }

        /* Parse key: value */
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *key = trim_whitespace(line);
            char *value_ptr = trim_whitespace(colon + 1);

            char final_value[1024] = {0};
            strncpy(final_value, value_ptr, sizeof(final_value) - 1);

            /* Strip quotes from value if present */
            size_t value_len = strlen(final_value);
            if (value_len >= 2 && ((final_value[0] == '"' && final_value[value_len - 1] == '"') ||
                                   (final_value[0] == '\'' && final_value[value_len - 1] == '\''))) {
                final_value[value_len - 1] = '\0';
                memmove(final_value, final_value + 1, value_len - 1);
                /* Re-trim after removing quotes */
                trim_whitespace(final_value);
                value_len = strlen(final_value);
            }

            if (*key) {
                add_metadata_item(&items, key, final_value);
            }
        }

        line_start = line_end + 1;
    }

    return items;
}

/**
 * Parse MultiMarkdown metadata
 * Format: key: value pairs at start of document, blank line to end
 */
static apex_metadata_item *parse_mmd_metadata(const char *text, size_t *consumed) {
    apex_metadata_item *items = NULL;
    const char *line_start = text;
    const char *line_end;
    bool found_metadata = false;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
        size_t len = line_end - line_start;
        char line[1024];

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';

        /* Check for blank line (end of metadata) */
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0') {
            if (found_metadata) {
                *consumed = (line_end + 1) - text;
                return items;
            }
            /* Skip leading blank lines */
            line_start = line_end + 1;
            continue;
        }

        /* Skip abbreviation definitions (*[abbr]: expansion or [>abbr]: expansion) */
        if ((trimmed[0] == '*' && trimmed[1] == '[') ||
            (trimmed[0] == '[' && trimmed[1] == '>')) {
            /* This is an abbreviation, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Skip HTML comments (<!--...-->) */
        if (strncmp(trimmed, "<!--", 4) == 0) {
            /* This is an HTML comment, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Skip Kramdown markers ({::...}) */
        if (strncmp(trimmed, "{::", 3) == 0) {
            /* This is a Kramdown marker, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Skip Markdown headings (# or ##) */
        if (trimmed[0] == '#') {
            /* This is a Markdown heading, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Skip IAL/ALD syntax ({: ...}) */
        if (strncmp(trimmed, "{:", 2) == 0) {
            /* This is IAL/ALD, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Skip TOC markers ({{TOC...}}) */
        if (strncmp(trimmed, "{{TOC", 5) == 0) {
            /* This is a TOC marker, not metadata */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* Haven't found metadata yet, skip this line */
            line_start = line_end + 1;
            continue;
        }

        /* Parse key: value */
        char *colon = strchr(line, ':');
        if (colon) {
            /* Check if there's a protocol (http://, https://, mailto:) BEFORE the colon */
            /* If so, this is likely a URL in the key, not metadata */
            size_t key_len = (size_t)(colon - line);
            if (key_len >= 7 && (
                (key_len >= 7 && strncmp(line, "http://", 7) == 0) ||
                (key_len >= 8 && strncmp(line, "https://", 8) == 0) ||
                (key_len >= 7 && strncmp(line, "mailto:", 7) == 0) ||
                strstr(line, "://") != NULL)) {
                /* Protocol found before colon - this is a URL, not metadata */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                *consumed = 0;
                return NULL;
            }

            /* Check if there's a < character BEFORE the colon (HTML/autolink in key) */
            if (memchr(line, '<', key_len) != NULL) {
                /* < found before colon - this is HTML/autolink, not metadata */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                *consumed = 0;
                return NULL;
            }

            /* Check for space after colon (MMD requires "KEY: VALUE" format) */
            char *after_colon = colon + 1;
            if (*after_colon != ' ' && *after_colon != '\t') {
                /* No space after colon - likely not metadata */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                *consumed = 0;
                return NULL;
            }

            *colon = '\0';
            char *key = trim_whitespace(line);
            char *value = trim_whitespace(colon + 1);

            if (*key && *value) {
                add_metadata_item(&items, key, value);
                found_metadata = true;
            } else {
                /* Line has colon but invalid key/value */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                /* No metadata found yet - this isn't metadata */
                *consumed = 0;
                return NULL;
            }
        } else {
            /* No colon - check if line contains URLs (bare URLs without angle brackets) */
            if (strstr(trimmed, "http://") || strstr(trimmed, "https://") || strstr(trimmed, "mailto:")) {
                /* This is a bare URL, not metadata */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                *consumed = 0;
                return NULL;
            }

            /* Skip lines with Markdown links or images */
            if (strchr(trimmed, '[') && strchr(trimmed, ']') && strchr(trimmed, '(')) {
                /* Contains markdown link/image syntax - not metadata */
                if (found_metadata) {
                    *consumed = line_start - text;
                    return items;
                }
                /* Haven't found metadata yet - this isn't metadata */
                *consumed = 0;
                return NULL;
            }

            /* Non-metadata line found (no colon) */
            if (found_metadata) {
                *consumed = line_start - text;
                return items;
            }
            /* No metadata found yet and hit non-metadata line - stop */
            *consumed = 0;
            return NULL;
        }

        line_start = line_end + 1;
    }

    if (found_metadata) {
        *consumed = strlen(text);
    }
    return items;
}

/**
 * Parse Pandoc title block metadata
 * Format: % Title, % Author, % Date as first three lines
 */
static apex_metadata_item *parse_pandoc_metadata(const char *text, size_t *consumed) {
    apex_metadata_item *items = NULL;
    const char *keys[] = {"title", "author", "date"};
    int key_index = 0;
    const char *line_start = text;
    const char *line_end;

    while (key_index < 3 && (line_end = strchr(line_start, '\n')) != NULL) {
        size_t len = line_end - line_start;
        char line[1024];

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';

        char *trimmed = trim_whitespace(line);

        /* Must start with % */
        if (*trimmed == '%') {
            char *value = trim_whitespace(trimmed + 1);
            if (*value) {
                add_metadata_item(&items, keys[key_index], value);
            }
            key_index++;
        } else {
            /* Non-Pandoc line */
            break;
        }

        line_start = line_end + 1;
    }

    if (key_index > 0) {
        *consumed = line_start - text;
    }
    return items;
}

/**
 * Detect and extract metadata from the start of document text
 * This modifies the input by removing the metadata section
 * Returns the extracted metadata
 */
apex_metadata_item *apex_extract_metadata(char **text_ptr) {
    if (!text_ptr || !*text_ptr || !**text_ptr) return NULL;

    char *text = *text_ptr;
    size_t consumed = 0;
    apex_metadata_item *items = NULL;

    /* Try YAML first (most explicit) */
    if (strncmp(text, "---", 3) == 0) {
        items = parse_yaml_metadata(text, &consumed);
    }
    /* Try Pandoc */
    else if (*text == '%') {
        items = parse_pandoc_metadata(text, &consumed);
    }
    /* Try MMD (least specific) */
    else {
        items = parse_mmd_metadata(text, &consumed);
    }

    /* Remove metadata from text if found */
    if (items && consumed > 0) {
        /* Skip past the metadata */
        *text_ptr = text + consumed;
    }

    return items;
}

/**
 * Placeholder extension creation - for future full integration
 * For now, metadata is handled via preprocessing
 */
cmark_syntax_extension *create_metadata_extension(void) {
    /* Return NULL for now - we handle metadata via preprocessing */
    /* In the future, this could create a proper block extension */
    return NULL;
}

/**
 * Get metadata from a document (stub for now)
 */
apex_metadata_item *apex_get_metadata(cmark_node *document) {
    (void)document;  /* Unused parameter */
    /* For now, metadata must be extracted before parsing */
    /* This would require storing metadata in the document's user_data */
    return NULL;
}

/**
 * Normalize metadata key by removing spaces and converting to lowercase
 * This matches MultiMarkdown's behavior where "HTML Header Level" becomes "htmlheaderlevel"
 */
static char *normalize_metadata_key(const char *key) {
    if (!key) return NULL;

    size_t len = strlen(key);
    char *normalized = malloc(len + 1);
    if (!normalized) return NULL;

    char *out = normalized;
    for (const char *in = key; *in; in++) {
        if (!isspace((unsigned char)*in)) {
            *out++ = (char)tolower((unsigned char)*in);
        }
    }
    *out = '\0';
    return normalized;
}

/**
 * Get a specific metadata value (case-insensitive, spaces ignored)
 * Matches MultiMarkdown behavior where "HTML Header Level" matches "htmlheaderlevel"
 */
const char *apex_metadata_get(apex_metadata_item *metadata, const char *key) {
    if (!key || !metadata) return NULL;

    /* Validate key is a valid string (not just a non-NULL pointer) */
    if (strlen(key) == 0) return NULL;

    /* Normalize the search key */
    char *normalized_key = normalize_metadata_key(key);
    if (!normalized_key) return NULL;

    /* Try exact case-insensitive match first (for backwards compatibility) */
    for (apex_metadata_item *item = metadata; item != NULL; item = item->next) {
        /* Validate item and its key before using */
        if (!item) break;
        if (!item->key) continue;

        /* Additional safety: check if key is a valid string */
        size_t key_len = strlen(item->key);
        if (key_len == 0) continue;

        if (strcasecmp(item->key, key) == 0) {
            free(normalized_key);
            return item->value;
        }
    }

    /* Try normalized match (spaces removed, lowercase) */
    for (apex_metadata_item *item = metadata; item != NULL; item = item->next) {
        if (!item || !item->key) continue;  /* Skip items with NULL keys */

        /* Additional safety: check if key is a valid string */
        size_t key_len = strlen(item->key);
        if (key_len == 0) continue;

        char *normalized_item_key = normalize_metadata_key(item->key);
        if (normalized_item_key) {
            bool match = (strcmp(normalized_item_key, normalized_key) == 0);
            free(normalized_item_key);
            if (match) {
                free(normalized_key);
                return item->value;
            }
        }
    }

    free(normalized_key);
    return NULL;
}

/**
 * Free transform chain
 */
static void free_transform_chain(apex_transform *transform) {
    while (transform) {
        apex_transform *next = transform->next;
        free(transform->name);
        free(transform->options);
        free(transform);
        transform = next;
    }
}

/**
 * Free string array
 */
static void free_string_array(apex_string_array *arr) {
    if (arr) {
        for (size_t i = 0; i < arr->count; i++) {
            free(arr->items[i]);
        }
        free(arr->items);
        free(arr);
    }
}

/**
 * Parse transform chain from string like "KEY:TRANSFORM1:TRANSFORM2(OPTIONS)"
 * Returns the first transform in the chain, or NULL on error
 * The key part is stored separately and returned via key_out
 */
static apex_transform *parse_transform_chain(const char *input, char **key_out) {
    if (!input || !key_out) return NULL;

    apex_transform *first = NULL;
    apex_transform *current = NULL;

    /* Find first colon to separate key from transforms */
    const char *first_colon = strchr(input, ':');
    if (!first_colon) {
        /* No transforms, just a key */
        *key_out = strdup(input);
        return NULL;
    }

    /* Extract key */
    size_t key_len = first_colon - input;
    *key_out = malloc(key_len + 1);
    if (!*key_out) return NULL;
    memcpy(*key_out, input, key_len);
    (*key_out)[key_len] = '\0';

    /* Parse transforms */
    const char *p = first_colon + 1;

    while (*p) {
        apex_transform *transform = calloc(1, sizeof(apex_transform));
        if (!transform) {
            free_transform_chain(first);
            free(*key_out);
            *key_out = NULL;
            return NULL;
        }

        if (!first) {
            first = transform;
        }
        if (current) {
            current->next = transform;
        }
        current = transform;

        /* Find end of transform name (colon or opening paren) */
        const char *name_end = p;
        while (*name_end && *name_end != ':' && *name_end != '(') {
            name_end++;
        }

        size_t name_len = name_end - p;
        transform->name = malloc(name_len + 1);
        if (!transform->name) {
            free_transform_chain(first);
            free(*key_out);
            *key_out = NULL;
            return NULL;
        }
        memcpy(transform->name, p, name_len);
        transform->name[name_len] = '\0';

        p = name_end;

        /* Check for options in parentheses */
        if (*p == '(') {
            p++;  /* Skip opening paren */
            const char *opt_start = p;
            const char *opt_end = strchr(p, ')');
            if (!opt_end) {
                /* Malformed - missing closing paren */
                free_transform_chain(first);
                free(*key_out);
                *key_out = NULL;
                return NULL;
            }

            size_t opt_len = opt_end - opt_start;
            transform->options = malloc(opt_len + 1);
            if (!transform->options) {
                free_transform_chain(first);
                free(*key_out);
                *key_out = NULL;
                return NULL;
            }
            memcpy(transform->options, opt_start, opt_len);
            transform->options[opt_len] = '\0';

            p = opt_end + 1;
        }

        /* Skip colon separator if present */
        if (*p == ':') {
            p++;
        } else if (*p != '\0') {
            /* Unexpected character */
            break;
        }
    }

    return first;
}

/**
 * Parse date string into struct tm
 * Supports: YYYY-MM-DD HH:MM:SS, YYYY-MM-DD HH:MM, YYYY-MM-DD
 */
static bool parse_date(const char *date_str, struct tm *tm_out) {
    if (!date_str || !tm_out) return false;

    memset(tm_out, 0, sizeof(struct tm));

    /* Try different formats */
    #ifdef HAVE_STRPTIME
    const char *formats[] = {
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d %H:%M",
        "%Y-%m-%d"
    };

    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        struct tm temp;
        memset(&temp, 0, sizeof(temp));

        char *end = strptime(date_str, formats[i], &temp);
        if (end && *end == '\0') {
            *tm_out = temp;
            return true;
        }
    }
    #else
    /* Manual parsing fallback */
    int year = 0, mon = 0, mday = 0, hour = 0, min = 0, sec = 0;
    int items = sscanf(date_str, "%d-%d-%d %d:%d:%d", &year, &mon, &mday, &hour, &min, &sec);
    if (items >= 3) {
        /* At least got date part */
        tm_out->tm_year = year - 1900;  /* tm_year is years since 1900 */
        tm_out->tm_mon = mon - 1;       /* tm_mon is 0-based */
        tm_out->tm_mday = mday;
        if (items >= 4) tm_out->tm_hour = hour;
        if (items >= 5) tm_out->tm_min = min;
        if (items >= 6) tm_out->tm_sec = sec;
        return true;
    }
    #endif

    return false;
}

/**
 * Create string array from comma-separated string
 */
static apex_string_array *split_string(const char *str, const char *delimiter) {
    if (!str) return NULL;

    apex_string_array *arr = calloc(1, sizeof(apex_string_array));
    if (!arr) return NULL;

    arr->capacity = 8;
    arr->items = malloc(arr->capacity * sizeof(char*));
    if (!arr->items) {
        free(arr);
        return NULL;
    }

    if (!delimiter || delimiter[0] == '\0') {
        delimiter = ",";  /* Default to comma */
    }

    char *str_copy = strdup(str);
    if (!str_copy) {
        free(arr->items);
        free(arr);
        return NULL;
    }

    char *token = strtok(str_copy, delimiter);
    while (token) {
        /* Trim whitespace from token */
        char *start = token;
        while (*start && isspace((unsigned char)*start)) start++;
        char *end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end-1))) end--;
        *end = '\0';

        if (arr->count >= arr->capacity) {
            arr->capacity *= 2;
            arr->items = realloc(arr->items, arr->capacity * sizeof(char*));
            if (!arr->items) {
                free(str_copy);
                free_string_array(arr);
                return NULL;
            }
        }

        arr->items[arr->count] = strdup(start);
        if (!arr->items[arr->count]) {
            free(str_copy);
            free_string_array(arr);
            return NULL;
        }
        arr->count++;

        token = strtok(NULL, delimiter);
    }

    free(str_copy);
    return arr;
}

/**
 * Apply a single transform to a value
 * Returns newly allocated string, or NULL on error
 * If is_array is true, value is treated as an array (apex_string_array*)
 */
static char *apply_transform(const char *transform_name, const char *options,
                            const char *value, apex_string_array **array_ptr, bool *is_array) {
    if (!transform_name || !value) return NULL;

    /* Handle array transforms */
    if (strcmp(transform_name, "split") == 0) {
        const char *delim = options && options[0] ? options : " ";
        apex_string_array *arr = split_string(value, delim);
        if (!arr) return NULL;
        if (*array_ptr) free_string_array(*array_ptr);
        *array_ptr = arr;
        *is_array = true;
        /* Return first element as representation */
        return arr->count > 0 ? strdup(arr->items[0]) : strdup("");
    }

    if (strcmp(transform_name, "join") == 0) {
        if (!*is_array || !*array_ptr) {
            /* Not an array yet, try to split on commas */
            apex_string_array *arr = split_string(value, ",");
            if (!arr) return NULL;
            if (*array_ptr) free_string_array(*array_ptr);
            *array_ptr = arr;
            *is_array = true;
        }

        const char *delim = options && options[0] ? options : ", ";
        apex_string_array *arr = *array_ptr;

        if (arr->count == 0) return strdup("");
        if (arr->count == 1) return strdup(arr->items[0]);

        /* Calculate total length */
        size_t total_len = 0;
        size_t delim_len = strlen(delim);
        for (size_t i = 0; i < arr->count; i++) {
            total_len += strlen(arr->items[i]);
            if (i < arr->count - 1) total_len += delim_len;
        }

        char *result = malloc(total_len + 1);
        if (!result) return NULL;

        char *p = result;
        for (size_t i = 0; i < arr->count; i++) {
            size_t len = strlen(arr->items[i]);
            memcpy(p, arr->items[i], len);
            p += len;
            if (i < arr->count - 1) {
                memcpy(p, delim, delim_len);
                p += delim_len;
            }
        }
        *p = '\0';

        /* Join converts array back to string */
        *is_array = false;
        return result;
    }

    if (strcmp(transform_name, "first") == 0) {
        if (!*is_array || !*array_ptr) {
            /* Not an array yet, try to split on commas */
            apex_string_array *arr = split_string(value, ",");
            if (!arr) return NULL;
            if (*array_ptr) free_string_array(*array_ptr);
            *array_ptr = arr;
            *is_array = true;
        }
        apex_string_array *arr = *array_ptr;
        if (arr->count == 0) return strdup("");
        return strdup(arr->items[0]);
    }

    if (strcmp(transform_name, "last") == 0) {
        if (!*is_array || !*array_ptr) {
            /* Not an array yet, try to split on commas */
            apex_string_array *arr = split_string(value, ",");
            if (!arr) return NULL;
            if (*array_ptr) free_string_array(*array_ptr);
            *array_ptr = arr;
            *is_array = true;
        }
        apex_string_array *arr = *array_ptr;
        if (arr->count == 0) return strdup("");
        return strdup(arr->items[arr->count - 1]);
    }

    if (strcmp(transform_name, "slice") == 0) {
        if (!*is_array || !*array_ptr) {
            /* Not an array yet - split string into individual characters */
            size_t value_len = strlen(value);
            apex_string_array *arr = calloc(1, sizeof(apex_string_array));
            if (!arr) return NULL;

            if (value_len == 0) {
                /* Empty string - create empty array */
                arr->capacity = 1;
                arr->items = malloc(sizeof(char*));
                if (!arr->items) {
                    free(arr);
                    return NULL;
                }
                arr->count = 0;
            } else {
                arr->capacity = value_len;
                arr->items = malloc(arr->capacity * sizeof(char*));
                if (!arr->items) {
                    free(arr);
                    return NULL;
                }

                for (size_t i = 0; i < value_len; i++) {
                    arr->items[i] = malloc(2);
                    if (!arr->items[i]) {
                        free_string_array(arr);
                        return NULL;
                    }
                    arr->items[i][0] = value[i];
                    arr->items[i][1] = '\0';
                    arr->count++;
                }
            }

            if (*array_ptr) free_string_array(*array_ptr);
            *array_ptr = arr;
            *is_array = true;
        }

        if (!options) return strdup(value);

        long start = 0, len = -1;
        if (sscanf(options, "%ld,%ld", &start, &len) >= 1) {
            apex_string_array *arr = *array_ptr;
            if (start < 0) start = 0;
            if (start >= (long)arr->count) return strdup("");
            if (len < 0) len = (long)arr->count - start;
            if (len > 0 && (size_t)(start + len) > arr->count) len = (long)arr->count - start;

            /* Create new array with slice */
            apex_string_array *slice = calloc(1, sizeof(apex_string_array));
            if (!slice) return NULL;
            slice->capacity = len;
            slice->items = malloc(len * sizeof(char*));
            if (!slice->items) {
                free(slice);
                return NULL;
            }

            for (long i = 0; i < len; i++) {
                slice->items[i] = strdup(arr->items[start + i]);
                if (!slice->items[i]) {
                    free_string_array(slice);
                    return NULL;
                }
            }
            slice->count = len;

            /* Replace old array */
            if (*array_ptr) free_string_array(*array_ptr);
            *array_ptr = slice;
            *is_array = true;

            /* Return joined representation with no separator */
            size_t total_len = 0;
            for (size_t i = 0; i < slice->count; i++) {
                total_len += strlen(slice->items[i]);
            }
            char *result = malloc(total_len + 1);
            if (!result) return NULL;
            char *p = result;
            for (size_t i = 0; i < slice->count; i++) {
                size_t item_len = strlen(slice->items[i]);
                memcpy(p, slice->items[i], item_len);
                p += item_len;
            }
            *p = '\0';
            return result;
        }
        return strdup(value);
    }

    /* String transforms */
    if (*is_array) {
        /* Convert array to string first */
        apex_string_array *arr = *array_ptr;
        if (arr->count == 0) value = "";
        else if (arr->count == 1) value = arr->items[0];
        else {
            /* Join array */
            size_t total_len = 0;
            for (size_t i = 0; i < arr->count; i++) {
                total_len += strlen(arr->items[i]);
                if (i < arr->count - 1) total_len += 2;
            }
            char *joined = malloc(total_len + 1);
            if (!joined) return NULL;
            char *p = joined;
            for (size_t i = 0; i < arr->count; i++) {
                size_t len = strlen(arr->items[i]);
                memcpy(p, arr->items[i], len);
                p += len;
                if (i < arr->count - 1) {
                    *p++ = ',';
                    *p++ = ' ';
                }
            }
            *p = '\0';
            value = joined;
            *is_array = false;
        }
    }

    if (strcmp(transform_name, "upper") == 0) {
        char *result = strdup(value);
        if (!result) return NULL;
        for (char *p = result; *p; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        return result;
    }

    if (strcmp(transform_name, "lower") == 0) {
        char *result = strdup(value);
        if (!result) return NULL;
        for (char *p = result; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        return result;
    }

    if (strcmp(transform_name, "trim") == 0) {
        const char *start = value;
        while (*start && isspace((unsigned char)*start)) start++;
        if (*start == '\0') return strdup("");
        const char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) end--;
        size_t len = end - start + 1;
        char *result = malloc(len + 1);
        if (!result) return NULL;
        memcpy(result, start, len);
        result[len] = '\0';
        return result;
    }

    if (strcmp(transform_name, "title") == 0) {
        char *result = strdup(value);
        if (!result) return NULL;
        bool prev_space = true;
        for (char *p = result; *p; p++) {
            if (isspace((unsigned char)*p)) {
                prev_space = true;
            } else {
                if (prev_space) {
                    *p = (char)toupper((unsigned char)*p);
                } else {
                    *p = (char)tolower((unsigned char)*p);
                }
                prev_space = false;
            }
        }
        return result;
    }

    if (strcmp(transform_name, "strftime") == 0) {
        if (!options) return strdup(value);

        struct tm tm;
        if (!parse_date(value, &tm)) {
            /* Date parsing failed, return original */
            return strdup(value);
        }

        /* Use strftime to format */
        char buffer[256];
        size_t result_len = strftime(buffer, sizeof(buffer), options, &tm);
        if (result_len == 0) {
            /* Format failed, return original */
            return strdup(value);
        }
        return strdup(buffer);
    }

    if (strcmp(transform_name, "capitalize") == 0) {
        char *result = strdup(value);
        if (!result) return NULL;
        if (*result) {
            *result = (char)toupper((unsigned char)*result);
        }
        return result;
    }

    if (strcmp(transform_name, "slug") == 0 || strcmp(transform_name, "slugify") == 0) {
        char *result = malloc(strlen(value) * 2 + 1);  /* Worst case: every char becomes hyphen */
        if (!result) return NULL;
        char *out = result;
        bool prev_was_hyphen = false;

        for (const char *p = value; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalnum(c)) {
                *out++ = (char)tolower(c);
                prev_was_hyphen = false;
            } else if (isspace(c) || c == '_') {
                if (!prev_was_hyphen) {
                    *out++ = '-';
                    prev_was_hyphen = true;
                }
            }
            /* Skip other special characters */
        }

        /* Remove trailing hyphens */
        while (out > result && out[-1] == '-') {
            out--;
        }

        *out = '\0';
        return result;
    }

    if (strcmp(transform_name, "replace") == 0) {
        if (!options) return strdup(value);

        /* Parse options: OLD,NEW or regex:OLD,NEW */
        char *options_copy = strdup(options);
        if (!options_copy) return strdup(value);

        char *old_str = options_copy;
        char *new_str = strchr(options_copy, ',');
        bool use_regex = false;

        /* Check if it's regex: prefix */
        if (strncmp(old_str, "regex:", 6) == 0) {
            use_regex = true;
        }

        if (!new_str) {
            /* No comma found, treat entire options as search string */
            free(options_copy);
            return strdup(value);
        }

        /* Extract old_str and new_str before modifying options_copy */
        /* Calculate length before any pointer modifications */
        size_t old_str_start_len = new_str - old_str;
        if (old_str_start_len == 0) {
            free(options_copy);
            return strdup(value);
        }
        char *old_pattern = malloc(old_str_start_len + 1);
        if (!old_pattern) {
            free(options_copy);
            return strdup(value);
        }
        memcpy(old_pattern, old_str, old_str_start_len);
        old_pattern[old_str_start_len] = '\0';

        /* If regex, remove the "regex:" prefix from the pattern */
        if (use_regex) {
            /* We already detected regex: prefix, so remove it from old_pattern */
            if (old_str_start_len > 6 && strncmp(old_pattern, "regex:", 6) == 0) {
                size_t pattern_len = old_str_start_len - 6;
                memmove(old_pattern, old_pattern + 6, pattern_len);
                old_pattern[pattern_len] = '\0';
            }
        }

        *new_str = '\0';
        new_str++;

        char *result = NULL;

        if (use_regex) {
            /* Use regex replacement */
            regex_t regex;
            int ret = regcomp(&regex, old_pattern, REG_EXTENDED);
            if (ret == 0) {
                regmatch_t matches[1];
                /* Count matches first to estimate size */
                size_t count = 0;
                const char *search_pos = value;
                while (regexec(&regex, search_pos, 1, matches, 0) == 0) {
                    count++;
                    search_pos += matches[0].rm_eo;
                    if (*search_pos == '\0') break;
                }

                if (count > 0) {
                    /* Build result with replacements */
                    size_t result_cap = strlen(value) + count * (strlen(new_str) + 10);
                    result = malloc(result_cap);
                    if (result) {
                        char *out = result;
                        const char *src = value;

                        search_pos = src;
                        while (regexec(&regex, search_pos, 1, matches, 0) == 0) {
                            /* Copy text before match */
                            size_t before_len = matches[0].rm_so;
                            memcpy(out, search_pos, before_len);
                            out += before_len;

                            /* Copy replacement */
                            size_t repl_len = strlen(new_str);
                            memcpy(out, new_str, repl_len);
                            out += repl_len;

                            search_pos += matches[0].rm_eo;
                        }

                        /* Copy remaining text */
                        strcpy(out, search_pos);
                    } else {
                        /* Malloc failed, return original */
                        result = strdup(value);
                    }
                } else {
                    /* No matches, return original */
                    result = strdup(value);
                }

                regfree(&regex);
            } else {
                /* Regex compilation failed, return original */
                result = strdup(value);
            }
                free(old_pattern);
        } else {
            /* Simple string replacement */
            size_t old_len = strlen(old_pattern);
            size_t new_len = strlen(new_str);
            size_t value_len = strlen(value);

            /* Count occurrences */
            size_t count = 0;
            const char *p = value;
            while ((p = strstr(p, old_pattern)) != NULL) {
                count++;
                p += old_len;
            }

            if (count == 0) {
                result = strdup(value);
            } else {
                size_t result_len = value_len + count * (new_len > old_len ? (new_len - old_len) : 0);
                result = malloc(result_len + 1);
                if (result) {
                    char *out = result;
                    const char *src = value;
                    const char *next;

                    while ((next = strstr(src, old_pattern)) != NULL) {
                        /* Copy text before match */
                        size_t before_len = next - src;
                        memcpy(out, src, before_len);
                        out += before_len;

                        /* Copy replacement */
                        memcpy(out, new_str, new_len);
                        out += new_len;

                        src = next + old_len;
                    }

                    /* Copy remaining text */
                    strcpy(out, src);
                } else {
                    /* Malloc failed, return original */
                    result = strdup(value);
                }
            }
            free(old_pattern);
        }

        free(options_copy);
        if (!result) return strdup(value);
        return result;
    }

    if (strcmp(transform_name, "substring") == 0 || strcmp(transform_name, "substr") == 0) {
        if (!options) return strdup(value);

        long start = 0, end = -1;
        if (sscanf(options, "%ld,%ld", &start, &end) >= 1) {
            size_t len = strlen(value);
            if (start < 0) start = len + start;  /* Negative index from end */
            if (end < 0) end = len + end;
            if (start < 0) start = 0;
            if (end < 0) end = (long)len;
            if (start > (long)len) start = len;
            if (end > (long)len) end = len;
            if (start > end) return strdup("");

            size_t substr_len = end - start;
            char *result = malloc(substr_len + 1);
            if (!result) return NULL;
            memcpy(result, value + start, substr_len);
            result[substr_len] = '\0';
            return result;
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "truncate") == 0) {
        if (!options) return strdup(value);

        long max_len = 0;
        char suffix[64] = "";
        if (sscanf(options, "%ld,%63s", &max_len, suffix) >= 1 ||
            sscanf(options, "%ld", &max_len) == 1) {
            size_t len = strlen(value);
            if ((long)len <= max_len) return strdup(value);

            size_t suffix_len = strlen(suffix);
            size_t trunc_len = max_len > (long)suffix_len ? max_len - suffix_len : max_len;

            char *result = malloc(trunc_len + suffix_len + 1);
            if (!result) return NULL;
            memcpy(result, value, trunc_len);
            memcpy(result + trunc_len, suffix, suffix_len);
            result[trunc_len + suffix_len] = '\0';
            return result;
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "default") == 0) {
        if (value[0] == '\0') {
            return options ? strdup(options) : strdup("");
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "escape") == 0 || strcmp(transform_name, "html_escape") == 0) {
        size_t len = strlen(value);
        size_t result_cap = len * 6 + 1;  /* Worst case: every char becomes &xxxx; */
        char *result = malloc(result_cap);
        if (!result) return NULL;

        char *out = result;
        for (const char *p = value; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '&': strcpy(out, "&amp;"); out += 5; break;
                case '<': strcpy(out, "&lt;"); out += 4; break;
                case '>': strcpy(out, "&gt;"); out += 4; break;
                case '"': strcpy(out, "&quot;"); out += 6; break;
                case '\'': strcpy(out, "&#39;"); out += 5; break;
                default:
                    if (c >= 32 && c < 127) {
                        *out++ = (char)c;
                    } else {
                        /* Encode other characters as entities */
                        out += sprintf(out, "&#%u;", c);
                    }
                    break;
            }
        }
        *out = '\0';
        return result;
    }

    if (strcmp(transform_name, "basename") == 0) {
        const char *last_slash = strrchr(value, '/');
        if (last_slash) {
            return strdup(last_slash + 1);
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "urlencode") == 0) {
        size_t len = strlen(value);
        char *result = malloc(len * 3 + 1);  /* Worst case: every char becomes %XX */
        if (!result) return NULL;

        char *out = result;
        for (const char *p = value; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                *out++ = (char)c;
            } else {
                out += sprintf(out, "%%%02X", c);
            }
        }
        *out = '\0';
        return result;
    }

    if (strcmp(transform_name, "urldecode") == 0) {
        size_t len = strlen(value);
        char *result = malloc(len + 1);
        if (!result) return NULL;

        char *out = result;
        for (const char *p = value; *p; p++) {
            if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                char hex[3] = {p[1], p[2], '\0'};
                *out++ = (char)strtoul(hex, NULL, 16);
                p += 2;
            } else if (*p == '+') {
                *out++ = ' ';
            } else {
                *out++ = *p;
            }
        }
        *out = '\0';
        return result;
    }

    if (strcmp(transform_name, "prefix") == 0) {
        if (!options) return strdup(value);
        size_t prefix_len = strlen(options);
        size_t value_len = strlen(value);
        char *result = malloc(prefix_len + value_len + 1);
        if (!result) return NULL;
        memcpy(result, options, prefix_len);
        memcpy(result + prefix_len, value, value_len);
        result[prefix_len + value_len] = '\0';
        return result;
    }

    if (strcmp(transform_name, "suffix") == 0) {
        if (!options) return strdup(value);
        size_t suffix_len = strlen(options);
        size_t value_len = strlen(value);
        char *result = malloc(value_len + suffix_len + 1);
        if (!result) return NULL;
        memcpy(result, value, value_len);
        memcpy(result + value_len, options, suffix_len);
        result[value_len + suffix_len] = '\0';
        return result;
    }

    if (strcmp(transform_name, "remove") == 0) {
        if (!options) return strdup(value);

        size_t remove_len = strlen(options);
        if (remove_len == 0) return strdup(value);

        size_t value_len = strlen(value);
        char *result = malloc(value_len + 1);
        if (!result) return NULL;

        char *out = result;
        for (const char *p = value; *p; p++) {
            if (strncmp(p, options, remove_len) == 0) {
                p += remove_len - 1;  /* -1 because loop increments */
            } else {
                *out++ = *p;
            }
        }
        *out = '\0';
        return result;
    }

    if (strcmp(transform_name, "repeat") == 0) {
        if (!options) return strdup(value);

        long count = 1;
        if (sscanf(options, "%ld", &count) == 1 && count > 0) {
            size_t value_len = strlen(value);
            if (value_len == 0) return strdup("");

            char *result = malloc(value_len * count + 1);
            if (!result) return NULL;

            char *out = result;
            for (long i = 0; i < count; i++) {
                memcpy(out, value, value_len);
                out += value_len;
            }
            *out = '\0';
            return result;
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "reverse") == 0) {
        size_t len = strlen(value);
        char *result = malloc(len + 1);
        if (!result) return NULL;

        for (size_t i = 0; i < len; i++) {
            result[i] = value[len - 1 - i];
        }
        result[len] = '\0';
        return result;
    }

    if (strcmp(transform_name, "format") == 0) {
        if (!options) return strdup(value);

        /* Try to parse as number for formatting */
        double num = 0.0;
        if (sscanf(value, "%lf", &num) == 1) {
            char buffer[256];
            int written = snprintf(buffer, sizeof(buffer), options, num);
            if (written > 0 && written < (int)sizeof(buffer)) {
                return strdup(buffer);
            }
        }
        /* If not a number or format failed, return original */
        return strdup(value);
    }

    if (strcmp(transform_name, "length") == 0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%zu", strlen(value));
        return strdup(buffer);
    }

    if (strcmp(transform_name, "pad") == 0) {
        if (!options) return strdup(value);

        long width = 0;
        char pad_char = ' ';
        if (sscanf(options, "%ld,%c", &width, &pad_char) >= 1 ||
            sscanf(options, "%ld", &width) == 1) {
            size_t len = strlen(value);
            if ((long)len >= width) return strdup(value);

            size_t pad_count = width - len;
            char *result = malloc(width + 1);
            if (!result) return NULL;

            memset(result, pad_char, pad_count);
            memcpy(result + pad_count, value, len);
            result[width] = '\0';
            return result;
        }
        return strdup(value);
    }

    if (strcmp(transform_name, "contains") == 0) {
        if (!options) return strdup("false");
        return strstr(value, options) ? strdup("true") : strdup("false");
    }

    /* Unknown transform, return original */
    return strdup(value);
}

/**
 * Apply transform chain to a value
 * Returns newly allocated string
 */
static char *apply_transform_chain(const char *value, apex_transform *chain) {
    if (!value || !chain) return strdup(value ? value : "");

    char *current_value = strdup(value);
    apex_string_array *array = NULL;
    bool is_array = false;

    apex_transform *transform = chain;
    while (transform) {
        char *new_value = apply_transform(transform->name, transform->options,
                                         current_value, &array, &is_array);
        if (!new_value) {
            /* Transform failed, return original value */
            free(current_value);
            if (array) free_string_array(array);
            return strdup(value ? value : "");
        }

        if (current_value != value) {  /* Don't free original value */
            free(current_value);
        }
        current_value = new_value;

        transform = transform->next;
    }

    if (array) free_string_array(array);

    return current_value;
}

/**
 * Replace [%key] patterns with metadata values
 * If options->enable_metadata_transforms is true, supports [%key:transform:transform2] syntax
 */
char *apex_metadata_replace_variables(const char *text, apex_metadata_item *metadata, const apex_options *options) {
    if (!text || !metadata) {
        return text ? strdup(text) : NULL;
    }

    bool transforms_enabled = options && options->enable_metadata_transforms;

    /* Build result incrementally */
    size_t result_capacity = strlen(text) + 512;  /* Extra space for potential expansions */
    char *result = malloc(result_capacity);
    if (!result) return NULL;

    size_t result_len = 0;
    const char *last_pos = text;  /* Track position in original text */

    const char *p = text;
    while ((p = strstr(p, "[%")) != NULL) {
        /* Find the matching closing ']' for '[%', accounting for brackets inside the pattern */
        /* Since '[%' opens a bracket, we need to find the matching ']' */
        /* We scan forward and track bracket depth to handle nested brackets in regex patterns */
        const char *end = p + 2;
        int bracket_depth = 1;  /* Start at 1 because '[%' opened a bracket */

        while (*end && bracket_depth > 0) {
            if (*end == '[') {
                bracket_depth++;
            } else if (*end == ']') {
                bracket_depth--;
            }
            if (bracket_depth > 0) {
                end++;
            }
        }

        if (!*end && bracket_depth > 0) {
            /* Malformed - no matching closing bracket found */
            break;
        }

        /* Copy text before [%...] */
        size_t prefix_len = p - last_pos;
        if (prefix_len > 0) {
            if (result_len + prefix_len + 1 > result_capacity) {
                result_capacity = (result_len + prefix_len + 1) * 2;
                char *new_result = realloc(result, result_capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + result_len, last_pos, prefix_len);
            result_len += prefix_len;
        }

        /* Extract key and transforms */
        size_t pattern_len = end - (p + 2);
        char pattern[512];
        if (pattern_len >= sizeof(pattern)) {
            /* Pattern too long, keep original */
            size_t keep_len = (end - p) + 1;
            if (result_len + keep_len + 1 > result_capacity) {
                result_capacity = (result_len + keep_len + 1) * 2;
                char *new_result = realloc(result, result_capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + result_len, p, keep_len);
            result_len += keep_len;
            last_pos = end + 1;
            p = end + 1;
            continue;
        }

        memcpy(pattern, p + 2, pattern_len);
        pattern[pattern_len] = '\0';

        /* Parse key and transforms */
        char *key = NULL;
        apex_transform *transform_chain = NULL;
        const char *value = NULL;
        char *transformed_value = NULL;

        if (transforms_enabled && strchr(pattern, ':')) {
            /* Has transforms */
            transform_chain = parse_transform_chain(pattern, &key);
            if (key) {
                value = apex_metadata_get(metadata, key);
                if (value && transform_chain) {
                    transformed_value = apply_transform_chain(value, transform_chain);
                    /* If transform chain failed, fall back to original value */
                    if (!transformed_value) {
                        transformed_value = strdup(value);
                    }
                } else if (value) {
                    transformed_value = strdup(value);
                }
                free(key);
                free_transform_chain(transform_chain);
            } else {
                /* Parse failed, treat as simple key */
                value = apex_metadata_get(metadata, pattern);
                if (value) {
                    transformed_value = strdup(value);
                }
            }
        } else {
            /* Simple key, no transforms */
            value = apex_metadata_get(metadata, pattern);
            if (value) {
                transformed_value = strdup(value);
            }
        }

        if (transformed_value) {
            size_t value_len = strlen(transformed_value);
            if (result_len + value_len + 1 > result_capacity) {
                result_capacity = (result_len + value_len + 1) * 2;
                char *new_result = realloc(result, result_capacity);
                if (!new_result) {
                    free(result);
                    free(transformed_value);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + result_len, transformed_value, value_len);
            result_len += value_len;
            free(transformed_value);
        } else {
            /* Keep original pattern if not found */
            size_t keep_len = (end - p) + 1;
            if (result_len + keep_len + 1 > result_capacity) {
                result_capacity = (result_len + keep_len + 1) * 2;
                char *new_result = realloc(result, result_capacity);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            memcpy(result + result_len, p, keep_len);
            result_len += keep_len;
        }

        last_pos = end + 1;
        p = end + 1;
    }

    /* Copy remaining text after last [%...] */
    if (last_pos && *last_pos) {
        size_t remaining = strlen(last_pos);
        if (result_len + remaining + 1 > result_capacity) {
            result_capacity = result_len + remaining + 1;
            char *new_result = realloc(result, result_capacity);
            if (!new_result) {
                free(result);
                return NULL;
            }
            result = new_result;
        }
        strcpy(result + result_len, last_pos);
        result_len += remaining;
    }

    result[result_len] = '\0';
    return result;
}

/**
 * Load metadata from a file
 * Auto-detects format based on first characters
 */
apex_metadata_item *apex_load_metadata_from_file(const char *filepath) {
    if (!filepath) return NULL;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return NULL;

    /* Read file into buffer */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0 || file_size > 1024 * 1024) {  /* Max 1MB */
        fclose(fp);
        return NULL;
    }

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);

    apex_metadata_item *items = NULL;
    size_t consumed = 0;

    /* Auto-detect format */
    if (strncmp(buffer, "---", 3) == 0) {
        /* YAML format */
        items = parse_yaml_metadata(buffer, &consumed);
    } else if (buffer[0] == '%') {
        /* Pandoc format */
        items = parse_pandoc_metadata(buffer, &consumed);
    } else {
        /* MMD format (default) */
        items = parse_mmd_metadata(buffer, &consumed);
    }

    free(buffer);
    return items;
}

/**
 * Parse a single KEY=VALUE pair, handling quoted values
 * Returns key and value in allocated strings (caller must free)
 */
static int parse_key_value_pair(const char *input, char **key_out, char **value_out) {
    if (!input) return 0;

    const char *equals = strchr(input, '=');
    if (!equals) return 0;

    /* Extract key */
    size_t key_len = equals - input;
    *key_out = malloc(key_len + 1);
    if (!*key_out) return 0;
    memcpy(*key_out, input, key_len);
    (*key_out)[key_len] = '\0';
    trim_whitespace(*key_out);

    /* Extract value */
    const char *value_start = equals + 1;

    /* Check if value is quoted */
    if (*value_start == '"' || *value_start == '\'') {
        char quote = *value_start;
        value_start++;
        const char *value_end = strchr(value_start, quote);
        if (!value_end) {
            /* Unmatched quote, treat rest as value */
            *value_out = strdup(value_start);
            return 1;
        }
        size_t value_len = value_end - value_start;
        *value_out = malloc(value_len + 1);
        if (!*value_out) {
            free(*key_out);
            return 0;
        }
        memcpy(*value_out, value_start, value_len);
        (*value_out)[value_len] = '\0';
    } else {
        /* Unquoted value - take until comma or end */
        const char *value_end = strchr(value_start, ',');
        if (!value_end) {
            value_end = value_start + strlen(value_start);
        }
        size_t value_len = value_end - value_start;
        *value_out = malloc(value_len + 1);
        if (!*value_out) {
            free(*key_out);
            return 0;
        }
        memcpy(*value_out, value_start, value_len);
        (*value_out)[value_len] = '\0';
        trim_whitespace(*value_out);
    }

    return 1;
}

/**
 * Parse command-line metadata from KEY=VALUE string
 * Handles quoted values and comma-separated pairs
 */
apex_metadata_item *apex_parse_command_metadata(const char *arg) {
    if (!arg || !*arg) return NULL;

    apex_metadata_item *items = NULL;
    const char *p = arg;

    while (*p) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Find the equals sign for this pair */
        const char *equals = strchr(p, '=');
        if (!equals) break;  /* No equals sign, invalid */

        /* Find where this KEY=VALUE pair ends */
        const char *value_start = equals + 1;
        const char *pair_end = NULL;

        /* Check if value is quoted */
        if (*value_start == '"' || *value_start == '\'') {
            char quote = *value_start;
            const char *quote_end = strchr(value_start + 1, quote);
            if (quote_end) {
                pair_end = quote_end + 1;
            } else {
                /* Unmatched quote - take rest of string */
                pair_end = p + strlen(p);
            }
        } else {
            /* Unquoted value - find next comma that's followed by KEY= pattern
             * We look for comma followed by whitespace and then KEY= */
            const char *search = value_start;
            pair_end = p + strlen(p);  /* Default to end of string */

            const char *comma = strchr(search, ',');
            if (comma) {
                /* Found a comma - check if it starts a new KEY= pair */
                const char *after_comma = comma + 1;
                while (*after_comma && isspace((unsigned char)*after_comma)) after_comma++;

                /* Look for KEY= pattern (alphanumeric key followed by =) */
                if ((isalnum((unsigned char)*after_comma) || *after_comma == '_')) {
                    const char *next_equals = strchr(after_comma, '=');
                    if (next_equals) {
                        /* Check if there's another comma between after_comma and next_equals */
                        const char *comma_between = strchr(after_comma, ',');
                        if (!comma_between || comma_between > next_equals) {
                            /* This comma starts the next KEY= pair */
                            pair_end = comma;
                        }
                    }
                }
            }
            /* If no comma or comma doesn't start KEY= pair, pair_end stays at end of string */
        }

        /* Extract this pair */
        size_t pair_len = pair_end - p;
        char *pair = malloc(pair_len + 1);
        if (!pair) break;
        memcpy(pair, p, pair_len);
        pair[pair_len] = '\0';

        /* Parse key=value */
        char *key = NULL;
        char *value = NULL;
        if (parse_key_value_pair(pair, &key, &value)) {
            if (key && value) {
                add_metadata_item(&items, key, value);
                free(key);
                free(value);
            }
        }

        free(pair);

        /* Move to next pair */
        if (pair_end < p + strlen(p) && *pair_end == ',') {
            p = pair_end + 1;
        } else {
            break;
        }
    }

    return items;
}

/**
 * Merge multiple metadata lists with precedence
 * Later lists take precedence over earlier ones
 */
apex_metadata_item *apex_merge_metadata(apex_metadata_item *first, ...) {
    apex_metadata_item *result = NULL;

    /* Start with a copy of the first list (if any) */
    apex_metadata_item *src;
    if (first) {
        src = first;
        while (src) {
            add_metadata_item(&result, src->key, src->value);
            src = src->next;
        }
    }

    /* Merge remaining lists */
    va_list args;
    va_start(args, first);

    apex_metadata_item *next_list;
    while ((next_list = va_arg(args, apex_metadata_item*)) != NULL) {
        /* For each item in next_list, add or replace in result */
        src = next_list;
        while (src) {
            /* Remove existing item with same key (case-insensitive) */
            apex_metadata_item *prev = NULL;
            apex_metadata_item *curr = result;
            while (curr) {
                if (strcasecmp(curr->key, src->key) == 0) {
                    /* Remove this item */
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        result = curr->next;
                    }
                    free(curr->key);
                    free(curr->value);
                    apex_metadata_item *to_free = curr;
                    curr = curr->next;
                    free(to_free);
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
            /* Add new item */
            add_metadata_item(&result, src->key, src->value);
            src = src->next;
        }
    }

    va_end(args);
    return result;
}
