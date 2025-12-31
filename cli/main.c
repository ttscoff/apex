/**
 * Apex CLI - Command-line interface for the Apex Markdown processor
 */

#include "../include/apex/apex.h"
#include "../src/extensions/metadata.h"
#include "../src/extensions/includes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>

/* Remote plugin directory helpers (from plugins_remote.c) */
typedef struct apex_remote_plugin apex_remote_plugin;
typedef struct apex_remote_plugin_list apex_remote_plugin_list;

apex_remote_plugin_list *apex_remote_fetch_directory(const char *url);
void apex_remote_print_plugins(apex_remote_plugin_list *list);
void apex_remote_print_plugins_filtered(apex_remote_plugin_list *list,
                                        const char **installed_ids,
                                        size_t installed_count);
apex_remote_plugin *apex_remote_find_plugin(apex_remote_plugin_list *list, const char *id);
void apex_remote_free_plugins(apex_remote_plugin_list *list);
const char *apex_remote_plugin_repo(apex_remote_plugin *p);

/* Profiling helpers (same as in apex.c) */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static bool profiling_enabled(void) {
    const char *env = getenv("APEX_PROFILE");
    return env && (strcmp(env, "1") == 0 || strcmp(env, "yes") == 0 || strcmp(env, "true") == 0);
}

#define PROFILE_START(name) \
    double name##_start = 0; \
    if (profiling_enabled()) { name##_start = get_time_ms(); }

#define PROFILE_END(name) \
    if (profiling_enabled()) { \
        double name##_elapsed = get_time_ms() - name##_start; \
        fprintf(stderr, "[PROFILE] %-30s: %8.2f ms\n", #name, name##_elapsed); \
    }

#define BUFFER_SIZE 4096

static void print_usage(const char *program_name) {
    fprintf(stderr, "Apex Markdown Processor v%s\n", apex_version_string());
    fprintf(stderr, "One Markdown processor to rule them all\n\n");
    fprintf(stderr, "Project homepage: https://github.com/ApexMarkdown/apex\n\n");
    fprintf(stderr, "Usage: %s [options] [file]\n", program_name);
    fprintf(stderr, "       %s --combine [files...]\n", program_name);
    fprintf(stderr, "       %s --mmd-merge [index files...]\n\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --accept               Accept all Critic Markup changes (apply edits)\n");
    fprintf(stderr, "  --[no-]alpha-lists     Support alpha list markers (a., b., c. and A., B., C.)\n");
    fprintf(stderr, "  --[no-]autolink        Enable autolinking of URLs and email addresses\n");
    fprintf(stderr, "  --base-dir DIR         Base directory for resolving relative paths (for images, includes, wiki links)\n");
    fprintf(stderr, "  --bibliography FILE     Bibliography file (BibTeX, CSL JSON, or CSL YAML) - can be used multiple times\n");
    fprintf(stderr, "  --captions POSITION    Table caption position: above or below (default: below)\n");
    fprintf(stderr, "  --combine              Concatenate Markdown files (expanding includes) into a single Markdown stream\n");
    fprintf(stderr, "                         When a SUMMARY.md file is provided, treat it as a GitBook index and combine\n");
    fprintf(stderr, "                         the linked files in order. Output is raw Markdown suitable for piping back into Apex.\n");
    fprintf(stderr, "  --csl FILE              Citation style file (CSL format)\n");
    fprintf(stderr, "  --css FILE, --style FILE  Link to CSS file in document head (requires --standalone, overrides CSS metadata)\n");
    fprintf(stderr, "  --embed-css            Embed CSS file contents into a <style> tag in the document head (used with --css)\n");
    fprintf(stderr, "  --embed-images         Embed local images as base64 data URLs in HTML output\n");
    fprintf(stderr, "  --hardbreaks           Treat newlines as hard breaks\n");
    fprintf(stderr, "  --header-anchors        Generate <a> anchor tags instead of header IDs\n");
    fprintf(stderr, "  -h, --help             Show this help message\n");
    fprintf(stderr, "  --id-format FORMAT      Header ID format: gfm (default), mmd, or kramdown\n");
    fprintf(stderr, "                          (modes auto-set format; use this to override in unified mode)\n");
    fprintf(stderr, "  --[no-]includes        Enable file inclusion (enabled by default in unified mode)\n");
    fprintf(stderr, "  --indices               Enable index processing (mmark and TextIndex syntax)\n");
    fprintf(stderr, "  --install-plugin ID    Install plugin by id from directory, or by Git URL/GitHub shorthand (user/repo)\n");
    fprintf(stderr, "  --link-citations       Link citations to bibliography entries\n");
    fprintf(stderr, "  --list-plugins         List installed plugins and available plugins from the remote directory\n");
    fprintf(stderr, "  --uninstall-plugin ID  Uninstall plugin by id\n");
    fprintf(stderr, "  --meta KEY=VALUE       Set metadata key-value pair (can be used multiple times, supports quotes and comma-separated pairs)\n");
    fprintf(stderr, "  --meta-file FILE       Load metadata from external file (YAML, MMD, or Pandoc format)\n");
    fprintf(stderr, "  --[no-]mixed-lists     Allow mixed list markers at same level (inherit type from first item)\n");
    fprintf(stderr, "  --mmd-merge            Merge files from one or more mmd_merge-style index files into a single Markdown stream\n");
    fprintf(stderr, "                         Index files list document parts line-by-line; indentation controls header level shifting.\n");
    fprintf(stderr, "  -m, --mode MODE        Processor mode: commonmark, gfm, mmd, kramdown, unified (default)\n");
    fprintf(stderr, "  --no-bibliography       Suppress bibliography output\n");
    fprintf(stderr, "  --no-footnotes         Disable footnote support\n");
    fprintf(stderr, "  --no-ids                Disable automatic header ID generation\n");
    fprintf(stderr, "  --no-indices            Disable index processing\n");
    fprintf(stderr, "  --no-index              Suppress index generation (markers still created)\n");
    fprintf(stderr, "  --no-math              Disable math support\n");
    fprintf(stderr, "  --aria                  Add ARIA labels and accessibility attributes to HTML output\n");
    fprintf(stderr, "  --no-plugins            Disable external/plugin processing\n");
    fprintf(stderr, "  --no-relaxed-tables    Disable relaxed table parsing\n");
    fprintf(stderr, "  --no-smart             Disable smart typography\n");
    fprintf(stderr, "  --no-sup-sub           Disable superscript/subscript syntax\n");
    fprintf(stderr, "  --[no-]divs            Enable or disable Pandoc fenced divs (Unified mode only)\n");
    fprintf(stderr, "  --[no-]spans           Enable or disable bracketed spans [text]{IAL} (Pandoc-style, enabled by default in unified mode)\n");
    fprintf(stderr, "  --no-tables            Disable table support\n");
    fprintf(stderr, "  --no-transforms        Disable metadata variable transforms\n");
    fprintf(stderr, "  --no-unsafe            Disable raw HTML in output\n");
    fprintf(stderr, "  --no-wikilinks         Disable wiki link syntax\n");
    fprintf(stderr, "  --obfuscate-emails     Obfuscate email links/text using HTML entities\n");
    fprintf(stderr, "  -o, --output FILE      Write output to FILE instead of stdout\n");
    fprintf(stderr, "  --plugins              Enable external/plugin processing\n");
    fprintf(stderr, "  --pretty               Pretty-print HTML with indentation and whitespace\n");
    fprintf(stderr, "  --reject               Reject all Critic Markup changes (revert edits)\n");
    fprintf(stderr, "  --[no-]relaxed-tables  Enable or disable relaxed table parsing (no separator rows required)\n");
    fprintf(stderr, "  --script VALUE         Inject <script> tags before </body> (standalone) or at end of HTML (snippet).\n");
    fprintf(stderr, "                          VALUE can be a path, URL, or shorthand (mermaid, mathjax, katex). Can be used multiple times or as a comma-separated list.\n");
    fprintf(stderr, "  --show-tooltips         Show tooltips on citations\n");
    fprintf(stderr, "  -s, --standalone       Generate complete HTML document (with <html>, <head>, <body>)\n");
    fprintf(stderr, "  --[no-]sup-sub         Enable or disable MultiMarkdown-style superscript (^text^) and subscript (~text~) syntax\n");
    fprintf(stderr, "  --title TITLE          Document title (requires --standalone, default: \"Document\")\n");
    fprintf(stderr, "  --[no-]transforms      Enable or disable metadata variable transforms [%%key:transform]\n");
    fprintf(stderr, "  --[no-]unsafe          Allow or disallow raw HTML in output\n");
    fprintf(stderr, "  -v, --version          Show version information\n");
    fprintf(stderr, "  --[no-]wikilinks       Enable or disable wiki link syntax [[PageName]]\n");
    fprintf(stderr, "  --wikilink-space MODE  Space replacement for wiki links: dash, none, underscore, space (default: dash)\n");
    fprintf(stderr, "  --wikilink-extension EXT  File extension to append to wiki links (e.g., html, md)\n\n");
    fprintf(stderr, "If no file is specified, reads from stdin.\n");
}

static void print_version(void) {
    printf("Apex %s\n", apex_version_string());
    printf("Copyright (c) 2025 Brett Terpstra\n");
    printf("Licensed under MIT License\n");
}

/* Helper to append a script tag string to a dynamic NULL-terminated array.
 * On success, returns 0 and updates *tags, *count, and *capacity.
 * On failure, prints an error and returns non-zero.
 */
static int add_script_tag(char ***tags, size_t *count, size_t *capacity, const char *tag_str) {
    if (!tag_str || !*tag_str) return 0;

    if (!*tags) {
        *tags = malloc((*capacity) * sizeof(char *));
        if (!*tags) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return 1;
        }
    } else if (*count >= *capacity) {
        *capacity *= 2;
        char **new_tags = realloc(*tags, (*capacity) * sizeof(char *));
        if (!new_tags) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return 1;
        }
        *tags = new_tags;
    }

    (*tags)[*count] = strdup(tag_str);
    if (!(*tags)[*count]) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return 1;
    }
    (*count)++;
    return 0;
}

/**
 * Normalize a plugin identifier to a Git repository URL.
 * Returns a newly allocated string that must be freed by caller, or NULL on error.
 *
 * Handles:
 * - Full Git URLs (https://github.com/user/repo.git, git@github.com:user/repo.git, etc.)
 * - GitHub shorthand (user/repo or ttscoff/apex-plugin-kbd)
 * - Returns NULL if it doesn't look like a URL (treat as directory ID)
 */
static char *normalize_plugin_repo_url(const char *arg) {
    if (!arg || !*arg) return NULL;

    /* Check if it's already a full URL */
    if (strstr(arg, "://") != NULL || strstr(arg, "@") != NULL) {
        /* Looks like a URL - return as-is (but ensure .git suffix for GitHub URLs) */
        if (strncmp(arg, "https://github.com/", 19) == 0 ||
            strncmp(arg, "http://github.com/", 18) == 0 ||
            strncmp(arg, "git@github.com:", 15) == 0) {
            /* GitHub URL - ensure it ends with .git */
            size_t len = strlen(arg);
            if (len < 4 || strcmp(arg + len - 4, ".git") != 0) {
                char *url = malloc(len + 5);
                if (!url) return NULL;
                snprintf(url, len + 5, "%s.git", arg);
                return url;
            }
        }
        return strdup(arg);
    }

    /* Check if it's GitHub shorthand (user/repo format) */
    const char *slash = strchr(arg, '/');
    if (slash && slash != arg && slash[1] != '\0') {
        /* Looks like user/repo - convert to https://github.com/user/repo.git */
        size_t len = strlen(arg);
        char *url = malloc(19 + len + 5); /* "https://github.com/" + arg + ".git" */
        if (!url) return NULL;
        snprintf(url, 19 + len + 5, "https://github.com/%s.git", arg);
        return url;
    }

    /* Doesn't look like a URL - return NULL to indicate it should be treated as an ID */
    return NULL;
}

/**
 * Extract plugin ID from a cloned repository.
 * Reads plugin.yml or plugin.yaml and extracts the 'id' field.
 * Falls back to the directory name if no manifest is found.
 * Returns a newly allocated string that must be freed by caller.
 */
static char *extract_plugin_id_from_repo(const char *repo_path) {
    char manifest[1300];
    snprintf(manifest, sizeof(manifest), "%s/plugin.yml", repo_path);
    FILE *mt = fopen(manifest, "r");
    if (!mt) {
        snprintf(manifest, sizeof(manifest), "%s/plugin.yaml", repo_path);
        mt = fopen(manifest, "r");
    }

    if (mt) {
        fclose(mt);
        apex_metadata_item *meta = apex_load_metadata_from_file(manifest);
        if (meta) {
            const char *id = NULL;
            for (apex_metadata_item *m = meta; m; m = m->next) {
                if (strcmp(m->key, "id") == 0) {
                    id = m->value;
                    break;
                }
            }
            if (id && *id) {
                char *result = strdup(id);
                apex_free_metadata(meta);
                return result;
            }
            apex_free_metadata(meta);
        }
    }

    /* Fallback: extract from repo path (last component) */
    const char *last_slash = strrchr(repo_path, '/');
    if (last_slash && last_slash[1] != '\0') {
        const char *name = last_slash + 1;
        /* Remove .git suffix if present */
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".git") == 0) {
            len -= 4;
        }
        char *result = malloc(len + 1);
        if (result) {
            memcpy(result, name, len);
            result[len] = '\0';
            return result;
        }
    }

    return NULL;
}

static char *read_file(const char *filename, size_t *len) {
    PROFILE_START(file_read);
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        fprintf(stderr, "Error: Cannot determine file size\n");
        return NULL;
    }

    /* Allocate buffer */
    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    /* Read file */
    size_t bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    PROFILE_END(file_read);

    if (len) *len = bytes_read;
    return buffer;
}

static char *read_stdin(size_t *len) {
    size_t capacity = BUFFER_SIZE;
    size_t size = 0;
    char *buffer = malloc(capacity);

    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    /* Use read() system call directly for better control with pipes */
    int fd = fileno(stdin);
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer + size, BUFFER_SIZE)) > 0) {
        size += bytes_read;
        if (size + BUFFER_SIZE > capacity) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                fprintf(stderr, "Error: Memory allocation failed\n");
                return NULL;
            }
            buffer = new_buffer;
        }
    }

    /* Check if we encountered an error (not EOF) */
    if (bytes_read < 0) {
        free(buffer);
        fprintf(stderr, "Error: Error reading from stdin\n");
        return NULL;
    }

    buffer[size] = '\0';
    if (len) *len = size;
    return buffer;
}

/**
 * Helper: get directory component of a path (malloc'd).
 */
static char *apex_cli_get_directory(const char *filepath) {
    if (!filepath || !*filepath) {
        char *dot = malloc(2);
        if (dot) {
            dot[0] = '.';
            dot[1] = '\0';
        }
        return dot;
    }

    char *copy = strdup(filepath);
    if (!copy) {
        return NULL;
    }
    char *dir = dirname(copy);
    char *result = dir ? strdup(dir) : NULL;
    free(copy);
    if (!result) {
        char *dot = malloc(2);
        if (dot) {
            dot[0] = '.';
            dot[1] = '\0';
        }
        return dot;
    }
    return result;
}

/**
 * Shift Markdown header levels in content by a given indent.
 *
 * For each indent level, this performs the equivalent of the Perl:
 *   $file =~ s/^#/##/gm;
 *
 * i.e., for each line that begins with '#', another '#' is inserted.
 * Lines that do not begin with '#' are left unchanged.
 */
static char *apex_cli_shift_headers(const char *content, int indent) {
    if (!content || indent <= 0) {
        return content ? strdup(content) : NULL;
    }

    size_t len = strlen(content);
    /* Worst case, every character is a header marker; be generous. */
    size_t capacity = len * (size_t)(indent + 1) + 1;
    char *buffer = malloc(capacity);
    if (!buffer) return NULL;

    char *out = buffer;
    const char *in = content;

    for (int level = 0; level < indent; level++) {
        out = buffer;
        in = (level == 0) ? content : buffer;

        bool at_line_start = true;
        while (*in) {
            char c = *in;
            if (at_line_start && c == '#') {
                /* Duplicate initial '#' */
                *out++ = '#';
                *out++ = '#';
                in++;
                at_line_start = false;
                continue;
            }

            *out++ = c;
            if (c == '\n') {
                at_line_start = true;
            } else {
                at_line_start = false;
            }
            in++;
        }
        *out = '\0';
    }

    return buffer;
}

/**
 * Process a MultiMarkdown mmd_merge-style index file:
 * - Each non-empty, non-comment line specifies a file to include
 * - Indentation (tabs or 4-space groups) controls header level shifting
 * - Lines whose first non-whitespace character is '#' are treated as comments
 */
static int apex_cli_mmd_merge_index(const char *index_path, FILE *out) {
    if (!index_path || !out) return 1;

    size_t len = 0;
    char *index_content = read_file(index_path, &len);
    if (!index_content) {
        fprintf(stderr, "Error: Cannot read mmd-merge index '%s'\n", index_path);
        return 1;
    }

    char *base_dir = apex_cli_get_directory(index_path);
    char *cursor = index_content;

    while (*cursor) {
        char *line_start = cursor;
        char *line_end = strchr(cursor, '\n');
        if (!line_end) {
            line_end = cursor + strlen(cursor);
        }

        /* Trim trailing whitespace (including CR) */
        char *trim_end = line_end;
        while (trim_end > line_start && (trim_end[-1] == ' ' || trim_end[-1] == '\t' ||
                                         trim_end[-1] == '\r')) {
            trim_end--;
        }

        size_t line_len = (size_t)(trim_end - line_start);

        if (line_len > 0) {
            /* Make a null-terminated copy for easier processing */
            char *line = malloc(line_len + 1);
            if (!line) {
                free(index_content);
                if (base_dir) free(base_dir);
                return 1;
            }
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';

            /* Skip leading whitespace to check for blank or comment lines */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;

            if (*p != '\0' && *p != '#') {
                /* Count indentation: tabs and groups of 4 spaces at start of line */
                int indent = 0;
                char *q = line;
                while (*q == ' ' || *q == '\t') {
                    if (*q == '\t') {
                        indent++;
                        q++;
                    } else {
                        int spaces = 0;
                        while (*q == ' ' && spaces < 4) {
                            spaces++;
                            q++;
                        }
                        if (spaces == 4) {
                            indent++;
                        } else {
                            /* Fewer than 4 trailing spaces at start are ignored for indent */
                            break;
                        }
                    }
                }

                /* Extract filename from the remainder of the line */
                while (*q == ' ' || *q == '\t') q++;
                char *name_start = q;
                char *name_end = name_start + strlen(name_start);
                while (name_end > name_start &&
                       (name_end[-1] == ' ' || name_end[-1] == '\t')) {
                    name_end--;
                }
                *name_end = '\0';

                if (*name_start) {
                    char full_path[4096];
                    if (name_start[0] == '/') {
                        /* Absolute path */
                        snprintf(full_path, sizeof(full_path), "%s", name_start);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 base_dir ? base_dir : ".",
                                 name_start);
                    }

                    size_t file_len = 0;
                    char *file_content = read_file(full_path, &file_len);
                    if (!file_content) {
                        fprintf(stderr, "Warning: Skipping unreadable file '%s' from mmd-merge index '%s'\n",
                                full_path, index_path);
                    } else {
                        char *shifted = apex_cli_shift_headers(file_content, indent);
                        if (!shifted) {
                            shifted = file_content;
                            file_content = NULL;
                        }

                        fputs(shifted, out);
                        fputc('\n', out);
                        fputc('\n', out);

                        if (shifted != file_content && shifted) {
                            free(shifted);
                        }
                        if (file_content) {
                            free(file_content);
                        }
                    }
                }
            }

            free(line);
        }

        cursor = (*line_end == '\n') ? line_end + 1 : line_end;
    }

    free(index_content);
    if (base_dir) free(base_dir);
    return 0;
}

/**
 * Process a single Markdown file:
 * - Read content
 * - Extract metadata (for transclude base)
 * - Run apex_process_includes
 * Returns newly allocated string or NULL on error.
 */
static char *apex_cli_combine_process_file(const char *filepath) {
    if (!filepath) return NULL;

    size_t len = 0;
    char *markdown = read_file(filepath, &len);
    if (!markdown) {
        return NULL;
    }

    /* Extract metadata in a copy so we don't modify the original text,
     * preserving verbatim Markdown while still honoring transclude base.
     */
    apex_metadata_item *doc_metadata = NULL;
    char *doc_copy = malloc(len + 1);
    if (doc_copy) {
        memcpy(doc_copy, markdown, len);
        doc_copy[len] = '\0';
        char *ptr = doc_copy;
        doc_metadata = apex_extract_metadata(&ptr);
    }

    char *base_dir = apex_cli_get_directory(filepath);
    char *processed = apex_process_includes(markdown, base_dir, doc_metadata, 0);

    if (doc_metadata) {
        apex_free_metadata(doc_metadata);
    }
    if (doc_copy) {
        free(doc_copy);
    }
    if (base_dir) {
        free(base_dir);
    }
    free(markdown);

    return processed ? processed : NULL;
}

/**
 * Append a chunk of Markdown to an output stream, ensuring separation
 * between documents.
 */
static void apex_cli_write_combined_chunk(FILE *out, const char *chunk, bool *needs_separator) {
    if (!out || !chunk) return;

    if (*needs_separator) {
        /* Ensure at least one blank line between documents */
        fputc('\n', out);
        fputc('\n', out);
    }

    fputs(chunk, out);
    *needs_separator = true;
}

/**
 * Parse a GitBook-style SUMMARY.md and write the combined Markdown
 * for all linked files in order.
 *
 * Returns 0 on success, non-zero on error.
 */
static int apex_cli_combine_from_summary(const char *summary_path, FILE *out) {
    size_t len = 0;
    char *summary = read_file(summary_path, &len);
    if (!summary) {
        fprintf(stderr, "Error: Cannot read SUMMARY file '%s'\n", summary_path);
        return 1;
    }

    char *base_dir = apex_cli_get_directory(summary_path);
    bool needs_separator = false;

    char *cursor = summary;
    while (*cursor) {
        char *line_start = cursor;
        char *line_end = strchr(cursor, '\n');
        if (!line_end) {
            line_end = cursor + strlen(cursor);
        }

        /* Look for [Title](path) pattern on this line */
        const char *lb = memchr(line_start, '[', (size_t)(line_end - line_start));
        const char *rb = NULL;
        const char *lp = NULL;
        const char *rp = NULL;

        if (lb) {
            rb = memchr(lb, ']', (size_t)(line_end - lb));
            if (rb && (rb + 1) < line_end && rb[1] == '(') {
                lp = rb + 2;
                rp = memchr(lp, ')', (size_t)(line_end - lp));
            }
        }

        if (lp && rp && rp > lp) {
            size_t path_len = (size_t)(rp - lp);
            char *rel_path = malloc(path_len + 1);
            if (rel_path) {
                memcpy(rel_path, lp, path_len);
                rel_path[path_len] = '\0';

                /* Trim whitespace */
                char *p = rel_path;
                while (*p == ' ' || *p == '\t') p++;
                char *start = p;
                char *end = start + strlen(start);
                while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
                    end--;
                }
                *end = '\0';

                if (*start) {
                    /* Strip anchor (#section) if present */
                    char *hash = strchr(start, '#');
                    if (hash) {
                        *hash = '\0';
                    }

                    /* Skip external links (with scheme) */
                    if (!strstr(start, "://")) {
                        size_t full_len = strlen(base_dir ? base_dir : ".") + strlen(start) + 2;
                        char *full_path = malloc(full_len);
                        if (full_path) {
                            snprintf(full_path, full_len, "%s/%s", base_dir ? base_dir : ".", start);
                            char *processed = apex_cli_combine_process_file(full_path);
                            if (processed) {
                                apex_cli_write_combined_chunk(out, processed, &needs_separator);
                                free(processed);
                            } else {
                                fprintf(stderr, "Warning: Skipping unreadable file '%s' from SUMMARY\n", full_path);
                            }
                            free(full_path);
                        }
                    }
                }

                free(rel_path);
            }
        }

        cursor = (*line_end == '\n') ? line_end + 1 : line_end;
    }

    free(summary);
    if (base_dir) free(base_dir);
    return 0;
}

int main(int argc, char *argv[]) {
    apex_options options = apex_options_default();
    bool plugins_cli_override = false;
    bool plugins_cli_value = false;
    bool list_plugins = false;
    const char *install_plugin_id = NULL;
    const char *uninstall_plugin_id = NULL;
    const char *input_file = NULL;
    const char *output_file = NULL;
    const char *meta_file = NULL;
    apex_metadata_item *cmdline_metadata = NULL;
    char *allocated_base_dir = NULL;      /* Track if we allocated base_directory */
    char *allocated_input_file_path = NULL;  /* Track if we allocate input_file_path */

    /* Combine mode: concatenate Markdown files (with includes expanded) */
    bool combine_mode = false;
    char **combine_files = NULL;
    size_t combine_file_count = 0;
    size_t combine_file_capacity = 0;

    /* mmd-merge mode: emulate MultiMarkdown mmd_merge.pl behavior */
    bool mmd_merge_mode = false;
    char **mmd_merge_files = NULL;
    size_t mmd_merge_file_count = 0;
    size_t mmd_merge_file_capacity = 0;

    /* Bibliography files (NULL-terminated array) */
    char **bibliography_files = NULL;
    size_t bibliography_count = 0;
    size_t bibliography_capacity = 4;

    /* Script tags (NULL-terminated array of raw <script> HTML snippets) */
    char **script_tags = NULL;
    size_t script_tag_count = 0;
    size_t script_tag_capacity = 4;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --mode requires an argument\n");
                return 1;
            }
            if (strcmp(argv[i], "commonmark") == 0) {
                options = apex_options_for_mode(APEX_MODE_COMMONMARK);
            } else if (strcmp(argv[i], "gfm") == 0) {
                options = apex_options_for_mode(APEX_MODE_GFM);
            } else if (strcmp(argv[i], "mmd") == 0 || strcmp(argv[i], "multimarkdown") == 0) {
                options = apex_options_for_mode(APEX_MODE_MULTIMARKDOWN);
            } else if (strcmp(argv[i], "kramdown") == 0) {
                options = apex_options_for_mode(APEX_MODE_KRAMDOWN);
            } else if (strcmp(argv[i], "unified") == 0) {
                options = apex_options_for_mode(APEX_MODE_UNIFIED);
            } else {
                fprintf(stderr, "Error: Unknown mode '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --output requires an argument\n");
                return 1;
            }
            output_file = argv[i];
        } else if (strcmp(argv[i], "--plugins") == 0) {
            options.enable_plugins = true;
            plugins_cli_override = true;
            plugins_cli_value = true;
        } else if (strcmp(argv[i], "--no-plugins") == 0) {
            options.enable_plugins = false;
            plugins_cli_override = true;
            plugins_cli_value = false;
        } else if (strcmp(argv[i], "--list-plugins") == 0) {
            list_plugins = true;
        } else if (strcmp(argv[i], "--install-plugin") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --install-plugin requires an id argument\n");
                return 1;
            }
            install_plugin_id = argv[i];
        } else if (strcmp(argv[i], "--uninstall-plugin") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --uninstall-plugin requires an id argument\n");
                return 1;
            }
            uninstall_plugin_id = argv[i];
        } else if (strcmp(argv[i], "--no-tables") == 0) {
            options.enable_tables = false;
        } else if (strcmp(argv[i], "--no-footnotes") == 0) {
            options.enable_footnotes = false;
        } else if (strcmp(argv[i], "--no-smart") == 0) {
            options.enable_smart_typography = false;
        } else if (strcmp(argv[i], "--no-math") == 0) {
            options.enable_math = false;
        } else if (strcmp(argv[i], "--includes") == 0) {
            options.enable_file_includes = true;
        } else if (strcmp(argv[i], "--no-includes") == 0) {
            options.enable_file_includes = false;
        } else if (strcmp(argv[i], "--hardbreaks") == 0) {
            options.hardbreaks = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--standalone") == 0) {
            options.standalone = true;
        } else if (strcmp(argv[i], "--css") == 0 || strcmp(argv[i], "--style") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: %s requires an argument\n", argv[i-1]);
                return 1;
            }
            options.stylesheet_path = argv[i];
            options.standalone = true;  /* Imply standalone if CSS is specified */
        } else if (strcmp(argv[i], "--embed-css") == 0) {
            options.embed_stylesheet = true;
        } else if (strcmp(argv[i], "--script") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --script requires an argument\n");
                return 1;
            }
            /* Argument may be a single value or comma-separated list */
            const char *arg = argv[i];
            char *arg_copy = strdup(arg);
            if (!arg_copy) {
                fprintf(stderr, "Error: Memory allocation failed\n");
                return 1;
            }

            char *token = arg_copy;
            while (token && *token) {
                /* Find next comma and split */
                char *comma = strchr(token, ',');
                if (comma) {
                    *comma = '\0';
                }

                /* Trim leading/trailing whitespace */
                char *start = token;
                while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
                    start++;
                }
                char *end = start + strlen(start);
                while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
                    end--;
                }
                *end = '\0';

                if (*start) {
                    /* Map common shorthands to CDN script tags, otherwise treat as src */
                    const char *lower = start;
                    /* Simple case-insensitive checks for known shorthands */
                    if (strcasecmp(lower, "mermaid") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script src=\"https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "mathjax") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script src=\"https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "katex") == 0) {
                        /* KaTeX typically needs both the core script and auto-render helper */
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js\" onload=\"renderMathInElement(document.body, {delimiters: [{left: '\\\\[', right: '\\\\]', display: true}, {left: '\\\\\\(', right: '\\\\\\)', display: false}], ignoredClasses: ['math']}); document.querySelectorAll('span.math').forEach(function(el){var text=el.textContent.trim();if(text.indexOf('\\\\(')==0)text=text.slice(2,-2);else if(text.indexOf('\\\\\\[')==0)text=text.slice(2,-2);var isDisplay=el.classList.contains('display');try{katex.render(text,el,{displayMode:isDisplay,throwOnError:false});}catch(e){}});\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "highlightjs") == 0 || strcasecmp(lower, "highlight.js") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script src=\"https://cdn.jsdelivr.net/npm/highlight.js@11/lib/highlight.min.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "prism") == 0 || strcasecmp(lower, "prismjs") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script src=\"https://cdn.jsdelivr.net/npm/prismjs@1/components/prism-core.min.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "htmx") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script src=\"https://unpkg.com/htmx.org@1.9.10\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else if (strcasecmp(lower, "alpine") == 0 || strcasecmp(lower, "alpinejs") == 0) {
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity,
                                           "<script defer src=\"https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js\"></script>") != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    } else {
                        /* Treat as a path or URL and create a simple script tag */
                        char buf[2048];
                        int n = snprintf(buf, sizeof(buf), "<script src=\"%s\"></script>", start);
                        if (n < 0 || (size_t)n >= sizeof(buf)) {
                            fprintf(stderr, "Error: --script value too long\n");
                            free(arg_copy);
                            return 1;
                        }
                        if (add_script_tag(&script_tags, &script_tag_count, &script_tag_capacity, buf) != 0) {
                            free(arg_copy);
                            return 1;
                        }
                    }
                }

                if (!comma) break;
                token = comma + 1;
            }

            free(arg_copy);
        } else if (strcmp(argv[i], "--title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --title requires an argument\n");
                return 1;
            }
            options.document_title = argv[i];
        } else if (strcmp(argv[i], "--pretty") == 0) {
            options.pretty = true;
        } else if (strcmp(argv[i], "--accept") == 0) {
            options.enable_critic_markup = true;
            options.critic_mode = 0;  /* CRITIC_ACCEPT */
        } else if (strcmp(argv[i], "--reject") == 0) {
            options.enable_critic_markup = true;
            options.critic_mode = 1;  /* CRITIC_REJECT */
        } else if (strcmp(argv[i], "--id-format") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --id-format requires an argument (gfm, mmd, or kramdown)\n");
                return 1;
            }
            if (strcmp(argv[i], "gfm") == 0) {
                options.id_format = 0;  /* GFM format */
            } else if (strcmp(argv[i], "mmd") == 0) {
                options.id_format = 1;  /* MMD format */
            } else if (strcmp(argv[i], "kramdown") == 0) {
                options.id_format = 2;  /* Kramdown format */
            } else {
                fprintf(stderr, "Error: --id-format must be 'gfm', 'mmd', or 'kramdown'\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--no-ids") == 0) {
            options.generate_header_ids = false;
        } else if (strcmp(argv[i], "--header-anchors") == 0) {
            options.header_anchors = true;
        } else if (strcmp(argv[i], "--relaxed-tables") == 0) {
            options.relaxed_tables = true;
        } else if (strcmp(argv[i], "--no-relaxed-tables") == 0) {
            options.relaxed_tables = false;
        } else if (strcmp(argv[i], "--captions") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --captions requires an argument (above or below)\n");
                return 1;
            }
            if (strcmp(argv[i], "above") == 0) {
                options.caption_position = 0;
            } else if (strcmp(argv[i], "below") == 0) {
                options.caption_position = 1;
            } else {
                fprintf(stderr, "Error: --captions must be 'above' or 'below'\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--alpha-lists") == 0) {
            options.allow_alpha_lists = true;
        } else if (strcmp(argv[i], "--no-alpha-lists") == 0) {
            options.allow_alpha_lists = false;
        } else if (strcmp(argv[i], "--mixed-lists") == 0) {
            options.allow_mixed_list_markers = true;
        } else if (strcmp(argv[i], "--no-mixed-lists") == 0) {
            options.allow_mixed_list_markers = false;
        } else if (strcmp(argv[i], "--unsafe") == 0) {
            options.unsafe = true;
        } else if (strcmp(argv[i], "--no-unsafe") == 0) {
            options.unsafe = false;
        } else if (strcmp(argv[i], "--sup-sub") == 0) {
            options.enable_sup_sub = true;
        } else if (strcmp(argv[i], "--no-sup-sub") == 0) {
            options.enable_sup_sub = false;
        } else if (strcmp(argv[i], "--divs") == 0) {
            options.enable_divs = true;
        } else if (strcmp(argv[i], "--no-divs") == 0) {
            options.enable_divs = false;
        } else if (strcmp(argv[i], "--spans") == 0) {
            options.enable_spans = true;
        } else if (strcmp(argv[i], "--no-spans") == 0) {
            options.enable_spans = false;
        } else if (strcmp(argv[i], "--autolink") == 0) {
            options.enable_autolink = true;
        } else if (strcmp(argv[i], "--no-autolink") == 0) {
            options.enable_autolink = false;
        } else if (strcmp(argv[i], "--obfuscate-emails") == 0) {
            options.obfuscate_emails = true;
        } else if (strcmp(argv[i], "--aria") == 0) {
            options.enable_aria = true;
        } else if (strcmp(argv[i], "--no-plugins") == 0) {
            options.enable_plugins = false;
        } else if (strcmp(argv[i], "--wikilinks") == 0) {
            options.enable_wiki_links = true;
        } else if (strcmp(argv[i], "--no-wikilinks") == 0) {
            options.enable_wiki_links = false;
        } else if (strcmp(argv[i], "--wikilink-space") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --wikilink-space requires an argument (dash, none, underscore, or space)\n");
                return 1;
            }
            if (strcmp(argv[i], "dash") == 0) {
                options.wikilink_space = 0;
            } else if (strcmp(argv[i], "none") == 0) {
                options.wikilink_space = 1;
            } else if (strcmp(argv[i], "underscore") == 0) {
                options.wikilink_space = 2;
            } else if (strcmp(argv[i], "space") == 0) {
                options.wikilink_space = 3;
            } else {
                fprintf(stderr, "Error: --wikilink-space must be one of: dash, none, underscore, space\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--wikilink-extension") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --wikilink-extension requires an argument\n");
                return 1;
            }
            options.wikilink_extension = argv[i];
        } else if (strcmp(argv[i], "--transforms") == 0) {
            options.enable_metadata_transforms = true;
        } else if (strcmp(argv[i], "--no-transforms") == 0) {
            options.enable_metadata_transforms = false;
        } else if (strcmp(argv[i], "--embed-images") == 0) {
            options.embed_images = true;
        } else if (strcmp(argv[i], "--base-dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --base-dir requires an argument\n");
                return 1;
            }
            options.base_directory = argv[i];
        } else if (strcmp(argv[i], "--bibliography") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --bibliography requires an argument\n");
                return 1;
            }
            /* Allocate or reallocate bibliography files array */
            if (!bibliography_files) {
                bibliography_files = malloc(bibliography_capacity * sizeof(char*));
                if (!bibliography_files) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    return 1;
                }
            } else if (bibliography_count >= bibliography_capacity) {
                bibliography_capacity *= 2;
                char **new_files = realloc(bibliography_files, bibliography_capacity * sizeof(char*));
                if (!new_files) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    return 1;
                }
                bibliography_files = new_files;
            }
            bibliography_files[bibliography_count++] = argv[i];
            options.enable_citations = true;  /* Enable citations when bibliography is provided */
        } else if (strcmp(argv[i], "--csl") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --csl requires an argument\n");
                return 1;
            }
            options.csl_file = argv[i];
            options.enable_citations = true;  /* Enable citations when CSL is provided */
        } else if (strcmp(argv[i], "--no-bibliography") == 0) {
            options.suppress_bibliography = true;
        } else if (strcmp(argv[i], "--link-citations") == 0) {
            options.link_citations = true;
        } else if (strcmp(argv[i], "--show-tooltips") == 0) {
            options.show_tooltips = true;
        } else if (strcmp(argv[i], "--indices") == 0) {
            options.enable_indices = true;
            options.enable_mmark_index_syntax = true;
            options.enable_textindex_syntax = true;
        } else if (strcmp(argv[i], "--no-indices") == 0) {
            options.enable_indices = false;
        } else if (strcmp(argv[i], "--no-index") == 0) {
            options.suppress_index = true;
        } else if (strcmp(argv[i], "--meta-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --meta-file requires an argument\n");
                return 1;
            }
            meta_file = argv[i];
        } else if (strncmp(argv[i], "--meta", 6) == 0) {
            const char *arg_value;
            if (strlen(argv[i]) > 6 && argv[i][6] == '=') {
                /* --meta=KEY=VALUE format */
                arg_value = argv[i] + 7;
            } else {
                /* --meta KEY=VALUE format */
                if (++i >= argc) {
                    fprintf(stderr, "Error: --meta requires an argument\n");
                    return 1;
                }
                arg_value = argv[i];
            }
            apex_metadata_item *new_meta = apex_parse_command_metadata(arg_value);
            if (new_meta) {
                /* Merge with existing command-line metadata */
                if (cmdline_metadata) {
                    apex_metadata_item *merged = apex_merge_metadata(cmdline_metadata, new_meta, NULL);
                    apex_free_metadata(cmdline_metadata);
                    apex_free_metadata(new_meta);
                    cmdline_metadata = merged;
                } else {
                    cmdline_metadata = new_meta;
                }
            }
        } else if (strcmp(argv[i], "--combine") == 0) {
            combine_mode = true;
        } else if (strcmp(argv[i], "--mmd-merge") == 0) {
            mmd_merge_mode = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            /* Positional argument: input file(s) */
            if (combine_mode) {
                if (combine_file_count >= combine_file_capacity) {
                    size_t new_cap = combine_file_capacity ? combine_file_capacity * 2 : 8;
                    char **tmp = realloc(combine_files, new_cap * sizeof(char *));
                    if (!tmp) {
                        fprintf(stderr, "Error: Memory allocation failed\n");
                        return 1;
                    }
                    combine_files = tmp;
                    combine_file_capacity = new_cap;
                }
                combine_files[combine_file_count++] = argv[i];
            } else if (mmd_merge_mode) {
                if (mmd_merge_file_count >= mmd_merge_file_capacity) {
                    size_t new_cap = mmd_merge_file_capacity ? mmd_merge_file_capacity * 2 : 8;
                    char **tmp = realloc(mmd_merge_files, new_cap * sizeof(char *));
                    if (!tmp) {
                        fprintf(stderr, "Error: Memory allocation failed\n");
                        return 1;
                    }
                    mmd_merge_files = tmp;
                    mmd_merge_file_capacity = new_cap;
                }
                mmd_merge_files[mmd_merge_file_count++] = argv[i];
            } else {
                /* Single-file mode: last positional wins (for compatibility) */
                input_file = argv[i];
            }
        }
    }

    /* If --combine was provided but no files, error out early */
    if (combine_mode && combine_file_count == 0) {
        fprintf(stderr, "Error: --combine requires at least one input file\n");
        return 1;
    }

    /* --combine and --mmd-merge are mutually exclusive */
    if (combine_mode && mmd_merge_mode) {
        fprintf(stderr, "Error: --combine and --mmd-merge cannot be used together\n");
        return 1;
    }

    /* If no explicit --meta-file was provided, look for a default config:
     *   1. $XDG_CONFIG_HOME/apex/config.yml
     *   2. ~/.config/apex/config.yml
     *
     * If found, treat it as if the user had passed:
     *   --meta-file ~/.config/apex/config.yml
     * (or the XDG path), i.e. normal external metadata with the usual
     * precedence rules (file < document < command-line).
     */
    if (!meta_file) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        char path[1024];
        const char *candidate = NULL;

        if (xdg && *xdg) {
            snprintf(path, sizeof(path), "%s/apex/config.yml", xdg);
            candidate = path;
        } else {
            const char *home = getenv("HOME");
            if (home && *home) {
                snprintf(path, sizeof(path), "%s/.config/apex/config.yml", home);
                candidate = path;
            }
        }

        if (candidate) {
            FILE *fp = fopen(candidate, "r");
            if (fp) {
                fclose(fp);
                meta_file = candidate;
            }
        }
    }

    /* Handle plugin listing/installation/uninstallation commands before normal conversion */
    if (list_plugins || install_plugin_id || uninstall_plugin_id) {
        if ((install_plugin_id && uninstall_plugin_id) || (install_plugin_id && list_plugins && uninstall_plugin_id)) {
            fprintf(stderr, "Error: --install-plugin and --uninstall-plugin cannot be combined.\n");
            return 1;
        }

        /* Determine plugins root: $XDG_CONFIG_HOME/apex/plugins or ~/.config/apex/plugins */
        const char *xdg = getenv("XDG_CONFIG_HOME");
        char root[1024];
        if (xdg && *xdg) {
            snprintf(root, sizeof(root), "%s/apex/plugins", xdg);
        } else {
            const char *home = getenv("HOME");
            if (!home || !*home) {
                fprintf(stderr, "Error: HOME not set; cannot determine plugin directory.\n");
                return 1;
            }
            snprintf(root, sizeof(root), "%s/.config/apex/plugins", home);
        }

        /* Uninstall plugin: local only, no remote directory needed */
        if (uninstall_plugin_id) {
            char target[1200];
            snprintf(target, sizeof(target), "%s/%s", root, uninstall_plugin_id);

            struct stat st;
            if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
                fprintf(stderr, "Error: plugin '%s' is not installed at %s\n", uninstall_plugin_id, target);
                return 1;
            }

            fprintf(stderr, "About to remove plugin directory:\n  %s\n", target);
            fprintf(stderr, "This will delete all files in that directory (but not any support data).\n");
            fprintf(stderr, "Proceed? [y/N]: ");
            fflush(stderr);

            char answer[16];
            if (!fgets(answer, sizeof(answer), stdin)) {
                fprintf(stderr, "Aborted.\n");
                return 1;
            }
            if (answer[0] != 'y' && answer[0] != 'Y') {
                fprintf(stderr, "Aborted.\n");
                return 1;
            }

            char rm_cmd[1400];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", target);
            int rm_rc = system(rm_cmd);
            if (rm_rc != 0) {
                fprintf(stderr, "Error: failed to remove plugin directory '%s'.\n", target);
                return 1;
            }

            fprintf(stderr, "Uninstalled plugin '%s' from %s\n", uninstall_plugin_id, target);
            return 0;
        }

        /* List and install rely on the remote directory as well as local plugins */

        /* Collect installed plugin ids from the global plugins directory (if it exists) */
        char **installed_ids = NULL;
        size_t installed_count = 0;
        size_t installed_cap = 0;

        DIR *d = opendir(root);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.') continue;

                char plugin_dir[1200];
                snprintf(plugin_dir, sizeof(plugin_dir), "%s/%s", root, ent->d_name);
                struct stat st2;
                if (stat(plugin_dir, &st2) != 0 || !S_ISDIR(st2.st_mode)) {
                    continue;
                }

                /* Look for plugin.yml or plugin.yaml */
                char manifest[1300];
                snprintf(manifest, sizeof(manifest), "%s/plugin.yml", plugin_dir);
                FILE *test = fopen(manifest, "r");
                if (!test) {
                    snprintf(manifest, sizeof(manifest), "%s/plugin.yaml", plugin_dir);
                    test = fopen(manifest, "r");
                }
                if (!test) {
                    continue;
                }
                fclose(test);

                apex_metadata_item *meta = apex_load_metadata_from_file(manifest);
                if (!meta) continue;

                const char *id = NULL;
                const char *title = NULL;
                const char *author = NULL;
                const char *description = NULL;
                const char *homepage = NULL;

                for (apex_metadata_item *m = meta; m; m = m->next) {
                    if (strcmp(m->key, "id") == 0) id = m->value;
                    else if (strcmp(m->key, "title") == 0) title = m->value;
                    else if (strcmp(m->key, "author") == 0) author = m->value;
                    else if (strcmp(m->key, "description") == 0) description = m->value;
                    else if (strcmp(m->key, "homepage") == 0) homepage = m->value;
                }

                const char *final_id = id ? id : ent->d_name;
                if (final_id) {
                    if (installed_count == installed_cap) {
                        size_t new_cap = installed_cap ? installed_cap * 2 : 8;
                        char **tmp = realloc(installed_ids, new_cap * sizeof(char *));
                        if (!tmp) {
                            apex_free_metadata(meta);
                            break;
                        }
                        installed_ids = tmp;
                        installed_cap = new_cap;
                    }
                    installed_ids[installed_count++] = strdup(final_id);
                }

                if (list_plugins) {
                    if (installed_count == 1) {
                        /* First installed plugin we print: emit header */
                        printf("## Installed Plugins\n\n");
                    }
                    const char *print_title = title ? title : final_id;
                    const char *print_author = author ? author : "";
                    printf("%-20s - %s", final_id, print_title);
                    if (print_author && *print_author) {
                        printf("  (author: %s)", print_author);
                    }
                    printf("\n");
                    if (description && *description) {
                        printf("    %s\n", description);
                    }
                    if (homepage && *homepage) {
                        printf("    homepage: %s\n", homepage);
                    }
                }

                apex_free_metadata(meta);
            }
            closedir(d);
        }

        if (list_plugins) {
            printf("\n---\n\n");
            printf("## Available Plugins\n\n");
        }

        /* Check if install_plugin_id is a direct URL/shorthand - if so, skip directory fetch */
        char *normalized_repo_check = NULL;
        bool is_direct_url = false;
        if (install_plugin_id) {
            normalized_repo_check = normalize_plugin_repo_url(install_plugin_id);
            is_direct_url = (normalized_repo_check != NULL);
            if (normalized_repo_check) {
                free(normalized_repo_check);
                normalized_repo_check = NULL;
            }
        }

        apex_remote_plugin_list *plist = NULL;
        if (!is_direct_url) {
            /* Only fetch directory if we need it (list_plugins or install by ID) */
            const char *dir_url = "https://raw.githubusercontent.com/ApexMarkdown/apex-plugins/refs/heads/main/apex-plugins.json";
            plist = apex_remote_fetch_directory(dir_url);
            if (!plist && (list_plugins || install_plugin_id)) {
                fprintf(stderr, "Error: failed to fetch plugin directory from %s\n", dir_url);
                if (installed_ids) {
                    for (size_t i = 0; i < installed_count; i++) free(installed_ids[i]);
                    free(installed_ids);
                }
                return 1;
            }
        }

        if (list_plugins) {
            if (!plist) {
                fprintf(stderr, "Error: cannot list plugins without directory access.\n");
                if (installed_ids) {
                    for (size_t i = 0; i < installed_count; i++) free(installed_ids[i]);
                    free(installed_ids);
                }
                return 1;
            }
            apex_remote_print_plugins_filtered(plist, (const char **)installed_ids, installed_count);
            apex_remote_free_plugins(plist);
            if (installed_ids) {
                for (size_t i = 0; i < installed_count; i++) free(installed_ids[i]);
                free(installed_ids);
            }
            return 0;
        }
        if (install_plugin_id) {
            const char *repo = NULL;
            char *normalized_repo = NULL;
            char *final_plugin_id = NULL;

            /* Check if install_plugin_id is a URL or GitHub shorthand */
            normalized_repo = normalize_plugin_repo_url(install_plugin_id);

            if (normalized_repo) {
                /* Direct URL/shorthand - use it as the repo URL */
                repo = normalized_repo;

                /* Security confirmation for out-of-directory installs */
                fprintf(stderr,
                        "Apex plugins execute unverified code. Only install plugins from trusted sources.\n"
                        "Continue? (y/n) ");
                fflush(stderr);
                char answer[8] = {0};
                if (!fgets(answer, sizeof(answer), stdin) ||
                    (answer[0] != 'y' && answer[0] != 'Y')) {
                    fprintf(stderr, "Aborted plugin install.\n");
                    free(normalized_repo);
                    if (plist) {
                        apex_remote_free_plugins(plist);
                    }
                    return 1;
                }
                /* We'll extract the plugin ID after cloning */
            } else {
                /* Not a URL - treat as directory ID and look it up */
                apex_remote_plugin *rp = apex_remote_find_plugin(plist, install_plugin_id);
                repo = apex_remote_plugin_repo(rp);
                if (!rp || !repo) {
                    fprintf(stderr, "Error: plugin '%s' not found in directory.\n", install_plugin_id);
                    if (plist) {
                        apex_remote_free_plugins(plist);
                    }
                    return 1;
                }
                final_plugin_id = strdup(install_plugin_id);
            }

            /* Determine plugins root: $XDG_CONFIG_HOME/apex/plugins or ~/.config/apex/plugins */
            const char *xdg = getenv("XDG_CONFIG_HOME");
            char root[1024];
            if (xdg && *xdg) {
                snprintf(root, sizeof(root), "%s/apex/plugins", xdg);
            } else {
                const char *home = getenv("HOME");
                if (!home || !*home) {
                    fprintf(stderr, "Error: HOME not set; cannot determine plugin install directory.\n");
                    if (normalized_repo) free(normalized_repo);
                    if (final_plugin_id) free(final_plugin_id);
                    apex_remote_free_plugins(plist);
                    return 1;
                }
                snprintf(root, sizeof(root), "%s/.config/apex/plugins", home);
            }

            /* Ensure root directory exists */
            char mkdir_cmd[1200];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", root);
            int mkrc = system(mkdir_cmd);
            if (mkrc != 0) {
                fprintf(stderr, "Error: failed to create plugin directory '%s'.\n", root);
                if (normalized_repo) free(normalized_repo);
                if (final_plugin_id) free(final_plugin_id);
                apex_remote_free_plugins(plist);
                return 1;
            }

            /* For direct URLs, we need a temporary directory name for cloning */
            /* We'll rename it after extracting the plugin ID */
            char temp_target[1200];
            if (!final_plugin_id) {
                /* Extract a temporary name from the URL for cloning */
                const char *last_slash = strrchr(repo, '/');
                const char *name_start = last_slash ? (last_slash + 1) : repo;
                const char *name_end = strstr(name_start, ".git");
                if (!name_end) name_end = name_start + strlen(name_start);
                size_t name_len = name_end - name_start;
                if (name_len > 0 && name_len < 200) {
                    char temp_name[256];
                    memcpy(temp_name, name_start, name_len);
                    temp_name[name_len] = '\0';
                    snprintf(temp_target, sizeof(temp_target), "%s/.apex_install_%s", root, temp_name);
                } else {
                    snprintf(temp_target, sizeof(temp_target), "%s/.apex_install_temp", root);
                }
            } else {
                snprintf(temp_target, sizeof(temp_target), "%s/%s", root, final_plugin_id);
            }

            /* Refuse to overwrite existing directory */
            char test_cmd[1300];
            if (final_plugin_id) {
                snprintf(test_cmd, sizeof(test_cmd), "[ -d \"%s\" ]", temp_target);
            } else {
                /* For temp dir, check if it exists and clean it up */
                snprintf(test_cmd, sizeof(test_cmd), "[ -d \"%s\" ]", temp_target);
            }
            int exists_rc = system(test_cmd);
            if (exists_rc == 0 && final_plugin_id) {
                fprintf(stderr, "Error: plugin directory '%s' already exists. Remove it first to reinstall.\n", temp_target);
                if (normalized_repo) free(normalized_repo);
                if (final_plugin_id) free(final_plugin_id);
                apex_remote_free_plugins(plist);
                return 1;
            }

            /* Clone repo using git */
            char clone_cmd[2048];
            snprintf(clone_cmd, sizeof(clone_cmd), "git clone \"%s\" \"%s\"", repo, temp_target);
            int git_rc = system(clone_cmd);
            if (git_rc != 0) {
                fprintf(stderr, "Error: git clone failed for '%s'. Is git installed and the URL correct?\n", repo);
                if (normalized_repo) free(normalized_repo);
                if (final_plugin_id) free(final_plugin_id);
                apex_remote_free_plugins(plist);
                return 1;
            }

            /* Extract plugin ID from cloned repo if we don't have it yet */
            if (!final_plugin_id) {
                final_plugin_id = extract_plugin_id_from_repo(temp_target);
                if (!final_plugin_id) {
                    fprintf(stderr, "Error: could not determine plugin ID from repository. Make sure plugin.yml exists with an 'id' field.\n");
                    /* Clean up temp directory */
                    char rm_cmd[1300];
                    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", temp_target);
                    system(rm_cmd);
                    if (normalized_repo) free(normalized_repo);
                    apex_remote_free_plugins(plist);
                    return 1;
                }

                /* Move temp directory to final location */
                char final_target[1200];
                snprintf(final_target, sizeof(final_target), "%s/%s", root, final_plugin_id);

                /* Check if final location already exists */
                char final_test_cmd[1300];
                snprintf(final_test_cmd, sizeof(final_test_cmd), "[ -d \"%s\" ]", final_target);
                int final_exists_rc = system(final_test_cmd);
                if (final_exists_rc == 0) {
                    fprintf(stderr, "Error: plugin directory '%s' already exists. Remove it first to reinstall.\n", final_target);
                    char rm_cmd[1300];
                    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", temp_target);
                    system(rm_cmd);
                    free(final_plugin_id);
                    if (normalized_repo) free(normalized_repo);
                    apex_remote_free_plugins(plist);
                    return 1;
                }

                /* Move temp to final */
                char mv_cmd[2500];
                snprintf(mv_cmd, sizeof(mv_cmd), "mv \"%s\" \"%s\"", temp_target, final_target);
                int mv_rc = system(mv_cmd);
                if (mv_rc != 0) {
                    fprintf(stderr, "Error: failed to move plugin to final location '%s'.\n", final_target);
                    char rm_cmd[1300];
                    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", temp_target);
                    system(rm_cmd);
                    free(final_plugin_id);
                    if (normalized_repo) free(normalized_repo);
                    apex_remote_free_plugins(plist);
                    return 1;
                }
                strncpy(temp_target, final_target, sizeof(temp_target) - 1);
                temp_target[sizeof(temp_target) - 1] = '\0';
            }

            /* After successful clone, look for a post_install hook in plugin.yml/yaml */
            char manifest[1300];
            snprintf(manifest, sizeof(manifest), "%s/plugin.yml", temp_target);
            FILE *mt = fopen(manifest, "r");
            if (!mt) {
                snprintf(manifest, sizeof(manifest), "%s/plugin.yaml", temp_target);
                mt = fopen(manifest, "r");
            }
            if (mt) {
                fclose(mt);
                apex_metadata_item *meta = apex_load_metadata_from_file(manifest);
                if (meta) {
                    const char *post_install = NULL;
                    for (apex_metadata_item *m = meta; m; m = m->next) {
                        if (strcmp(m->key, "post_install") == 0) {
                            post_install = m->value;
                            break;
                        }
                    }
                    if (post_install && *post_install) {
                        fprintf(stderr, "Running post-install hook for '%s'...\n", final_plugin_id);
                        char hook_cmd[2048];
                        snprintf(hook_cmd, sizeof(hook_cmd), "cd \"%s\" && %s", temp_target, post_install);
                        int hook_rc = system(hook_cmd);
                        if (hook_rc != 0) {
                            fprintf(stderr, "Warning: post-install hook for '%s' exited with status %d\n",
                                    final_plugin_id, hook_rc);
                        }
                    }
                    apex_free_metadata(meta);
                }
            }

            fprintf(stderr, "Installed plugin '%s' into %s\n", final_plugin_id, temp_target);
            if (normalized_repo) free(normalized_repo);
            if (final_plugin_id) free(final_plugin_id);
            apex_remote_free_plugins(plist);
            return 0;
        }
    }

    /* mmd-merge mode: emulate MultiMarkdown mmd_merge.pl and exit */
    if (mmd_merge_mode) {
        FILE *out = stdout;
        if (output_file) {
            out = fopen(output_file, "w");
            if (!out) {
                fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
                return 1;
            }
        }

        if (mmd_merge_file_count == 0) {
            fprintf(stderr, "Error: --mmd-merge requires at least one index file\n");
            if (out != stdout) fclose(out);
            return 1;
        }

        int rc = 0;
        for (size_t i = 0; i < mmd_merge_file_count; i++) {
            const char *path = mmd_merge_files[i];
            if (!path) continue;
            if (apex_cli_mmd_merge_index(path, out) != 0) {
                rc = 1;
                break;
            }
        }

        if (out != stdout) {
            fclose(out);
        }
        return rc;
    }

    /* Combine mode: concatenate Markdown files (with includes expanded) and exit */
    if (combine_mode) {
        FILE *out = stdout;
        if (output_file) {
            out = fopen(output_file, "w");
            if (!out) {
                fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
                return 1;
            }
        }

        int rc = 0;
        bool needs_separator = false;

        for (size_t i = 0; i < combine_file_count; i++) {
            const char *path = combine_files[i];
            if (!path) continue;

            /* Detect GitBook SUMMARY.md by basename */
            char *path_copy = strdup(path);
            if (!path_copy) {
                rc = 1;
                break;
            }
            char *base = basename(path_copy);
            bool is_summary = (base && strcasecmp(base, "SUMMARY.md") == 0);

            if (is_summary) {
                if (apex_cli_combine_from_summary(path, out) != 0) {
                    rc = 1;
                    free(path_copy);
                    break;
                }
                /* SUMMARY already handles its own separation */
                needs_separator = true;
            } else {
                char *processed = apex_cli_combine_process_file(path);
                if (!processed) {
                    fprintf(stderr, "Warning: Skipping unreadable file '%s'\n", path);
                } else {
                    apex_cli_write_combined_chunk(out, processed, &needs_separator);
                    free(processed);
                }
            }

            free(path_copy);
        }

        if (out != stdout) {
            fclose(out);
        }
        return rc;
    }

    /* Set base_directory from input file if not already set */
    if (input_file && !options.base_directory) {
        char *input_path_copy = strdup(input_file);
        if (input_path_copy) {
            char *dir = dirname(input_path_copy);
            if (dir && dir[0] != '\0' && strcmp(dir, ".") != 0) {
                allocated_base_dir = strdup(dir);
                options.base_directory = allocated_base_dir;
            }
            free(input_path_copy);
        }
    }

    /* Set input_file_path for plugins (APEX_FILE_PATH) */
    if (input_file) {
        /* When a file is provided, use the original path (as passed in) */
        options.input_file_path = input_file;
    } else {
        /* When reading from stdin:
         * - Prefer an explicit base_directory, if set.
         * - Otherwise, leave input_file_path empty (plugins see APEX_FILE_PATH="").
         */
        if (options.base_directory && options.base_directory[0] != '\0') {
            options.input_file_path = options.base_directory;
        } else {
            options.input_file_path = NULL;
        }
    }

    /* Read input */
    size_t input_len;
    char *markdown;

    PROFILE_START(cli_total);
    if (input_file) {
        markdown = read_file(input_file, &input_len);
    } else {
        PROFILE_START(stdin_read);
        markdown = read_stdin(&input_len);
        PROFILE_END(stdin_read);
    }

    if (!markdown) {
        if (cmdline_metadata) apex_free_metadata(cmdline_metadata);
        return 1;
    }

    /* Load metadata from file if specified */
    PROFILE_START(metadata_file_load);
    apex_metadata_item *file_metadata = NULL;
    if (meta_file) {
        file_metadata = apex_load_metadata_from_file(meta_file);
        if (!file_metadata) {
            fprintf(stderr, "Warning: Could not load metadata from file '%s'\n", meta_file);
        }
    }
    PROFILE_END(metadata_file_load);

    /* Extract document metadata to merge with external sources
     * We'll extract it here and then inject the merged result */
    PROFILE_START(metadata_extract_cli);
    apex_metadata_item *doc_metadata = NULL;
    size_t doc_metadata_end = 0;

    if (options.mode == APEX_MODE_MULTIMARKDOWN ||
        options.mode == APEX_MODE_KRAMDOWN ||
        options.mode == APEX_MODE_UNIFIED) {
        /* Make a copy to extract metadata without modifying original */
        char *doc_copy = malloc(input_len + 1);
        if (doc_copy) {
            memcpy(doc_copy, markdown, input_len);
            doc_copy[input_len] = '\0';
            char *doc_ptr = doc_copy;
            doc_metadata = apex_extract_metadata(&doc_ptr);
            if (doc_metadata) {
                /* Calculate where metadata ended in original */
                doc_metadata_end = doc_ptr - doc_copy;
            }
            free(doc_copy);
        }
    }
    PROFILE_END(metadata_extract_cli);

    /* Merge metadata in priority order: file -> document -> command-line */
    PROFILE_START(metadata_merge);
    apex_metadata_item *merged_metadata = NULL;
    if (file_metadata || doc_metadata || cmdline_metadata) {
        merged_metadata = apex_merge_metadata(
            file_metadata,
            doc_metadata,
            cmdline_metadata,
            NULL
        );
    }
    PROFILE_END(metadata_merge);

    /* Build enhanced markdown with merged metadata as YAML front matter */
    PROFILE_START(metadata_yaml_build);
    char *enhanced_markdown = NULL;
    size_t enhanced_len = input_len;

    if (merged_metadata) {
        /* Build YAML front matter from merged metadata */
        size_t yaml_size = 512;
        char *yaml_buf = malloc(yaml_size);
        if (yaml_buf) {
            size_t yaml_pos = 0;
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '\n';

            /* Use extracted metadata position if available */
            bool has_existing_metadata = (doc_metadata_end > 0);
            size_t metadata_start_pos = 0;
            size_t metadata_end_pos = doc_metadata_end;

            /* Add all merged metadata */
            apex_metadata_item *item = merged_metadata;
            while (item) {
                /* Escape value if it contains special characters */
                bool needs_quotes = strchr(item->value, ':') || strchr(item->value, '\n') ||
                                    strchr(item->value, '"') || strchr(item->value, '\\');

                size_t needed = strlen(item->key) + strlen(item->value) + (needs_quotes ? 4 : 0) + 10;
                if (yaml_pos + needed >= yaml_size) {
                    yaml_size = (yaml_pos + needed) * 2;
                    char *new_buf = realloc(yaml_buf, yaml_size);
                    if (!new_buf) {
                        free(yaml_buf);
                        yaml_buf = NULL;
                        break;
                    }
                    yaml_buf = new_buf;
                }

                if (needs_quotes) {
                    int written = snprintf(yaml_buf + yaml_pos, yaml_size - yaml_pos, "%s: \"%s\"\n", item->key, item->value);
                    if (written > 0) yaml_pos += written;
                } else {
                    int written = snprintf(yaml_buf + yaml_pos, yaml_size - yaml_pos, "%s: %s\n", item->key, item->value);
                    if (written > 0) yaml_pos += written;
                }
                item = item->next;
            }

            if (yaml_buf) {
                /* Add closing --- */
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '\n';
                yaml_buf[yaml_pos] = '\0';

                if (has_existing_metadata) {
                    /* Replace existing metadata */
                    size_t before_len = metadata_start_pos;
                    size_t after_len = input_len - metadata_end_pos;
                    enhanced_len = before_len + yaml_pos + after_len;
                    enhanced_markdown = malloc(enhanced_len + 1);
                    if (enhanced_markdown) {
                        if (before_len > 0) {
                            memcpy(enhanced_markdown, markdown, before_len);
                        }
                        memcpy(enhanced_markdown + before_len, yaml_buf, yaml_pos);
                        if (after_len > 0) {
                            memcpy(enhanced_markdown + before_len + yaml_pos, markdown + metadata_end_pos, after_len);
                        }
                        enhanced_markdown[enhanced_len] = '\0';
                    }
                } else {
                    /* Prepend metadata */
                    enhanced_len = yaml_pos + input_len;
                    enhanced_markdown = malloc(enhanced_len + 1);
                    if (enhanced_markdown) {
                        memcpy(enhanced_markdown, yaml_buf, yaml_pos);
                        memcpy(enhanced_markdown + yaml_pos, markdown, input_len);
                        enhanced_markdown[enhanced_len] = '\0';
                    }
                }
                free(yaml_buf);
            }
        }
    }
    PROFILE_END(metadata_yaml_build);

    /* Set bibliography files in options (NULL-terminated array) */
    char **saved_bibliography_files = NULL;
    if (bibliography_count > 0) {
        bibliography_files = realloc(bibliography_files, (bibliography_count + 1) * sizeof(char*));
        if (bibliography_files) {
            bibliography_files[bibliography_count] = NULL;  /* NULL terminator */
            options.bibliography_files = bibliography_files;
            /* Save reference in case metadata mode resets options */
            saved_bibliography_files = bibliography_files;
        }
    }

    /* Apply metadata to options - allows per-document control of command-line options */
    /* Note: Bibliography file loading from metadata will be handled in citations extension */
    if (merged_metadata) {
        apex_apply_metadata_to_options(merged_metadata, &options);
        /* Restore bibliography files if they were lost (e.g., if mode was set in metadata) */
        if (saved_bibliography_files && !options.bibliography_files) {
            options.bibliography_files = saved_bibliography_files;
        }
    }

    /* Re-apply explicit CLI override for plugins so it wins over metadata. */
    if (plugins_cli_override) {
        options.enable_plugins = plugins_cli_value;
    }

    /* Attach any collected script tags to options as a NULL-terminated array */
    if (script_tags) {
        /* Ensure NULL terminator */
        script_tags = realloc(script_tags, (script_tag_count + 1) * sizeof(char *));
        if (!script_tags) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return 1;
        }
        script_tags[script_tag_count] = NULL;
        options.script_tags = script_tags;
    }

    /* Use enhanced markdown if we created it, otherwise use original */
    char *final_markdown = enhanced_markdown ? enhanced_markdown : markdown;
    size_t final_len = enhanced_markdown ? enhanced_len : input_len;

    /* Convert to HTML */
    char *html = apex_markdown_to_html(final_markdown, final_len, &options);

    /* Cleanup */
    if (enhanced_markdown) free(enhanced_markdown);
    free(markdown);
    if (allocated_input_file_path) free(allocated_input_file_path);
    if (file_metadata) apex_free_metadata(file_metadata);
    if (doc_metadata) apex_free_metadata(doc_metadata);
    if (cmdline_metadata) apex_free_metadata(cmdline_metadata);
    if (merged_metadata) apex_free_metadata(merged_metadata);

    if (!html) {
        fprintf(stderr, "Error: Conversion failed\n");
        return 1;
    }

    /* Write output */
    PROFILE_START(file_write);
    if (output_file) {
        FILE *fp = fopen(output_file, "w");
        if (!fp) {
            fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
            apex_free_string(html);
            return 1;
        }
        size_t html_len = strlen(html);
        fwrite(html, 1, html_len, fp);
        fclose(fp);
    } else {
        size_t html_len = strlen(html);
        fwrite(html, 1, html_len, stdout);
        /* Don't fflush - let the system buffer for better performance */
    }
    PROFILE_END(file_write);

    PROFILE_END(cli_total);

    apex_free_string(html);

    /* Free bibliography files array */
    if (bibliography_files) {
        free(bibliography_files);
    }

    /* Free script tags array and contents */
    if (script_tags) {
        for (size_t i = 0; i < script_tag_count; i++) {
            free(script_tags[i]);
        }
        free(script_tags);
    }

    /* Free base_directory if we allocated it */
    if (allocated_base_dir) {
        free(allocated_base_dir);
    }

    return 0;
}
