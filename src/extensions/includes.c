/**
 * File Includes Extension for Apex
 * Implementation
 */

#include "includes.h"
#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <strings.h>
#include <regex.h>
#include <glob.h>

/**
 * Read file contents
 */
static char *read_file_contents(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 10 * 1024 * 1024) {  /* Limit to 10MB */
        fclose(fp);
        return NULL;
    }

    /* Read content */
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(content, 1, size, fp);
    content[read] = '\0';
    fclose(fp);

    return content;
}

/**
 * Resolve relative path from base directory
 */
static char *resolve_path(const char *filepath, const char *base_dir) {
    if (!filepath) return NULL;

    /* If absolute path, return as-is */
    if (filepath[0] == '/') {
        return strdup(filepath);
    }

    /* Relative path - combine with base_dir */
    if (!base_dir || !*base_dir) {
        return strdup(filepath);
    }

    size_t len = strlen(base_dir) + strlen(filepath) + 2;
    char *resolved = malloc(len);
    if (!resolved) return NULL;

    snprintf(resolved, len, "%s/%s", base_dir, filepath);
    return resolved;
}

/**
 * Get directory of a file path
 */
static char *get_directory(const char *filepath) {
    if (!filepath) return strdup(".");

    char *path_copy = strdup(filepath);
    if (!path_copy) return strdup(".");

    char *dir = dirname(path_copy);
    char *result = strdup(dir);
    free(path_copy);

    return result ? result : strdup(".");
}

/**
 * Check if a file exists
 */
bool apex_file_exists(const char *filepath) {
    if (!filepath) return false;
    struct stat st;
    return (stat(filepath, &st) == 0);
}

/**
 * File type enum
 */
typedef enum {
    FILE_TYPE_MARKDOWN,
    FILE_TYPE_IMAGE,
    FILE_TYPE_CODE,
    FILE_TYPE_HTML,
    FILE_TYPE_CSV,
    FILE_TYPE_TSV,
    FILE_TYPE_TEXT
} apex_file_type_t;

/**
 * Address specification structure
 */
typedef struct {
    bool is_line_range;
    bool is_regex_range;
    int start_line;      // 1-based
    int end_line;        // 1-based, -1 means to end
    char *regex_start;   // NULL if not regex
    char *regex_end;     // NULL if not regex
    char *prefix;        // NULL if no prefix
} address_spec_t;

/**
 * Detect file type from extension
 */
static apex_file_type_t apex_detect_file_type(const char *filepath) {
    if (!filepath) return FILE_TYPE_TEXT;

    const char *ext = strrchr(filepath, '.');
    if (!ext) return FILE_TYPE_TEXT;
    ext++;

    /* Images */
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
        strcasecmp(ext, "png") == 0 || strcasecmp(ext, "gif") == 0 ||
        strcasecmp(ext, "webp") == 0 || strcasecmp(ext, "svg") == 0) {
        return FILE_TYPE_IMAGE;
    }

    /* CSV/TSV */
    if (strcasecmp(ext, "csv") == 0) return FILE_TYPE_CSV;
    if (strcasecmp(ext, "tsv") == 0) return FILE_TYPE_TSV;

    /* HTML */
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
        return FILE_TYPE_HTML;
    }

    /* Markdown */
    if (strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0 ||
        strcasecmp(ext, "mmd") == 0) {
        return FILE_TYPE_MARKDOWN;
    }

    /* Code files */
    if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0 ||
        strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "py") == 0 ||
        strcasecmp(ext, "js") == 0 || strcasecmp(ext, "java") == 0 ||
        strcasecmp(ext, "swift") == 0 || strcasecmp(ext, "go") == 0 ||
        strcasecmp(ext, "rs") == 0 || strcasecmp(ext, "sh") == 0) {
        return FILE_TYPE_CODE;
    }

    return FILE_TYPE_TEXT;
}

/**
 * Convert CSV/TSV to Markdown table
 *
 * Alignment handling:
 * - First row is always treated as header.
 * - If the second row cells are all one of: left, right, center, auto (case-insensitive),
 *   it is treated as an alignment row and converted to :---, ---:, :---:, or ---.
 *   The alignment row itself is NOT emitted as a data row.
 * - Otherwise, a default '---' separator row is generated after the header.
 *   The second row (including rows that contain only '-' and ':' characters) is emitted
 *   as normal data.
 */
char *apex_csv_to_table(const char *csv_content, bool is_tsv) {
    if (!csv_content) return NULL;

    char delim = is_tsv ? '\t' : ',';
    size_t len = strlen(csv_content);
    if (len == 0) return NULL;

    /* First pass: parse into rows and cells */
    typedef struct {
        char **cells;
        int cell_count;
    } csv_row_t;

    int row_cap = 8;
    int row_count = 0;
    csv_row_t *rows = calloc(row_cap, sizeof(csv_row_t));
    if (!rows) return NULL;

    const char *line_start = csv_content;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        if (!line_end) line_end = csv_content + strlen(csv_content);

        /* Allocate new row */
        if (row_count >= row_cap) {
            row_cap *= 2;
            csv_row_t *tmp = realloc(rows, (size_t)row_cap * sizeof(csv_row_t));
            if (!tmp) {
                /* Cleanup already allocated cells */
                for (int r = 0; r < row_count; r++) {
                    for (int c = 0; c < rows[r].cell_count; c++) {
                        free(rows[r].cells[c]);
                    }
                    free(rows[r].cells);
                }
                free(rows);
                return NULL;
            }
            rows = tmp;
        }

        csv_row_t *row = &rows[row_count];
        row->cells = NULL;
        row->cell_count = 0;

        int cell_cap = 8;
        row->cells = malloc((size_t)cell_cap * sizeof(char *));
        if (!row->cells) {
            for (int r = 0; r < row_count; r++) {
                for (int c = 0; c < rows[r].cell_count; c++) {
                    free(rows[r].cells[c]);
                }
                free(rows[r].cells);
            }
            free(rows);
            return NULL;
        }

        const char *cell_start = line_start;
        while (cell_start <= line_end) {
            const char *cell_end = cell_start;
            while (cell_end < line_end && *cell_end != delim) cell_end++;

            if (row->cell_count >= cell_cap) {
                cell_cap *= 2;
                char **tmp_cells = realloc(row->cells, (size_t)cell_cap * sizeof(char *));
                if (!tmp_cells) {
                    /* Cleanup */
                    for (int r = 0; r <= row_count; r++) {
                        int max_c = (r == row_count) ? row->cell_count : rows[r].cell_count;
                        char **cell_arr = (r == row_count) ? row->cells : rows[r].cells;
                        if (cell_arr) {
                            for (int c = 0; c < max_c; c++) {
                                free(cell_arr[c]);
                            }
                            free(cell_arr);
                        }
                    }
                    free(rows);
                    return NULL;
                }
                row->cells = tmp_cells;
            }

            size_t cell_len = (size_t)(cell_end - cell_start);
            char *cell = malloc(cell_len + 1);
            if (!cell) {
                for (int r = 0; r <= row_count; r++) {
                    int max_c = (r == row_count) ? row->cell_count : rows[r].cell_count;
                    char **cell_arr = (r == row_count) ? row->cells : rows[r].cells;
                    if (cell_arr) {
                        for (int c = 0; c < max_c; c++) {
                            free(cell_arr[c]);
                        }
                        free(cell_arr);
                    }
                }
                free(rows);
                return NULL;
            }
            memcpy(cell, cell_start, cell_len);
            cell[cell_len] = '\0';
            row->cells[row->cell_count++] = cell;

            if (cell_end < line_end) cell_start = cell_end + 1;
            else break;
        }

        row_count++;

        line_start = line_end;
        if (*line_start == '\n') line_start++;
    }

    if (row_count == 0) {
        free(rows);
        return NULL;
    }

    /* Determine column count from first row */
    int col_count = rows[0].cell_count;
    if (col_count <= 0) {
        for (int r = 0; r < row_count; r++) {
            for (int c = 0; c < rows[r].cell_count; c++) free(rows[r].cells[c]);
            free(rows[r].cells);
        }
        free(rows);
        return NULL;
    }

    /* Check for alignment row (second row with keywords) */
    bool has_alignment_row = false;
    enum { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER, ALIGN_AUTO } *align = NULL;

    if (row_count > 1) {
        csv_row_t *arow = &rows[1];
        bool all_keywords = (arow->cell_count == col_count);

        if (all_keywords) {
            align = malloc((size_t)col_count * sizeof(*align));
            if (!align) {
                for (int r = 0; r < row_count; r++) {
                    for (int c = 0; c < rows[r].cell_count; c++) free(rows[r].cells[c]);
                    free(rows[r].cells);
                }
                free(rows);
                return NULL;
            }

            for (int i = 0; i < col_count; i++) {
                char *cell = arow->cells[i];
                /* Trim whitespace */
                char *start = cell;
                while (*start && isspace((unsigned char)*start)) start++;
                char *end = start + strlen(start);
                while (end > start && isspace((unsigned char)end[-1])) end--;
                size_t tlen = (size_t)(end - start);

                if (tlen == 0) { all_keywords = false; break; }

                /* Lowercase copy for comparison */
                char buf[16];
                if (tlen >= sizeof(buf)) { all_keywords = false; break; }
                for (size_t j = 0; j < tlen; j++) {
                    buf[j] = (char)tolower((unsigned char)start[j]);
                }
                buf[tlen] = '\0';

                if (strcmp(buf, "left") == 0) {
                    align[i] = ALIGN_LEFT;
                } else if (strcmp(buf, "right") == 0) {
                    align[i] = ALIGN_RIGHT;
                } else if (strcmp(buf, "center") == 0) {
                    align[i] = ALIGN_CENTER;
                } else if (strcmp(buf, "auto") == 0) {
                    align[i] = ALIGN_AUTO;
                } else {
                    all_keywords = false;
                    break;
                }
            }

            if (!all_keywords) {
                free(align);
                align = NULL;
            } else {
                has_alignment_row = true;
            }
        }
    }

    /* Allocate output buffer: original size * 4 should be enough with extra alignment row */
    char *output = malloc(len * 4 + 64);
    if (!output) {
        if (align) free(align);
        for (int r = 0; r < row_count; r++) {
            for (int c = 0; c < rows[r].cell_count; c++) free(rows[r].cells[c]);
            free(rows[r].cells);
        }
        free(rows);
        return NULL;
    }

    char *write = output;

    /* Emit header row (first row) */
    {
        csv_row_t *row = &rows[0];
        *write++ = '|';
        for (int c = 0; c < col_count; c++) {
            *write++ = ' ';
            if (c < row->cell_count && row->cells[c]) {
                const char *val = row->cells[c];
                size_t vlen = strlen(val);
                memcpy(write, val, vlen);
                write += vlen;
            }
            *write++ = ' ';
            *write++ = '|';
        }
        *write++ = '\n';
    }

    /* Emit separator/alignment row */
    *write++ = '|';
    for (int c = 0; c < col_count; c++) {
        const char *spec = " --- ";
        if (has_alignment_row && align) {
            switch (align[c]) {
                case ALIGN_LEFT:   spec = " :--- "; break;
                case ALIGN_RIGHT:  spec = " ---: "; break;
                case ALIGN_CENTER: spec = " :---: "; break;
                case ALIGN_AUTO:   spec = " --- "; break;
            }
        }
        size_t slen = strlen(spec);
        memcpy(write, spec, slen);
        write += slen;
        *write++ = '|';
    }
    *write++ = '\n';

    /* Emit data rows (skip alignment row if present) */
    int start_row = has_alignment_row ? 2 : 1;
    for (int r = start_row; r < row_count; r++) {
        csv_row_t *row = &rows[r];
        *write++ = '|';
        for (int c = 0; c < col_count; c++) {
            *write++ = ' ';
            if (c < row->cell_count && row->cells[c]) {
                const char *val = row->cells[c];
                size_t vlen = strlen(val);
                memcpy(write, val, vlen);
                write += vlen;
            }
            *write++ = ' ';
            *write++ = '|';
        }
        *write++ = '\n';
    }

    *write = '\0';

    if (align) free(align);
    for (int r = 0; r < row_count; r++) {
        for (int c = 0; c < rows[r].cell_count; c++) free(rows[r].cells[c]);
        free(rows[r].cells);
    }
    free(rows);

    return output;
}

/**
 * Free address specification
 */
static void free_address_spec(address_spec_t *spec) {
    if (!spec) return;
    if (spec->regex_start) free(spec->regex_start);
    if (spec->regex_end) free(spec->regex_end);
    if (spec->prefix) free(spec->prefix);
    free(spec);
}

/**
 * Parse address specification
 * Supports:
 * - Line numbers: N,M or N,
 * - Regex: /pattern1/,/pattern2/
 * - Prefix: prefix="..."
 */
static address_spec_t *parse_address_spec(const char *address_str) {
    if (!address_str || *address_str == '\0') {
        return NULL;
    }

    address_spec_t *spec = calloc(1, sizeof(address_spec_t));
    if (!spec) return NULL;

    const char *pos = address_str;

    /* Skip leading whitespace */
    while (*pos && isspace(*pos)) pos++;

    /* Check if it's just a prefix parameter (starts with prefix=) */
    if (strncmp(pos, "prefix=\"", 8) == 0) {
        /* Just a prefix, no line range or regex */
        pos += 8;
        const char *prefix_start = pos;
        const char *prefix_end = strchr(prefix_start, '"');

        if (prefix_end) {
            size_t prefix_len = prefix_end - prefix_start;
            spec->prefix = malloc(prefix_len + 1);
            if (spec->prefix) {
                memcpy(spec->prefix, prefix_start, prefix_len);
                spec->prefix[prefix_len] = '\0';
            }
        }
        return spec;
    }

    /* Check if it's a regex pattern (starts with /) */
    if (*pos == '/') {
        spec->is_regex_range = true;
        const char *regex_start_begin = pos + 1;
        const char *regex_start_end = strchr(regex_start_begin, '/');

        if (!regex_start_end) {
            free_address_spec(spec);
            return NULL;
        }

        /* Extract first regex pattern */
        size_t regex_start_len = regex_start_end - regex_start_begin;
        spec->regex_start = malloc(regex_start_len + 1);
        if (!spec->regex_start) {
            free_address_spec(spec);
            return NULL;
        }
        memcpy(spec->regex_start, regex_start_begin, regex_start_len);
        spec->regex_start[regex_start_len] = '\0';

        pos = regex_start_end + 1;

        /* Skip comma and whitespace */
        while (*pos && (isspace(*pos) || *pos == ',')) pos++;

        /* Check for second regex pattern */
        if (*pos == '/') {
            const char *regex_end_begin = pos + 1;
            const char *regex_end_end = strchr(regex_end_begin, '/');

            if (regex_end_end) {
                size_t regex_end_len = regex_end_end - regex_end_begin;
                spec->regex_end = malloc(regex_end_len + 1);
                if (spec->regex_end) {
                    memcpy(spec->regex_end, regex_end_begin, regex_end_len);
                    spec->regex_end[regex_end_len] = '\0';
                }
                pos = regex_end_end + 1;
            }
        }
    } else {
        /* Line number format */
        spec->is_line_range = true;
        char *endptr;
        spec->start_line = (int)strtol(pos, &endptr, 10);

        if (endptr == pos || spec->start_line < 1) {
            /* Invalid start line, check if it's just a prefix */
            spec->is_line_range = false;
            spec->start_line = 0;
            spec->end_line = -1;
        } else {
            pos = endptr;

            /* Skip whitespace */
            while (*pos && isspace(*pos)) pos++;

            if (*pos == ',') {
                pos++;
                /* Skip whitespace after comma */
                while (*pos && isspace(*pos)) pos++;

                if (*pos == '\0' || *pos == ';') {
                    /* N, format - from line N to end */
                    spec->end_line = -1;
                } else {
                    /* N,M format */
                    spec->end_line = (int)strtol(pos, &endptr, 10);
                    if (endptr == pos || spec->end_line < spec->start_line) {
                        /* Invalid end line */
                        spec->end_line = -1;
                    }
                    pos = endptr;
                }
            } else {
                /* Just N - single line */
                spec->end_line = spec->start_line;
            }
        }
    }

    /* Look for prefix parameter */
    while (*pos && isspace(*pos)) pos++;
    if (*pos == ';') {
        pos++;
        while (*pos && isspace(*pos)) pos++;

        /* Check for prefix="..." */
        if (strncmp(pos, "prefix=\"", 8) == 0) {
            pos += 8;
            const char *prefix_start = pos;
            const char *prefix_end = strchr(prefix_start, '"');

            if (prefix_end) {
                size_t prefix_len = prefix_end - prefix_start;
                spec->prefix = malloc(prefix_len + 1);
                if (spec->prefix) {
                    memcpy(spec->prefix, prefix_start, prefix_len);
                    spec->prefix[prefix_len] = '\0';
                }
            }
        }
    }

    return spec;
}

/**
 * Extract lines from content based on address specification
 */
static char *extract_lines(const char *content, address_spec_t *spec) {
    if (!content || !spec) return NULL;

    /* If no specification, return full content */
    if (!spec->is_line_range && !spec->is_regex_range) {
        if (spec->prefix) {
            /* Apply prefix to all lines */
            size_t content_len = strlen(content);
            size_t prefix_len = strlen(spec->prefix);
            size_t lines = 0;
            const char *p = content;
            while (*p) {
                if (*p == '\n') lines++;
                p++;
            }
            if (*p == '\0' && p > content && p[-1] != '\n') lines++;

            size_t output_size = content_len + (lines * prefix_len) + 1;
            char *output = malloc(output_size);
            if (!output) return NULL;

            char *write = output;
            const char *read = content;
            bool at_line_start = true;

            while (*read) {
                if (at_line_start && *read != '\n') {
                    strcpy(write, spec->prefix);
                    write += prefix_len;
                    at_line_start = false;
                }
                *write++ = *read++;
                if (read[-1] == '\n') {
                    at_line_start = true;
                }
            }
            *write = '\0';
            return output;
        }
        return strdup(content);
    }

    /* Count lines in content */
    int total_lines = 1;
    const char *p = content;
    while (*p) {
        if (*p == '\n') total_lines++;
        p++;
    }

    if (spec->is_regex_range) {
        /* Regex-based extraction */
        regex_t regex_start, regex_end;
        int ret_start = 0, ret_end = 0;
        bool compiled_start = false, compiled_end = false;

        /* Compile start regex */
        if (spec->regex_start) {
            ret_start = regcomp(&regex_start, spec->regex_start, REG_EXTENDED);
            if (ret_start == 0) {
                compiled_start = true;
            }
        }

        /* Compile end regex */
        if (spec->regex_end) {
            ret_end = regcomp(&regex_end, spec->regex_end, REG_EXTENDED);
            if (ret_end == 0) {
                compiled_end = true;
            }
        }

        if (!compiled_start && !compiled_end) {
            /* No valid regex, return full content */
            if (compiled_start) regfree(&regex_start);
            if (compiled_end) regfree(&regex_end);
            return spec->prefix ? extract_lines(content, spec) : strdup(content);
        }

        /* Find line numbers matching regex patterns */
        int start_line = 1;
        int end_line = total_lines;
        regmatch_t match;
        int line_num = 1;
        const char *line_start = content;
        bool found_start = false;

        while (*line_start && line_num <= total_lines) {
            const char *line_end = strchr(line_start, '\n');
            if (!line_end) line_end = line_start + strlen(line_start);

            size_t line_len = line_end - line_start;
            char *line = malloc(line_len + 1);
            if (!line) {
                if (compiled_start) regfree(&regex_start);
                if (compiled_end) regfree(&regex_end);
                return NULL;
            }
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';

            /* Check start pattern */
            if (compiled_start && !found_start) {
                if (regexec(&regex_start, line, 1, &match, 0) == 0) {
                    start_line = line_num;
                    found_start = true;
                }
            }

            /* Check end pattern (only if start found or no start pattern) */
            if (compiled_end && (found_start || !compiled_start)) {
                if (regexec(&regex_end, line, 1, &match, 0) == 0) {
                    end_line = line_num;
                    break;
                }
            }

            free(line);
            line_start = line_end;
            if (*line_start == '\n') line_start++;
            line_num++;
        }

        if (compiled_start) regfree(&regex_start);
        if (compiled_end) regfree(&regex_end);

        /* If start not found and we have a start pattern, return empty */
        if (compiled_start && !found_start) {
            return strdup("");
        }

        /* Extract lines from start_line to end_line */
        line_num = 1;
        line_start = content;
        size_t output_size = strlen(content) + 1024;
        char *output = malloc(output_size);
        if (!output) return NULL;
        char *write = output;
        size_t remaining = output_size;

        while (*line_start && line_num <= total_lines) {
            const char *line_end = strchr(line_start, '\n');
            if (!line_end) line_end = line_start + strlen(line_start);

            if (line_num >= start_line && line_num < end_line) {
                size_t line_len = line_end - line_start;
                if (line_len + 100 < remaining) {
                    if (spec->prefix) {
                        size_t prefix_len = strlen(spec->prefix);
                        if (prefix_len + line_len + 10 < remaining) {
                            strcpy(write, spec->prefix);
                            write += prefix_len;
                            remaining -= prefix_len;
                        }
                    }
                    memcpy(write, line_start, line_len);
                    write += line_len;
                    remaining -= line_len;
                    *write++ = '\n';
                    remaining--;
                }
            }

            line_start = line_end;
            if (*line_start == '\n') line_start++;
            line_num++;
        }

        *write = '\0';
        return output;
    } else {
        /* Line number-based extraction */
        if (spec->start_line < 1 || spec->start_line > total_lines) {
            return strdup("");
        }

        int end = spec->end_line;
        if (end == -1) {
            end = total_lines;
        } else if (end > total_lines) {
            end = total_lines;
        }
        if (end < spec->start_line) {
            return strdup("");
        }

        /* Extract lines */
        int line_num = 1;
        const char *line_start = content;
        size_t output_size = strlen(content) + 1024;
        char *output = malloc(output_size);
        if (!output) return NULL;
        char *write = output;
        size_t remaining = output_size;

        while (*line_start && line_num <= total_lines) {
            const char *line_end = strchr(line_start, '\n');
            if (!line_end) line_end = line_start + strlen(line_start);

            if (line_num >= spec->start_line && line_num < end) {
                size_t line_len = line_end - line_start;
                if (line_len + 100 < remaining) {
                    if (spec->prefix) {
                        size_t prefix_len = strlen(spec->prefix);
                        if (prefix_len + line_len + 10 < remaining) {
                            strcpy(write, spec->prefix);
                            write += prefix_len;
                            remaining -= prefix_len;
                        }
                    }
                    memcpy(write, line_start, line_len);
                    write += line_len;
                    remaining -= line_len;
                    *write++ = '\n';
                    remaining--;
                }
            }

            line_start = line_end;
            if (*line_start == '\n') line_start++;
            line_num++;
        }

        *write = '\0';
        return output;
    }
}

/**
 * Resolve wildcard path.
 *
 * Supported patterns:
 * - Legacy "file.*" patterns:
 *   - Preferentially resolve to file.html, file.md, file.txt, file.tex (in that order)
 * - General globbing using standard shell-style patterns:
 *   - '*' and '?' wildcards
 *   - Character classes '[]'
 *   - Brace expansion '{a,b}.md', '*.{html,md}' where supported by the platform
 *
 * The path is resolved relative to base_dir (or current directory) before globbing.
 * Returns a newly-allocated path string or NULL if no match is found.
 */
char *apex_resolve_wildcard(const char *filepath, const char *base_dir) {
    if (!filepath) return NULL;

    /* Fast path: legacy "file.*" handling with explicit extension preference.
     * Only trigger this when the pattern ends with ".*" and contains no
     * other glob characters. This preserves the documented MMD-style
     * wildcard behavior while allowing more general globbing for
     * patterns like "*.c" or "{intro,part1}.md".
     */
    const char *wildcard = strstr(filepath, ".*");
    bool has_other_glob = (strpbrk(filepath, "*?[{") != NULL &&
                           !(wildcard && wildcard[1] == '\0'));

    if (wildcard && !has_other_glob) {
        /* Extract base filename (before .*) */
        size_t base_len = (size_t)(wildcard - filepath);
        char base_filename[1024];
        if (base_len >= sizeof(base_filename)) return NULL;

        memcpy(base_filename, filepath, base_len);
        base_filename[base_len] = '\0';

        /* Try common extensions in preferred order */
        const char *extensions[] = {".html", ".md", ".txt", ".tex", NULL};

        for (int i = 0; extensions[i]; i++) {
            char test_path[1024];
            snprintf(test_path, sizeof(test_path), "%s%s", base_filename, extensions[i]);

            char *resolved = resolve_path(test_path, base_dir);
            if (resolved && apex_file_exists(resolved)) {
                return resolved;
            }
            free(resolved);
        }

        /* No match found for legacy pattern, fall through to globbing
         * so that users can still benefit from more general patterns
         * if they mixed syntax.
         */
    }

    /* General case: use glob() to resolve shell-style patterns
     * (supports *, ?, [], and optionally brace expansion).
     */
    if (strpbrk(filepath, "*?[{") != NULL) {
        char *pattern_path = resolve_path(filepath, base_dir);
        if (!pattern_path) return NULL;

        glob_t results;
        int flags = 0;
#ifdef GLOB_BRACE
        /* Enable brace expansion where available (BSD/macOS, some libcs) */
        if (strchr(pattern_path, '{') || strchr(pattern_path, '}')) {
            flags |= GLOB_BRACE;
        }
#endif

        int rc = glob(pattern_path, flags, NULL, &results);
        free(pattern_path);

        if (rc == 0 && results.gl_pathc > 0 && results.gl_pathv[0]) {
            char *match = strdup(results.gl_pathv[0]);
            globfree(&results);
            return match;
        }

        globfree(&results);
        return NULL;
    }

    /* No wildcard characters - behave like resolve_path */
    return resolve_path(filepath, base_dir);
}

/**
 * Get transclude base from metadata, or return default base_dir
 * Returns newly allocated string (caller must free) or NULL
 */
static char *get_transclude_base(const char *base_dir, apex_metadata_item *metadata) {
    if (!metadata) {
        return base_dir ? strdup(base_dir) : NULL;
    }

    /* Note: apex_metadata_get now handles case-insensitive matching and spaces being removed
     * So "transclude base", "Transclude Base", "transcludebase" all work
     */
    const char *transclude_base = apex_metadata_get(metadata, "transclude base");

    if (transclude_base) {
        /* If absolute path, return as-is */
        if (transclude_base[0] == '/') {
            return strdup(transclude_base);
        }

        /* If relative path starting with ".", use current file's directory */
        if (transclude_base[0] == '.' && (transclude_base[1] == '/' || transclude_base[1] == '\0')) {
            if (base_dir) {
                return strdup(base_dir);
            }
            return strdup(".");
        }

        /* Relative path - combine with base_dir */
        if (base_dir) {
            size_t len = strlen(base_dir) + strlen(transclude_base) + 2;
            char *result = malloc(len);
            if (result) {
                snprintf(result, len, "%s/%s", base_dir, transclude_base);
            }
            return result;
        }

        return strdup(transclude_base);
    }

    /* No transclude base metadata, use default */
    return base_dir ? strdup(base_dir) : NULL;
}

/**
 * Process file includes in text
 */
char *apex_process_includes(const char *text, const char *base_dir, apex_metadata_item *metadata, int depth) {
    if (!text) return NULL;
    if (depth > MAX_INCLUDE_DEPTH) {
        return strdup(text);  /* Silently return original text */
    }

    size_t text_len = strlen(text);
    size_t output_capacity = text_len * 10;  /* Generous for includes */
    if (output_capacity < 1024 * 1024) output_capacity = 1024 * 1024;  /* At least 1MB */
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    /* Get effective base directory from transclude base metadata */
    char *effective_base_dir = get_transclude_base(base_dir, metadata);
    if (!effective_base_dir && base_dir) {
        effective_base_dir = strdup(base_dir);
    }

    const char *read_pos = text;
    char *write_pos = output;
    size_t remaining = output_capacity;

    while (*read_pos) {
        bool processed_include = false;

        /* Look for iA Writer transclusion /filename (at start of line only) */
        if (read_pos[0] == '/' && (read_pos == text || read_pos[-1] == '\n')) {
            const char *filepath_start = read_pos + 1;
            const char *filepath_end = filepath_start;

            /* Find end of filename */
            while (*filepath_end && *filepath_end != ' ' && *filepath_end != '\t' &&
                   *filepath_end != '\n' && *filepath_end != '\r') {
                filepath_end++;
            }

            if (filepath_end > filepath_start && (filepath_end - filepath_start) < 1024) {
                char filepath[1024];
                size_t filepath_len = filepath_end - filepath_start;
                memcpy(filepath, filepath_start, filepath_len);
                filepath[filepath_len] = '\0';

                /* Resolve and check file exists */
                char *resolved_path = resolve_path(filepath, effective_base_dir);
                if (resolved_path && apex_file_exists(resolved_path)) {
                    apex_file_type_t file_type = apex_detect_file_type(resolved_path);
                    char *content = read_file_contents(resolved_path);

                    if (content) {
                        char *to_insert = NULL;

                        if (file_type == FILE_TYPE_IMAGE) {
                            /* Image: create ![](path) */
                            size_t buf_size = strlen(filepath) + 10;
                            to_insert = malloc(buf_size);
                            if (to_insert) snprintf(to_insert, buf_size, "![](%s)\n", filepath);
                        } else if (file_type == FILE_TYPE_CSV || file_type == FILE_TYPE_TSV) {
                            /* CSV/TSV: convert to table */
                            to_insert = apex_csv_to_table(content, file_type == FILE_TYPE_TSV);
                        } else if (file_type == FILE_TYPE_CODE) {
                            /* Code: wrap in fenced code block */
                            const char *ext = strrchr(filepath, '.');
                            const char *lang = ext ? ext + 1 : "";
                            size_t buf_size = strlen(content) + strlen(lang) + 20;
                            to_insert = malloc(buf_size);
                            if (to_insert) snprintf(to_insert, buf_size, "\n```%s\n%s\n```\n", lang, content);
                        } else {
                            /* Text/Markdown: process and include */
                            /* Extract metadata from transcluded file */
                            char *file_content_for_metadata = strdup(content);
                            apex_metadata_item *file_metadata = NULL;
                            char *file_text_after_metadata = file_content_for_metadata;
                            if (file_content_for_metadata) {
                                file_metadata = apex_extract_metadata(&file_text_after_metadata);
                            }

                            /* Get transclude base from file's metadata, or use file's directory */
                            char *transclude_base = NULL;
                            if (file_metadata) {
                                transclude_base = get_transclude_base(get_directory(resolved_path), file_metadata);
                            }
                            if (!transclude_base) {
                                transclude_base = get_directory(resolved_path);
                            }

                            to_insert = apex_process_includes(content, transclude_base, file_metadata, depth + 1);

                            /* Cleanup */
                            if (transclude_base) free(transclude_base);
                            if (file_metadata) apex_free_metadata(file_metadata);
                            if (file_content_for_metadata) free(file_content_for_metadata);
                        }

                        if (to_insert) {
                            size_t insert_len = strlen(to_insert);
                            if (insert_len < remaining) {
                                memcpy(write_pos, to_insert, insert_len);
                                write_pos += insert_len;
                                remaining -= insert_len;
                            }
                            if (to_insert != content) free(to_insert);
                        }

                        free(content);
                        free(resolved_path);
                        read_pos = filepath_end;
                        processed_include = true;
                    } else {
                        free(resolved_path);
                    }
                } else if (resolved_path) {
                    free(resolved_path);
                }
            }
        }

        /* Look for MMD transclusion {{file}} */
        if (!processed_include && read_pos[0] == '{' && read_pos[1] == '{') {
            const char *filepath_start = read_pos + 2;
            const char *filepath_end = strstr(filepath_start, "}}");

            if (filepath_end && (filepath_end - filepath_start) > 0 && (filepath_end - filepath_start) < 1024) {
                /* Extract filepath */
                int filepath_len = filepath_end - filepath_start;
                char filepath[1024];
                memcpy(filepath, filepath_start, filepath_len);
                filepath[filepath_len] = '\0';

                /* Check for address specification [address] */
                const char *address_start = filepath_end + 2;
                const char *address_end = NULL;
                address_spec_t *address_spec = NULL;

                if (*address_start == '[') {
                    address_start++;
                    address_end = strchr(address_start, ']');
                    if (address_end) {
                        int address_len = address_end - address_start;
                        char address_str[1024];
                        if (address_len > 0 && address_len < (int)sizeof(address_str)) {
                            memcpy(address_str, address_start, address_len);
                            address_str[address_len] = '\0';
                            address_spec = parse_address_spec(address_str);
                        }
                    }
                }

                /* Resolve path (handle wildcards) */
                char *resolved_path = apex_resolve_wildcard(filepath, effective_base_dir);
                if (!resolved_path) {
                    /* Try without wildcard resolution */
                    resolved_path = resolve_path(filepath, effective_base_dir);
                }

                if (resolved_path) {
                    apex_file_type_t file_type = apex_detect_file_type(resolved_path);
                    char *content = read_file_contents(resolved_path);
                    if (content) {
                        /* Extract metadata from original file content FIRST (before any processing) */
                        char *file_content_for_metadata = strdup(content);
                        apex_metadata_item *file_metadata = NULL;
                        char *file_text_after_metadata = file_content_for_metadata;
                        if (file_content_for_metadata) {
                            file_metadata = apex_extract_metadata(&file_text_after_metadata);
                        }

                        /* Apply address specification if present */
                        char *extracted_content = content;
                        bool free_extracted = false;

                        if (address_spec) {
                            extracted_content = extract_lines(content, address_spec);
                            if (extracted_content && extracted_content != content) {
                                free_extracted = true;
                            }
                        }

                        char *to_process = extracted_content;
                        bool free_to_process = false;

                        /* Convert CSV/TSV to table */
                        if (file_type == FILE_TYPE_CSV || file_type == FILE_TYPE_TSV) {
                            char *table = apex_csv_to_table(extracted_content, file_type == FILE_TYPE_TSV);
                            if (table) {
                                to_process = table;
                                free_to_process = true;
                            }
                        }

                        /* Get transclude base from file's metadata, or use file's directory */
                        char *transclude_base = NULL;
                        if (file_metadata) {
                            transclude_base = get_transclude_base(get_directory(resolved_path), file_metadata);
                        }
                        if (!transclude_base) {
                            transclude_base = get_directory(resolved_path);
                        }

                        /* Recursively process with file's metadata and transclude base */
                        char *processed = apex_process_includes(to_process, transclude_base, file_metadata, depth + 1);

                        /* Cleanup */
                        if (transclude_base) free(transclude_base);
                        if (file_metadata) apex_free_metadata(file_metadata);
                        if (file_content_for_metadata) free(file_content_for_metadata);

                        if (processed) {
                            size_t proc_len = strlen(processed);
                            if (proc_len < remaining) {
                                memcpy(write_pos, processed, proc_len);
                                write_pos += proc_len;
                                remaining -= proc_len;
                            }
                            free(processed);
                        }

                        if (free_to_process) free(to_process);
                        if (free_extracted) free(extracted_content);
                        free(content);

                        if (address_end) {
                            read_pos = address_end + 1;
                        } else {
                            read_pos = filepath_end + 2;
                        }
                        free(resolved_path);
                        if (address_spec) free_address_spec(address_spec);
                        processed_include = true;
                    } else {
                        free(resolved_path);
                        if (address_spec) free_address_spec(address_spec);
                    }
                } else {
                    if (address_spec) free_address_spec(address_spec);
                }
            }
        }

        /* Look for << (Marked syntax) */
        if (!processed_include && read_pos[0] == '<' && read_pos[1] == '<') {
            char bracket_type = 0;
            const char *filepath_start = NULL;
            const char *filepath_end = NULL;

            /* Determine include type */
            if (read_pos[2] == '[') {
                /* <<[file.md] - Markdown include */
                bracket_type = '[';
                filepath_start = read_pos + 3;
                filepath_end = strchr(filepath_start, ']');
            } else if (read_pos[2] == '(') {
                /* <<(file.ext) - Code block include */
                bracket_type = '(';
                filepath_start = read_pos + 3;
                filepath_end = strchr(filepath_start, ')');
            } else if (read_pos[2] == '{') {
                /* <<{file.html} - Raw HTML include */
                bracket_type = '{';
                filepath_start = read_pos + 3;
                filepath_end = strchr(filepath_start, '}');
            }

            if (bracket_type && filepath_start && filepath_end) {
                /* Extract filepath */
                int filepath_len = filepath_end - filepath_start;
                char filepath[1024];
                if (filepath_len > 0 && filepath_len < (int)sizeof(filepath)) {
                    memcpy(filepath, filepath_start, filepath_len);
                    filepath[filepath_len] = '\0';

                    /* Check for address specification [address] */
                    const char *address_start = filepath_end + 1;
                    /* Skip whitespace */
                    while (*address_start && isspace(*address_start)) address_start++;
                    const char *address_end = NULL;
                    address_spec_t *address_spec = NULL;

                    if (*address_start == '[') {
                        address_start++;
                        address_end = strchr(address_start, ']');
                        if (address_end) {
                            int address_len = address_end - address_start;
                            char address_str[1024];
                            if (address_len > 0 && address_len < (int)sizeof(address_str)) {
                                memcpy(address_str, address_start, address_len);
                                address_str[address_len] = '\0';
                                address_spec = parse_address_spec(address_str);
                            }
                        }
                    }

                    /* Resolve path */
                    char *resolved_path = resolve_path(filepath, effective_base_dir);
                    if (resolved_path) {
                        apex_file_type_t file_type = apex_detect_file_type(resolved_path);
                        char *content = read_file_contents(resolved_path);
                        if (content) {
                            /* Extract metadata from original file content FIRST (before any processing) */
                            char *file_content_for_metadata = strdup(content);
                            apex_metadata_item *file_metadata = NULL;
                            char *file_text_after_metadata = file_content_for_metadata;
                            if (file_content_for_metadata) {
                                file_metadata = apex_extract_metadata(&file_text_after_metadata);
                            }

                            /* Apply address specification if present */
                            char *extracted_content = content;
                            bool free_extracted = false;

                            if (address_spec) {
                                extracted_content = extract_lines(content, address_spec);
                                if (extracted_content && extracted_content != content) {
                                    free_extracted = true;
                                }
                            }

                            /* Process based on include type */
                            if (bracket_type == '[') {
                                char *to_process = extracted_content;
                                bool free_to_process = false;

                                /* Convert CSV/TSV to table */
                                if (file_type == FILE_TYPE_CSV || file_type == FILE_TYPE_TSV) {
                                    char *table = apex_csv_to_table(extracted_content, file_type == FILE_TYPE_TSV);
                                    if (table) {
                                        to_process = table;
                                        free_to_process = true;
                                    }
                                }

                                /* Markdown include - recursively process */

                                /* Get transclude base from file's metadata, or use file's directory */
                                char *transclude_base = NULL;
                                if (file_metadata) {
                                    transclude_base = get_transclude_base(get_directory(resolved_path), file_metadata);
                                }
                                if (!transclude_base) {
                                    transclude_base = get_directory(resolved_path);
                                }

                                char *processed = apex_process_includes(to_process, transclude_base, file_metadata, depth + 1);

                                /* Cleanup */
                                if (transclude_base) free(transclude_base);
                                if (file_metadata) apex_free_metadata(file_metadata);
                                if (file_content_for_metadata) free(file_content_for_metadata);

                                if (processed) {
                                    size_t proc_len = strlen(processed);
                                    if (proc_len < remaining) {
                                        memcpy(write_pos, processed, proc_len);
                                        write_pos += proc_len;
                                        remaining -= proc_len;
                                    }
                                    free(processed);
                                }

                                if (free_to_process) free(to_process);
                            } else if (bracket_type == '(') {
                                /* Code block include - wrap in code fence */
                                /* Try to detect language from extension */
                                const char *ext = strrchr(filepath, '.');
                                const char *lang = "";
                                if (ext) {
                                    ext++;
                                    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) lang = "c";
                                    else if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0) lang = "cpp";
                                    else if (strcmp(ext, "py") == 0) lang = "python";
                                    else if (strcmp(ext, "js") == 0) lang = "javascript";
                                    else if (strcmp(ext, "rb") == 0) lang = "ruby";
                                    else if (strcmp(ext, "sh") == 0) lang = "bash";
                                    else lang = ext;
                                }

                                char code_header[128];
                                snprintf(code_header, sizeof(code_header), "\n```%s\n", lang);
                                const char *code_footer = "\n```\n";

                                size_t total_len = strlen(code_header) + strlen(extracted_content) + strlen(code_footer);
                                if (total_len < remaining) {
                                    strcpy(write_pos, code_header);
                                    write_pos += strlen(code_header);
                                    strcpy(write_pos, extracted_content);
                                    write_pos += strlen(extracted_content);
                                    strcpy(write_pos, code_footer);
                                    write_pos += strlen(code_footer);
                                    remaining -= total_len;
                                }
                            } else if (bracket_type == '{') {
                                /* Raw HTML - will be inserted after processing */
                                /* For now, insert a placeholder marker */
                                char marker[1024];
                                snprintf(marker, sizeof(marker), "<!--APEX_RAW_INCLUDE:%s-->", resolved_path);
                                size_t marker_len = strlen(marker);
                                if (marker_len < remaining) {
                                    memcpy(write_pos, marker, marker_len);
                                    write_pos += marker_len;
                                    remaining -= marker_len;
                                }
                            }

                            if (free_extracted) free(extracted_content);
                            free(content);
                        }
                        free(resolved_path);
                    }

                    /* Skip past the include syntax */
                    if (address_end) {
                        read_pos = address_end + 1;
                    } else {
                        read_pos = filepath_end + 1;
                    }
                    if (address_spec) free_address_spec(address_spec);
                    processed_include = true;
                }
            }
        }

        /* Not an include, copy character */
        if (!processed_include) {
            if (remaining > 0) {
                *write_pos++ = *read_pos;
                remaining--;
            }
            read_pos++;
        }
    }

    *write_pos = '\0';

    /* Cleanup */
    if (effective_base_dir) free(effective_base_dir);

    return output;
}

