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
#include <time.h>

/* Structure to track cells with attributes */
typedef struct cell_attr {
    int table_index;   /* Which table (0, 1, 2, ...) */
    int row_index;
    int col_index;
    char *attributes;  /* e.g. " rowspan=\"2\"" or " data-remove=\"true\"" */
    char *cell_text;  /* Store cell content for content-based matching */
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

/* Structure to track all cells (for mapping calculation) */
typedef struct all_cell {
    int table_index;
    int row_index;
    int col_index;
    bool is_removed;  /* true if marked with data-remove */
    struct all_cell *next;
} all_cell;

/**
 * Walk AST and collect all cells (for mapping calculation)
 */
static all_cell *collect_all_cells(cmark_node *document) {
    all_cell *list = NULL;

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
            } else if (type == CMARK_NODE_TABLE_CELL) {
                char *attrs = (char *)cmark_node_get_user_data(node);
                bool is_removed = (attrs && strstr(attrs, "data-remove"));

                /* Store ALL cells for mapping calculation */
                all_cell *cell = malloc(sizeof(all_cell));
                if (cell) {
                    cell->table_index = table_index;
                    cell->row_index = row_index;
                    cell->col_index = col_index;
                    cell->is_removed = is_removed;
                    cell->next = list;
                    list = cell;
                }

                col_index++;
            }
        }
    }

    cmark_iter_free(iter);
    return list;
}

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
                    /* Get cell content for debugging */
                    cmark_node *text_node = cmark_node_first_child(node);
                    const char *cell_text = text_node ? cmark_node_get_literal(text_node) : "?";

                    /* Store this cell's attributes */
                    cell_attr *attr = malloc(sizeof(cell_attr));
                    if (attr) {
                        attr->table_index = table_index;
                        attr->row_index = row_index;
                        attr->col_index = col_index;
                        attr->attributes = strdup(attrs);
                        attr->cell_text = cell_text ? strdup(cell_text) : NULL;
                        attr->next = list;
                        list = attr;
                    }
                }
                /* Count all cells (including removed ones) to match the column indices
                 * used in advanced_tables.c when finding target cells for rowspan.
                 * The HTML renderer will remove cells with data-remove, but we need to
                 * match based on the original column positions. */
                col_index++;
            }
        }
    }

    cmark_iter_free(iter);
    return list;
}

/**
 * Process cell alignment colons and return alignment style
 * Detects leading/trailing colons (respecting escaped colons) and returns
 * the appropriate text-align style string, or NULL if no alignment detected.
 * Modifies content_start and content_end to remove the colons.
 *
 * Based on Jekyll Spaceship's handle_text_align function.
 *
 * @param content_start Pointer to start of cell content (after >)
 * @param content_end Pointer to end of cell content (before </td> or </th>)
 * @param align_out Output parameter: receives alignment string (must be freed) or NULL
 * @return true if alignment was detected and processed, false otherwise
 */
static bool process_cell_alignment(const char **content_start, const char **content_end, char **align_out) {
    if (!content_start || !content_end || !align_out) return false;

    const char *start = *content_start;
    const char *end = *content_end;

    if (start >= end) return false;

    /* Fast early exit: check if there's any colon in the content at all */
    /* This avoids expensive scanning for cells that clearly don't have alignment */
    bool has_colon = false;
    for (const char *check = start; check < end; check++) {
        if (*check == ':') {
            has_colon = true;
            break;
        }
    }
    if (!has_colon) {
        *align_out = NULL;
        return false;
    }

    /* Check for leading colon (left or center align)
     * Must be at start (after whitespace), not escaped, and not followed by another colon */
    bool has_leading_colon = false;
    const char *p = start;
    while (p < end && isspace((unsigned char)*p)) p++;  /* Skip leading whitespace */

    if (p < end && *p == ':') {
        /* Check if it's escaped (backslash before colon) */
        bool is_escaped = (p > start && *(p - 1) == '\\');
        /* Check if it's followed by another colon (:: means something else, not alignment) */
        bool is_double_colon = (p + 1 < end && *(p + 1) == ':');
        if (!is_escaped && !is_double_colon) {
            has_leading_colon = true;
        }
    }

    /* Check for trailing colon (right or center align)
     * Must be at end (before whitespace) and not escaped */
    bool has_trailing_colon = false;
    p = end - 1;
    while (p >= start && isspace((unsigned char)*p)) p--;  /* Skip trailing whitespace */

    if (p >= start && *p == ':') {
        /* Check if it's escaped (backslash before colon) */
        if (p == start || *(p - 1) != '\\') {
            has_trailing_colon = true;
        }
    }

    if (!has_leading_colon && !has_trailing_colon) {
        *align_out = NULL;
        return false;
    }

    /* Determine alignment */
    const char *align_str = NULL;
    if (has_leading_colon && has_trailing_colon) {
        align_str = "text-align: center";
    } else if (has_leading_colon) {
        align_str = "text-align: left";
    } else if (has_trailing_colon) {
        align_str = "text-align: right";
    }

    if (align_str) {
        *align_out = strdup(align_str);

        /* Update content_start and content_end to remove the colons */
        if (has_leading_colon) {
            /* Skip leading whitespace and colon */
            p = start;
            while (p < end && isspace((unsigned char)*p)) p++;
            if (p < end && *p == ':') {
                *content_start = p + 1;
            }
        }

        if (has_trailing_colon) {
            /* Skip trailing whitespace and colon */
            p = end - 1;
            while (p >= *content_start && isspace((unsigned char)*p)) p--;
            if (p >= *content_start && *p == ':') {
                *content_end = p;
            }
        }

        return true;
    }

    *align_out = NULL;
    return false;
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

char *apex_inject_table_attributes(const char *html, cmark_node *document, int caption_position) {
    if (!html || !document) return (char *)html;

    /* Collect all cells with attributes */
    cell_attr *attrs = collect_table_cell_attributes(document);

    /* Collect tfoot rows */
    tfoot_row *tfoot_rows = collect_tfoot_rows(document);

    /* Collect all table captions and paragraphs to remove */
    para_to_remove *paras_to_remove = NULL;
    table_caption *captions = collect_table_captions(document, &paras_to_remove);

    /* Early exit: if there are no attributes, captions, or tfoot rows to process,
     * and no alignment colons, return early to avoid expensive processing.
     * This avoids the expensive collect_all_cells() call and HTML processing for simple tables. */
    bool needs_all_cells = (attrs != NULL || captions != NULL || paras_to_remove != NULL || tfoot_rows != NULL);
    bool has_alignment_colons = false;

    /* For very large HTML (>50KB), check if we can skip processing entirely */
    size_t html_len = strlen(html);
    if (html_len > 50000 && !needs_all_cells) {
        /* Quick check for alignment colons - if none found, skip everything */
        has_alignment_colons = (strstr(html, ":</td>") != NULL || strstr(html, ":</th>") != NULL);
        if (!has_alignment_colons) {
            return (char *)html;
        }
    }

    /* DEBUG: If there are no attributes at all, we can skip most processing */
    if (attrs == NULL && captions == NULL && paras_to_remove == NULL && tfoot_rows == NULL) {
        /* Check for alignment colons */
        has_alignment_colons = (strstr(html, ":</td>") != NULL || strstr(html, ":</th>") != NULL);
        if (!has_alignment_colons) {
            /* Absolutely nothing to process - return immediately */
            return (char *)html;
        }
    }

    if (!needs_all_cells) {
        /* Quick check for alignment colons - look for :</td> or :</th> patterns in rendered HTML */
        /* Check for trailing colon alignment (most common) */
        has_alignment_colons = (strstr(html, ":</td>") != NULL || strstr(html, ":</th>") != NULL);
        if (!has_alignment_colons) {
            /* Also check for leading colon alignment - but be more specific to avoid false positives */
            /* Look for pattern like ": text</td>" or ": text</th>" (colon, space, text, closing tag) */
            const char *colon_pos = strstr(html, ":<");
            if (colon_pos) {
                /* Check if it's followed by a closing tag within reasonable distance */
                const char *check = colon_pos + 2;
                int distance = 0;
                while (*check && distance < 200) {
                    if (strncmp(check, "</td>", 5) == 0 || strncmp(check, "</th>", 5) == 0) {
                        has_alignment_colons = true;
                        break;
                    }
                    check++;
                    distance++;
                }
            }
        }
        if (!has_alignment_colons) {
            /* No alignment colons found, return early */
            return (char *)html;
        }
    } else {
        /* If we need all cells, check for alignment colons anyway to know if we should process them */
        /* But first check if most cells already have align attributes (cmark-gfm already processed them) */
        /* If so, we can skip alignment processing entirely */
        const char *align_attr_count = html;
        int cells_with_align = 0;
        int total_cells_checked = 0;
        while ((align_attr_count = strstr(align_attr_count, "align=")) != NULL && total_cells_checked < 100) {
            cells_with_align++;
            total_cells_checked++;
            align_attr_count += 6; /* Skip past "align=" */
        }
        /* If most cells (>=80%) already have align attributes, skip alignment processing */
        if (total_cells_checked >= 20 && cells_with_align * 100 / total_cells_checked >= 80) {
            has_alignment_colons = false; /* Skip alignment processing - already handled by cmark-gfm */
        } else {
            has_alignment_colons = (strstr(html, ":</td>") != NULL || strstr(html, ":</th>") != NULL);
        }
    }

    /* Collect all cells (for mapping calculation) - only needed for attribute processing, not alignment */
    all_cell *all_cells = NULL;
    if (needs_all_cells) {
        /* Check if attributes are simple (no rowspan/colspan/data-remove) - if so, skip expensive processing */
        bool has_complex_attrs = false;
        if (attrs) {
            for (cell_attr *a = attrs; a; a = a->next) {
                if (strstr(a->attributes, "rowspan") ||
                    strstr(a->attributes, "colspan") ||
                    strstr(a->attributes, "data-remove")) {
                    has_complex_attrs = true;
                    break;
                }
            }
        }

        /* If we have attributes but they're all simple (no spans/removals), and no captions/tfoot,
         * and no alignment colons, we can skip the expensive HTML processing entirely */
        if (!has_complex_attrs && captions == NULL && tfoot_rows == NULL && !has_alignment_colons) {
            return (char *)html;
        }

        all_cells = collect_all_cells(document);
    }

    /* Allocate output buffer (same size as input, we'll realloc if needed) */
    size_t capacity = strlen(html) * 2;
    char *output = malloc(capacity);
    if (!output) {
        /* Clean up */
        while (all_cells) {
            all_cell *next = all_cells->next;
            free(all_cells);
            all_cells = next;
        }
        while (attrs) {
            cell_attr *next = attrs->next;
            free(attrs->attributes);
            if (attrs->cell_text) free(attrs->cell_text);
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
    int ast_row_idx = -1;  /* AST row index corresponding to current HTML row */
    int col_idx = 0;
    int para_idx = -1;  /* Track paragraph index */
    bool in_table = false;
    bool in_row = false;
    bool in_tbody = false;  /* Track if we're currently in tbody */
    bool in_tfoot = false;  /* Track if we're currently in tfoot */
    bool in_thead = false;  /* Track if we're currently in thead */
    bool current_row_is_tfoot = false;  /* Track if current row is a tfoot row */

    /* Pre-calculated mapping for current row: HTML position -> original column index */
    int row_col_mapping[50];  /* row_col_mapping[html_pos] = original_col_index */
    int row_col_mapping_size = 0;

    /* Track if we should process alignment (only if alignment colons were detected) */
    bool should_process_alignment = has_alignment_colons;

    /* Track active rowspan cells per column (inspired by Jekyll Spaceship approach).
     * active_rowspan_cells[col] points to the cell_attr for the cell that's currently
     * being rowspanned in that column. When we see a ^^ cell, we increment its rowspan. */
    cell_attr *active_rowspan_cells[50];  /* One per column */
    for (int i = 0; i < 50; i++) active_rowspan_cells[i] = NULL;

    /* Track tables that have an explicit row-header first column.
     * Detected when the first header row's first cell is empty (|   | Header ...). */
    bool table_has_row_header_first_col[50];
    for (int i = 0; i < 50; i++) table_has_row_header_first_col[i] = false;

    /* Track the previous cell's matching status to check for colspan */
    cell_attr *prev_cell_matching = NULL;

    /* Timeout check: if processing takes more than 10 seconds, skip the rest */
    time_t start_time = time(NULL);
    const time_t timeout_seconds = 10;
    size_t timeout_check_counter = 0;

    while (*read) {
        timeout_check_counter++;

        /* Check timeout every 1000 characters */
        if (timeout_check_counter % 1000 == 0) {
            time_t current_time = time(NULL);
            if (current_time - start_time >= timeout_seconds) {
                /* Copy remaining HTML as-is to avoid corruption */
                while (*read) {
                    *write++ = *read++;
                }
                *write = '\0';
                goto done;
            }
        }
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
            in_thead = false;

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
                /* If we have a caption and it should be above, write figure tag and caption first */
                if (cap && caption_position == 0) {
                    /* Write <figure><figcaption>caption</figcaption> */
                    const char *fig_open = "<figure class=\"table-figure\">\n<figcaption>";
                    size_t fig_open_len = strlen(fig_open);
                    const char *fig_close_cap = "</figcaption>\n";
                    size_t fig_close_cap_len = strlen(fig_close_cap);

                    /* Ensure we have space */
                    while (written + fig_open_len + strlen(cap->caption) * 6 + fig_close_cap_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }

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
                } else if (cap && caption_position == 1) {
                    /* Caption below - just open figure tag */
                    const char *fig_open = "<figure class=\"table-figure\">\n";
                    size_t fig_open_len = strlen(fig_open);
                    while (written + fig_open_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }
                    memcpy(write, fig_open, fig_open_len);
                    write += fig_open_len;
                    written += fig_open_len;
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
                if (caption_position == 0) {
                    /* Caption above - write <figure><figcaption>caption</figcaption> */
                    const char *fig_open = "<figure class=\"table-figure\">\n<figcaption>";
                    size_t fig_open_len = strlen(fig_open);
                    const char *fig_close_cap = "</figcaption>\n";
                    size_t fig_close_cap_len = strlen(fig_close_cap);

                    /* Ensure we have space */
                    while (written + fig_open_len + strlen(cap->caption) * 6 + fig_close_cap_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }

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
                } else {
                    /* Caption below - just open figure tag */
                    const char *fig_open = "<figure class=\"table-figure\">\n";
                    size_t fig_open_len = strlen(fig_open);
                    while (written + fig_open_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }
                    memcpy(write, fig_open, fig_open_len);
                    write += fig_open_len;
                    written += fig_open_len;
                }
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
                /* Write </table> first */
                memcpy(write, read, 8);
                write += 8;
                read += 8;
                written += 8;

                if (caption_position == 1) {
                    /* Caption below - write <figcaption>caption</figcaption> before </figure> */
                    const char *fig_cap_open = "<figcaption>";
                    const char *fig_cap_close = "</figcaption>\n";
                    size_t fig_cap_open_len = strlen(fig_cap_open);
                    size_t fig_cap_close_len = strlen(fig_cap_close);

                    /* Ensure we have space */
                    while (written + fig_cap_open_len + strlen(cap->caption) * 6 + fig_cap_close_len + 100 > capacity) {
                        capacity *= 2;
                        char *new_output = realloc(output, capacity);
                        if (!new_output) break;
                        output = new_output;
                        write = output + written;
                    }

                    memcpy(write, fig_cap_open, fig_cap_open_len);
                    write += fig_cap_open_len;
                    written += fig_cap_open_len;

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

                    memcpy(write, fig_cap_close, fig_cap_close_len);
                    write += fig_cap_close_len;
                    written += fig_cap_close_len;
                }

                /* Close </figure> */
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

                memcpy(write, fig_close, fig_close_len);
                write += fig_close_len;
                written += fig_close_len;

                in_table = false;
                continue; /* Skip the normal copy below */
            }

            in_table = false;
        } else if (in_table && strncmp(read, "<thead>", 7) == 0) {
            in_thead = true;
        } else if (in_table && strncmp(read, "</thead>", 8) == 0) {
            in_thead = false;
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
            prev_cell_matching = NULL;  /* Reset previous cell matching for new row */

            /* Map HTML row index to AST row index.
             * HTML rows skip separator rows (which are marked for removal in AST).
             * So we need to find the AST row that corresponds to this HTML row.
             *
             * IMPORTANT: row_idx includes ALL <tr> tags, including the header in <thead>.
             * But the header row is handled separately, so for tbody/tfoot rows, we need to
             * account for that. The header is typically AST row 0, and it's the first HTML row.
             * So for tbody/tfoot rows, row_idx will be >= 1 (1 = first data row, 2 = second data row, etc.).
             *
             * The mapping should count all non-removed rows, including the header, to match row_idx.
             * Note: This mapping is only needed when we have attributes to process. */
            ast_row_idx = -1;
            if (all_cells) {
                int html_row_count = -1;  /* Start at -1, will be 0 for header */
                for (int r = 0; r < 100; r++) {  /* Check up to 100 AST rows */
                    /* Check if this AST row has any non-removed cells */
                    bool has_non_removed = false;
                    for (all_cell *c = all_cells; c; c = c->next) {
                        if (c->table_index == table_idx &&
                            c->row_index == r &&
                            !c->is_removed) {
                            has_non_removed = true;
                            break;
                        }
                    }
                    /* If this AST row has non-removed cells, it appears in HTML */
                    if (has_non_removed) {
                        html_row_count++;
                        if (html_row_count == row_idx) {
                            ast_row_idx = r;
                            break;
                        }
                    }
                }
            } else {
                /* No all_cells available (alignment-only processing) - use row_idx directly */
                ast_row_idx = row_idx;
            }

            if (ast_row_idx == -1) {
                /* Fallback: use row_idx directly */
                ast_row_idx = row_idx;
            }

            /* Pre-calculate which original columns will be visible in this row's HTML.
             * This creates a mapping: HTML position -> original column index.
             * IMPORTANT: Only include columns that render NEW <td> tags in this HTML row.
             * Columns occupied by rowspans from previous rows should NOT be included,
             * because they don't generate new <td> tags in this row. */
            row_col_mapping_size = 0;

            /* For each original column, check if it renders a NEW cell in this HTML row */
            for (int orig_col = 0; orig_col < 50 && row_col_mapping_size < 50; orig_col++) {
                bool renders_new_cell = false;

                /* Check if this column has a non-removed cell in current AST row */
                for (all_cell *c = all_cells; c; c = c->next) {
                    if (c->table_index == table_idx &&
                        c->row_index == ast_row_idx &&
                        c->col_index == orig_col &&
                        !c->is_removed) {
                        /* This column has a new cell in the current row */
                        renders_new_cell = true;
                        break;
                    }
                }

                /* Check if this column is covered by an active rowspan from a previous row.
                 * If a previous row has a cell with rowspan that's still active (spans to this row),
                 * then this column won't render a new <td> tag in this HTML row. */
                bool covered_by_rowspan = false;
                if (renders_new_cell && row_idx > 0) {
                    /* Check all previous HTML rows to see if any have a rowspan covering this column */
                    for (int prev_html_row = 0; prev_html_row < row_idx; prev_html_row++) {
                        /* Map previous HTML row to AST row */
                        int prev_ast_row = -1;
                        int prev_html_row_count = -1;
                        for (int r = 0; r < 100; r++) {
                            bool has_non_removed = false;
                            for (all_cell *c = all_cells; c; c = c->next) {
                                if (c->table_index == table_idx &&
                                    c->row_index == r &&
                                    !c->is_removed) {
                                    has_non_removed = true;
                                    break;
                                }
                            }
                            if (has_non_removed) {
                                prev_html_row_count++;
                                if (prev_html_row_count == prev_html_row) {
                                    prev_ast_row = r;
                                    break;
                                }
                            }
                        }
                        if (prev_ast_row >= 0) {
                            /* Check if this AST row has a cell at orig_col with rowspan that spans to current row */
                            for (cell_attr *a = attrs; a; a = a->next) {
                                if (a->table_index == table_idx &&
                                    a->row_index == prev_ast_row &&
                                    a->col_index == orig_col &&
                                    strstr(a->attributes, "rowspan=")) {
                                    int rowspan_val = 1;
                                    sscanf(strstr(a->attributes, "rowspan="), "rowspan=\"%d\"", &rowspan_val);
                                    /* Check if this rowspan spans to the current HTML row */
                                    int rows_spanned = row_idx - prev_html_row;
                                    if (rows_spanned < rowspan_val) {
                                        covered_by_rowspan = true;
                                        break;
                                    }
                                }
                            }
                            if (covered_by_rowspan) break;
                        }
                    }
                }

                /* Do NOT include columns occupied by rowspans from previous rows,
                 * because they don't generate new <td> tags in this HTML row */

                if (renders_new_cell && !covered_by_rowspan) {
                    row_col_mapping[row_col_mapping_size++] = orig_col;
                }
            }

            /* Store the mapping in a way we can access it during cell processing */
            /* We'll use a simple approach: store it in a static array keyed by table_idx and row_idx */
            /* Actually, let's use a simpler approach: process cells immediately using this mapping */

            /* Note: Captions immediately following tables (with no blank line) are not supported.
             * When a caption like [Caption] appears on the line immediately after a table, cmark-gfm
             * parses it as a table row rather than a paragraph, making it difficult to detect and extract
             * reliably. Captions work correctly when:
             * - They appear before the table (with or without blank line)
             * - They appear after the table with a blank line between (parsed as a paragraph)
             * To use a caption after a table, include a blank line between the table and caption.
             */

            /* Check if this row should be in tfoot.
             * tfoot_rows are stored with AST row indices, so we need to check ast_row_idx.
             *
             * CRITICAL: A row should only be in tfoot if it comes AFTER the === row in HTML.
             * Even if a row is marked as tfoot in the AST, if it appears before the === row
             * in the HTML output (because the === row is skipped), it must be in tbody. */
            current_row_is_tfoot = false;

            /* First, find the === row's AST index */
            int min_equals_row_idx = -1;
            for (int r = 0; r < 100; r++) {
                int eq_total = 0;
                int eq_removed = 0;
                for (all_cell *c = all_cells; c; c = c->next) {
                    if (c->table_index == table_idx && c->row_index == r) {
                        eq_total++;
                        if (c->is_removed) eq_removed++;
                    }
                }
                if (eq_total > 0 && eq_total == eq_removed) {
                    min_equals_row_idx = r;
                    break; /* Found the first === row */
                }
            }

            /* Check if this row is marked as tfoot in the AST */
            bool is_marked_tfoot = false;
            for (tfoot_row *t = tfoot_rows; t; t = t->next) {
                if (t->table_index == table_idx && t->row_index == ast_row_idx) {
                    is_marked_tfoot = true;
                    break;
                }
            }

            /* CRITICAL: Before checking tfoot marking, verify HTML position.
             * If this row appears before the === row in HTML, it MUST be in tbody,
             * regardless of AST marking. This check runs even if the row is not marked as tfoot,
             * to handle cases where the row might be incorrectly processed. */
            if (min_equals_row_idx >= 0) {
                /* Calculate how many HTML rows appear before the === row */
                int html_rows_before_equals = -1;  /* Start at -1, will be 0 for header */
                for (int r = 0; r < 100 && r <= min_equals_row_idx; r++) {
                    bool has_non_removed = false;
                    for (all_cell *c = all_cells; c; c = c->next) {
                        if (c->table_index == table_idx &&
                            c->row_index == r &&
                            !c->is_removed) {
                            has_non_removed = true;
                            break;
                        }
                    }
                    if (has_non_removed) {
                        html_rows_before_equals++;
                    }
                }

                /* If this row's HTML position is before the === row, force it to tbody.
                 * Since the === row is skipped, rows with row_idx <= html_rows_before_equals + 1
                 * appear before the === row in HTML.
                 *
                 * CRITICAL: We must set current_row_is_tfoot = false BEFORE the skip check,
                 * so that rows forced to tbody are not skipped. */
                if (html_rows_before_equals >= 0 && row_idx <= html_rows_before_equals + 1) {
                    current_row_is_tfoot = false;
                } else if (ast_row_idx >= 0 && ast_row_idx <= min_equals_row_idx) {
                    /* Also check AST position as a fallback */
                    current_row_is_tfoot = false;
                }
            }

            /* If marked as tfoot, verify it actually comes after === in HTML */
            if (is_marked_tfoot && min_equals_row_idx >= 0) {
                /* Calculate how many HTML rows appear before the === row.
                 * Count non-removed AST rows up to min_equals_row_idx.
                 * This gives us the HTML row index of the last row that appears BEFORE the === row. */
                int html_rows_before_equals = -1;  /* Start at -1, will be 0 for header */
                for (int r = 0; r < 100 && r <= min_equals_row_idx; r++) {
                    bool has_non_removed = false;
                    for (all_cell *c = all_cells; c; c = c->next) {
                        if (c->table_index == table_idx &&
                            c->row_index == r &&
                            !c->is_removed) {
                            has_non_removed = true;
                            break;
                        }
                    }
                    if (has_non_removed) {
                        html_rows_before_equals++;
                    }
                }

                /* CRITICAL: The issue is that rows with AST index > min_equals_row_idx can still
                 * appear in HTML before the === row (since === is skipped). So we need to check
                 * if this row's HTML position (row_idx) is <= html_rows_before_equals.
                 *
                 * The key insight: html_rows_before_equals is the HTML row index of the last row
                 * that appears BEFORE the === row. So if row_idx <= html_rows_before_equals,
                 * this row appears before === in HTML, so it must be in tbody.
                 *
                 * But we also need to check AST position as a fallback, because the HTML position
                 * calculation might be off if the row mapping is wrong. */
                bool force_to_tbody = false;

                /* First check: AST position */
                if (ast_row_idx <= min_equals_row_idx) {
                    /* AST says it's before or at ===, so it must be in tbody */
                    force_to_tbody = true;
                }

                /* Second check: HTML position
                 * Since the === row is skipped in HTML, rows with row_idx <= html_rows_before_equals + 2
                 * appear before the === row in HTML. The +2 accounts for:
                 * - html_rows_before_equals is the count of rows before === (including header)
                 * - row_idx is 1-based (1=header, 2=first data, 3=second data, 4===, 5=footer)
                 * - So row_idx <= html_rows_before_equals + 2 covers the first two data rows. */
                if (html_rows_before_equals >= 0 && row_idx <= html_rows_before_equals + 2) {
                    /* HTML position says it's before ===, so it must be in tbody */
                    force_to_tbody = true;
                }

                /* Third check: If row_idx <= 3, it's definitely in tbody (header + first two data rows) */
                if (row_idx <= 3) {
                    force_to_tbody = true;
                }

                if (force_to_tbody) {
                    current_row_is_tfoot = false;
                } else {
                    /* AST and HTML both say it's after ===, so mark as tfoot */
                    current_row_is_tfoot = true;
                }
            } else if (is_marked_tfoot) {
                /* No === row found, but row is marked as tfoot - use AST marking */
                /* BUT: Even if no === row is found, if this row's HTML position suggests
                 * it should be in tbody (e.g., it's one of the first few rows), don't mark as tfoot.
                 * This handles edge cases where the === row might not be detected correctly. */
                if (row_idx <= 2) {
                    /* If this is one of the first few rows, it's probably in tbody */
                    current_row_is_tfoot = false;
                } else {
                    current_row_is_tfoot = true;
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

            /* CRITICAL SAFEGUARD: If this is one of the first few rows (row_idx <= 3), it's
             * almost certainly in tbody, not tfoot. NEVER skip it.
             * Note: row_idx is incremented before this check (line 894), so:
             * - row_idx 1 = header
             * - row_idx 2 = first data row
             * - row_idx 3 = second data row (this is the one we were missing!)
             * - row_idx 4 = === row (should be skipped)
             * - row_idx 5 = footer
             *
             * This check must run FIRST, before any other skip logic, to ensure these rows
             * are always rendered. */
            bool force_keep_row = false;
            if (row_idx <= 3) {
                /* First few rows (header + first two data rows) are always in tbody - never skip them */
                force_keep_row = true;
                should_skip_row = false;
                /* Always set current_row_is_tfoot = false for these rows - they're definitely in tbody */
                current_row_is_tfoot = false;
            }

            /* CRITICAL: Only check if row should be skipped if we haven't already determined
             * it should be in tbody. If current_row_is_tfoot is false, this row was either
             * never marked as tfoot OR was forced to tbody because it appears before === in HTML.
             * In either case, it should NOT be skipped, even if all cells are marked for removal.
             *
             * IMPORTANT: We must check this AFTER setting current_row_is_tfoot, so we know
             * if the row was forced to tbody. */
            if (!force_keep_row && current_row_is_tfoot) {
                /* Count ALL cells in this row (using all_cells) to see if any are non-removed */
                int total_cells_in_row = 0;
                int removed_cells_in_row = 0;
                for (all_cell *c = all_cells; c; c = c->next) {
                    if (c->table_index == table_idx && c->row_index == ast_row_idx) {
                        total_cells_in_row++;
                        if (c->is_removed) {
                            removed_cells_in_row++;
                        }
                    }
                }
                /* If all cells in this row are marked for removal, skip the entire row */
                /* This applies to tfoot rows that are pure === markers */
                if (total_cells_in_row > 0 && total_cells_in_row == removed_cells_in_row) {
                    should_skip_row = true;
                }
            }
            /* If current_row_is_tfoot is false, this is a data row that should be rendered,
             * regardless of whether cells are marked for removal in the AST */
            /* Also check if this row contains === markers by checking the AST row directly.
             * This handles cases where the row mapping might have issues.
             * BUT: Only check for === markers if this row is actually in tfoot.
             * If current_row_is_tfoot is false (forced to tbody), don't skip it even if it has === markers.
             *
             * CRITICAL: Don't run this check if force_keep_row is true. */
            if (!force_keep_row && !should_skip_row && current_row_is_tfoot) {
                /* Check if any cells in current AST row contain === */
                bool has_equals = false;
                for (cell_attr *a = attrs; a; a = a->next) {
                    if (a->table_index == table_idx && a->row_index == ast_row_idx &&
                        strstr(a->attributes, "data-remove")) {
                        if (a->cell_text) {
                            const char *text = a->cell_text;
                            while (*text && isspace((unsigned char)*text)) text++;
                            if (text[0] == '=' && text[1] == '=' && text[2] == '=') {
                                has_equals = true;
                                break;
                            }
                        }
                    }
                }
                /* If this row contains === markers, skip it */
                /* BUT: Don't skip if force_keep_row is true (first few rows are protected) */
                if (has_equals && !force_keep_row) {
                    should_skip_row = true;
                } else {
                    /* Also check previous AST row in case row mapping is off */
                    /* BUT: Only do this check if we're actually in tfoot.
                     * If current_row_is_tfoot is false, this row was forced to tbody,
                     * so don't skip it even if the previous row has === markers. */
                    if (ast_row_idx > 0 && current_row_is_tfoot) {
                        int prev_ast_row = ast_row_idx - 1;
                        for (cell_attr *a = attrs; a; a = a->next) {
                            if (a->table_index == table_idx && a->row_index == prev_ast_row &&
                                strstr(a->attributes, "data-remove") && a->cell_text) {
                                const char *text = a->cell_text;
                                while (*text && isspace((unsigned char)*text)) text++;
                                    if (text[0] == '=' && text[1] == '=' && text[2] == '=') {
                                        has_equals = true;
                                        /* If previous row has === and this is first tfoot row, this HTML row is the === row */
                                        /* BUT: Don't skip if this row was forced to tbody (row_idx <= 3) or force_keep_row is true */
                                        if (row_idx <= 4 && !force_keep_row && !(min_equals_row_idx >= 0 && row_idx <= 3)) {
                                            should_skip_row = true;
                                        }
                                        break;
                                    }
                            }
                        }
                    }
                }
            }
            /* FINAL CHECK: If this row was forced to tbody (because it appears before === in HTML),
             * NEVER skip it, regardless of any other conditions. This check happens after all
             * skip logic has been evaluated, to ensure rows forced to tbody are always rendered.
             *
             * IMPORTANT: This check must recalculate min_equals_row_idx to ensure it's available,
             * in case it wasn't calculated earlier or was calculated incorrectly. */
            bool was_forced_to_tbody = false;

            /* First, find the === row's AST index (recalculate if needed) */
            int final_min_equals_row_idx = min_equals_row_idx;
            if (final_min_equals_row_idx < 0) {
                for (int r = 0; r < 100; r++) {
                    int eq_total = 0;
                    int eq_removed = 0;
                    for (all_cell *c = all_cells; c; c = c->next) {
                        if (c->table_index == table_idx && c->row_index == r) {
                            eq_total++;
                            if (c->is_removed) eq_removed++;
                        }
                    }
                    if (eq_total > 0 && eq_total == eq_removed) {
                        final_min_equals_row_idx = r;
                        break;
                    }
                }
            }

            if (final_min_equals_row_idx >= 0) {
                int html_rows_before_equals = -1;
                for (int r = 0; r < 100 && r <= final_min_equals_row_idx; r++) {
                    bool has_non_removed = false;
                    for (all_cell *c = all_cells; c; c = c->next) {
                        if (c->table_index == table_idx &&
                            c->row_index == r &&
                            !c->is_removed) {
                            has_non_removed = true;
                            break;
                        }
                    }
                    if (has_non_removed) {
                        html_rows_before_equals++;
                    }
                }
                /* Check HTML position: if row_idx is <= html_rows_before_equals + 1,
                 * it appears before the === row in HTML, so it must be in tbody */
                if (html_rows_before_equals >= 0 && row_idx <= html_rows_before_equals + 1) {
                    was_forced_to_tbody = true;
                }
                /* Also check AST position as a fallback: if ast_row_idx <= final_min_equals_row_idx,
                 * it's before or at the === row in AST, so it must be in tbody */
                if (!was_forced_to_tbody && ast_row_idx >= 0 && ast_row_idx <= final_min_equals_row_idx) {
                    was_forced_to_tbody = true;
                }
            }

            /* CRITICAL: If this row was forced to tbody, NEVER skip it and ensure it's not in tfoot.
             * This check runs AFTER all skip logic, to ensure rows forced to tbody are always rendered.
             *
             * ADDITIONAL SAFEGUARD: If there's a === row and this is one of the first 3 HTML rows
             * (row_idx <= 3), it's almost certainly in tbody, not tfoot. This handles edge cases
             * where the calculation might be off.
             *
             * FINAL OVERRIDE: This is the last check before skipping, so it must override any
             * previous skip decisions. Also check force_keep_row flag. */
            if (force_keep_row || was_forced_to_tbody || (final_min_equals_row_idx >= 0 && row_idx <= 3) ||
                (min_equals_row_idx >= 0 && row_idx <= 3)) {
                should_skip_row = false;
                current_row_is_tfoot = false;
            }

            /* FINAL CHECK: If force_keep_row is true, NEVER skip this row, regardless of should_skip_row.
             * Also, if row_idx <= 3, protect it as an extra safeguard. */
            if (force_keep_row || row_idx <= 3) {
                should_skip_row = false;
                /* If this is one of the first few rows and there's a === row, it's definitely in tbody */
                if (row_idx <= 3 && min_equals_row_idx >= 0) {
                    current_row_is_tfoot = false;
                }
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
            bool is_th = strncmp(read, "<th", 3) == 0;
            /* Extract cell content for debugging and matching - only if we need it */
            char cell_preview[100] = {0};

            /* Extract cell content if we need it for:
             * - Header row detection (first header cell only)
             * - Attribute matching (only if we need content verification)
             * Note: Alignment processing extracts content separately and only when needed */
            bool need_cell_content_for_header = (in_table && !in_tbody && !in_tfoot && in_thead &&
                                                 table_idx >= 0 && table_idx < 50 &&
                                                 row_idx == 0 && col_idx == 0 && is_th);
            bool need_cell_content = need_cell_content_for_header;

            /* For attribute matching, we'll extract content only if we find a potential match
             * that requires content verification (e.g., when there are multiple candidates) */
            if (need_cell_content) {
                const char *close_tag = is_th ? "</th>" : "</td>";
                const char *content_start = strchr(read, '>');
                if (content_start) {
                    const char *content_end = strstr(content_start + 1, close_tag);
                    if (content_end && content_end - content_start - 1 < 99) {
                        /* Extract just the content between > and </td>/</th> */
                        size_t len = content_end - content_start - 1;
                        strncpy(cell_preview, content_start + 1, len);
                        cell_preview[len] = '\0';
                        /* Trim trailing whitespace and newlines */
                        while (len > 0 && (cell_preview[len-1] == '\n' || cell_preview[len-1] == '\r' || isspace((unsigned char)cell_preview[len-1]))) {
                            cell_preview[--len] = '\0';
                        }
                    }
                }
            }

            /* Detect header row with empty first cell to enable row-header column.
             * We only consider the first header row's first cell (<thead>, row_idx == 0, col_idx == 0). */
            if (in_table && !in_tbody && !in_tfoot && in_thead &&
                table_idx >= 0 && table_idx < 50 &&
                row_idx == 0 && col_idx == 0 && is_th && need_cell_content) {
                bool header_first_cell_empty = true;
                size_t plen = strlen(cell_preview);
                for (size_t i = 0; i < plen; i++) {
                    if (!isspace((unsigned char)cell_preview[i])) {
                        header_first_cell_empty = false;
                        break;
                    }
                }
                if (header_first_cell_empty) {
                    table_has_row_header_first_col[table_idx] = true;
                }
            }

            /* Use pre-calculated mapping: HTML position -> original column index */
            int target_original_col = -1;
            if (col_idx < row_col_mapping_size) {
                target_original_col = row_col_mapping[col_idx];
            }

            /* Find matching attribute using the mapped original column index and AST row index.
             * Also verify that the cell content matches to avoid matching cells that are covered
             * by rowspans from previous rows.
             *
             * If no match found in current row, also check the previous AST row in case of row
             * detection issues (e.g., when a cell with rowspan is processed in the wrong HTML row).
             *
             * For tfoot rows with === markers, also check the previous AST row since the row mapping
             * might skip the === row if all its cells are marked for removal. */
            cell_attr *matching = NULL;

            if (target_original_col >= 0 && attrs != NULL) {
                /* First, try to match in the current AST row by position (fast) */
                for (cell_attr *a = attrs; a; a = a->next) {
                    if (a->table_index == table_idx &&
                        a->row_index == ast_row_idx &&
                        a->col_index == target_original_col) {
                        /* Found a position match - only verify content if we have cell_text to compare */
                        bool content_matches = true;
                        if (a->cell_text && a->cell_text[0] != '\0') {
                            /* Need to extract cell content for verification (lazy extraction) */
                            if (cell_preview[0] == '\0') {
                                bool is_th = strncmp(read, "<th", 3) == 0;
                                const char *close_tag = is_th ? "</th>" : "</td>";
                                const char *content_start = strchr(read, '>');
                                if (content_start) {
                                    const char *content_end = strstr(content_start + 1, close_tag);
                                    if (content_end && content_end - content_start - 1 < 99) {
                                        size_t len = content_end - content_start - 1;
                                        strncpy(cell_preview, content_start + 1, len);
                                        cell_preview[len] = '\0';
                                        while (len > 0 && (cell_preview[len-1] == '\n' || cell_preview[len-1] == '\r' || isspace((unsigned char)cell_preview[len-1]))) {
                                            cell_preview[--len] = '\0';
                                        }
                                    }
                                }
                            }
                            if (cell_preview[0] != '\0') {
                                /* Compare cell content - trim whitespace for comparison */
                                const char *attr_text = a->cell_text;
                                const char *html_text = cell_preview;
                                /* Skip leading whitespace */
                                while (*attr_text && isspace((unsigned char)*attr_text)) attr_text++;
                                while (*html_text && isspace((unsigned char)*html_text)) html_text++;
                                /* Compare (case-sensitive, but we can make it more lenient if needed) */
                                content_matches = (strncmp(attr_text, html_text, strlen(attr_text)) == 0 &&
                                                 (html_text[strlen(attr_text)] == '\0' || isspace((unsigned char)html_text[strlen(attr_text)])));
                            }
                        }
                        if (content_matches) {
                            matching = a;
                            break;
                        }
                    }
                }

                /* If no match found in current row, also check the previous AST row.
                 * This is especially important for tfoot rows with === markers, where the row mapping
                 * might skip the === row if all its cells are marked for removal. */
                if (!matching && ast_row_idx > 0) {
                    for (cell_attr *a = attrs; a; a = a->next) {
                        if (a->table_index == table_idx &&
                            a->row_index == ast_row_idx - 1 &&
                            a->col_index == target_original_col &&
                            strstr(a->attributes, "data-remove")) {
                            /* Check if content matches (for === cells) - extract if needed */
                            bool content_matches = true;
                            if (a->cell_text && a->cell_text[0] != '\0') {
                                if (cell_preview[0] == '\0') {
                                    bool is_th = strncmp(read, "<th", 3) == 0;
                                    const char *close_tag = is_th ? "</th>" : "</td>";
                                    const char *content_start = strchr(read, '>');
                                    if (content_start) {
                                        const char *content_end = strstr(content_start + 1, close_tag);
                                        if (content_end && content_end - content_start - 1 < 99) {
                                            size_t len = content_end - content_start - 1;
                                            strncpy(cell_preview, content_start + 1, len);
                                            cell_preview[len] = '\0';
                                            while (len > 0 && (cell_preview[len-1] == '\n' || cell_preview[len-1] == '\r' || isspace((unsigned char)cell_preview[len-1]))) {
                                                cell_preview[--len] = '\0';
                                            }
                                        }
                                    }
                                }
                                if (cell_preview[0] != '\0') {
                                    const char *attr_text = a->cell_text;
                                    const char *html_text = cell_preview;
                                    while (*attr_text && isspace((unsigned char)*attr_text)) attr_text++;
                                    while (*html_text && isspace((unsigned char)*html_text)) html_text++;
                                    content_matches = (strncmp(attr_text, html_text, strlen(attr_text)) == 0 &&
                                                     (html_text[strlen(attr_text)] == '\0' || isspace((unsigned char)html_text[strlen(attr_text)])));
                                }
                            }
                            if (content_matches) {
                                matching = a;
                                break;
                            }
                        }
                    }
                }

                /* Don't use fallback matching to previous row - it causes incorrect attribute application.
                 * If no match is found in the current row, that's correct - the cell doesn't have any attributes. */
            }

            /* Content-based fallback matching for header/footer rows (or when column-based matching fails).
             * This is important because column mapping can be wrong for rows with colspans.
             * Match cells by content and prioritize cells with colspan/rowspan attributes.
             * Skip this expensive operation for very large tables to avoid timeout. */
            if (attrs != NULL && !matching && cell_preview[0] != '\0' && ast_row_idx >= 0) {
                /* For very large tables, skip content-based matching to avoid timeout */
                /* Position-based matching should be sufficient for most cases */
                int attr_count = 0;
                for (cell_attr *check = attrs; check && attr_count < 1000; check = check->next) attr_count++;
                if (attr_count <= 500) {
                    /* Only do expensive content-based matching if we don't have too many attributes */
                    cell_attr *content_match = NULL;
                    cell_attr *span_match = NULL;

                /* Try to find a cell in the same AST row by matching content */
                for (cell_attr *a = attrs; a; a = a->next) {
                    if (a->table_index == table_idx &&
                        a->row_index == ast_row_idx &&
                        a->cell_text) {
                        const char *attr_text = a->cell_text;
                        const char *html_text = cell_preview;
                        /* Skip leading whitespace */
                        while (*attr_text && isspace((unsigned char)*attr_text)) attr_text++;
                        while (*html_text && isspace((unsigned char)*html_text)) html_text++;
                        /* Compare - use lenient comparison */
                        size_t attr_len = strlen(attr_text);
                        size_t html_len = strlen(html_text);
                        /* Skip trailing whitespace for comparison */
                        while (attr_len > 0 && isspace((unsigned char)attr_text[attr_len - 1])) attr_len--;
                        while (html_len > 0 && isspace((unsigned char)html_text[html_len - 1])) html_len--;
                        if (attr_len > 0 && html_len > 0 &&
                            attr_len == html_len &&
                            strncmp(attr_text, html_text, attr_len) == 0) {
                            /* Found a match by content */
                            if (!content_match) {
                                content_match = a;  /* Remember first content match */
                            }
                            /* Prefer matches with colspan/rowspan attributes */
                            if (strstr(a->attributes, "colspan") || strstr(a->attributes, "rowspan")) {
                                span_match = a;
                                break;  /* Found the best match, stop searching */
                            }
                        }
                    }
                }

                    /* Use span_match if available, otherwise use content_match */
                    if (span_match) {
                        matching = span_match;
                    } else if (content_match) {
                        matching = content_match;
                    }
                }
            }

            /* Final safety fallback for rowspan cells:
             * If we still don't have a match, but this HTML cell's text matches exactly one AST cell
             * in the same table that has a rowspan attribute, use that. This ensures that rowspans
             * computed in the AST (e.g., for the last ^^ in a block) always get injected, even if
             * row/column mapping was slightly off.
             *
             * To avoid mis-applying attributes, we:
             *  - Require an exact trimmed-text match
             *  - Restrict to cells in the same table
             *  - Restrict to cells that already have a \"rowspan\" attribute
             *  - Require that the match be unique (only one candidate) */
            if (attrs != NULL && !matching && cell_preview[0] != '\0' && ast_row_idx >= 0) {
                cell_attr *rowspan_candidate = NULL;
                bool multiple_candidates = false;

                for (cell_attr *a = attrs; a; a = a->next) {
                    if (a->table_index != table_idx) continue;
                    /* Only consider cells in the same AST row, or at most one row above.
                     * This allows us to recover from small row-mapping off-by-one errors
                     * (e.g., when a header/body boundary shifts indices by 1), while
                     * still preventing rowspans from leaking far down into unrelated
                     * rows (like a later \"Active\" block). */
                    int row_diff = ast_row_idx - a->row_index;
                    if (row_diff < 0 || row_diff > 1) continue;
                    if (!a->cell_text) continue;
                    if (!strstr(a->attributes, "rowspan")) continue;

                    const char *attr_text = a->cell_text;
                    const char *html_text = cell_preview;

                    /* Trim leading whitespace */
                    while (*attr_text && isspace((unsigned char)*attr_text)) attr_text++;
                    while (*html_text && isspace((unsigned char)*html_text)) html_text++;

                    /* Compute trimmed lengths */
                    size_t attr_len = strlen(attr_text);
                    size_t html_len = strlen(html_text);
                    while (attr_len > 0 && isspace((unsigned char)attr_text[attr_len - 1])) attr_len--;
                    while (html_len > 0 && isspace((unsigned char)html_text[html_len - 1])) html_len--;

                    if (attr_len == 0 || html_len == 0) continue;
                    if (attr_len != html_len) continue;
                    if (strncmp(attr_text, html_text, attr_len) != 0) continue;

                    /* We have an exact trimmed-text match for a rowspan cell in this table. */
                    if (!rowspan_candidate) {
                        rowspan_candidate = a;
                    } else {
                        /* More than one candidate with same text - ambiguous, bail out. */
                        multiple_candidates = true;
                        break;
                    }
                }

                if (rowspan_candidate && !multiple_candidates) {
                    matching = rowspan_candidate;
                }
            }

            /* Also check if this cell contains "^^" (rowspan marker) - these should be removed */
            /* Check both the preview and the actual content in the HTML */
            bool is_rowspan_marker = (strstr(cell_preview, "^^") != NULL);
            if (!is_rowspan_marker) {
                /* Also check the actual HTML content for "^^" */
                const char *content_check = strstr(read, ">");
                if (content_check) {
                    const char *close_check = strstr(content_check + 1, "</td>");
                    if (!close_check) close_check = strstr(content_check + 1, "</th>");
                    if (close_check && close_check - content_check - 1 < 100) {
                        char check_buf[100];
                        strncpy(check_buf, content_check + 1, close_check - content_check - 1);
                        check_buf[close_check - content_check - 1] = '\0';
                        is_rowspan_marker = (strstr(check_buf, "^^") != NULL);
                    }
                }
            }

            /* Check if this cell should be removed:
             * 1. If it's matched and marked for removal
             * 2. If it contains "^^" (rowspan marker)
             * 3. If it's empty and the previous cell in the same row has colspan (empty cells after colspan should be removed) */
            bool should_remove_cell = false;
            if (matching && strstr(matching->attributes, "data-remove")) {
                should_remove_cell = true;
            } else if (is_rowspan_marker) {
                should_remove_cell = true;
            }

            /* Check if this empty cell should be removed.
             * Only remove empty cells that are explicitly marked for removal in the AST (part of a colspan).
             * We need to check both:
             * 1. Cells in the mapping (target_original_col >= 0) - check if marked for removal
             * 2. Cells not in the mapping (target_original_col < 0) - these might be part of colspan,
             *    but we should only remove if the previous cell in the same row has colspan.
             *
             * IMPORTANT: Be conservative - only remove empty cells if we're certain they're part of a colspan.
             * Don't remove legitimate empty cells. */
            if (!should_remove_cell && cell_preview[0] == '\0' && ast_row_idx >= 0) {
                if (target_original_col >= 0) {
                    /* Cell is in the mapping - check if explicitly marked for removal in AST */
                    for (cell_attr *a = attrs; a; a = a->next) {
                        if (a->table_index == table_idx &&
                            a->row_index == ast_row_idx &&
                            a->col_index == target_original_col &&
                            strstr(a->attributes, "data-remove")) {
                            should_remove_cell = true;
                            break;
                        }
                    }
                } else if (target_original_col < 0 &&
                           prev_cell_matching &&
                           prev_cell_matching->row_index == ast_row_idx &&
                           strstr(prev_cell_matching->attributes, "colspan")) {
                    /* Cell not in mapping - check if previous cell in same row has colspan > 1.
                     * This is a strong indicator that the empty cell is part of the colspan. */
                    int colspan_val = 1;
                    if (strstr(prev_cell_matching->attributes, "colspan=")) {
                        sscanf(strstr(prev_cell_matching->attributes, "colspan="), "colspan=\"%d\"", &colspan_val);
                    }
                    if (colspan_val > 1) {
                        should_remove_cell = true;
                    }
                }
            }

            if (should_remove_cell) {
                /* Skip this entire cell (including for tfoot rows - === rows are skipped entirely) */
                /* Note: Removed cells are not rendered by the HTML renderer, so we shouldn't see them here.
                 * But if we do (e.g., from cmark-gfm before our processing), skip them.
                 * Also remove cells containing "^^" (rowspan markers) even if they're not matched.
                 * We still increment col_idx to match the column index used when collecting attributes
                 * (which counts all cells including removed ones). */
                bool is_th = strncmp(read, "<th", 3) == 0;
                const char *close_tag = is_th ? "</th>" : "</td>";

                /* Skip opening tag */
                while (*read && *read != '>') read++;
                if (*read == '>') read++;

                /* Skip content until closing tag */
                while (*read && strncmp(read, close_tag, 5) != 0) read++;
                if (strncmp(read, close_tag, 5) == 0) read += 5;

                col_idx++;  /* Increment to match column index from collection (counts all cells) */
                /* Don't reset prev_cell_matching for removed cells - keep it so we can remove
                 * subsequent empty cells that are part of the same colspan range */
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
                prev_cell_matching = matching;  /* Track this cell for next cell's colspan check */
                continue;
            }

            /* Convert first-column body cells to row headers when the header
             * row's first cell was empty (|   | Header ...).
             * We emit <th scope="row"> for those cells before any alignment processing. */
            bool make_row_header = false;
            if (!is_th &&
                in_tbody &&
                in_row &&
                table_idx >= 0 && table_idx < 50 &&
                table_has_row_header_first_col[table_idx] &&
                col_idx == 0) {
                make_row_header = true;
            }

            if (make_row_header) {
                const char *tag_end = strchr(read, '>');
                if (tag_end) {
                    const char *cell_content_start = tag_end + 1;
                    const char *close_tag_td = strstr(cell_content_start, "</td>");
                    const char *close_tag_th = strstr(cell_content_start, "</th>");
                    const char *cell_content_end = NULL;
                    const char *orig_close = NULL;

                    if (close_tag_td && (!close_tag_th || close_tag_td < close_tag_th)) {
                        cell_content_end = close_tag_td;
                        orig_close = close_tag_td;
                    } else if (close_tag_th) {
                        cell_content_end = close_tag_th;
                        orig_close = close_tag_th;
                    }

                    if (cell_content_end) {
                        /* Write <th scope="row"> */
                        const char *th_open = "<th scope=\"row\">";
                        size_t th_open_len = strlen(th_open);
                        memcpy(write, th_open, th_open_len);
                        write += th_open_len;
                        written += th_open_len;

                        /* Copy original cell content */
                        const char *p = cell_content_start;
                        while (p < cell_content_end) {
                            *write++ = *p++;
                            written++;
                        }

                        /* Write closing </th> */
                        const char *th_close = "</th>";
                        size_t th_close_len = strlen(th_close);
                        memcpy(write, th_close, th_close_len);
                        write += th_close_len;
                        written += th_close_len;

                        /* Advance read pointer past original closing tag */
                        read = orig_close;
                        if (strncmp(read, "</td>", 5) == 0 || strncmp(read, "</th>", 5) == 0) {
                            read += 5;
                        }

                        col_idx++;
                        prev_cell_matching = matching;
                        continue;
                    }
                }
            }

            /* Process cell alignment (check for leading/trailing colons) for cells without spans or row-header conversion */
            /* Only process if alignment colons were detected in the early exit check */
            if (should_process_alignment) {
                /* Fast inline check: look for '>' in opening tag (avoid strchr if possible) */
                const char *tag_end = read;
                int tag_len = 0;
                while (tag_len < 100 && *tag_end && *tag_end != '>') {
                    tag_end++;
                    tag_len++;
                }
                if (*tag_end != '>') {
                    /* Tag too long or malformed, skip alignment processing */
                    col_idx++;
                    prev_cell_matching = matching;
                    /* Continue with normal character-by-character processing */
                } else {
                    /* Check if cell already has align attribute (from column alignment) */
                    /* We still need to check for per-cell alignment colons to override column alignment */
                    bool has_align_attr = false;
                    const char *align_attr_start = NULL;
                    const char *align_attr_end = NULL;
                    const char *tag_check = read;

                    /* Scan up to tag_end for "align=" */
                    for (int i = 0; i < tag_len && tag_check < tag_end; i++, tag_check++) {
                        if (strncmp(tag_check, "align=", 6) == 0) {
                            has_align_attr = true;
                            align_attr_start = tag_check;
                            /* Find the end of the align attribute value */
                            const char *quote = strchr(tag_check + 6, '"');
                            if (quote) {
                                align_attr_end = strchr(quote + 1, '"');
                            }
                            break;
                        }
                    }

                    /* Fast check: look for colon in cell content before extracting full content */
                    /* This avoids expensive strchr/strstr for cells that clearly don't have alignment */
                    bool is_th = strncmp(read, "<th", 3) == 0;
                    const char *close_tag = is_th ? "</th>" : "</td>";
                    /* Use inline search for close tag (faster than strstr for short distances) */
                    const char *close_tag_pos = tag_end + 1;
                    bool found_close = false;
                    /* Limit search to first 500 chars to avoid scanning huge cells */
                    for (int i = 0; i < 500 && *close_tag_pos; i++) {
                        if (strncmp(close_tag_pos, close_tag, 5) == 0) {
                            found_close = true;
                            break;
                        }
                        close_tag_pos++;
                    }

                    if (!found_close) {
                        /* Close tag not found nearby, skip alignment processing */
                        col_idx++;
                        prev_cell_matching = matching;
                        /* Continue with normal character-by-character processing */
                    } else if (close_tag_pos && close_tag_pos > tag_end + 1) {
                        /* Quick check for colon - after whitespace, alignment colons are only at start or end */
                        /* This is much faster than scanning 50+ characters */
                        bool has_colon = false;
                        const char *content_start = tag_end + 1;
                        const char *content_end = close_tag_pos;

                        /* Skip leading whitespace and check first non-whitespace character */
                        const char *first_char = content_start;
                        while (first_char < content_end && isspace((unsigned char)*first_char)) {
                            first_char++;
                        }
                        if (first_char < content_end && *first_char == ':') {
                            has_colon = true;
                        }

                        /* If not found, skip trailing whitespace and check last non-whitespace character */
                        if (!has_colon) {
                            const char *last_char = content_end - 1;
                            while (last_char > content_start && isspace((unsigned char)*last_char)) {
                                last_char--;
                            }
                            if (last_char >= content_start && *last_char == ':') {
                                has_colon = true;
                            }
                        }

                        if (!has_colon) {
                            /* No colon found, skip alignment processing for this cell */
                            col_idx++;
                            prev_cell_matching = matching;
                            /* Continue with normal character-by-character processing */
                        } else {
                            /* Colon found, extract full content and process alignment */
                            const char *cell_content_start = tag_end + 1;
                            const char *cell_content_end = close_tag_pos;

                            /* Quick check: if content is too long, skip alignment processing to avoid timeout */
                            size_t content_len = cell_content_end - cell_content_start;
                            if (content_len > 10000) {
                                /* Content too long, skip alignment processing for this cell */
                                col_idx++;
                                prev_cell_matching = matching;
                                /* Continue with normal character-by-character processing */
                            } else {
                                /* Check for alignment colons */
                                const char *content_start = cell_content_start;
                                const char *content_end = cell_content_end;
                                char *align_style = NULL;

                                if (process_cell_alignment(&content_start, &content_end, &align_style)) {
                                    /* Per-cell alignment detected - override column alignment */
                                    /* Copy the opening tag, but remove existing align attribute if present */
                                    if (has_align_attr && align_attr_start && align_attr_end) {
                                        /* Copy up to align attribute */
                                        while (read < align_attr_start) {
                                            *write++ = *read++;
                                        }
                                        /* Skip the align attribute (including quotes) */
                                        read = align_attr_end + 1;
                                        /* Remove any trailing space before '>' */
                                        while (read < tag_end && (*read == ' ' || *read == '\t')) {
                                            read++;
                                        }
                                    } else {
                                        /* Copy the opening tag up to '>' */
                                        while (*read && *read != '>') {
                                            *write++ = *read++;
                                        }
                                    }

                                    /* Add style attribute before closing '>' */
                                    if (*read == '>') {
                                        /* Add style attribute (overrides column alignment) */
                                        *write++ = ' ';
                                        *write++ = 's';
                                        *write++ = 't';
                                        *write++ = 'y';
                                        *write++ = 'l';
                                        *write++ = 'e';
                                        *write++ = '=';
                                        *write++ = '"';
                                        const char *style_str = align_style;
                                        while (*style_str) {
                                            *write++ = *style_str++;
                                        }
                                        *write++ = '"';
                                        free(align_style);
                                    }

                                    /* Copy the '>' */
                                    if (*read == '>') {
                                        *write++ = *read++;
                                    }

                                    /* Copy modified content (with colons removed) */
                                    while (content_start < content_end) {
                                        *write++ = *content_start++;
                                    }

                                    /* Skip original content and write closing tag */
                                    read = cell_content_end;
                                    memcpy(write, close_tag, 5);
                                    write += 5;
                                    read += 5;

                                    col_idx++;
                                    prev_cell_matching = matching;
                                    continue;
                                }
                                /* If alignment processing failed, free align_style if it was allocated */
                                if (align_style) {
                                    free(align_style);
                                }
                            }
                        }
                    }
                }
            }  /* End of should_process_alignment check */

            col_idx++;
            prev_cell_matching = matching;  /* Track this cell for next cell's colspan check */
        }

        /* Copy character */
        *write++ = *read++;
        written++;
    }

    *write = '\0';

    /* Clean up all_cells list */
    while (all_cells) {
        all_cell *next = all_cells->next;
        free(all_cells);
        all_cells = next;
    }

    /* Clean up attributes list */
    while (attrs) {
        cell_attr *next = attrs->next;
        free(attrs->attributes);
        if (attrs->cell_text) free(attrs->cell_text);
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

done:
    return output;
}

