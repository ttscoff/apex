/**
 * Relaxed Tables Extension for Apex
 * Implementation
 *
 * Detects tables without separator rows and inserts separator rows
 * so the existing table parser can handle them as data-only tables.
 */

#include "relaxed_tables.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

/**
 * Count the number of columns in a table row (by counting pipes)
 * Returns -1 if the line doesn't contain a pipe
 * Number of columns = number of pipe-separated cells
 * For GFM tables: | one | two | has 2 columns, one | two has 2 columns
 */
static int count_columns(const char *line, size_t len) {
    if (!line || len == 0) return -1;

    bool has_pipe = false;
    int pipe_count = 0;

    /* Skip leading whitespace */
    size_t start = 0;
    while (start < len && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }

    bool starts_with_pipe = (start < len && line[start] == '|');

    /* Count pipes */
    for (size_t i = start; i < len; i++) {
        if (line[i] == '|') {
            has_pipe = true;
            pipe_count++;
            if (i == start) {
                starts_with_pipe = true;
            }
            /* Check if this is the last non-whitespace character */
            /* Note: ends_with_pipe was set but never used, removed to fix warning */
        }
    }

    if (!has_pipe) return -1;

    /* Calculate number of columns:
     * - If line starts with | and ends with |: columns = pipe_count - 1
     * - If line starts with | but doesn't end with |: columns = pipe_count
     * - If line doesn't start with | but ends with |: columns = pipe_count
     * - If line doesn't start or end with |: columns = pipe_count + 1
     *
     * Actually, simpler: number of columns = number of pipe-separated segments
     * | one | two | = 3 segments (empty, one, two, empty) but 2 data columns
     * one | two = 2 segments = 2 columns
     * | one | two = 3 segments (empty, one, two) but effectively 2 columns
     *
     * For GFM, we want the number of data columns, which is typically:
     * - If starts with |: pipe_count - 1 (leading pipe creates empty first cell)
     * - If doesn't start with |: pipe_count + 1
     *
     * But actually, for table parsing, we need the separator to match the structure.
     * Let's use: if starts with |, columns = pipe_count - 1, else columns = pipe_count + 1
     */
    if (starts_with_pipe) {
        /* Leading pipe: number of columns = number of pipes - 1 */
        /* | one | two | has 3 pipes, 2 columns */
        return pipe_count - 1;
    } else {
        /* No leading pipe: number of columns = number of pipes + 1 */
        /* one | two has 1 pipe, 2 columns */
        return pipe_count + 1;
    }
}

/**
 * Check if a line is blank (empty or only whitespace)
 */
static bool is_blank_line(const char *line, size_t len) {
    if (!line || len == 0) return true;

    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)line[i])) {
            return false;
        }
    }

    return true;
}

/**
 * Check if a line is a horizontal rule (--- on a line by itself)
 */
static bool is_horizontal_rule(const char *line, size_t len) {
    if (!line || len == 0) return false;

    /* Skip leading whitespace */
    size_t start = 0;
    while (start < len && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }

    /* Check if it's all dashes (at least 3) */
    size_t dash_count = 0;
    for (size_t i = start; i < len; i++) {
        if (line[i] == '-') {
            dash_count++;
        } else if (line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '\r') {
            return false;  /* Contains non-dash, non-whitespace character */
        }
    }

    /* Must have at least 3 dashes and no pipes */
    return dash_count >= 3;
}

/**
 * Check if a line is a table separator row (contains |, -, :, spaces only)
 * Must have both dash and pipe to be a separator row
 */
static bool is_separator_row(const char *line, size_t len) {
    if (!line || len == 0) return false;

    /* Horizontal rules are not table separators */
    if (is_horizontal_rule(line, len)) {
        return false;
    }

    bool has_dash = false;
    bool has_pipe = false;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '-') {
            has_dash = true;
        } else if (c == '|') {
            has_pipe = true;
        } else if (c != ' ' && c != '\t' && c != ':' && c != '+') {
            /* Contains a character that's not allowed in separator rows */
            return false;
        }
    }

    /* Separator row must have both dash and pipe */
    return has_dash && has_pipe;
}

/**
 * Generate a separator row for a given number of columns
 * Format: |---|---| (no spaces to avoid smart typography conversion)
 * @param num_columns Number of columns
 * @param starts_with_pipe If true, generate separator starting with | to match row format
 */
static char *generate_separator_row(int num_columns, bool starts_with_pipe) {
    if (num_columns < 1) return NULL;

    /* Generate separator matching the row format */
    /* For rows with leading pipes: "| --- | --- |" (with spaces) */
    /* For rows without: "|---|---|" (no spaces) */
    size_t len;
    if (starts_with_pipe) {
        /* "| " + num_columns * "--- | " + "\n" + null */
        len = 2 + num_columns * 6 + 1 + 1;
    } else {
        /* "|" + num_columns * "---|" + "\n" + null */
        len = 1 + num_columns * 4 + 1 + 1;
    }
    char *sep = malloc(len);
    if (!sep) return NULL;

    char *p = sep;
    if (starts_with_pipe) {
        *p++ = '|';
        *p++ = ' ';
    }

    for (int i = 0; i < num_columns; i++) {
        *p++ = '-';
        *p++ = '-';
        *p++ = '-';
        if (starts_with_pipe) {
            *p++ = ' ';
            *p++ = '|';
            if (i < num_columns - 1) {
                *p++ = ' ';  /* Space after pipe for all but last column */
            }
        } else {
            *p++ = '|';
        }
    }

    *p++ = '\n';
    *p = '\0';

    return sep;
}

/**
 * Check if a line looks like a table row (contains pipes and non-separator content)
 */
static bool is_table_row(const char *line, size_t len) {
    if (!line || len == 0) return false;

    /* Must contain a pipe */
    bool has_pipe = false;
    bool has_content = false;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '|') {
            has_pipe = true;
        } else if (!isspace(c) && c != '-') {
            /* Has content that's not just dashes */
            has_content = true;
        }
    }

    /* Must have both pipe and non-dash content */
    return has_pipe && has_content;
}

/**
 * Generate a dummy header row (empty cells) for a given number of columns
 * This will be removed in post-processing
 * @param num_columns Number of columns
 * @param starts_with_pipe If true, generate header starting with | to match separator format
 */
static char *generate_dummy_header_row(int num_columns, bool starts_with_pipe) {
    if (num_columns < 1) return NULL;

    /* Calculate size: need space for pipes and spaces */
    size_t len;
    if (starts_with_pipe) {
        /* "| " + num_columns * " | " + "\n" + null */
        len = 2 + num_columns * 4 + 1 + 1;
    } else {
        /* num_columns * " |" + "\n" + null */
        len = num_columns * 3 + 1 + 1;
    }

    char *header = malloc(len);
    if (!header) return NULL;

    char *p = header;
    if (starts_with_pipe) {
        *p++ = '|';
        *p++ = ' ';
    }

    for (int i = 0; i < num_columns; i++) {
        *p++ = ' ';
        *p++ = '|';
        if (i < num_columns - 1 || !starts_with_pipe) {
            *p++ = ' ';
        }
    }

    *p++ = '\n';
    *p = '\0';

    return header;
}

/**
 * Process relaxed tables - detect tables without separator rows and insert them
 */
char *apex_process_relaxed_tables(const char *text) {
    if (!text) return NULL;

    size_t text_len = strlen(text);
    if (text_len == 0) return NULL;

    /* Check if input ends with a newline */
    bool input_ends_with_newline = (text_len > 0 && text[text_len - 1] == '\n');

    /* Allocate output buffer (may grow, but start with 2x input size) */
    size_t output_capacity = text_len * 2;
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = text;
    char *write = output;
    size_t remaining = output_capacity;
    size_t output_len = 0;

    /* Track potential table rows */
    typedef struct {
        const char *start;
        size_t len;
        int columns;
        bool starts_with_pipe;
    } table_row;

    table_row *rows = NULL;
    size_t rows_capacity = 0;
    size_t rows_count = 0;

    /* Process line by line */
    while (*read) {
        const char *line_start = read;
        const char *line_end = strchr(read, '\n');
        if (!line_end) {
            line_end = read + strlen(read);
        }

        size_t line_len = line_end - line_start;
        bool has_newline = (*line_end == '\n');

        /* Check if this is a blank line */
        if (is_blank_line(line_start, line_len)) {
            /* Blank line: if we have accumulated table rows, process them */
            if (rows_count >= 2) {
                /* We have a relaxed table - insert separator after first row */
                /* First, write all rows except the last one */
                for (size_t i = 0; i < rows_count; i++) {
                    /* Write the row */
                    if (rows[i].len < remaining) {
                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        /* Write newline */
                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    } else {
                        /* Need to grow buffer */
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    }

                    /* After first row, insert separator */
                    if (i == 0) {
                        char *sep = generate_separator_row(rows[0].columns, rows[0].starts_with_pipe);
                        if (sep) {
                            size_t sep_len = strlen(sep);
                            if (sep_len < remaining) {
                                memcpy(write, sep, sep_len);
                                write += sep_len;
                                remaining -= sep_len;
                                output_len += sep_len;
                            } else {
                                /* Need to grow buffer */
                                size_t new_capacity = output_capacity * 2;
                                char *new_output = realloc(output, new_capacity);
                                if (!new_output) {
                                    free(sep);
                                    free(output);
                                    free(rows);
                                    return NULL;
                                }
                                write = new_output + output_len;
                                output = new_output;
                                remaining = new_capacity - output_len;
                                output_capacity = new_capacity;

                                memcpy(write, sep, sep_len);
                                write += sep_len;
                                remaining -= sep_len;
                                output_len += sep_len;
                            }
                            free(sep);
                        }
                    }
                }

                /* Reset rows */
                rows_count = 0;

                /* After outputting table, also write the blank line */
                if (remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            } else {
                /* No table accumulated, just write the blank line */
                if (line_len < remaining) {
                    memcpy(write, line_start, line_len);
                    write += line_len;
                    remaining -= line_len;
                    output_len += line_len;

                    if (has_newline && remaining > 0) {
                        *write++ = '\n';
                        remaining--;
                        output_len++;
                    }
                } else {
                    /* Need to grow buffer */
                    size_t new_capacity = output_capacity * 2;
                    char *new_output = realloc(output, new_capacity);
                    if (!new_output) {
                        free(output);
                        free(rows);
                        return NULL;
                    }
                    write = new_output + output_len;
                    output = new_output;
                    remaining = new_capacity - output_len;
                    output_capacity = new_capacity;

                    memcpy(write, line_start, line_len);
                    write += line_len;
                    remaining -= line_len;
                    output_len += line_len;

                    if (has_newline && remaining > 0) {
                        *write++ = '\n';
                        remaining--;
                        output_len++;
                    }
                }
            }

            /* Move to next line */
            read = line_end;
            if (has_newline) read++;
            continue;
        }

        /* Check if this is a separator row */
        if (is_separator_row(line_start, line_len)) {
            /* Separator row: if we have accumulated rows, write them as-is */
            if (rows_count > 0) {
                for (size_t i = 0; i < rows_count; i++) {
                    if (rows[i].len < remaining) {
                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    } else {
                        /* Need to grow buffer */
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    }
                }
                rows_count = 0;
            }

            /* Write the separator row */
            if (line_len < remaining) {
                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            } else {
                /* Need to grow buffer */
                size_t new_capacity = output_capacity * 2;
                char *new_output = realloc(output, new_capacity);
                if (!new_output) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                write = new_output + output_len;
                output = new_output;
                remaining = new_capacity - output_len;
                output_capacity = new_capacity;

                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            }

            read = line_end;
            if (has_newline) read++;
            continue;
        }

        /* Check if this is a horizontal rule - skip it entirely */
        if (is_horizontal_rule(line_start, line_len)) {
            /* Write horizontal rule as-is and reset accumulated rows */
            if (rows_count > 0) {
                /* Write accumulated rows first */
                for (size_t i = 0; i < rows_count; i++) {
                    if (rows[i].len < remaining) {
                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    } else {
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    }
                }
                rows_count = 0;
            }

            /* Write horizontal rule as-is */
            if (line_len < remaining) {
                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            } else {
                size_t new_capacity = output_capacity * 2;
                char *new_output = realloc(output, new_capacity);
                if (!new_output) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                write = new_output + output_len;
                output = new_output;
                remaining = new_capacity - output_len;
                output_capacity = new_capacity;

                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            }

            read = line_end;
            if (has_newline) read++;
            continue;
        }

        /* Check if this line contains a pipe (potential table row) */
        const char *p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        bool starts_with_pipe = (p < line_end && *p == '|');

        /* Check if this is a separator row - if so, skip relaxed table processing for previous rows */
        if (is_separator_row(line_start, line_len)) {
            /* Separator row found - if we have accumulated rows, they're already a valid table */
            /* Write them as-is and reset */
            if (rows_count > 0) {
                for (size_t i = 0; i < rows_count; i++) {
                    if (rows[i].len < remaining) {
                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    } else {
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    }
                }
                rows_count = 0;
            }

            /* Write the separator row */
            if (line_len < remaining) {
                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            } else {
                size_t new_capacity = output_capacity * 2;
                char *new_output = realloc(output, new_capacity);
                if (!new_output) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                write = new_output + output_len;
                output = new_output;
                remaining = new_capacity - output_len;
                output_capacity = new_capacity;

                memcpy(write, line_start, line_len);
                write += line_len;
                remaining -= line_len;
                output_len += line_len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            }

            read = line_end;
            if (has_newline) read++;
            continue;
        }

        /* Process rows with or without leading pipes - both need separators if missing */
        int columns = count_columns(line_start, line_len);
        if (columns > 0) {
            /* Potential table row - add to accumulator */
            if (rows_count == 0) {
                /* First row - allocate array */
                rows_capacity = 16;
                rows = malloc(sizeof(table_row) * rows_capacity);
                if (!rows) {
                    free(output);
                    return NULL;
                }
            } else if (rows_count >= rows_capacity) {
                /* Grow array */
                rows_capacity *= 2;
                table_row *new_rows = realloc(rows, sizeof(table_row) * rows_capacity);
                if (!new_rows) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                rows = new_rows;
            }

            /* Check if columns match previous rows */
            if (rows_count > 0 && rows[0].columns != columns) {
                /* Column count mismatch - write accumulated rows and start fresh */
                for (size_t i = 0; i < rows_count; i++) {
                    if (rows[i].len < remaining) {
                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    } else {
                        /* Need to grow buffer */
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, rows[i].start, rows[i].len);
                        write += rows[i].len;
                        remaining -= rows[i].len;
                        output_len += rows[i].len;

                        if (has_newline && remaining > 0) {
                            *write++ = '\n';
                            remaining--;
                            output_len++;
                        }
                    }
                }
                rows_count = 0;
            }

            /* Add this row to accumulator */
            rows[rows_count].start = line_start;
            rows[rows_count].len = line_len;
            rows[rows_count].columns = columns;
            rows[rows_count].starts_with_pipe = starts_with_pipe;
            rows_count++;

            /* Don't write yet - wait for blank line or separator */
            read = line_end;
            if (has_newline) read++;
            continue;
        }

        /* Not a table row - if we have accumulated rows, write them first */
        if (rows_count >= 2) {
            /* We have a relaxed table - insert separator after first row */
            for (size_t i = 0; i < rows_count; i++) {
                if (rows[i].len < remaining) {
                    memcpy(write, rows[i].start, rows[i].len);
                    write += rows[i].len;
                    remaining -= rows[i].len;
                    output_len += rows[i].len;

                    if (has_newline && remaining > 0) {
                        *write++ = '\n';
                        remaining--;
                        output_len++;
                    }
                } else {
                    /* Need to grow buffer */
                    size_t new_capacity = output_capacity * 2;
                    char *new_output = realloc(output, new_capacity);
                    if (!new_output) {
                        free(output);
                        free(rows);
                        return NULL;
                    }
                    write = new_output + output_len;
                    output = new_output;
                    remaining = new_capacity - output_len;
                    output_capacity = new_capacity;

                    memcpy(write, rows[i].start, rows[i].len);
                    write += rows[i].len;
                    remaining -= rows[i].len;
                    output_len += rows[i].len;

                    if (has_newline && remaining > 0) {
                        *write++ = '\n';
                        remaining--;
                        output_len++;
                    }
                }

                /* After first row, insert separator */
                if (i == 0) {
                    char *sep = generate_separator_row(rows[0].columns, rows[0].starts_with_pipe);
                    if (sep) {
                        size_t sep_len = strlen(sep);
                        if (sep_len < remaining) {
                            memcpy(write, sep, sep_len);
                            write += sep_len;
                            remaining -= sep_len;
                            output_len += sep_len;
                        } else {
                            /* Need to grow buffer */
                            size_t new_capacity = output_capacity * 2;
                            char *new_output = realloc(output, new_capacity);
                            if (!new_output) {
                                free(sep);
                                free(output);
                                free(rows);
                                return NULL;
                            }
                            write = new_output + output_len;
                            output = new_output;
                            remaining = new_capacity - output_len;
                            output_capacity = new_capacity;

                            memcpy(write, sep, sep_len);
                            write += sep_len;
                            remaining -= sep_len;
                            output_len += sep_len;
                        }
                        free(sep);
                    }
                }
            }
            rows_count = 0;
        } else if (rows_count > 0) {
            /* Only one row - not a table, write it as-is */
            if (rows[0].len < remaining) {
                memcpy(write, rows[0].start, rows[0].len);
                write += rows[0].len;
                remaining -= rows[0].len;
                output_len += rows[0].len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            } else {
                /* Need to grow buffer */
                size_t new_capacity = output_capacity * 2;
                char *new_output = realloc(output, new_capacity);
                if (!new_output) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                write = new_output + output_len;
                output = new_output;
                remaining = new_capacity - output_len;
                output_capacity = new_capacity;

                memcpy(write, rows[0].start, rows[0].len);
                write += rows[0].len;
                remaining -= rows[0].len;
                output_len += rows[0].len;

                if (has_newline && remaining > 0) {
                    *write++ = '\n';
                    remaining--;
                    output_len++;
                }
            }
            rows_count = 0;
        }

        /* Write this line as-is */
        if (line_len < remaining) {
            memcpy(write, line_start, line_len);
            write += line_len;
            remaining -= line_len;
            output_len += line_len;

            if (has_newline && remaining > 0) {
                *write++ = '\n';
                remaining--;
                output_len++;
            }
        } else {
            /* Need to grow buffer */
            size_t new_capacity = output_capacity * 2;
            char *new_output = realloc(output, new_capacity);
            if (!new_output) {
                free(output);
                free(rows);
                return NULL;
            }
            write = new_output + output_len;
            output = new_output;
            remaining = new_capacity - output_len;
            output_capacity = new_capacity;

            memcpy(write, line_start, line_len);
            write += line_len;
            remaining -= line_len;
            output_len += line_len;

            if (has_newline && remaining > 0) {
                *write++ = '\n';
                remaining--;
                output_len++;
            }
        }

        read = line_end;
        if (has_newline) read++;
    }

    /* Handle any remaining accumulated rows at end of file */
    if (rows_count >= 2) {
        /* We have a relaxed table - insert separator after first row */
        for (size_t i = 0; i < rows_count; i++) {
            if (rows[i].len < remaining) {
                memcpy(write, rows[i].start, rows[i].len);
                write += rows[i].len;
                remaining -= rows[i].len;
                output_len += rows[i].len;

                *write++ = '\n';
                remaining--;
                output_len++;
            } else {
                /* Need to grow buffer */
                size_t new_capacity = output_capacity * 2;
                char *new_output = realloc(output, new_capacity);
                if (!new_output) {
                    free(output);
                    free(rows);
                    return NULL;
                }
                write = new_output + output_len;
                output = new_output;
                remaining = new_capacity - output_len;
                output_capacity = new_capacity;

                memcpy(write, rows[i].start, rows[i].len);
                write += rows[i].len;
                remaining -= rows[i].len;
                output_len += rows[i].len;

                *write++ = '\n';
                remaining--;
                output_len++;
            }

            /* After first row, insert separator */
            if (i == 0) {
                char *sep = generate_separator_row(rows[0].columns, rows[0].starts_with_pipe);
                if (sep) {
                    size_t sep_len = strlen(sep);
                    if (sep_len < remaining) {
                        memcpy(write, sep, sep_len);
                        write += sep_len;
                        remaining -= sep_len;
                        output_len += sep_len;
                    } else {
                        /* Need to grow buffer */
                        size_t new_capacity = output_capacity * 2;
                        char *new_output = realloc(output, new_capacity);
                        if (!new_output) {
                            free(sep);
                            free(output);
                            free(rows);
                            return NULL;
                        }
                        write = new_output + output_len;
                        output = new_output;
                        remaining = new_capacity - output_len;
                        output_capacity = new_capacity;

                        memcpy(write, sep, sep_len);
                        write += sep_len;
                        remaining -= sep_len;
                        output_len += sep_len;
                    }
                    free(sep);
                }
            }
        }
    } else if (rows_count == 1) {
        /* Only one row - not a table, write it as-is */
        if (rows[0].len < remaining) {
            memcpy(write, rows[0].start, rows[0].len);
            write += rows[0].len;
            remaining -= rows[0].len;
            output_len += rows[0].len;

            /* Only add newline if input ended with one */
            if (input_ends_with_newline && remaining > 0) {
                *write++ = '\n';
                remaining--;
                output_len++;
            }
        } else {
            /* Need to grow buffer */
            size_t new_capacity = output_capacity * 2;
            char *new_output = realloc(output, new_capacity);
            if (!new_output) {
                free(output);
                free(rows);
                return NULL;
            }
            write = new_output + output_len;
            output = new_output;
            remaining = new_capacity - output_len;
            output_capacity = new_capacity;

            memcpy(write, rows[0].start, rows[0].len);
            write += rows[0].len;
            remaining -= rows[0].len;
            output_len += rows[0].len;

            /* Only add newline if input ended with one */
            if (input_ends_with_newline && remaining > 0) {
                *write++ = '\n';
                remaining--;
                output_len++;
            }
        }
    }

    free(rows);

    /* Null-terminate */
    if (remaining > 0) {
        *write = '\0';
    } else {
        /* Need one more byte */
        size_t new_capacity = output_capacity + 1;
        char *new_output = realloc(output, new_capacity);
        if (!new_output) {
            free(output);
            return NULL;
        }
        new_output[output_len] = '\0';
        output = new_output;
    }

    /* Check if we made any changes */
    if (strcmp(text, output) == 0) {
        free(output);
        return NULL;  /* No changes */
    }

    return output;
}

/**
 * Process headerless tables - detect separator rows without header rows and insert dummy headers
 * This allows alignment to be applied to tables that start with a separator row
 */
char *apex_process_headerless_tables(const char *text) {
    if (!text) return NULL;

    size_t text_len = strlen(text);
    if (text_len == 0) return NULL;

    /* Allocate output buffer */
    size_t output_capacity = text_len * 2;
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = text;
    char *write = output;
    size_t remaining = output_capacity;
    size_t output_len = 0;

    /* Track previous line for context */
    bool prev_line_is_table_row = false;

    /* Process line by line */
    while (*read) {
        const char *line_start = read;
        const char *line_end = strchr(read, '\n');
        if (!line_end) {
            line_end = read + strlen(read);
        }

        size_t line_len = line_end - line_start;
        bool has_newline = (*line_end == '\n');

        /* Skip to next line for next iteration */
        const char *next_line_start = has_newline ? line_end + 1 : line_end;

        /* Check if this is a separator or table row */
        bool is_separator = is_separator_row(line_start, line_len);
        bool is_table = is_table_row(line_start, line_len);

        /* Check if we have a separator row that's not part of a valid table:
         * - Previous line is NOT a table row (so separator has no header)
         * - This line IS a separator row
         * - Next line IS a table row (so separator is followed by data)
         */
        if (is_separator && !prev_line_is_table_row) {
            /* Look ahead to see if next non-blank line is a table row */
            const char *look_ahead = next_line_start;
            bool found_table_row = false;

            while (*look_ahead) {
                const char *next_line_end = strchr(look_ahead, '\n');
                if (!next_line_end) {
                    next_line_end = look_ahead + strlen(look_ahead);
                }

                size_t next_line_len = next_line_end - look_ahead;

                if (!is_blank_line(look_ahead, next_line_len)) {
                    /* Found next non-blank line */
                    if (is_table_row(look_ahead, next_line_len)) {
                        found_table_row = true;
                    }
                    break;
                }

                look_ahead = (*next_line_end == '\n') ? next_line_end + 1 : next_line_end;
                if (look_ahead >= text + text_len) break;
            }

            if (found_table_row) {
                /* We need to insert a dummy header row before the separator */
                int num_columns = count_columns(line_start, line_len);
                if (num_columns > 0) {
                    /* Check if separator starts with pipe to match format */
                    size_t start = 0;
                    while (start < line_len && (line_start[start] == ' ' || line_start[start] == '\t')) {
                        start++;
                    }
                    bool starts_with_pipe = (start < line_len && line_start[start] == '|');
                    char *dummy_header = generate_dummy_header_row(num_columns, starts_with_pipe);

                    if (dummy_header) {
                        size_t header_len = strlen(dummy_header);

                        /* Ensure we have space for header + original line */
                        while (output_len + header_len + line_len + 10 > output_capacity) {
                            output_capacity *= 2;
                            char *new_output = realloc(output, output_capacity);
                            if (!new_output) {
                                free(dummy_header);
                                free(output);
                                return NULL;
                            }
                            write = new_output + output_len;
                            output = new_output;
                            remaining = output_capacity - output_len;
                        }

                        /* Write dummy header */
                        memcpy(write, dummy_header, header_len);
                        write += header_len;
                        output_len += header_len;
                        remaining -= header_len;

                        free(dummy_header);
                    }
                }
            }
        }

        /* Copy the current line to output */
        if (line_len + 1 > remaining) {
            output_capacity *= 2;
            char *new_output = realloc(output, output_capacity);
            if (!new_output) {
                free(output);
                return NULL;
            }
            write = new_output + output_len;
            output = new_output;
            remaining = output_capacity - output_len;
        }

        memcpy(write, line_start, line_len);
        write += line_len;
        output_len += line_len;
        remaining -= line_len;

        if (has_newline) {
            *write++ = '\n';
            output_len++;
            remaining--;
        }

        /* Update tracking for next iteration */
        prev_line_is_table_row = is_table && !is_separator;

        /* Move to next line */
        read = next_line_start;
    }

    /* Null terminate */
    if (remaining > 0) {
        *write = '\0';
    } else {
        free(output);
        return NULL;
    }

    /* If nothing changed, return NULL to allow caller to use original */
    if (strcmp(text, output) == 0) {
        free(output);
        return NULL;
    }

    return output;
}

