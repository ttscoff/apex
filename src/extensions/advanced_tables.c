/**
 * Advanced Tables Extension for Apex
 * Implementation
 *
 * Postprocessing approach to add table enhancements:
 * - Column spans (empty cells merge with previous)
 * - Row spans (^^ marker merges with cell above)
 * - Table captions (paragraph before/after table with [Caption] format)
 * - Multi-line support (future)
 */

#include "advanced_tables.h"
#include "parser.h"
#include "node.h"
#include "render.h"
#include "table.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

/**
 * Check if a cell should span (is empty or contains <<)
 */
static bool is_colspan_cell(cmark_node *cell) {
    if (!cell) return false;

    cmark_node *child = cmark_node_first_child(cell);
    if (!child) return true; /* Empty cell */

    /* Check if it's text node with just "<< " or whitespace */
    if (cmark_node_get_type(child) == CMARK_NODE_TEXT) {
        const char *text = cmark_node_get_literal(child);
        if (!text) return true;

        /* Trim whitespace */
        while (*text && isspace((unsigned char)*text)) text++;

        if (*text == '\0') return true; /* Just whitespace */

        /* Check for << marker */
        if (text[0] == '<' && text[1] == '<') {
            const char *rest = text + 2;
            while (*rest && isspace((unsigned char)*rest)) rest++;
            if (*rest == '\0') return true; /* Just << */
        }
    }

    return false;
}

/**
 * Check if a cell should rowspan (contains ^^ marker)
 */
static bool is_rowspan_cell(cmark_node *cell) {
    if (!cell) return false;

    cmark_node *child = cmark_node_first_child(cell);
    if (!child || cmark_node_get_type(child) != CMARK_NODE_TEXT) return false;

    const char *text = cmark_node_get_literal(child);
    if (!text) return false;

    /* Trim and check for ^^ */
    while (*text && isspace((unsigned char)*text)) text++;

    if (text[0] == '^' && text[1] == '^') {
        const char *rest = text + 2;
        while (*rest && isspace((unsigned char)*rest)) rest++;
        return (*rest == '\0'); /* Just ^^ */
    }

    return false;
}

/**
 * Check if a row should be in tfoot (contains === markers)
 */
static bool is_tfoot_row(cmark_node *row) {
    if (!row || cmark_node_get_type(row) != CMARK_NODE_TABLE_ROW) return false;

    cmark_node *cell = cmark_node_first_child(row);
    bool has_equals = false;

    while (cell) {
        if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
            cmark_node *text_node = cmark_node_first_child(cell);
            if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                const char *text = cmark_node_get_literal(text_node);
                if (text) {
                    /* Trim whitespace */
                    while (*text && isspace((unsigned char)*text)) text++;
                    /* Check if it's only === (three or more equals) */
                    if (*text == '=' && text[1] == '=' && text[2] == '=') {
                        const char *after = text + 3;
                        while (*after == '=') after++; /* Allow more equals */
                        while (*after && isspace((unsigned char)*after)) after++;
                        if (*after == '\0') {
                            has_equals = true;
                        }
                    }
                }
            }
        }
        cell = cmark_node_next(cell);
    }

    return has_equals;
}

/**
 * Check if a row contains only a caption marker (last row with [Caption])
 * Note: This detection works when captions are parsed as table rows, but currently
 * captions immediately following tables without a blank line are not reliably detected
 * because cmark-gfm parses them as table rows which interferes with detection.
 */
static bool is_caption_row(cmark_node *row) {
    if (!row || cmark_node_get_type(row) != CMARK_NODE_TABLE_ROW) return false;

    /* Check if this row has a cell with [Caption] format */
    /* It might be a single cell, or a cell that spans columns */
    cmark_node *cell = cmark_node_first_child(row);
    int cell_count = 0;
    bool has_caption = false;
    int caption_cell_count = 0;

    while (cell) {
        if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
            cell_count++;
            cmark_node *text_node = cmark_node_first_child(cell);
            if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                const char *text = cmark_node_get_literal(text_node);
                if (text && text[0] == '[') {
                    const char *end = strchr(text + 1, ']');
                    if (end) {
                        const char *after = end + 1;
                        while (*after && isspace((unsigned char)*after)) after++;
                        if (*after == '\0') {
                            has_caption = true;
                            caption_cell_count++;
                        }
                    }
                }
            }
        }
        cell = cmark_node_next(cell);
    }

    /* Caption row: has caption text, and either:
     * - Single cell with caption, OR
     * - All cells contain caption text (shouldn't happen, but be safe) */
    return has_caption && (cell_count == 1 || caption_cell_count == cell_count);
}

/**
 * Add colspan/rowspan attributes to table cells
 * This modifies the AST by setting user_data with HTML attributes
 */
static void process_table_spans(cmark_node *table) {
    if (!table || cmark_node_get_type(table) != CMARK_NODE_TABLE) return;

    /* Walk through table rows - start from first TABLE_ROW node */
    cmark_node *row = cmark_node_first_child(table);
    while (row && cmark_node_get_type(row) != CMARK_NODE_TABLE_ROW) {
        row = cmark_node_next(row); /* Skip non-row nodes */
    }

    cmark_node *prev_row = NULL;
    bool is_first_row = true; /* Track header row */
    bool in_tfoot_section = false; /* Track if we've entered tfoot section */
    int row_index = 0; /* For debugging rowspan behavior */

    /* Track active rowspan cells per column (inspired by Jekyll Spaceship).
     * active_rowspan[col] points to the cell node that's currently being rowspanned in that column.
     * When we see a ^^ cell, we merge it with the active cell for that column.
     * This persists across rows - when a regular cell appears, it becomes the new active cell.
     * Initialize all to NULL at the start of each table. */
    cmark_node *active_rowspan[50] = {NULL};  /* One per column, max 50 columns, persists across rows */

    while (row) {
        if (cmark_node_get_type(row) == CMARK_NODE_TABLE_ROW) {
            /* Process span for header row too - don't skip it */
            if (is_first_row) {
                is_first_row = false;
            }

            /* Check if this row is a tfoot row (contains ===) */
            /* Once we encounter a tfoot row, all subsequent rows are in tfoot */
            if (is_tfoot_row(row)) {
                /* This is the === row itself - mark it as tfoot and set flag */
                in_tfoot_section = true;
                char *existing = (char *)cmark_node_get_user_data(row);
                if (existing) free(existing);
                cmark_node_set_user_data(row, strdup(" data-tfoot=\"true\""));
            } else if (in_tfoot_section) {
                /* We've already encountered the === row, so mark this row as tfoot */
                char *existing = (char *)cmark_node_get_user_data(row);
                if (existing) free(existing);
                cmark_node_set_user_data(row, strdup(" data-tfoot=\"true\""));
            }

            /* If this row is a tfoot row (either === or after ===), process it */
            if (is_tfoot_row(row) || in_tfoot_section) {

                /* Mark === cells for removal (they'll be rendered as empty cells) */
                /* Only do this if this row actually contains === (the === row itself) */
                if (is_tfoot_row(row)) {
                    cmark_node *cell = cmark_node_first_child(row);
                while (cell) {
                    if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
                        cmark_node *text_node = cmark_node_first_child(cell);
                        if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                            const char *text = cmark_node_get_literal(text_node);
                            if (text) {
                                /* Trim whitespace */
                                while (*text && isspace((unsigned char)*text)) text++;
                                /* Check if it's === */
                                if (text[0] == '=' && text[1] == '=' && text[2] == '=') {
                                    const char *after = text + 3;
                                    while (*after == '=') after++; /* Allow more equals */
                                    while (*after && isspace((unsigned char)*after)) after++;
                                    if (*after == '\0') {
                                        /* This is a === cell - mark for removal */
                                        char *cell_attrs = (char *)cmark_node_get_user_data(cell);
                                        if (cell_attrs) free(cell_attrs);
                                        cmark_node_set_user_data(cell, strdup(" data-remove=\"true\""));
                                    }
                                }
                            }
                        }
                    }
                    cell = cmark_node_next(cell);
                }
                }
                /* Don't skip the row - continue processing but don't update prev_row for rowspan */
                /* (tfoot rows shouldn't participate in rowspan calculations) */
                prev_row = row;
                row = cmark_node_next(row);
                continue;
            }

            /* Check if this row only contains '—' cells (separator row or empty row) */
            cmark_node *check_cell = cmark_node_first_child(row);
            bool all_dash = true;
            bool has_cells = false;
            while (check_cell) {
                if (cmark_node_get_type(check_cell) == CMARK_NODE_TABLE_CELL) {
                    has_cells = true;
                    cmark_node *check_text = cmark_node_first_child(check_cell);
                    if (check_text && cmark_node_get_type(check_text) == CMARK_NODE_TEXT) {
                        const char *check_content = cmark_node_get_literal(check_text);
                        if (check_content && strcmp(check_content, "—") != 0) {
                            all_dash = false;
                            break;
                        }
                    } else {
                        /* Empty cell, check if it's really empty */
                        if (cmark_node_first_child(check_cell)) {
                            all_dash = false;
                            break;
                        }
                    }
                }
                check_cell = cmark_node_next(check_cell);
            }
            /* Skip rows that only contain '—' characters (alignment/separator rows) */
            if (has_cells && all_dash) {
                /* Mark the entire row for removal in HTML output */
                cmark_node *dash_cell = cmark_node_first_child(row);
                while (dash_cell) {
                    if (cmark_node_get_type(dash_cell) == CMARK_NODE_TABLE_CELL) {
                        char *existing = (char *)cmark_node_get_user_data(dash_cell);
                        if (existing) free(existing);
                        cmark_node_set_user_data(dash_cell, strdup(" data-remove=\"true\""));
                    }
                    dash_cell = cmark_node_next(dash_cell);
                }
                /* Don't update prev_row when skipping - keep it pointing to the previous valid row */
                row = cmark_node_next(row);
                continue;
            }

            cmark_node *cell = cmark_node_first_child(row);
            cmark_node *prev_cell = NULL;  /* Always reset to NULL at start of each row */
            int col_index = 0;

            /* Note: We don't initialize active_rowspan here because it persists across rows.
             * When we process the first data row, active_rowspan will be NULL for all columns,
             * so ^^ cells will find cells from prev_row and set them as active.
             * Regular cells in subsequent rows will update active_rowspan as they're processed. */

            while (cell) {
                if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
                    /* Check for colspan */
                    bool is_colspan = is_colspan_cell(cell);
                    if (is_colspan) {
                        /* Only process colspan if we have a previous cell in the SAME ROW */
                        if (!prev_cell || cmark_node_parent(prev_cell) != row) {
                            /* No previous cell in same row, can't do colspan.
                             * This happens for:
                             * 1. First cell in a row (prev_cell is NULL)
                             * 2. First non-empty cell after removed cells (prev_cell is from previous row)
                             * In these cases, mark empty cells for removal. */
                            cmark_node *child = cmark_node_first_child(cell);
                            if (child == NULL) {
                                /* Empty cell with no previous cell - mark for removal */
                                char *existing = (char *)cmark_node_get_user_data(cell);
                                if (existing) free(existing);
                                cmark_node_set_user_data(cell, strdup(" data-remove=\"true\""));
                            }
                            prev_cell = cell;
                            col_index++;
                            cell = cmark_node_next(cell);
                            continue;
                        }
                        /* Find the first non-empty cell going backwards (skip cells marked for removal) */
                        cmark_node *target_cell = prev_cell;
                        while (target_cell) {
                            /* Verify target_cell is in the same row */
                            if (cmark_node_parent(target_cell) != row) {
                                break; /* target_cell is not in the same row, stop */
                            }
                            char *target_attrs = (char *)cmark_node_get_user_data(target_cell);
                            /* Skip cells marked for removal */
                            if (!target_attrs || !strstr(target_attrs, "data-remove")) {
                                break; /* Found a real cell */
                            }
                            /* Move to previous cell */
                            cmark_node *prev = cmark_node_previous(target_cell);
                            while (prev && cmark_node_get_type(prev) != CMARK_NODE_TABLE_CELL) {
                                prev = cmark_node_previous(prev);
                            }
                            target_cell = prev;
                        }

                        if (target_cell && cmark_node_parent(target_cell) == row) {
                            /* Merge empty cells with the previous cell to create colspan.
                             * This handles both:
                             * - Consecutive empty cells (like |||) merging together
                             * - Empty cells after a content cell (like | header |||) merging with the content cell
                             *
                             * However, we need to be careful: a single empty cell between two content cells
                             * (like | Absent | | 92.00 |) should NOT merge, as that's just a missing value.
                             *
                             * The distinction: if the target_cell is empty, we're merging consecutive empty cells.
                             * If the target_cell has content, we're merging an empty cell with content (colspan).
                             * Both cases are valid for creating colspan. */

                            /* Check if target_cell is empty (consecutive empty cells) */
                            bool target_is_empty = is_colspan_cell(target_cell);

                            /* Check if the next cell (after current) has content.
                             * If target has content AND next also has content, don't merge (isolated empty cell).
                             * Example: | Absent | | 92.00 | - empty cell between two content cells should NOT merge.
                             * But if target has content and next is empty or end-of-row, merge (empty cells after content).
                             * Example: | header ||| - empty cells after content should merge. */
                            cmark_node *next_cell = cmark_node_next(cell);
                            while (next_cell && cmark_node_get_type(next_cell) != CMARK_NODE_TABLE_CELL) {
                                next_cell = cmark_node_next(next_cell);
                            }
                            bool next_has_content = false;
                            if (next_cell && cmark_node_get_type(next_cell) == CMARK_NODE_TABLE_CELL) {
                                cmark_node *next_child = cmark_node_first_child(next_cell);
                                next_has_content = (next_child != NULL && !is_colspan_cell(next_cell));
                            }

                            /* Merge if:
                             * 1. Target is empty (consecutive empty cells merge together), OR
                             * 2. Target has content AND next is empty/end (empty cells after content merge with content) */
                            bool should_merge = target_is_empty || (!target_is_empty && !next_has_content);

                            if (should_merge) {
                                /* Target cell is empty or has << marker - merge them (colspan) */
                                /* Get or create colspan attribute */
                                char *prev_attrs = (char *)cmark_node_get_user_data(target_cell);
                                int current_colspan = 1;

                                if (prev_attrs && strstr(prev_attrs, "colspan=")) {
                                    sscanf(strstr(prev_attrs, "colspan="), "colspan=\"%d\"", &current_colspan);
                                }

                                /* Increment colspan - append or replace */
                                char new_attrs[256];
                                if (prev_attrs && !strstr(prev_attrs, "colspan=")) {
                                    /* Append to existing attributes */
                                    snprintf(new_attrs, sizeof(new_attrs), "%s colspan=\"%d\"", prev_attrs, current_colspan + 1);
                                } else {
                                    /* Replace or create new */
                                    snprintf(new_attrs, sizeof(new_attrs), " colspan=\"%d\"", current_colspan + 1);
                                }
                                /* Free old user_data before setting new */
                                if (prev_attrs) free(prev_attrs);
                                cmark_node_set_user_data(target_cell, strdup(new_attrs));

                                /* Mark current cell for removal */
                                cmark_node_set_user_data(cell, strdup(" data-remove=\"true\""));
                            }
                        }
                    }
                    /* Check for rowspan */
                    else if (is_rowspan_cell(cell) && col_index < 50) {
                        /* Use Jekyll Spaceship approach: merge with active rowspan cell for this column.
                         * If there's an active rowspan cell, increment its rowspan.
                         * Otherwise, find the cell in the previous row and make it active. */
                        cmark_node *target_cell = active_rowspan[col_index];

                        /* Debug: log rowspan marker and current row/column */
                        cmark_node *rs_child = cmark_node_first_child(cell);
                        const char *rs_text = rs_child && cmark_node_get_type(rs_child) == CMARK_NODE_TEXT
                                              ? cmark_node_get_literal(rs_child)
                                              : NULL;
                        fprintf(stderr, "DEBUG RS: marker at row=%d col=%d text=\"%s\"\n",
                                row_index, col_index, rs_text ? rs_text : "(null)");

                        /* If no active cell, find one in the previous row */
                        if (!target_cell && prev_row) {
                            cmark_node *candidate = cmark_node_first_child(prev_row);
                            int prev_col = 0;

                            /* Find cell at col_index in the previous row */
                            while (candidate) {
                                if (cmark_node_get_type(candidate) == CMARK_NODE_TABLE_CELL) {
                                    if (prev_col == col_index) {
                                        /* Check if this cell is marked for removal */
                                        char *cand_attrs = (char *)cmark_node_get_user_data(candidate);
                                        if (!cand_attrs || !strstr(cand_attrs, "data-remove")) {
                                            /* Found a real cell (not marked for removal) at this column index */
                                            target_cell = candidate;
                                            active_rowspan[col_index] = candidate;  /* Make it active */
                                        }
                                        break;
                                    }
                                    prev_col++;
                                }
                                candidate = cmark_node_next(candidate);
                            }
                        }

                        if (target_cell && cmark_node_get_type(target_cell) == CMARK_NODE_TABLE_CELL) {
                            /* Get or create rowspan attribute */
                            char *prev_attrs = (char *)cmark_node_get_user_data(target_cell);
                            int current_rowspan = 1;

                            if (prev_attrs && strstr(prev_attrs, "rowspan=")) {
                                sscanf(strstr(prev_attrs, "rowspan="), "rowspan=\"%d\"", &current_rowspan);
                            }

                            /* Increment rowspan - append or replace */
                            char new_attrs[256];
                            if (prev_attrs && !strstr(prev_attrs, "rowspan=")) {
                                /* Append to existing attributes */
                                snprintf(new_attrs, sizeof(new_attrs), "%s rowspan=\"%d\"", prev_attrs, current_rowspan + 1);
                            } else {
                                /* Replace or create new */
                                snprintf(new_attrs, sizeof(new_attrs), " rowspan=\"%d\"", current_rowspan + 1);
                            }
                            /* Free old user_data before setting new */
                            if (prev_attrs) free(prev_attrs);
                            cmark_node_set_user_data(target_cell, strdup(new_attrs));

                            /* Debug: log target cell for rowspan */
                            cmark_node *t_child = cmark_node_first_child(target_cell);
                            const char *t_text = t_child && cmark_node_get_type(t_child) == CMARK_NODE_TEXT
                                                 ? cmark_node_get_literal(t_child)
                                                 : NULL;
                            fprintf(stderr, "DEBUG RS: applying rowspan to row=%d col=%d text=\"%s\" new_rowspan=%d\n",
                                    row_index - 1, col_index, t_text ? t_text : "(null)", current_rowspan + 1);
                        }
                        /* Always mark rowspan cell for removal, even if target not found */
                        char *existing = (char *)cmark_node_get_user_data(cell);
                        if (existing) free(existing);
                        cmark_node_set_user_data(cell, strdup(" data-remove=\"true\""));
                    }

                    /* If this is a regular cell (not rowspan), it becomes the new active cell for this column.
                     * This happens AFTER processing rowspan cells, so regular cells replace the previous active cell.
                     * This is the Jekyll Spaceship approach: regular cells become the new active cells.
                     *
                     * IMPORTANT: We set this AFTER processing the cell, so that when we process ^^ cells
                     * in the NEXT row, they will use the correct active cell from THIS row. */
                    if (!is_rowspan_cell(cell)) {
                        char *cell_attrs = (char *)cmark_node_get_user_data(cell);
                        if (!cell_attrs || !strstr(cell_attrs, "data-remove")) {
                            /* This is a regular cell, so it becomes the new active cell for this column */
                            active_rowspan[col_index] = cell;
                        }
                    }

                    prev_cell = cell;
                    col_index++;
                }
                cell = cmark_node_next(cell);
            }

            /* Update prev_row after processing this row */
            prev_row = row;
        }
        row = cmark_node_next(row);
    }
}

/**
 * Check if a paragraph is a table caption ([Caption Text])
 */
static bool is_table_caption(cmark_node *para, char **caption_text) {
    if (!para || cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) return false;

    /* Get first child (should be text) */
    cmark_node *text_node = cmark_node_first_child(para);
    if (!text_node || cmark_node_get_type(text_node) != CMARK_NODE_TEXT) return false;

    const char *text = cmark_node_get_literal(text_node);
    if (!text) return false;

    /* Check for [Caption] format */
    if (text[0] != '[') return false;

    const char *end = strchr(text + 1, ']');
    if (!end) return false;

    /* Make sure rest is whitespace/empty */
    const char *after = end + 1;
    while (*after && isspace((unsigned char)*after)) after++;
    if (*after != '\0') return false;

    /* Extract caption */
    size_t len = end - (text + 1);
    *caption_text = malloc(len + 1);
    if (*caption_text) {
        memcpy(*caption_text, text + 1, len);
        (*caption_text)[len] = '\0';
    }

    return true;
}

/**
 * Add caption to table
 */
static void add_table_caption(cmark_node *table, const char *caption) {
    if (!table || !caption) return;

    /* Store caption in user_data */
    char *attrs = malloc(strlen(caption) + 50);
    if (attrs) {
        snprintf(attrs, strlen(caption) + 50, " data-caption=\"%s\"", caption);

        /* Append to existing user_data if present */
        char *existing = (char *)cmark_node_get_user_data(table);
        if (existing) {
            /* Check if caption is already in existing data */
            if (strstr(existing, "data-caption=")) {
                free(attrs);
                return; /* Caption already present */
            }
            char *combined = malloc(strlen(existing) + strlen(attrs) + 1);
            if (combined) {
                strcpy(combined, existing);
                strcat(combined, attrs);
                free(existing); /* Free old user_data before replacing */
                cmark_node_set_user_data(table, combined);
                free(attrs);
            } else {
                free(attrs);
            }
        } else {
            cmark_node_set_user_data(table, attrs);
        }
    }
}

/**
 * Process tables in document
 */
cmark_node *apex_process_advanced_tables(cmark_node *root) {
    if (!root) return root;

    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;
    cmark_node *cur;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cur = cmark_iter_get_node(iter);

        if (ev_type == CMARK_EVENT_ENTER) {
            cmark_node_type type = cmark_node_get_type(cur);

            /* Process table */
            if (type == CMARK_NODE_TABLE) {
                /* Check for caption before table */
                cmark_node *prev = cmark_node_previous(cur);
                if (prev) {
                    char *prev_data = (char *)cmark_node_get_user_data(prev);
                    /* Skip paragraphs that have already been used as captions */
                    if (!prev_data || !strstr(prev_data, "data-remove")) {
                        char *caption = NULL;
                        if (is_table_caption(prev, &caption)) {
                            add_table_caption(cur, caption);
                            /* Mark caption paragraph for removal so it is not reused */
                            cmark_node_set_user_data(prev, strdup(" data-remove=\"true\""));
                            free(caption);
                        }
                    }
                }

                /* Check for caption after table */
                cmark_node *next = cmark_node_next(cur);
                if (next) {
                    char *next_data = (char *)cmark_node_get_user_data(next);
                    /* Skip paragraphs that have already been used as captions */
                    if (!next_data || !strstr(next_data, "data-remove")) {
                        char *caption = NULL;
                        if (is_table_caption(next, &caption)) {
                            add_table_caption(cur, caption);
                            /* Mark caption paragraph for removal so it is not reused */
                            cmark_node_set_user_data(next, strdup(" data-remove=\"true\""));
                            free(caption);
                        }
                    }
                }

                /* Check for caption rows BEFORE processing spans */
                /* Caption rows should be detected and removed before colspan processing */
                cmark_node *row_check = cmark_node_first_child(cur);
                cmark_node *caption_row = NULL;
                while (row_check) {
                    if (cmark_node_get_type(row_check) == CMARK_NODE_TABLE_ROW && is_caption_row(row_check)) {
                        caption_row = row_check;
                        /* Continue to find the last caption row if there are multiple */
                    }
                    row_check = cmark_node_next(row_check);
                }

                /* If we found a caption row, extract the caption and mark row for removal */
                if (caption_row) {
                    cmark_node *cell = cmark_node_first_child(caption_row);
                    if (cell) {
                        /* Find the cell with caption text (might be first cell, might span) */
                        while (cell) {
                            if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
                                cmark_node *text_node = cmark_node_first_child(cell);
                                if (text_node && cmark_node_get_type(text_node) == CMARK_NODE_TEXT) {
                                    const char *text = cmark_node_get_literal(text_node);
                                    if (text && text[0] == '[') {
                                        const char *end = strchr(text + 1, ']');
                                        if (end) {
                                            size_t caption_len = end - text - 1;
                                            char *caption = malloc(caption_len + 1);
                                            if (caption) {
                                                memcpy(caption, text + 1, caption_len);
                                                caption[caption_len] = '\0';
                                                add_table_caption(cur, caption);
                                                free(caption);
                                                /* Mark the entire row for removal */
                                                char *existing = (char *)cmark_node_get_user_data(caption_row);
                                                if (existing) free(existing);
                                                cmark_node_set_user_data(caption_row, strdup(" data-remove=\"true\""));
                                                break; /* Found caption, done */
                                            }
                                        }
                                    }
                                }
                            }
                            cell = cmark_node_next(cell);
                        }
                    }
                }

                /* Process spans - this also detects tfoot rows */
                process_table_spans(cur);
            }
        }
    }

    cmark_iter_free(iter);
    return root;
}

/**
 * Custom HTML renderer for tables with spans and captions
 */
__attribute__((unused))
static void html_render_table(cmark_syntax_extension *ext,
                              struct cmark_html_renderer *renderer,
                              cmark_node *node,
                              cmark_event_type ev_type,
                              int options) {
    (void)ext;
    (void)options;
    cmark_strbuf *html = renderer->html;
    cmark_node_type type = cmark_node_get_type(node);

    if (ev_type == CMARK_EVENT_ENTER && type == CMARK_NODE_TABLE) {
        /* Check for caption */
        char *user_data = (char *)cmark_node_get_user_data(node);
        if (user_data && strstr(user_data, "data-caption=")) {
            char caption[512];
            if (sscanf(user_data, " data-caption=\"%[^\"]\"", caption) == 1) {
                cmark_strbuf_puts(html, "<figure class=\"table-figure\">\n");
                cmark_strbuf_puts(html, "<figcaption>");
                cmark_strbuf_puts(html, caption);
                cmark_strbuf_puts(html, "</figcaption>\n");
            }
        }
        /* Let default renderer handle the table tag */
        return;
    } else if (ev_type == CMARK_EVENT_EXIT && type == CMARK_NODE_TABLE) {
        char *user_data = (char *)cmark_node_get_user_data(node);
        if (user_data && strstr(user_data, "data-caption=")) {
            cmark_strbuf_puts(html, "</figure>\n");
        }
        return;
    }

    /* Handle ALL table cells to properly handle removal and spans */
    if (type == CMARK_NODE_TABLE_CELL) {
        char *attrs = (char *)cmark_node_get_user_data(node);

        /* Skip cells marked for removal entirely (don't render enter or exit) */
        if (attrs && strstr(attrs, "data-remove")) {
            return; /* Don't render this cell at all */
        }

        /* If this cell has rowspan/colspan, we need custom rendering */
        if (ev_type == CMARK_EVENT_ENTER && attrs &&
            (strstr(attrs, "colspan=") || strstr(attrs, "rowspan="))) {
            /* Determine if header or data cell by checking parent row */
            bool is_header = false;
            cmark_node *row = cmark_node_parent(node);
            if (row) {
                cmark_node *parent = cmark_node_parent(row);
                if (parent && cmark_node_get_type(parent) == CMARK_NODE_TABLE) {
                    /* First row is header in cmark-gfm tables */
                    cmark_node *first_row = cmark_node_first_child(parent);
                    is_header = (first_row == row);
                }
            }

            /* Output opening tag */
            if (is_header) {
                cmark_strbuf_puts(html, "<th");
            } else {
                cmark_strbuf_puts(html, "<td");
            }

            /* Add rowspan/colspan attributes */
            cmark_strbuf_puts(html, attrs);

            cmark_strbuf_putc(html, '>');
            return; /* We've handled the opening tag */
        } else if (ev_type == CMARK_EVENT_EXIT && attrs &&
                   (strstr(attrs, "colspan=") || strstr(attrs, "rowspan="))) {
            /* Closing tag */
            bool is_header = false;
            cmark_node *row = cmark_node_parent(node);
            if (row) {
                cmark_node *parent = cmark_node_parent(row);
                if (parent && cmark_node_get_type(parent) == CMARK_NODE_TABLE) {
                    cmark_node *first_row = cmark_node_first_child(parent);
                    is_header = (first_row == row);
                }
            }

            if (is_header) {
                cmark_strbuf_puts(html, "</th>\n");
            } else {
                cmark_strbuf_puts(html, "</td>\n");
            }
            return; /* We've handled the closing tag */
        }
        /* For normal cells without spans, let default renderer handle them */
    }
}

/**
 * Postprocess function
 */
static cmark_node *postprocess(cmark_syntax_extension *ext,
                               cmark_parser *parser,
                               cmark_node *root) {
    (void)ext;
    (void)parser;
    return apex_process_advanced_tables(root);
}

/**
 * Create advanced tables extension
 */
cmark_syntax_extension *create_advanced_tables_extension(void) {
    cmark_syntax_extension *ext = cmark_syntax_extension_new("advanced_tables");
    if (!ext) return NULL;

    /* Set postprocess callback to add span/caption attributes to AST */
    cmark_syntax_extension_set_postprocess_func(ext, postprocess);

    /* NOTE: We don't use html_render_func here because it conflicts with GFM table renderer.
     * Instead, we do HTML postprocessing in apex.c after rendering. */
    /* cmark_syntax_extension_set_html_render_func(ext, html_render_table); */

    /* Register to handle table and table cell rendering */
    cmark_syntax_extension_set_can_contain_func(ext, NULL);

    return ext;
}

