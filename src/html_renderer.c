/**
 * Custom HTML Renderer for Apex
 * Implementation
 */

#include "html_renderer.h"
#include "table.h"  /* For CMARK_NODE_TABLE */
#include "extensions/header_ids.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

/**
 * Inject attributes into HTML opening tags
 * This postprocesses the HTML output to add attributes stored in user_data
 */
__attribute__((unused))
static char *inject_attributes_in_html(const char *html, cmark_node *document) {
    if (!html || !document) return html ? strdup(html) : NULL;

    /* For now, we'll use a simpler approach: */
    /* Since we can't easily modify cmark's renderer, we'll inject attributes */
    /* by pattern matching on the HTML output */

    /* This is a simplified implementation */
    /* A full implementation would require forking cmark's HTML renderer */

    return strdup(html);
}

/**
 * Walk AST and collect nodes with attributes
 */
typedef struct attr_node {
    cmark_node *node;
    char *attrs;
    cmark_node_type node_type;
    int element_index;  /* nth element of this type (0=first p, 1=second p, etc.) */
    char *text_fingerprint;  /* First 50 chars of text content for matching */
    struct attr_node *next;
} attr_node;

/* Counters for element indexing */
typedef struct {
    int para_count;
    int heading_count;
    int table_count;
    int blockquote_count;
    int list_count;
    int item_count;
    int code_count;
} element_counters;

/**
 * Get text fingerprint from node (first 50 chars for matching)
 */
static char *get_node_text_fingerprint(cmark_node *node) {
    if (!node) return NULL;

    cmark_node_type type = cmark_node_get_type(node);

    /* For headings, get the literal */
    if (type == CMARK_NODE_HEADING) {
        cmark_node *text = cmark_node_first_child(node);
        if (text && cmark_node_get_type(text) == CMARK_NODE_TEXT) {
            const char *literal = cmark_node_get_literal(text);
            if (literal) {
                size_t len = strlen(literal);
                if (len > 50) len = 50;
                char *fingerprint = malloc(len + 1);
                if (fingerprint) {
                    memcpy(fingerprint, literal, len);
                    fingerprint[len] = '\0';
                    return fingerprint;
                }
            }
        }
    }

    /* For paragraphs, get text from first text node */
    if (type == CMARK_NODE_PARAGRAPH) {
        cmark_node *child = cmark_node_first_child(node);
        if (child && cmark_node_get_type(child) == CMARK_NODE_TEXT) {
            const char *literal = cmark_node_get_literal(child);
            if (literal) {
                size_t len = strlen(literal);
                if (len > 50) len = 50;
                char *fingerprint = malloc(len + 1);
                if (fingerprint) {
                    memcpy(fingerprint, literal, len);
                    fingerprint[len] = '\0';
                    return fingerprint;
                }
            }
        }
    }

    /* For links, use the URL */
    if (type == CMARK_NODE_LINK) {
        const char *url = cmark_node_get_url(node);
        if (url) {
            size_t len = strlen(url);
            if (len > 50) len = 50;
            char *fingerprint = malloc(len + 1);
            if (fingerprint) {
                memcpy(fingerprint, url, len);
                fingerprint[len] = '\0';
                return fingerprint;
            }
        }
    }

    /* For images, use the URL */
    if (type == CMARK_NODE_IMAGE) {
        const char *url = cmark_node_get_url(node);
        if (url) {
            size_t len = strlen(url);
            if (len > 50) len = 50;
            char *fingerprint = malloc(len + 1);
            if (fingerprint) {
                memcpy(fingerprint, url, len);
                fingerprint[len] = '\0';
                return fingerprint;
            }
        }
    }

    return NULL;
}

static void collect_nodes_with_attrs_recursive(cmark_node *node, attr_node **list, element_counters *counters) {
    if (!node) return;

    cmark_node_type type = cmark_node_get_type(node);

    /* Increment counter for this element type */
    int elem_idx = -1;
    if (type == CMARK_NODE_PARAGRAPH) elem_idx = counters->para_count++;
    else if (type >= CMARK_NODE_HEADING && type <= CMARK_NODE_HEADING + 5) elem_idx = counters->heading_count++;
    else if (type == CMARK_NODE_TABLE) elem_idx = counters->table_count++;
    else if (type == CMARK_NODE_BLOCK_QUOTE) elem_idx = counters->blockquote_count++;
    else if (type == CMARK_NODE_LIST) elem_idx = counters->list_count++;
    else if (type == CMARK_NODE_ITEM) elem_idx = counters->item_count++;
    else if (type == CMARK_NODE_CODE_BLOCK) elem_idx = counters->code_count++;
    /* Inline elements need indices too */
    else if (type == CMARK_NODE_LINK) elem_idx = counters->para_count++;
    else if (type == CMARK_NODE_IMAGE) elem_idx = counters->para_count++;

    /* Check if this node has attributes */
    void *user_data = cmark_node_get_user_data(node);
    if (user_data) {
        attr_node *new_node = malloc(sizeof(attr_node));
        if (new_node) {
            new_node->node = node;
            new_node->attrs = (char *)user_data;
            new_node->node_type = type;
            new_node->element_index = elem_idx;
            new_node->text_fingerprint = get_node_text_fingerprint(node);
            new_node->next = *list;
            *list = new_node;
        }

        /* If node is marked for removal, don't traverse children */
        if (strstr((char *)user_data, "data-remove")) {
            return;
        }
    }

    /* Recurse */
    for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
        collect_nodes_with_attrs_recursive(child, list, counters);
    }
}

static void collect_nodes_with_attrs(cmark_node *node, attr_node **list) {
    element_counters counters = {0};
    collect_nodes_with_attrs_recursive(node, list, &counters);

    /* Reverse the list to get document order */
    attr_node *reversed = NULL;
    while (*list) {
        attr_node *next = (*list)->next;
        (*list)->next = reversed;
        reversed = *list;
        *list = next;
    }
    *list = reversed;
}

/**
 * Enhanced HTML rendering with attribute support
 */
char *apex_render_html_with_attributes(cmark_node *document, int options) {
    if (!document) return NULL;

    /* First, render normally */
    char *html = cmark_render_html(document, options, NULL);
    if (!html) return NULL;

    /* Collect all nodes with attributes */
    attr_node *attr_list = NULL;
    collect_nodes_with_attrs(document, &attr_list);

    if (!attr_list) {
        return html; /* No attributes to inject */
    }

    /* Build new HTML with attributes injected */
    size_t html_len = strlen(html);

    /* Calculate needed capacity: original HTML + all attribute strings */
    size_t attrs_size = 0;
    for (attr_node *a = attr_list; a; a = a->next) {
        attrs_size += strlen(a->attrs);
    }
    size_t capacity = html_len + attrs_size + 1024; /* +1KB buffer */
    char *output = malloc(capacity);
    if (!output) {
        /* Clean up attr list */
        while (attr_list) {
            attr_node *next = attr_list->next;
            free(attr_list);
            attr_list = next;
        }
        return html;
    }

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;

    /* Track which attributes we've used */
    int attr_count = 0;
    for (attr_node *a = attr_list; a; a = a->next) attr_count++;
    bool *used = calloc(attr_count + 1, sizeof(bool));

    /* Track element counts in HTML (same as AST walker) */
    element_counters html_counters = {0};

    /* Process HTML, injecting attributes */
    while (*read) {
        /* Check if we're at an opening tag */
        if (*read == '<' && read[1] != '/' && read[1] != '!') {
            const char *tag_start = read + 1;
            const char *tag_name_end = tag_start;

            /* Get tag name */
            while (*tag_name_end && !isspace((unsigned char)*tag_name_end) &&
                   *tag_name_end != '>' && *tag_name_end != '/') {
                tag_name_end++;
            }

            /* Find the end of the tag (> or />) */
            const char *tag_end = tag_name_end;
            while (*tag_end && *tag_end != '>') tag_end++;

            /* Check if this is a block tag or table cell we care about */
            int tag_len = tag_name_end - tag_start;

            /* Determine element type and increment counter */
            cmark_node_type elem_type = 0;
            int elem_idx = -1;

            if (tag_len == 1 && *tag_start == 'p') {
                elem_type = CMARK_NODE_PARAGRAPH;
                elem_idx = html_counters.para_count++;
            } else if (tag_len == 2 && tag_start[0] == 'h' && tag_start[1] >= '1' && tag_start[1] <= '6') {
                elem_type = CMARK_NODE_HEADING;
                elem_idx = html_counters.heading_count++;
            } else if (tag_len == 10 && memcmp(tag_start, "blockquote", 10) == 0) {
                elem_type = CMARK_NODE_BLOCK_QUOTE;
                elem_idx = html_counters.blockquote_count++;
            } else if (tag_len == 5 && memcmp(tag_start, "table", 5) == 0) {
                elem_type = CMARK_NODE_TABLE;
                elem_idx = html_counters.table_count++;
            } else if (tag_len == 2 && (memcmp(tag_start, "ul", 2) == 0 || memcmp(tag_start, "ol", 2) == 0)) {
                elem_type = CMARK_NODE_LIST;
                elem_idx = html_counters.list_count++;
            } else if (tag_len == 2 && memcmp(tag_start, "li", 2) == 0) {
                elem_type = CMARK_NODE_ITEM;
                elem_idx = html_counters.item_count++;
            } else if (tag_len == 3 && memcmp(tag_start, "pre", 3) == 0) {
                elem_type = CMARK_NODE_CODE_BLOCK;
                elem_idx = html_counters.code_count++;
            } else if (tag_len == 1 && *tag_start == 'a') {
                /* Links - inline elements */
                elem_type = CMARK_NODE_LINK;
                elem_idx = html_counters.para_count++;
            } else if (tag_len == 3 && memcmp(tag_start, "img", 3) == 0) {
                /* Images - inline elements */
                elem_type = CMARK_NODE_IMAGE;
                elem_idx = html_counters.para_count++;
            }

            /* Check if we should skip this element (marked for removal) */
            /* We do this BEFORE the main matching to remove elements first */
            bool should_remove = false;
            int removal_idx = -1;
            if (elem_type != 0) {
                int check_idx = 0;
                for (attr_node *a = attr_list; a; a = a->next, check_idx++) {
                    /* Check by element type and index for removal */
                    if (!used[check_idx] &&
                        (a->node_type == elem_type ||
                         (elem_type == CMARK_NODE_HEADING && a->node_type >= CMARK_NODE_HEADING && a->node_type <= CMARK_NODE_HEADING + 5)) &&
                        a->element_index == elem_idx) {
                        if (strstr(a->attrs, "data-remove")) {
                            should_remove = true;
                            removal_idx = check_idx;
                            break;
                        }
                        /* Found matching element but not for removal - stop checking */
                        break;
                    }
                }
            }

            if (should_remove) {
                /* Skip this entire element */
                const char *close_start = read;
                int depth = 1;
                while (*close_start && depth > 0) {
                    if (*close_start == '<') {
                        if (close_start[1] == '/') {
                            /* Closing tag */
                            const char *tag_check = close_start + 2;
                            if (memcmp(tag_check, tag_start, tag_len) == 0 &&
                                (tag_check[tag_len] == '>' || isspace((unsigned char)tag_check[tag_len]))) {
                                depth--;
                                if (depth == 0) {
                                    /* Found matching close tag */
                                    while (*close_start && *close_start != '>') close_start++;
                                    if (*close_start == '>') close_start++;
                                    read = close_start;
                                    if (removal_idx >= 0) used[removal_idx] = true;
                                    goto skip_element;
                                }
                            }
                        } else if (close_start[1] != '!' && close_start[1] != '?') {
                            /* Another opening tag of same type */
                            const char *tag_check = close_start + 1;
                            if (memcmp(tag_check, tag_start, tag_len) == 0 &&
                                (tag_check[tag_len] == '>' || tag_check[tag_len] == ' ')) {
                                depth++;
                            }
                        }
                    }
                    close_start++;
                }
            }

            skip_element:
            if (read != html && *read != '<') {
                continue; /* We skipped an element */
            }

            /* Handle both block and inline elements with attributes */
            if (elem_type != 0) {
                /* Extract fingerprint for matching */
                char html_fingerprint[51] = {0};
                int fp_idx = 0;

                if (elem_type == CMARK_NODE_LINK || elem_type == CMARK_NODE_IMAGE) {
                    /* For links/images, extract the href/src attribute */
                    const char *url_attr = (elem_type == CMARK_NODE_LINK) ? "href=\"" : "src=\"";
                    const char *url_start = strstr(read, url_attr);
                    if (url_start) {
                        url_start += strlen(url_attr);
                        const char *url_end = strchr(url_start, '"');
                        if (url_end) {
                            size_t url_len = url_end - url_start;
                            if (url_len > 50) url_len = 50;
                            memcpy(html_fingerprint, url_start, url_len);
                            html_fingerprint[url_len] = '\0';
                            fp_idx = url_len;
                        }
                    }
                } else {
                    /* For block elements, extract text content */
                    const char *content_start = tag_name_end;
                    while (*content_start && *content_start != '>') content_start++;
                    if (*content_start == '>') content_start++;

                    const char *text_p = content_start;
                    while (*text_p && *text_p != '<' && fp_idx < 50) {
                        html_fingerprint[fp_idx++] = *text_p++;
                    }
                    html_fingerprint[fp_idx] = '\0';
                }

                /* Find matching attribute - try fingerprint first, then index */
                attr_node *matching = NULL;
                int idx = 0;
                for (attr_node *a = attr_list; a; a = a->next, idx++) {
                    if (used[idx]) continue;

                    /* Check type match (including inline elements) */
                    bool type_match = (a->node_type == elem_type ||
                         (elem_type == CMARK_NODE_HEADING && a->node_type >= CMARK_NODE_HEADING && a->node_type <= CMARK_NODE_HEADING + 5));

                    if (!type_match) continue;

                    /* Try fingerprint match first (works for both block and inline) */
                    if (a->text_fingerprint && fp_idx > 0 &&
                        strncmp(a->text_fingerprint, html_fingerprint, 50) == 0) {
                        matching = a;
                        used[idx] = true;
                        break;
                    }

                    /* Fall back to index match if no fingerprint */
                    if (!a->text_fingerprint && a->element_index == elem_idx) {
                        matching = a;
                        used[idx] = true;
                        break;
                    }
                }

                if (matching) {
                    /* Skip internal attributes and table span attributes */
                    /* Table spans are handled by apex_inject_table_attributes() */
                    if (strstr(matching->attrs, "data-remove") ||
                        strstr(matching->attrs, "data-caption") ||
                        strstr(matching->attrs, "colspan=") ||
                        strstr(matching->attrs, "rowspan=")) {
                        /* These are handled elsewhere, don't inject here */
                    } else {
                        /* Find where to inject attributes */
                        const char *inject_point = NULL;

                        if (elem_type == CMARK_NODE_IMAGE || elem_type == CMARK_NODE_LINK) {
                            /* For inline elements (img, a), inject before the closing > or /> */
                            /* Find the closing > for this tag */
                            const char *close_pos = tag_end;
                            bool is_self_closing = false;
                            if (*close_pos == '>') {
                                /* Check if it's a self-closing tag /> */
                                if (close_pos > tag_name_end && close_pos[-1] == '/') {
                                    inject_point = close_pos - 1; /* Before /> */
                                    is_self_closing = true;
                                } else {
                                    inject_point = close_pos; /* Before > */
                                }
                            } else {
                                /* Fallback: after tag name if we can't find > */
                                inject_point = tag_name_end;
                                while (*inject_point && isspace((unsigned char)*inject_point) && *inject_point != '>') inject_point++;
                            }

                            /* Copy up to injection point (but for self-closing tags, don't include the space before /) */
                            size_t prefix_len;
                            if (is_self_closing && inject_point > read && inject_point[-1] == ' ') {
                                /* Don't copy the space before / - we'll add it back after attributes */
                                prefix_len = inject_point - read - 1;
                            } else {
                                prefix_len = inject_point - read;
                            }

                            if (prefix_len < remaining && prefix_len > 0) {
                                memcpy(write, read, prefix_len);
                                write += prefix_len;
                                remaining -= prefix_len;
                            }

                            /* Always add a space before attributes (they need to be separated from existing attributes) */
                            /* The only exception is if inject_point is at > and there's already a space before it */
                            /* But since we're injecting attributes, we always need a space before them */
                            if (remaining > 0) {
                                *write++ = ' ';
                                remaining--;
                            }

                            /* Inject attributes */
                            size_t attr_len = strlen(matching->attrs);
                            if (attr_len <= remaining) {
                                memcpy(write, matching->attrs, attr_len);
                                write += attr_len;
                                remaining -= attr_len;
                            }

                            /* For self-closing tags, ensure space before / */
                            if (is_self_closing && remaining > 0) {
                                *write++ = ' ';
                                remaining--;
                            }

                            read = inject_point;
                        } else {
                            /* For block elements, inject after tag name */
                            inject_point = tag_name_end;
                            while (*inject_point && isspace((unsigned char)*inject_point)) inject_point++;

                            /* Copy up to injection point */
                            size_t prefix_len = inject_point - read;
                            if (prefix_len < remaining) {
                                memcpy(write, read, prefix_len);
                                write += prefix_len;
                                remaining -= prefix_len;
                            }

                            /* Add space before attributes if needed */
                            if (*inject_point != '>') {
                                if (remaining > 0) {
                                    *write++ = ' ';
                                    remaining--;
                                }
                            }

                            /* Inject attributes */
                            size_t attr_len = strlen(matching->attrs);
                            if (attr_len <= remaining) {
                                memcpy(write, matching->attrs, attr_len);
                                write += attr_len;
                                remaining -= attr_len;
                            }

                            read = inject_point;
                        }
                        continue;
                    }
                }
            }
        }

        /* Copy character */
        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            read++;
        }
    }

    free(used);

    *write = '\0';

    /* Clean up */
    while (attr_list) {
        attr_node *next = attr_list->next;
        free(attr_list->text_fingerprint);
        free(attr_list);
        attr_list = next;
    }

    free(html);
    return output;
}

/**
 * Inject header IDs into HTML output
 */
char *apex_inject_header_ids(const char *html, cmark_node *document, bool generate_ids, bool use_anchors, int id_format) {
    if (!html || !document || !generate_ids) {
        return html ? strdup(html) : NULL;
    }

    /* Collect all headers from AST with their IDs */
    typedef struct header_id_map {
        char *text;
        char *id;
        int index;
        struct header_id_map *next;
    } header_id_map;

    header_id_map *header_map = NULL;
    int header_count = 0;

    /* Walk AST to collect headers */
    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type event;
    while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (event == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_HEADING) {
            char *text = apex_extract_heading_text(node);
            char *id = NULL;

            /* Check if ID already exists from IAL or manual ID (stored in user_data) */
            char *user_data = (char *)cmark_node_get_user_data(node);
            if (user_data) {
                /* Look for id="..." in user_data */
                const char *id_attr = strstr(user_data, "id=\"");
                if (id_attr) {
                    const char *id_start = id_attr + 4;
                    const char *id_end = strchr(id_start, '"');
                    if (id_end && id_end > id_start) {
                        size_t id_len = id_end - id_start;
                        id = malloc(id_len + 1);
                        if (id) {
                            memcpy(id, id_start, id_len);
                            id[id_len] = '\0';
                        }
                    }
                }
            }

            /* If no manual/IAL ID, generate one automatically */
            if (!id) {
                id = apex_generate_header_id(text, (apex_id_format_t)id_format);
            }

            header_id_map *entry = malloc(sizeof(header_id_map));
            if (entry) {
                entry->text = text;
                entry->id = id;
                entry->index = header_count++;
                entry->next = header_map;
                header_map = entry;
            } else {
                free(text);
                free(id);
            }
        }
    }
    cmark_iter_free(iter);

    if (!header_map) {
        return strdup(html);
    }

    /* Reverse the list to get document order */
    header_id_map *reversed = NULL;
    while (header_map) {
        header_id_map *next = header_map->next;
        header_map->next = reversed;
        reversed = header_map;
        header_map = next;
    }
    header_map = reversed;

    /* Process HTML to inject IDs */
    size_t html_len = strlen(html);
    size_t capacity = html_len + header_count * 100;  /* Extra space for IDs */
    char *output = malloc(capacity + 1);  /* +1 for null terminator */
    if (!output) {
        /* Clean up */
        while (header_map) {
            header_id_map *next = header_map->next;
            free(header_map->text);
            free(header_map->id);
            free(header_map);
            header_map = next;
        }
        return strdup(html);
    }

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;  /* Reserve 1 byte for null terminator */
    int current_header_idx = 0;

    while (*read) {
        /* Look for header opening tags: <h1>, <h2>, etc. */
        if (*read == '<' && read[1] == 'h' &&
            read[2] >= '1' && read[2] <= '6' &&
            (read[3] == '>' || isspace((unsigned char)read[3]))) {

            /* Find the end of the tag */
            const char *tag_start = read;
            const char *tag_end = read + 3;
            while (*tag_end && *tag_end != '>') tag_end++;
            if (*tag_end != '>') {
                /* Malformed tag, just copy */
                if (remaining > 0) {
                    *write++ = *read++;
                    remaining--;
                } else {
                    read++;
                }
                continue;
            }

            /* Check if ID already exists in the tag */
            bool has_id = false;
            const char *id_attr = strstr(tag_start, "id=");
            if (id_attr && id_attr < tag_end) {
                has_id = true;
            }

            /* Get the header ID if we need to inject one */
            header_id_map *header = NULL;
            if (!has_id && current_header_idx < header_count) {
                header = header_map;
                for (int i = 0; i < current_header_idx && header; i++) {
                    header = header->next;
                }
            }

            if (use_anchors && header && header->id) {
                /* For anchor tags: copy the entire header tag, then inject anchor after '>' */
                size_t tag_len = tag_end - tag_start + 1;  /* Include '>' */
                if (tag_len <= remaining) {
                    memcpy(write, tag_start, tag_len);
                    write += tag_len;
                    remaining -= tag_len;
                }
                read = tag_end + 1;

                /* Inject anchor tag after the header tag */
                char anchor_tag[512];
                snprintf(anchor_tag, sizeof(anchor_tag),
                        "<a href=\"#%s\" aria-hidden=\"true\" class=\"anchor\" id=\"%s\"></a>",
                        header->id, header->id);
                size_t anchor_len = strlen(anchor_tag);
                if (anchor_len <= remaining) {
                    memcpy(write, anchor_tag, anchor_len);
                    write += anchor_len;
                    remaining -= anchor_len;
                }
                current_header_idx++;
            } else if (!use_anchors && header && header->id) {
                /* For header IDs: copy tag up to '>', inject id attribute, then copy '>' */
                const char *after_tag_name = tag_start + 3;
                while (*after_tag_name && *after_tag_name != '>' && !isspace((unsigned char)*after_tag_name)) {
                    after_tag_name++;
                }

                /* Copy '<hN' */
                size_t tag_prefix_len = after_tag_name - tag_start;
                if (tag_prefix_len <= remaining) {
                    memcpy(write, tag_start, tag_prefix_len);
                    write += tag_prefix_len;
                    remaining -= tag_prefix_len;
                }
                read = after_tag_name;

                /* Copy any existing attributes before injecting id */
                const char *attr_start = read;
                while (*read && *read != '>') {
                    read++;
                }

                /* If there are existing attributes, copy them */
                if (read > attr_start) {
                    size_t attr_len = read - attr_start;
                    if (attr_len <= remaining) {
                        memcpy(write, attr_start, attr_len);
                        write += attr_len;
                        remaining -= attr_len;
                    }
                }

                /* Add space before id attribute if needed */
                if ((read > attr_start || *read == '>') && remaining > 0) {
                    *write++ = ' ';
                    remaining--;
                }

                /* Inject id="..." */
                char id_attr_str[512];
                snprintf(id_attr_str, sizeof(id_attr_str), "id=\"%s\"", header->id);
                size_t id_len = strlen(id_attr_str);
                if (id_len <= remaining) {
                    memcpy(write, id_attr_str, id_len);
                    write += id_len;
                    remaining -= id_len;
                }

                /* Copy closing '>' */
                if (*read == '>') {
                    if (remaining > 0) {
                        *write++ = *read++;
                        remaining--;
                    } else {
                        read++;
                    }
                }
                current_header_idx++;
            } else {
                /* No ID to inject, just copy the tag */
                size_t tag_len = tag_end - tag_start + 1;
                if (tag_len <= remaining) {
                    memcpy(write, tag_start, tag_len);
                    write += tag_len;
                    remaining -= tag_len;
                }
                read = tag_end + 1;
                if (!has_id) {
                    current_header_idx++;
                }
            }
        } else {
            /* Copy character */
            if (remaining > 0) {
                *write++ = *read++;
                remaining--;
            } else {
                read++;
            }
        }
    }

    /* Ensure we have space for null terminator */
    if (remaining < 1) {
        size_t used = write - output;
        size_t new_capacity = (used + 1) * 2;
        char *new_output = realloc(output, new_capacity + 1);
        if (new_output) {
            output = new_output;
            write = output + used;
            remaining = new_capacity - used;
        }
    }
    *write = '\0';

    /* Clean up */
    while (header_map) {
        header_id_map *next = header_map->next;
        free(header_map->text);
        free(header_map->id);
        free(header_map);
        header_map = next;
    }

    return output;
}

/**
 * Clean up HTML tag spacing
 * - Compresses multiple spaces in tags to single spaces
 * - Removes spaces before closing >
 */
char *apex_clean_html_tag_spacing(const char *html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char *output = malloc(len + 1);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    bool in_tag = false;
    bool last_was_space = false;

    while (*read) {
        if (*read == '<' && (read[1] != '/' && read[1] != '!' && read[1] != '?')) {
            /* Entering a tag */
            in_tag = true;
            last_was_space = false;
            *write++ = *read++;
        } else if (*read == '>') {
            /* Exiting a tag - skip any trailing space */
            if (last_was_space && write > output && write[-1] == ' ') {
                write--;
            }
            in_tag = false;
            last_was_space = false;
            *write++ = *read++;
        } else if (in_tag && isspace((unsigned char)*read)) {
            /* Space inside tag */
            if (!last_was_space) {
                /* First space - keep it */
                *write++ = ' ';
                last_was_space = true;
            }
            /* Skip additional spaces */
            read++;
        } else {
            /* Regular character */
            last_was_space = false;
            *write++ = *read++;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Collapse newlines and surrounding whitespace between adjacent tags.
 *
 * Example:
 *   "</table>\n\n<figure>" -> "</table><figure>"
 *
 * Strategy:
 * - Whenever we see a '>' character, look ahead over any combination of
 *   spaces/tabs/newlines/carriage returns.
 * - If the next non-whitespace character is '<' and there was at least one
 *   newline in the skipped range, we drop all of that whitespace so the tags
 *   become adjacent.
 * - Otherwise, we leave the whitespace untouched.
 *
 * This keeps text content (including code/pre blocks) intact, while
 * compacting vertical spacing between block-level HTML elements in
 * non-pretty mode.
 */
char *apex_collapse_intertag_newlines(const char *html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char *output = malloc(len + 1);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;

    while (*read) {
        if (*read == '>') {
            /* Copy the '>' */
            *write++ = *read++;

            /* Look ahead over whitespace between this tag and the next content */
            const char *look = read;
            int newline_count = 0;
            while (*look == ' ' || *look == '\t' || *look == '\n' || *look == '\r') {
                if (*look == '\n' || *look == '\r') {
                    newline_count++;
                }
                look++;
            }

            if (newline_count > 0 && *look == '<') {
                /* We are between two tags. Compress any run of newlines here so that
                 * \n{2,} becomes exactly \n\n (one blank line), and a single newline
                 * stays a single newline.
                 */
                int to_emit = (newline_count >= 2) ? 2 : 1;
                for (int i = 0; i < to_emit; i++) {
                    *write++ = '\n';
                }
                read = look;
                continue;
            }
            /* Otherwise, fall through and let the normal loop copy whitespace */
        }

        *write++ = *read++;
    }

    *write = '\0';
    return output;
}

/**
 * Check if a table cell contains only em dashes and whitespace
 */
static bool cell_contains_only_dashes(const char *cell_start, const char *cell_end) {
    const char *p = cell_start;
    bool has_content = false;

    while (p < cell_end) {
        /* Check for em dash (—) U+2014: 0xE2 0x80 0x94 */
        if ((unsigned char)*p == 0xE2 && p + 2 < cell_end &&
            (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x94) {
            has_content = true;
            p += 3;
        } else if (*p == ':' || *p == '-' || *p == '|') {
            /* Colons, dashes, and pipes are OK in separator rows (for alignment: |:----|:---:|----:|) */
            if (*p == '-' || *p == ':') {
                has_content = true;  /* Dashes and colons count as content */
            }
            p++;
        } else if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            /* Whitespace is OK */
            p++;
        } else if (*p == '<') {
            /* HTML tags are OK (opening/closing tags) */
            if (strncmp(p, "<td", 3) == 0 || strncmp(p, "</td>", 5) == 0 ||
                strncmp(p, "<th", 3) == 0 || strncmp(p, "</th>", 5) == 0) {
                /* Skip the tag */
                if (strncmp(p, "</td>", 5) == 0 || strncmp(p, "</th>", 5) == 0) {
                    p += 5;
                } else {
                    /* Skip to > */
                    while (p < cell_end && *p != '>') p++;
                    if (p < cell_end) p++;
                }
            } else {
                /* Other content - not a separator cell */
                return false;
            }
        } else {
            /* Non-dash, non-whitespace, non-tag content */
            return false;
        }
    }

    return has_content;  /* Must have at least one em dash */
}

/**
 * Convert thead to tbody for relaxed tables ONLY
 * Converts <thead><tr><th>...</th></tr></thead> to <tbody><tr><td>...</td></tr></tbody>
 * ONLY for tables that were created from relaxed table input (no separator rows in original)
 *
 * Strategy: Check if there's a separator row (with em dashes) in the tbody.
 * - If there IS a separator row in tbody → regular table (keep thead)
 * - If there is NO separator row in tbody → relaxed table (convert thead to tbody)
 *
 * This works because:
 * - Regular tables: separator row is in tbody (between header and data)
 * - Relaxed tables: separator row was inserted by preprocessing, but we removed it
 *   (or it was converted to em dashes and removed)
 */
char *apex_convert_relaxed_table_headers(const char *html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char *output = malloc(len * 2);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = len * 2;

    while (*read) {
        /* Expand buffer if needed */
        if (remaining < 100) {
            size_t written = write - output;
            size_t new_capacity = (write - output) * 2;
            if (new_capacity < written + 100) {
                new_capacity = written + 1000;  /* Ensure we have enough space */
            }
            char *old_output = output;  /* Save old pointer */
            char *new_output = realloc(output, new_capacity);
            if (!new_output) {
                /* realloc failed - original pointer is still valid, free it */
                free(old_output);
                return NULL;
            }
            /* realloc succeeded - update pointers */
            output = new_output;
            write = output + written;
            remaining = new_capacity - written;
        }

        /* Check for <thead> */
        if (strncmp(read, "<thead>", 7) == 0) {
            const char *after_thead = read + 7;
            const char *thead_end = strstr(after_thead, "</thead>");
            const char *tbody_start = strstr(after_thead, "<tbody>");

            if (thead_end) {
                /* Check if thead contains only empty cells (dummy headers from headerless tables) */
                bool all_cells_empty = true;
                bool found_any_th = false;

                /* Search for all <th> or <th ...> tags in thead */
                const char *search = after_thead;
                while (search < thead_end) {
                    /* Check for <th> without attributes */
                    if (strncmp(search, "<th>", 4) == 0) {
                        found_any_th = true;
                        const char *th_end = strstr(search, "</th>");
                        if (!th_end || th_end >= thead_end) {
                            all_cells_empty = false;
                            break;
                        }
                        /* Check if content between > and </th> is empty */
                        const char *content_start = search + 4; /* After <th>, which is > */
                        if (content_start < th_end) {
                            while (content_start < th_end) {
                                if (!isspace((unsigned char)*content_start)) {
                                    all_cells_empty = false;
                                    break;
                                }
                                content_start++;
                            }
                        }
                        /* If content_start >= th_end, tags are adjacent (empty) - OK */
                        if (!all_cells_empty) break;
                        search = th_end + 5; /* Move past </th> */
                    }
                    /* Check for <th with attributes */
                    else if (strncmp(search, "<th", 3) == 0 &&
                             (search[3] == ' ' || search[3] == '\t' || search[3] == '>')) {
                        found_any_th = true;
                        /* Find the closing > of opening tag */
                        const char *tag_end = strchr(search, '>');
                        if (!tag_end || tag_end >= thead_end) {
                            all_cells_empty = false;
                            break;
                        }
                        const char *th_end = strstr(tag_end, "</th>");
                        if (!th_end || th_end >= thead_end) {
                            all_cells_empty = false;
                            break;
                        }
                        /* Check content between > and </th> */
                        const char *content_start = tag_end + 1;
                        if (content_start < th_end) {
                            while (content_start < th_end) {
                                if (!isspace((unsigned char)*content_start)) {
                                    all_cells_empty = false;
                                    break;
                                }
                                content_start++;
                            }
                        }
                        /* If content_start >= th_end, tags are adjacent (empty) - OK */
                        if (!all_cells_empty) break;
                        search = th_end + 5; /* Move past </th> */
                    } else {
                        search++;
                    }
                }

                /* If we found th cells and they're all empty, remove the entire thead */
                if (found_any_th && all_cells_empty) {
                    read = thead_end + 8; /* Skip <thead>...</thead> */
                    continue;
                }
            }

            if (thead_end && tbody_start && thead_end < tbody_start) {
                /* Check if tbody contains a separator row (row with only em dashes) */
                bool has_separator_row = false;
                const char *tbody_end = strstr(tbody_start, "</tbody>");
                const char *table_end = strstr(tbody_start, "</table>");

                if (tbody_end && (!table_end || tbody_end < table_end)) {
                    /* Look for rows with only em dashes in tbody */
                    const char *search = tbody_start;
                    while (search < tbody_end) {
                        if (strncmp(search, "<tr>", 4) == 0) {
                            const char *tr_end = strstr(search, "</tr>");
                            if (tr_end && tr_end < tbody_end) {
                                /* Check if this row contains only em dashes */
                                bool row_is_separator = true;
                                const char *cell_start = search + 4;
                                while (cell_start < tr_end) {
                                    if (strncmp(cell_start, "<td", 3) == 0 || strncmp(cell_start, "<th", 3) == 0) {
                                        const char *tag_end = strstr(cell_start, ">");
                                        if (!tag_end) break;
                                        tag_end++;

                                        const char *cell_end = NULL;
                                        if (strncmp(cell_start, "<td", 3) == 0) {
                                            cell_end = strstr(tag_end, "</td>");
                                            if (cell_end) cell_end += 5;
                                        } else {
                                            cell_end = strstr(tag_end, "</th>");
                                            if (cell_end) cell_end += 5;
                                        }

                                        if (cell_end && cell_end <= tr_end) {
                                            if (!cell_contains_only_dashes(tag_end, cell_end - 5)) {
                                                row_is_separator = false;
                                                break;
                                            }
                                            cell_start = cell_end;
                                        } else {
                                            break;
                                        }
                                    } else {
                                        cell_start++;
                                    }
                                }

                                if (row_is_separator) {
                                    has_separator_row = true;
                                    break;
                                }

                                search = tr_end + 5;
                            } else {
                                break;
                            }
                        } else {
                            search++;
                        }
                    }
                }

                /* If there's a separator row, it's a regular table - keep thead */
                /* If there's no separator row, it's a relaxed table - convert thead to tbody */
                if (!has_separator_row) {
                    /* Convert thead to tbody */
                    memcpy(write, "<tbody>", 7);
                    write += 7;
                    remaining -= 7;
                    read += 7;  /* Skip <thead> */

                    /* Convert <th> to <td> and skip </thead> */
                    while (read < thead_end + 8) {
                        if (strncmp(read, "<th>", 4) == 0) {
                            memcpy(write, "<td>", 4);
                            write += 4;
                            remaining -= 4;
                            read += 4;
                        } else if (strncmp(read, "</th>", 5) == 0) {
                            memcpy(write, "</td>", 5);
                            write += 5;
                            remaining -= 5;
                            read += 5;
                        } else if (strncmp(read, "<th ", 4) == 0) {
                            memcpy(write, "<td", 3);
                            write += 3;
                            remaining -= 3;
                            read += 3;
                            /* Copy attributes until > */
                            while (*read && *read != '>') {
                                *write++ = *read++;
                                remaining--;
                            }
                            if (*read == '>') {
                                *write++ = *read++;
                                remaining--;
                            }
                        } else if (strncmp(read, "</thead>", 8) == 0) {
                            /* Skip </thead> - we'll close tbody later if needed */
                            read += 8;
                            /* Check if next is <tbody> - if so, skip opening tbody */
                            const char *next = read;
                            while (*next && (*next == ' ' || *next == '\n' || *next == '\t')) next++;
                            if (strncmp(next, "<tbody>", 7) == 0) {
                                read = next + 7;
                            }
                            break;
                        } else {
                            *write++ = *read++;
                            remaining--;
                        }
                    }
                    continue;
                }
            }
        }

        /* Copy character */
        *write++ = *read++;
        remaining--;
    }

    *write = '\0';
    return output;
}

/**
 * Remove blank lines within tables
 * Removes lines containing only whitespace/newlines between <table> and </table> tags
 */
char *apex_remove_table_blank_lines(const char *html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char *output = malloc(len + 1);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    bool in_table = false;
    const char *line_start = read;
    bool line_is_blank = true;

    while (*read) {
        /* Check for table tags */
        if (strncmp(read, "<table", 6) == 0 && (read[6] == '>' || read[6] == ' ')) {
            in_table = true;
        } else if (strncmp(read, "</table>", 8) == 0) {
            in_table = false;
        }

        /* On newline, check if the line was blank */
        if (*read == '\n') {
            if (in_table && line_is_blank) {
                /* Blank line in table - skip it */
                read++;
                line_start = read;
                line_is_blank = true;
                continue;
            }
            /* Not blank or not in table - write the line including newline */
            while (line_start <= read) {
                *write++ = *line_start++;
            }
            read++;
            line_start = read;
            line_is_blank = true;
            continue;
        }

        /* Check if line has non-whitespace content */
        if (*read != ' ' && *read != '\t' && *read != '\r') {
            line_is_blank = false;
        }

        read++;
    }

    /* Write any remaining content */
    while (*line_start) {
        *write++ = *line_start++;
    }

    *write = '\0';
    return output;
}

/**
 * Remove table rows that contain only em dashes (separator rows incorrectly rendered as data rows)
 * This happens when smart typography converts --- to — in separator rows
 * @param html The HTML to process
 * @return Newly allocated HTML with separator rows removed (must be freed)
 */
char *apex_remove_table_separator_rows(const char *html) {
    if (!html) return NULL;

    size_t len = strlen(html);
    char *output = malloc(len + 1);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    bool in_table = false;
    const char *row_start = NULL;

    while (*read) {
        /* Check for table tags */
        if (strncmp(read, "<table", 6) == 0 && (read[6] == '>' || read[6] == ' ')) {
            in_table = true;
        } else if (strncmp(read, "</table>", 8) == 0) {
            in_table = false;
        } else if (in_table && strncmp(read, "<tr>", 4) == 0) {
            row_start = read;
            read += 4;

            /* Check all cells in this row */
            bool is_separator_row = true;
            const char *row_end = NULL;

            /* Find the end of this row */
            const char *search = read;
            while (*search) {
                if (strncmp(search, "</tr>", 5) == 0) {
                    row_end = search + 5;
                    break;
                }
                search++;
            }

            if (row_end) {
                /* Check each cell in the row */
                const char *cell_start = read;
                while (cell_start < row_end) {
                    if (strncmp(cell_start, "<td", 3) == 0 || strncmp(cell_start, "<th", 3) == 0) {
                        /* Find the closing tag */
                        const char *tag_end = strstr(cell_start, ">");
                        if (!tag_end) break;
                        tag_end++;

                        /* Find the closing </td> or </th> */
                        const char *cell_end = NULL;
                        if (strncmp(cell_start, "<td", 3) == 0) {
                            cell_end = strstr(tag_end, "</td>");
                            if (cell_end) cell_end += 5;
                        } else {
                            cell_end = strstr(tag_end, "</th>");
                            if (cell_end) cell_end += 5;
                        }

                        if (cell_end && cell_end <= row_end) {
                            /* Check if this cell contains only dashes */
                            if (!cell_contains_only_dashes(tag_end, cell_end - 5)) {
                                is_separator_row = false;
                                break;
                            }
                            cell_start = cell_end;
                        } else {
                            break;
                        }
                    } else {
                        cell_start++;
                    }
                }
            }

            if (is_separator_row && row_end) {
                /* Skip this entire row */
                read = row_end;
                continue;
            } else {
                /* Write the row start */
                while (row_start < read) {
                    *write++ = *row_start++;
                }
            }
            continue;
        }

        /* Copy character */
        *write++ = *read++;
    }

    *write = '\0';
    return output;
}

/**
 * Adjust header levels in HTML based on Base Header Level metadata
 * Shifts all headers by the specified offset (e.g., Base Header Level: 2 means h1->h2, h2->h3, etc.)
 */
char *apex_adjust_header_levels(const char *html, int base_header_level) {
    if (!html || base_header_level <= 0 || base_header_level > 6) {
        return html ? strdup(html) : NULL;
    }

    /* If base_header_level is 1, no adjustment needed */
    if (base_header_level == 1) {
        return strdup(html);
    }

    size_t len = strlen(html);
    size_t capacity = len + 1024;  /* Extra space for potential changes */
    char *output = malloc(capacity);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;

    while (*read) {
        /* Look for header opening tags: <h1>, <h2>, etc. or closing tags: </h1>, </h2>, etc. */
        bool is_closing_tag = false;
        int header_level = -1;

        if (*read == '<') {
            /* Check for closing tag </h1> first */
            if (read[1] == '/' && read[2] == 'h' &&
                read[3] >= '1' && read[3] <= '6' && read[4] == '>') {
                is_closing_tag = true;
                header_level = read[3] - '0';
            }
            /* Check for opening tag <h1> or <h1 ...> */
            else if (read[1] == 'h' && read[2] >= '1' && read[2] <= '6' &&
                     (read[3] == '>' || isspace((unsigned char)read[3]))) {
                is_closing_tag = false;
                header_level = read[2] - '0';
            }
        }

        if (header_level >= 1 && header_level <= 6) {
            /* Calculate new level */
            int new_level = header_level + (base_header_level - 1);

            /* Clamp to valid range (1-6) */
            if (new_level > 6) {
                new_level = 6;
            } else if (new_level < 1) {
                new_level = 1;
            }

            /* Find the end of the tag */
            const char *tag_start = read;
            const char *tag_end = strchr(tag_start, '>');
            if (!tag_end) {
                /* Malformed tag, just copy */
                if (remaining > 0) {
                    *write++ = *read++;
                    remaining--;
                } else {
                    read++;
                }
                continue;
            }

            /* Check if we need to adjust the level */
            if (new_level != header_level) {
                /* Need to replace h<header_level> with h<new_level> */
                size_t tag_len = tag_end - tag_start;

                /* Ensure we have enough space */
                if (remaining < tag_len + 10) {
                    size_t written = write - output;
                    capacity = (written + tag_len + 10) * 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) {
                        free(output);
                        return NULL;
                    }
                    output = new_output;
                    write = output + written;
                    remaining = capacity - written;
                }

                if (is_closing_tag) {
                    /* Closing tag: </h1> -> </h2> */
                    *write++ = '<';
                    *write++ = '/';
                    *write++ = 'h';
                    *write++ = '0' + new_level;
                    *write++ = '>';
                    remaining -= 5;
                    read = tag_end + 1;
                } else {
                    /* Opening tag: <h1> or <h1 ...> */
                    const char *h_pos = tag_start + 1;  /* After '<' */
                    size_t before_h = h_pos - tag_start;
                    memcpy(write, tag_start, before_h);
                    write += before_h;
                    remaining -= before_h;

                    /* Write 'h' */
                    *write++ = 'h';
                    remaining--;

                    /* Write new level */
                    *write++ = '0' + new_level;
                    remaining--;

                    /* Copy rest of tag */
                    const char *after_level = tag_start + 3;  /* After 'h' and level digit */
                    size_t rest_len = tag_end - after_level;
                    if (rest_len > 0 && remaining >= rest_len) {
                        memcpy(write, after_level, rest_len);
                        write += rest_len;
                        remaining -= rest_len;
                    }

                    /* Copy closing '>' */
                    *write++ = '>';
                    remaining--;

                    read = tag_end + 1;
                }
            } else {
                /* No change needed, copy tag as-is */
                size_t tag_len = tag_end - tag_start + 1;
                if (tag_len < remaining) {
                    memcpy(write, tag_start, tag_len);
                    write += tag_len;
                    remaining -= tag_len;
                } else {
                    /* Need more space */
                    size_t written = write - output;
                    capacity = (written + tag_len + 1) * 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) {
                        free(output);
                        return NULL;
                    }
                    output = new_output;
                    write = output + written;
                    remaining = capacity - written;
                    memcpy(write, tag_start, tag_len);
                    write += tag_len;
                    remaining -= tag_len;
                }
                read = tag_end + 1;
            }
        } else {
            /* Not a header tag, copy character */
            if (remaining > 0) {
                *write++ = *read++;
                remaining--;
            } else {
                /* Need more space */
                size_t written = write - output;
                capacity = (written + 1) * 2;
                char *new_output = realloc(output, capacity);
                if (!new_output) {
                    free(output);
                    return NULL;
                }
                output = new_output;
                write = output + written;
                remaining = capacity - written;
                *write++ = *read++;
                remaining--;
            }
        }
    }

    *write = '\0';
    return output;
}

/**
 * Adjust quote styles in HTML based on Quotes Language metadata
 * Replaces default English quote entities with language-specific quotes
 */
char *apex_adjust_quote_language(const char *html, const char *quotes_language) {
    if (!html) return NULL;

    /* Default to English if not specified */
    if (!quotes_language || *quotes_language == '\0') {
        return strdup(html);
    }

    /* Normalize quotes language (lowercase, no spaces) */
    char normalized[64] = {0};
    const char *src = quotes_language;
    char *dst = normalized;
    while (*src && (dst - normalized) < (int)sizeof(normalized) - 1) {
        if (!isspace((unsigned char)*src)) {
            *dst++ = (char)tolower((unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';

    /* Determine quote replacements based on language */
    const char *double_open = NULL;
    const char *double_close = NULL;
    const char *single_open = NULL;
    const char *single_close = NULL;

    if (strcmp(normalized, "english") == 0 || strcmp(normalized, "en") == 0) {
        /* English: &ldquo; &rdquo; &lsquo; &rsquo; (default, no change needed) */
        return strdup(html);
    } else if (strcmp(normalized, "french") == 0 || strcmp(normalized, "fr") == 0) {
        /* French: « » (guillemets) with spaces, ' ' for single */
        double_open = "&laquo;&nbsp;";
        double_close = "&nbsp;&raquo;";
        single_open = "&rsquo;";
        single_close = "&rsquo;";
    } else if (strcmp(normalized, "german") == 0 || strcmp(normalized, "de") == 0) {
        /* German: „ " (bottom/top) */
        double_open = "&bdquo;";
        double_close = "&ldquo;";
        single_open = "&sbquo;";
        single_close = "&lsquo;";
    } else if (strcmp(normalized, "germanguillemets") == 0) {
        /* German guillemets: » « (reversed) */
        double_open = "&raquo;";
        double_close = "&laquo;";
        single_open = "&rsaquo;";
        single_close = "&lsaquo;";
    } else if (strcmp(normalized, "spanish") == 0 || strcmp(normalized, "es") == 0) {
        /* Spanish: « » (guillemets) */
        double_open = "&laquo;";
        double_close = "&raquo;";
        single_open = "&lsquo;";
        single_close = "&rsquo;";
    } else if (strcmp(normalized, "dutch") == 0 || strcmp(normalized, "nl") == 0) {
        /* Dutch: „ " (like German) */
        double_open = "&bdquo;";
        double_close = "&ldquo;";
        single_open = "&sbquo;";
        single_close = "&lsquo;";
    } else if (strcmp(normalized, "swedish") == 0 || strcmp(normalized, "sv") == 0) {
        /* Swedish: " " (straight quotes become curly) */
        double_open = "&rdquo;";
        double_close = "&rdquo;";
        single_open = "&rsquo;";
        single_close = "&rsquo;";
    } else {
        /* Unknown language, use English (no change) */
        return strdup(html);
    }

    /* If no replacements needed, return copy */
    if (!double_open) {
        return strdup(html);
    }

    /* Replace quote entities in HTML */
    size_t html_len = strlen(html);
    size_t capacity = html_len * 2;  /* Extra space for longer entities */
    char *output = malloc(capacity);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;

    while (*read) {
        /* Check for double quote HTML entities */
        if (strncmp(read, "&ldquo;", 7) == 0) {
            size_t repl_len = strlen(double_open);
            if (repl_len < remaining) {
                memcpy(write, double_open, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 7;
                continue;
            }
        } else if (strncmp(read, "&rdquo;", 7) == 0) {
            size_t repl_len = strlen(double_close);
            if (repl_len < remaining) {
                memcpy(write, double_close, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 7;
                continue;
            }
        } else if (strncmp(read, "&lsquo;", 7) == 0) {
            size_t repl_len = strlen(single_open);
            if (repl_len < remaining) {
                memcpy(write, single_open, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 7;
                continue;
            }
        } else if (strncmp(read, "&rsquo;", 7) == 0) {
            size_t repl_len = strlen(single_close);
            if (repl_len < remaining) {
                memcpy(write, single_close, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 7;
                continue;
            }
        }
        /* Check for Unicode curly quotes (UTF-8 encoded) */
        /* Left double quotation mark: U+201C = 0xE2 0x80 0x9C */
        else if ((unsigned char)read[0] == 0xE2 && (unsigned char)read[1] == 0x80 && (unsigned char)read[2] == 0x9C) {
            size_t repl_len = strlen(double_open);
            if (repl_len < remaining) {
                memcpy(write, double_open, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 3;
                continue;
            }
        }
        /* Right double quotation mark: U+201D = 0xE2 0x80 0x9D */
        else if ((unsigned char)read[0] == 0xE2 && (unsigned char)read[1] == 0x80 && (unsigned char)read[2] == 0x9D) {
            size_t repl_len = strlen(double_close);
            if (repl_len < remaining) {
                memcpy(write, double_close, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 3;
                continue;
            }
        }
        /* Left single quotation mark: U+2018 = 0xE2 0x80 0x98 */
        else if ((unsigned char)read[0] == 0xE2 && (unsigned char)read[1] == 0x80 && (unsigned char)read[2] == 0x98) {
            size_t repl_len = strlen(single_open);
            if (repl_len < remaining) {
                memcpy(write, single_open, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 3;
                continue;
            }
        }
        /* Right single quotation mark: U+2019 = 0xE2 0x80 0x99 */
        else if ((unsigned char)read[0] == 0xE2 && (unsigned char)read[1] == 0x80 && (unsigned char)read[2] == 0x99) {
            size_t repl_len = strlen(single_close);
            if (repl_len < remaining) {
                memcpy(write, single_close, repl_len);
                write += repl_len;
                remaining -= repl_len;
                read += 3;
                continue;
            }
        }

        /* Not a quote entity, copy character */
        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            /* Need more space */
            size_t written = write - output;
            capacity = (written + 1) * 2;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                free(output);
                return NULL;
            }
            output = new_output;
            write = output + written;
            remaining = capacity - written;
            *write++ = *read++;
            remaining--;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Apply ARIA labels and accessibility attributes to HTML output
 * @param html The HTML output
 * @param document The AST document (currently unused but kept for consistency with other functions)
 * @return Newly allocated HTML with ARIA attributes injected (must be freed)
 */
char *apex_apply_aria_labels(const char *html, cmark_node *document) {
    (void)document;  /* Currently unused, but kept for API consistency */

    if (!html) return NULL;

    size_t html_len = strlen(html);
    /* Allocate buffer with extra space for ARIA attributes */
    size_t capacity = html_len + 2048;  /* Extra space for ARIA attributes */
    char *output = malloc(capacity + 1);
    if (!output) return strdup(html);

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;
    int table_caption_counter = 0;

    /* Helper macro to append strings safely */
    #define APPEND_SAFE(str) do { \
        size_t len = strlen(str); \
        if (len <= remaining) { \
            memcpy(write, str, len); \
            write += len; \
            remaining -= len; \
        } \
    } while(0)

    /* Helper macro to copy characters safely */
    #define COPY_CHAR(c) do { \
        if (remaining > 0) { \
            *write++ = (c); \
            remaining--; \
        } \
    } while(0)

    while (*read) {
        /* Check for <nav class="toc"> */
        if (*read == '<' && strncmp(read, "<nav", 4) == 0) {
            const char *tag_start = read;
            const char *tag_end = strchr(read, '>');
            if (!tag_end) {
                COPY_CHAR(*read++);
                continue;
            }

            /* Check if this is a TOC nav element */
            const char *class_attr = strstr(tag_start, "class=\"toc\"");
            if (!class_attr) {
                class_attr = strstr(tag_start, "class='toc'");
            }

            if (class_attr && class_attr < tag_end) {
                /* Check if aria-label already exists */
                const char *aria_label = strstr(tag_start, "aria-label=");
                if (!aria_label || aria_label > tag_end) {
                    /* Copy up to just before closing >, add aria-label, then close */
                    size_t prefix_len = tag_end - tag_start;
                    if (prefix_len <= remaining) {
                        memcpy(write, tag_start, prefix_len);
                        write += prefix_len;
                        remaining -= prefix_len;
                    }

                    /* Add aria-label before closing > */
                    APPEND_SAFE(" aria-label=\"Table of contents\"");
                    COPY_CHAR('>');
                    read = tag_end + 1;
                    continue;
                }
            }
        }

        /* Check for <figure> */
        if (*read == '<' && strncmp(read, "<figure", 7) == 0) {
            const char *tag_start = read;
            const char *tag_end = strchr(read, '>');
            if (!tag_end) {
                COPY_CHAR(*read++);
                continue;
            }

            /* Check if role already exists */
            const char *role_attr = strstr(tag_start, "role=");
            if (!role_attr || role_attr > tag_end) {
                /* Copy up to just before closing >, add role, then close */
                size_t prefix_len = tag_end - tag_start;
                if (prefix_len <= remaining) {
                    memcpy(write, tag_start, prefix_len);
                    write += prefix_len;
                    remaining -= prefix_len;
                }

                /* Add role="figure" before closing > */
                APPEND_SAFE(" role=\"figure\"");
                COPY_CHAR('>');
                read = tag_end + 1;
                continue;
            }
        }

        /* Check for <table> */
        if (*read == '<' && strncmp(read, "<table", 6) == 0) {
            const char *tag_start = read;
            const char *tag_end = strchr(read, '>');
            if (!tag_end) {
                COPY_CHAR(*read++);
                continue;
            }

            /* Check if role already exists */
            const char *role_attr = strstr(tag_start, "role=");
            bool needs_role = (!role_attr || role_attr > tag_end);

            /* Check if aria-describedby already exists */
            const char *aria_desc = strstr(tag_start, "aria-describedby=");
            bool has_aria_desc = (aria_desc && aria_desc < tag_end);

            /* Check if we're in a table-figure context and look for figcaption */
            bool in_table_figure = false;
            const char *before_table = read - 1;
            while (before_table >= html && before_table > read - 500) {
                if (*before_table == '<' && strncmp(before_table, "<figure", 7) == 0) {
                    const char *class_check = strstr(before_table, "class=\"table-figure\"");
                    if (!class_check) {
                        class_check = strstr(before_table, "class='table-figure'");
                    }
                    if (class_check && class_check < tag_start) {
                        in_table_figure = true;
                        break;
                    }
                }
                before_table--;
            }

            /* Look backwards for figcaption ID in the same figure */
            char *caption_id = NULL;
            if (in_table_figure && !has_aria_desc) {
                const char *search = read - 1;
                while (search >= html && search > read - 2000) {
                    if (*search == '<' && strncmp(search, "<figcaption", 11) == 0) {
                        const char *cap_tag_end = strchr(search, '>');
                        if (cap_tag_end && cap_tag_end < tag_start) {
                            /* Found a figcaption before this table */
                            const char *id_attr = strstr(search, "id=\"");
                            if (!id_attr) {
                                id_attr = strstr(search, "id='");
                            }
                            if (id_attr && id_attr < cap_tag_end) {
                                /* Extract ID */
                                const char *id_start = id_attr + 4;
                                const char *id_end = strchr(id_start, '"');
                                if (!id_end) id_end = strchr(id_start, '\'');
                                if (id_end && id_end > id_start) {
                                    size_t id_len = id_end - id_start;
                                    caption_id = malloc(id_len + 1);
                                    if (caption_id) {
                                        memcpy(caption_id, id_start, id_len);
                                        caption_id[id_len] = '\0';
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    search--;
                }
            }

            if (needs_role || caption_id) {
                /* Copy up to just before closing >, add attributes, then close */
                size_t prefix_len = tag_end - tag_start;
                if (prefix_len <= remaining) {
                    memcpy(write, tag_start, prefix_len);
                    write += prefix_len;
                    remaining -= prefix_len;
                }

                /* Add role="table" if needed */
                if (needs_role) {
                    APPEND_SAFE(" role=\"table\"");
                }

                /* Add aria-describedby if we found a caption ID */
                if (caption_id) {
                    char aria_desc_str[256];
                    snprintf(aria_desc_str, sizeof(aria_desc_str), " aria-describedby=\"%s\"", caption_id);
                    APPEND_SAFE(aria_desc_str);
                    free(caption_id);
                }

                COPY_CHAR('>');
                read = tag_end + 1;
                continue;
            }
        }

        /* Check for <figcaption> within table-figure to add IDs if missing */
        if (*read == '<' && strncmp(read, "<figcaption", 11) == 0) {
            const char *tag_start = read;
            const char *tag_end = strchr(read, '>');
            if (!tag_end) {
                COPY_CHAR(*read++);
                continue;
            }

            /* Check if we're in a table-figure context by looking backwards */
            const char *before_cap = read - 1;
            bool in_table_figure = false;
            while (before_cap >= html && before_cap > read - 200) {
                if (*before_cap == '<' && strncmp(before_cap, "<figure", 7) == 0) {
                    const char *class_check = strstr(before_cap, "class=\"table-figure\"");
                    if (!class_check) {
                        class_check = strstr(before_cap, "class='table-figure'");
                    }
                    if (class_check && class_check < tag_start) {
                        in_table_figure = true;
                        break;
                    }
                }
                before_cap--;
            }

            if (in_table_figure) {
                /* Check if ID already exists */
                const char *id_attr = strstr(tag_start, "id=\"");
                if (!id_attr) {
                    id_attr = strstr(tag_start, "id='");
                }

                if (!id_attr || id_attr > tag_end) {
                    /* No ID, generate one */
                    char caption_id[64];
                    table_caption_counter++;
                    snprintf(caption_id, sizeof(caption_id), "table-caption-%d", table_caption_counter);

                    /* Copy up to just before closing >, add id, then close */
                    size_t prefix_len = tag_end - tag_start;
                    if (prefix_len <= remaining) {
                        memcpy(write, tag_start, prefix_len);
                        write += prefix_len;
                        remaining -= prefix_len;
                    }

                    /* Add id attribute */
                    char id_attr_str[128];
                    snprintf(id_attr_str, sizeof(id_attr_str), " id=\"%s\"", caption_id);
                    APPEND_SAFE(id_attr_str);
                    COPY_CHAR('>');
                    read = tag_end + 1;
                    continue;
                }
            }
        }

        /* Default: copy character */
        COPY_CHAR(*read++);
    }

    #undef APPEND_SAFE
    #undef COPY_CHAR

    *write = '\0';
    return output;
}

