/**
 * Table HTML Postprocessing
 * Implementation
 *
 * This is a pragmatic solution: we walk the AST to collect cells with
 * rowspan/colspan attributes, then do pattern matching on the HTML to inject them.
 */

#include "table_html_postprocess.h"
#include "cmark-gfm-core-extensions.h"
#include "table.h"  /* For CMARK_NODE_TABLE_ROW, CMARK_NODE_TABLE_CELL */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/* Structure to track cells with attributes */
typedef struct cell_attr {
    int table_index;   /* Which table (0, 1, 2, ...) */
    int row_index;
    int col_index;
    char *attributes;  /* e.g. " rowspan=\"2\"" or " data-remove=\"true\"" */
    struct cell_attr *next;
} cell_attr;

/* Structure to track rows that should be in tfoot */
typedef struct tfoot_row {
    int table_index;
    int row_index;
    struct tfoot_row *next;
} tfoot_row;

/* Structure to track table captions */
typedef struct table_caption {
    int table_index;   /* Which table (0, 1, 2, ...) */
    char *caption;     /* Caption text */
    struct table_caption *next;
} table_caption;

/* Structure to track paragraphs to remove (caption paragraphs) */
typedef struct para_to_remove {
    int para_index;    /* Which paragraph (0, 1, 2, ...) */
    char *text_fingerprint;  /* First 50 chars for matching */
    struct para_to_remove *next;
} para_to_remove;

/**
 * Walk AST and collect cells with attributes
 */
static cell_attr *collect_table_cell_attributes(cmark_node *document) {
    cell_attr *list = NULL;

    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type ev_type;

    int table_index = -1; /* Track which table we're in */
    int row_index = -1;  /* Start at -1, will increment to 0 on first row */
    int col_index = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (ev_type == CMARK_EVENT_ENTER) {
            if (type == CMARK_NODE_TABLE) {
                table_index++;  /* New table */
                row_index = -1;  /* Reset row index for new table */
            } else if (type == CMARK_NODE_TABLE_ROW) {
                row_index++;  /* Increment for each row */
                col_index = 0;

                /* Check if this row is marked as tfoot */
                char *row_attrs = (char *)cmark_node_get_user_data(node);
                if (row_attrs && strstr(row_attrs, "data-tfoot")) {
                    /* Store this row as a tfoot row */
                    tfoot_row *tfoot = malloc(sizeof(tfoot_row));
                    if (tfoot) {
                        tfoot->table_index = table_index;
                        tfoot->row_index = row_index;
                        /* We'll add this to a list later - for now just mark it on the row */
                        /* We'll check for it during HTML processing */
                    }
                }
            } else if (type == CMARK_NODE_TABLE_CELL) {
                char *attrs = (char *)cmark_node_get_user_data(node);
                if (attrs) {
                    /* Store this cell's attributes */
                    cell_attr *attr = malloc(sizeof(cell_attr));
                    if (attr) {
                        attr->table_index = table_index;
                        attr->row_index = row_index;
                        attr->col_index = col_index;
                        attr->attributes = strdup(attrs);
                        attr->next = list;
                        list = attr;
                    }
                }
                col_index++;
            }
        }
    }

    cmark_iter_free(iter);
    return list;
}

/**
 * Get text fingerprint from paragraph node (first 50 chars for matching)
 */
static char *get_para_text_fingerprint(cmark_node *node) {
    if (!node || cmark_node_get_type(node) != CMARK_NODE_PARAGRAPH) return NULL;

    cmark_node *child = cmark_node_first_child(node);
    if (child && cmark_node_get_type(child) == CMARK_NODE_TEXT) {
        const char *text = cmark_node_get_literal(child);
        if (text) {
            size_t len = strlen(text);
            if (len > 50) len = 50;
            char *fingerprint = malloc(len + 1);
            if (fingerprint) {
                memcpy(fingerprint, text, len);
                fingerprint[len] = '\0';
                return fingerprint;
            }
        }
    }
    return NULL;
}

/**
 * Walk AST and collect table captions and paragraphs to remove
 */
static table_caption *collect_table_captions(cmark_node *document, para_to_remove **paras_to_remove) {
    table_caption *list = NULL;
    *paras_to_remove = NULL;

    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type ev_type;

    int table_index = -1; /* Track which table we're in */
    int para_index = -1;  /* Track paragraph index */

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (ev_type == CMARK_EVENT_ENTER) {
            if (type == CMARK_NODE_TABLE) {
                table_index++;  /* New table */

                /* Check for caption in user_data */
                char *user_data = (char *)cmark_node_get_user_data(node);
                bool caption_found = false;
                if (user_data && strstr(user_data, "data-caption=")) {
                    caption_found = true;
                    /* Extract caption text - handle both " data-caption=" and just "data-caption=" */
                    char caption[512];
                    const char *caption_start = strstr(user_data, "data-caption=");
                    if (caption_start) {
                        caption_start += strlen("data-caption=");
                        /* Skip the opening quote */
                        if (*caption_start == '"') {
                            caption_start++;
                            /* Extract until closing quote */
                            int i = 0;
                            while (*caption_start && *caption_start != '"' && i < 511) {
                                caption[i++] = *caption_start++;
                            }
                            caption[i] = '\0';
                            if (i > 0) {
                                table_caption *cap = malloc(sizeof(table_caption));
                                if (cap) {
                                    cap->table_index = table_index;
                                    cap->caption = strdup(caption);
                                    cap->next = list;
                                    list = cap;
                                }
                            }
                        }
                    }
                }
                /* If caption not found in user_data, check for caption paragraph before/after table */
                /* This handles cases where IAL processing replaced user_data */
                if (!caption_found) {
                    /* Check previous node for caption */
                    cmark_node *prev = cmark_node_previous(node);
                    if (prev && cmark_node_get_type(prev) == CMARK_NODE_PARAGRAPH) {
                        /* Check if previous paragraph is a caption */
                        cmark_node *text_node = cmark_node_first_child(prev);
                        if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                            const char *text = cmark_node_get_literal(text_node);
                            if (text && text[0] == '[') {
                                const char *end = strchr(text + 1, ']');
                                if (end) {
                                    const char *after = end + 1;
                                    while (*after && isspace((unsigned char)*after)) after++;
                                    if (*after == '\0') {
                                        /* This is a caption - extract it */
                                        size_t caption_len = end - text - 1;
                                        char *caption = malloc(caption_len + 1);
                                        if (caption) {
                                            memcpy(caption, text + 1, caption_len);
                                            caption[caption_len] = '\0';
                                            table_caption *cap = malloc(sizeof(table_caption));
                                            if (cap) {
                                                cap->table_index = table_index;
                                                cap->caption = caption;
                                                cap->next = list;
                                                list = cap;
                                                caption_found = true;
                                            } else {
                                                free(caption);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    /* Also check next node for caption */
                    if (!caption_found) {
                        cmark_node *next = cmark_node_next(node);
                        if (next && cmark_node_get_type(next) == CMARK_NODE_PARAGRAPH) {
                            cmark_node *text_node = cmark_node_first_child(next);
                            if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                                const char *text = cmark_node_get_literal(text_node);
                                if (text && text[0] == '[') {
                                    const char *end = strchr(text + 1, ']');
                                    if (end) {
                                        const char *after = end + 1;
                                        while (*after && isspace((unsigned char)*after)) after++;
                                        if (*after == '\0') {
                                            /* This is a caption - extract it */
                                            size_t caption_len = end - text - 1;
                                            char *caption = malloc(caption_len + 1);
                                            if (caption) {
                                                memcpy(caption, text + 1, caption_len);
                                                caption[caption_len] = '\0';
                                                table_caption *cap = malloc(sizeof(table_caption));
                                                if (cap) {
                                                    cap->table_index = table_index;
                                                    cap->caption = caption;
                                                    cap->next = list;
                                                    list = cap;
                                                } else {
                                                    free(caption);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } else if (type == CMARK_NODE_PARAGRAPH) {
                para_index++;

                /* Check if this paragraph is marked for removal */
                char *user_data = (char *)cmark_node_get_user_data(node);
                if (user_data && strstr(user_data, "data-remove")) {
                    char *fingerprint = get_para_text_fingerprint(node);
                    if (fingerprint) {
                        para_to_remove *para = malloc(sizeof(para_to_remove));
                        if (para) {
                            para->para_index = para_index;
                            para->text_fingerprint = fingerprint;
                            para->next = *paras_to_remove;
                            *paras_to_remove = para;
                        } else {
                            free(fingerprint);
                        }
                    }
                }
            }
        }
    }

    cmark_iter_free(iter);
    return list;
}

/**
 * Inject attributes into HTML or remove cells
 * Also wraps tables with captions in <figure> tags
 */
/**
 * Collect rows that should be in tfoot sections
 */
static tfoot_row *collect_tfoot_rows(cmark_node *document) {
    tfoot_row *list = NULL;

    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type ev_type;

    int table_index = -1;
    int row_index = -1;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (ev_type == CMARK_EVENT_ENTER) {
            if (type == CMARK_NODE_TABLE) {
                table_index++;
                row_index = -1;
            } else if (type == CMARK_NODE_TABLE_ROW) {
                row_index++;
                char *row_attrs = (char *)cmark_node_get_user_data(node);
                if (row_attrs && strstr(row_attrs, "data-tfoot")) {
                    tfoot_row *tfoot = malloc(sizeof(tfoot_row));
                    if (tfoot) {
                        tfoot->table_index = table_index;
                        tfoot->row_index = row_index;
                        tfoot->next = list;
                        list = tfoot;
                    }
                }
            }
        }
    }

    cmark_iter_free(iter);
    return list;
}

char *apex_inject_table_attributes(const char *html, cmark_node *document) {
    if (!html || !document) return (char *)html;

    /* Collect all cells with attributes */
    cell_attr *attrs = collect_table_cell_attributes(document);

    /* Collect tfoot rows */
    tfoot_row *tfoot_rows = collect_tfoot_rows(document);

    /* Collect all table captions and paragraphs to remove */
    para_to_remove *paras_to_remove = NULL;
    table_caption *captions = collect_table_captions(document, &paras_to_remove);

    /* If nothing to do, return original */
    if (!attrs && !captions && !paras_to_remove) return (char *)html;

    /* Allocate output buffer (same size as input, we'll realloc if needed) */
    size_t capacity = strlen(html) * 2;
    char *output = malloc(capacity);
    if (!output) {
        /* Clean up and return original */
        while (attrs) {
            cell_attr *next = attrs->next;
            free(attrs->attributes);
            free(attrs);
            attrs = next;
        }
        while (captions) {
            table_caption *next = captions->next;
            free(captions->caption);
            free(captions);
            captions = next;
        }
        while (paras_to_remove) {
            para_to_remove *next = paras_to_remove->next;
            free(paras_to_remove->text_fingerprint);
            free(paras_to_remove);
            paras_to_remove = next;
        }
        return (char *)html;
    }

    const char *read = html;
    char *write = output;
    size_t written = 0;
    int table_idx = -1; /* Track which table we're in */
    int row_idx = -1;
    int col_idx = 0;
    int para_idx = -1;  /* Track paragraph index */
    bool in_table = false;
    bool in_row = false;
    bool in_tbody = false;  /* Track if we're currently in tbody */
    bool in_tfoot = false;  /* Track if we're currently in tfoot */
    bool current_row_is_tfoot = false;  /* Track if current row is a tfoot row */

    while (*read) {
        /* Ensure we have space (realloc if needed) */
        if (written + 100 > capacity) {
            capacity *= 2;
            char *new_output = realloc(output, capacity);
            if (!new_output) break;
            output = new_output;
            write = output + written;
        }
        /* Track table structure (BEFORE cell processing so indices are correct) */
        /* Also fix missing space in table tag (e.g., <tableid= -> <table id=) */
        if (strncmp(read, "<table", 6) == 0 && (read[6] == '>' || read[6] == ' ' || (read[6] == 'i' && strncmp(read + 6, "id=", 3) == 0) || isalnum((unsigned char)read[6]))) {
            in_table = true;
            table_idx++; /* New table */
            row_idx = -1; /* Reset for each new table */

            /* Check if this table has a caption (before fixing spacing so we can add figure tag) */
            table_caption *cap = NULL;
            for (table_caption *c = captions; c; c = c->next) {
                if (c->table_index == table_idx) {
                    cap = c;
                    break;
                }
            }

            /* Fix missing space before id or class attributes */
            if (read[6] == 'i' && strncmp(read + 6, "id=", 3) == 0) {
                /* If we have a caption, write figure tag first */
                if (cap) {
                    const char *fig_open = "<figure class=\"table-figure\">\n<figcaption>";
                    size_t fig_open_len = strlen(fig_open);
                    const char *fig_close_cap = "</figcaption>\n";
                    size_t fig_close_cap_len = strlen(fig_close_cap);

                    /* Ensure we have space */
                    while (written + fig_open_len + strlen(cap->caption) + fig_close_cap_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }

                    /* Write <figure><figcaption>caption</figcaption> */
                    memcpy(write, fig_open, fig_open_len);
                    write += fig_open_len;
                    written += fig_open_len;

                    /* Write caption text (escape HTML entities if needed) */
                    const char *cap_text = cap->caption;
                    while (*cap_text) {
                        if (*cap_text == '&') {
                            const char *amp = "&amp;";
                            memcpy(write, amp, 5);
                            write += 5;
                            written += 5;
                        } else if (*cap_text == '<') {
                            const char *lt = "&lt;";
                            memcpy(write, lt, 4);
                            write += 4;
                            written += 4;
                        } else if (*cap_text == '>') {
                            const char *gt = "&gt;";
                            memcpy(write, gt, 4);
                            write += 4;
                            written += 4;
                        } else if (*cap_text == '"') {
                            const char *quot = "&quot;";
                            memcpy(write, quot, 6);
                            write += 6;
                            written += 6;
                        } else {
                            *write++ = *cap_text;
                            written++;
                        }
                        cap_text++;
                    }

                    memcpy(write, fig_close_cap, fig_close_cap_len);
                    write += fig_close_cap_len;
                    written += fig_close_cap_len;
                }

                /* Write "<table " then copy the rest of the tag */
                memcpy(write, read, 6);
                write += 6;
                *write++ = ' ';
                written += 7;
                read += 6;
                /* Copy the rest of the tag until closing > */
                while (*read && *read != '>') {
                    *write++ = *read++;
                    written++;
                }
                if (*read == '>') {
                    *write++ = *read++;
                    written++;
                }
                continue; /* Skip the normal copy below, we've handled it */
            }

            /* Normal table tag (no spacing fix needed) - check for caption */
            if (cap) {
                /* Wrap table in <figure> and add <figcaption> */
                const char *fig_open = "<figure class=\"table-figure\">\n<figcaption>";
                size_t fig_open_len = strlen(fig_open);
                const char *fig_close_cap = "</figcaption>\n";
                size_t fig_close_cap_len = strlen(fig_close_cap);

                /* Ensure we have space */
                while (written + fig_open_len + strlen(cap->caption) + fig_close_cap_len + 100 > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }

                /* Write <figure><figcaption>caption</figcaption> */
                memcpy(write, fig_open, fig_open_len);
                write += fig_open_len;
                written += fig_open_len;

                /* Write caption text (escape HTML entities if needed) */
                const char *cap_text = cap->caption;
                while (*cap_text) {
                    if (*cap_text == '&') {
                        const char *amp = "&amp;";
                        memcpy(write, amp, 5);
                        write += 5;
                        written += 5;
                    } else if (*cap_text == '<') {
                        const char *lt = "&lt;";
                        memcpy(write, lt, 4);
                        write += 4;
                        written += 4;
                    } else if (*cap_text == '>') {
                        const char *gt = "&gt;";
                        memcpy(write, gt, 4);
                        write += 4;
                        written += 4;
                    } else if (*cap_text == '"') {
                        const char *quot = "&quot;";
                        memcpy(write, quot, 6);
                        write += 6;
                        written += 6;
                    } else {
                        *write++ = *cap_text;
                        written++;
                    }
                    cap_text++;
                }

                memcpy(write, fig_close_cap, fig_close_cap_len);
                write += fig_close_cap_len;
                written += fig_close_cap_len;
            }
        } else if (strncmp(read, "</table>", 8) == 0) {
            /* Close tfoot or tbody if still open */
            if (in_tfoot) {
                const char *tfoot_close = "</tfoot>\n";
                size_t tfoot_close_len = strlen(tfoot_close);
                while (written + tfoot_close_len > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }
                memcpy(write, tfoot_close, tfoot_close_len);
                write += tfoot_close_len;
                written += tfoot_close_len;
                in_tfoot = false;
            } else if (in_tbody) {
                const char *tbody_close = "</tbody>\n";
                size_t tbody_close_len = strlen(tbody_close);
                while (written + tbody_close_len > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }
                memcpy(write, tbody_close, tbody_close_len);
                write += tbody_close_len;
                written += tbody_close_len;
                in_tbody = false;
            }

            /* Check if this table had a caption */
            table_caption *cap = NULL;
            for (table_caption *c = captions; c; c = c->next) {
                if (c->table_index == table_idx) {
                    cap = c;
                    break;
                }
            }

            if (cap) {
                /* Close </figure> after table */
                const char *fig_close = "</figure>\n";
                size_t fig_close_len = strlen(fig_close);

                /* Ensure we have space */
                while (written + fig_close_len + 100 > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }

                /* Write </table> first */
                memcpy(write, read, 8);
                write += 8;
                read += 8;
                written += 8;

                /* Then write </figure> */
                memcpy(write, fig_close, fig_close_len);
                write += fig_close_len;
                written += fig_close_len;

                in_table = false;
                continue; /* Skip the normal copy below */
            }

            in_table = false;
        } else if (in_table && strncmp(read, "<tbody>", 7) == 0) {
            /* We're entering tbody - mark it */
            in_tbody = true;
            in_tfoot = false;
        } else if (in_table && strncmp(read, "</tbody>", 8) == 0) {
            /* We're leaving tbody - but only if we haven't already closed it for tfoot */
            if (!in_tfoot) {
                in_tbody = false;
            } else {
                /* We're already in tfoot, so skip this </tbody> tag (we already closed it) */
                read += 8;
                continue;
            }
        } else if (in_table && strncmp(read, "<tr>", 4) == 0) {
            row_idx++;
            col_idx = 0;

            /* Note: Captions immediately following tables (with no blank line) are not supported.
             * When a caption like [Caption] appears on the line immediately after a table, cmark-gfm
             * parses it as a table row rather than a paragraph, making it difficult to detect and extract
             * reliably. Captions work correctly when:
             * - They appear before the table (with or without blank line)
             * - They appear after the table with a blank line between (parsed as a paragraph)
             * To use a caption after a table, include a blank line between the table and caption.
             */

            /* Check if this row should be in tfoot */
            current_row_is_tfoot = false;
            for (tfoot_row *t = tfoot_rows; t; t = t->next) {
                if (t->table_index == table_idx && t->row_index == row_idx) {
                    current_row_is_tfoot = true;
                    break;
                }
            }

            /* If we're in tbody and this is a tfoot row, close tbody and open tfoot */
            if (current_row_is_tfoot && in_tbody && !in_tfoot) {
                /* Close tbody */
                const char *tbody_close = "</tbody>\n";
                size_t tbody_close_len = strlen(tbody_close);
                while (written + tbody_close_len > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }
                memcpy(write, tbody_close, tbody_close_len);
                write += tbody_close_len;
                written += tbody_close_len;
                in_tbody = false;

                /* Open tfoot */
                const char *tfoot_open = "<tfoot>\n";
                size_t tfoot_open_len = strlen(tfoot_open);
                while (written + tfoot_open_len > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) break;
                    output = new_output;
                    write = output + written;
                }
                memcpy(write, tfoot_open, tfoot_open_len);
                write += tfoot_open_len;
                written += tfoot_open_len;
                in_tfoot = true;
            }
            /* Note: Once we're in tfoot, we stay in tfoot - don't reopen tbody */
            /* tfoot rows should be at the end of the table */

            /* Check if this row should be completely removed (all cells marked for removal) */
            /* For tfoot rows that are pure === markers (all cells marked), skip the entire row */
            /* For tfoot rows with actual content, render them normally */
            bool should_skip_row = false;
            /* Count cells in this row that are marked for removal */
            int cells_in_row = 0;
            int cells_to_remove = 0;
            for (cell_attr *a = attrs; a; a = a->next) {
                if (a->table_index == table_idx && a->row_index == row_idx) {
                    cells_in_row++;
                    if (strstr(a->attributes, "data-remove")) {
                        cells_to_remove++;
                    }
                }
            }
            /* If all cells in this row are marked for removal, skip the entire row */
            /* This applies to both regular rows and tfoot rows that are pure === markers */
            if (cells_in_row > 0 && cells_in_row == cells_to_remove) {
                should_skip_row = true;
            }
            if (should_skip_row) {
                /* Skip the opening <tr> tag and everything until </tr> */
                read += 4;
                const char *tr_end = strstr(read, "</tr>");
                if (tr_end) {
                    read = tr_end + 5;
                } else {
                    /* Fallback: skip to next tag */
                    while (*read && strncmp(read, "</tr>", 5) != 0) read++;
                    if (*read) read += 5;
                }
                continue; /* Don't set in_row, skip to next iteration (won't copy <tr>) */
            }
            /* Otherwise, this is a normal row (or tfoot row) - copy <tr> and process cells */
            in_row = true;
        } else if (in_row && strncmp(read, "</tr>", 5) == 0) {
            in_row = false;
        } else if (strncmp(read, "<p>", 3) == 0) {
            /* Check if this paragraph should be removed */
            para_idx++;
            para_to_remove *para_remove = NULL;
            for (para_to_remove *p = paras_to_remove; p; p = p->next) {
                if (p->para_index == para_idx) {
                    para_remove = p;
                    break;
                }
            }

            /* Extract paragraph text to check */
            const char *para_start = read + 3;
            const char *para_end = strstr(para_start, "</p>");
            if (para_end) {
                /* Check if paragraph starts with [ (caption format) */
                const char *text_start = para_start;
                /* Skip any leading whitespace */
                while (*text_start && text_start < para_end && isspace((unsigned char)*text_start)) text_start++;

                if (*text_start == '[' ||
                    (text_start < para_end - 4 && strncmp(text_start, "&lt;", 4) == 0)) {
                    /* This looks like a caption paragraph */

                    /* First check: if we have a fingerprint match from AST */
                    if (para_remove && para_remove->text_fingerprint) {
                        const char *fingerprint_text = para_remove->text_fingerprint;
                        if (fingerprint_text && strlen(fingerprint_text) > 0) {
                            /* Simple substring match - check if fingerprint appears anywhere in paragraph content */
                            if (strstr(para_start, fingerprint_text) != NULL) {
                                /* Skip this entire paragraph */
                                read = para_end + 4; /* Skip past </p> */
                                continue; /* Skip normal copy */
                            }
                        }
                    }

                    /* Second check: if we're near a table/figure that has a caption, remove this paragraph */
                    /* This is a fallback for cases where fingerprint matching fails */
                    /* Check if any table has a caption, and if this paragraph's caption text matches */
                    /* Extract caption text from paragraph (just the text between brackets) */
                    const char *bracket_start = text_start;
                    if (*bracket_start == '[') bracket_start++;
                    else if (text_start < para_end - 4 && strncmp(bracket_start, "&lt;", 4) == 0) bracket_start += 4;
                    const char *bracket_end = strchr(bracket_start, ']');
                    if (!bracket_end || bracket_end >= para_end) {
                        /* Try HTML entity version */
                        const char *gt_entity = strstr(bracket_start, "&gt;");
                        if (gt_entity && gt_entity < para_end) bracket_end = gt_entity;
                    }
                    if (bracket_end && bracket_end < para_end) {
                        size_t caption_len = bracket_end - bracket_start;
                        if (caption_len > 0 && caption_len < 512) {
                            char para_caption[512];
                            memcpy(para_caption, bracket_start, caption_len);
                            para_caption[caption_len] = '\0';
                            /* Compare with all table captions - if any match, remove this paragraph */
                            for (table_caption *cap = captions; cap; cap = cap->next) {
                                if (cap->caption && strcmp(para_caption, cap->caption) == 0) {
                                    /* Match! Remove this paragraph */
                                    read = para_end + 4;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Check for cell opening tags */
        if (in_row && (strncmp(read, "<td", 3) == 0 || strncmp(read, "<th", 3) == 0)) {
            /* Find matching attribute (match table_idx, row_idx, AND col_idx) */
            cell_attr *matching = NULL;
            for (cell_attr *a = attrs; a; a = a->next) {
                if (a->table_index == table_idx && a->row_index == row_idx && a->col_index == col_idx) {
                    matching = a;
                    break;
                }
            }

            if (matching && strstr(matching->attributes, "data-remove")) {
                /* Skip this entire cell (including for tfoot rows - === rows are skipped entirely) */
                bool is_th = strncmp(read, "<th", 3) == 0;
                const char *close_tag = is_th ? "</th>" : "</td>";

                /* Skip opening tag */
                while (*read && *read != '>') read++;
                if (*read == '>') read++;

                /* Skip content until closing tag */
                while (*read && strncmp(read, close_tag, 5) != 0) read++;
                if (strncmp(read, close_tag, 5) == 0) read += 5;

                col_idx++;
                continue;
            } else if (matching && (strstr(matching->attributes, "rowspan") || strstr(matching->attributes, "colspan"))) {
                /* Copy opening tag */
                while (*read && *read != '>') {
                    *write++ = *read++;
                }
                /* Inject attributes before > */
                const char *attr_str = matching->attributes;
                while (*attr_str) {
                    *write++ = *attr_str++;
                }
                /* Copy the > */
                if (*read == '>') {
                    *write++ = *read++;
                }
                col_idx++;
                continue;
            }

            col_idx++;
        }

        /* Copy character */
        *write++ = *read++;
        written++;
    }

    *write = '\0';

    /* Clean up attributes list */
    while (attrs) {
        cell_attr *next = attrs->next;
        free(attrs->attributes);
        free(attrs);
        attrs = next;
    }

    /* Clean up captions list */
    while (captions) {
        table_caption *next = captions->next;
        free(captions->caption);
        free(captions);
        captions = next;
    }

    /* Clean up paragraphs to remove list */
    while (paras_to_remove) {
        para_to_remove *next = paras_to_remove->next;
        free(paras_to_remove->text_fingerprint);
        free(paras_to_remove);
        paras_to_remove = next;
    }

    return output;
}

