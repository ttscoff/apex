/**
 * Kramdown IAL (Inline Attribute Lists) Implementation
 */

#include "ial.h"
#include "table.h"  /* For CMARK_NODE_TABLE */
#include "apex/apex.h"  /* For apex_mode_t */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/**
 * Free attributes structure
 */
void apex_free_attributes(apex_attributes *attrs) {
    if (!attrs) return;

    free(attrs->id);

    for (int i = 0; i < attrs->class_count; i++) {
        free(attrs->classes[i]);
    }
    free(attrs->classes);

    for (int i = 0; i < attrs->attr_count; i++) {
        free(attrs->keys[i]);
        free(attrs->values[i]);
    }
    free(attrs->keys);
    free(attrs->values);

    free(attrs);
}

/**
 * Free ALD list
 */
void apex_free_alds(ald_entry *alds) {
    while (alds) {
        ald_entry *next = alds->next;
        free(alds->name);
        apex_free_attributes(alds->attrs);
        free(alds);
        alds = next;
    }
}

/**
 * Create empty attributes structure
 */
static apex_attributes *create_attributes(void) {
    apex_attributes *attrs = calloc(1, sizeof(apex_attributes));
    return attrs;
}

/**
 * Add class to attributes
 */
static void add_class(apex_attributes *attrs, const char *class_name) {
    if (!attrs || !class_name) return;

    attrs->classes = realloc(attrs->classes, sizeof(char*) * (attrs->class_count + 1));
    attrs->classes[attrs->class_count++] = strdup(class_name);
}

/**
 * Add key-value attribute
 */
static void add_attribute(apex_attributes *attrs, const char *key, const char *value) {
    if (!attrs || !key) return;

    attrs->keys = realloc(attrs->keys, sizeof(char*) * (attrs->attr_count + 1));
    attrs->values = realloc(attrs->values, sizeof(char*) * (attrs->attr_count + 1));
    attrs->keys[attrs->attr_count] = strdup(key);
    attrs->values[attrs->attr_count] = value ? strdup(value) : strdup("");
    attrs->attr_count++;
}

/**
 * Parse IAL/ALD content
 * Format: #id .class .class2 key="value" key2='value2'
 */
static apex_attributes *parse_ial_content(const char *content, int len) {
    apex_attributes *attrs = create_attributes();
    if (!attrs) return NULL;

    char buffer[2048];
    if (len >= (int)sizeof(buffer)) len = (int)sizeof(buffer) - 1;
    memcpy(buffer, content, len);
    buffer[len] = '\0';

    char *p = buffer;
    while (*p) {
        /* Skip whitespace */
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Check for ID (#id) */
        if (*p == '#') {
            p++;
            char *id_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '.' && *p != '}') p++;
            if (p > id_start) {
                char saved = *p;
                *p = '\0';
                if (attrs->id) free(attrs->id);
                attrs->id = strdup(id_start);
                *p = saved;
            }
            continue;
        }

        /* Check for class (.class) */
        if (*p == '.') {
            p++;
            char *class_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '.' && *p != '#' && *p != '}') p++;
            if (p > class_start) {
                char saved = *p;
                *p = '\0';
                add_class(attrs, class_start);
                *p = saved;
            }
            continue;
        }

        /* Check for key="value" or key='value' */
        char *key_start = p;
        while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '}') p++;

        if (*p == '=') {
            /* Found key=value */
            char saved = *p;
            *p = '\0';
            char *key = strdup(key_start);
            *p = saved;
            p++; /* Skip = */

            /* Parse value (could be quoted - handle both straight and curly quotes) */
            char *value = NULL;
            bool is_curly_left = ((unsigned char)*p == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x9C);  /* " */
            bool is_curly_right = ((unsigned char)*p == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x9D);  /* " */
            bool is_straight_quote = (*p == '"' || *p == '\'');

            if (is_straight_quote || is_curly_left || is_curly_right) {
                bool is_curly = (is_curly_left || is_curly_right);
                char *value_start;

                if (is_curly) {
                    /* Skip UTF-8 curly quote (3 bytes) */
                    p += 3;
                    value_start = p;

                    /* Find closing curly quote (either left or right) */
                    char *value_end = p;
                    while (*value_end) {
                        if ((unsigned char)*value_end == 0xE2 && (unsigned char)value_end[1] == 0x80 &&
                            ((unsigned char)value_end[2] == 0x9C || (unsigned char)value_end[2] == 0x9D)) {
                            break;  /* Found closing curly quote */
                        }
                        value_end++;
                    }
                    if ((unsigned char)*value_end == 0xE2) {
                        /* Extract value (content between curly quotes, excluding quotes) */
                        size_t value_len = value_end - value_start;
                        value = malloc(value_len + 1);
                        if (value) {
                            memcpy(value, value_start, value_len);
                            value[value_len] = '\0';
                        }
                        p = value_end + 3;  /* Skip closing curly quote */
                    }
                } else {
                    /* Straight quote */
                    char quote = *p++;
                    value_start = p;
                    while (*p && *p != quote) {
                        if (*p == '\\' && *(p+1)) p++; /* Skip escaped char */
                        p++;
                    }
                    if (*p == quote) {
                        *p = '\0';
                        value = strdup(value_start);
                        *p = quote;
                        p++;
                    }
                }
            } else {
                /* Unquoted value */
                char *value_start = p;
                while (*p && !isspace((unsigned char)*p) && *p != '}') p++;
                char saved_val = *p;
                *p = '\0';
                value = strdup(value_start);
                *p = saved_val;
            }

            add_attribute(attrs, key, value);
            free(key);
            free(value);
            continue;
        }

        /* Unknown token, skip */
        p++;
    }

    return attrs;
}

/**
 * Check if line is an ALD
 * Pattern: {:ref-name: attributes}
 */
static bool is_ald_line(const char *line, char **ref_name, apex_attributes **attrs) {
    const char *p = line;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*p)) p++;

    /* Check for {: */
    if (p[0] != '{' || p[1] != ':') return false;
    p += 2;

    /* Extract reference name */
    const char *name_start = p;
    while (*p && *p != ':' && *p != '}') p++;

    if (*p != ':') return false; /* Not an ALD, maybe regular IAL */

    /* Found ALD */
    int name_len = p - name_start;
    if (name_len <= 0) return false;

    *ref_name = malloc(name_len + 1);
    memcpy(*ref_name, name_start, name_len);
    (*ref_name)[name_len] = '\0';

    p++; /* Skip second : */

    /* Find closing } */
    const char *content_start = p;
    const char *close = strchr(p, '}');
    if (!close) {
        free(*ref_name);
        return false;
    }

    /* Parse attributes */
    *attrs = parse_ial_content(content_start, close - content_start);

    return true;
}

/**
 * Extract ALDs from text
 */
ald_entry *apex_extract_alds(char **text_ptr) {
    if (!text_ptr || !*text_ptr) return NULL;

    char *text = *text_ptr;
    ald_entry *alds = NULL;
    ald_entry **tail = &alds;

    char *line_start = text;
    char *line_end;

    char *output = malloc(strlen(text) + 1);
    char *output_write = output;

    if (!output) return NULL;

    while ((line_end = strchr(line_start, '\n')) != NULL || *line_start) {
        if (!line_end) line_end = line_start + strlen(line_start);

        size_t line_len = line_end - line_start;
        char line[2048];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /* Check if this is an ALD */
        char *ref_name = NULL;
        apex_attributes *attrs = NULL;

        if (is_ald_line(line, &ref_name, &attrs)) {
            /* Found ALD - store it */
            ald_entry *entry = malloc(sizeof(ald_entry));
            if (entry) {
                entry->name = ref_name;
                entry->attrs = attrs;
                entry->next = NULL;

                *tail = entry;
                tail = &entry->next;
            }

            /* Skip this line in output */
            line_start = *line_end ? line_end + 1 : line_end;
            continue;
        }

        /* Not an ALD, copy line to output */
        memcpy(output_write, line_start, line_len);
        output_write += line_len;
        if (*line_end) {
            *output_write++ = '\n';
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    *output_write = '\0';

    /* Use the output buffer as the new text */
    size_t output_len = strlen(output);
    if (output_len <= strlen(*text_ptr)) {
        strcpy(*text_ptr, output);
    } else {
        /* Output is larger, need to reallocate */
        free(*text_ptr);
        *text_ptr = strdup(output);
    }
    free(output);

    return alds;
}

/**
 * Find ALD by name
 */
static apex_attributes *find_ald(ald_entry *alds, const char *name) {
    for (ald_entry *entry = alds; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return entry->attrs;
        }
    }
    return NULL;
}

/**
 * Merge attributes (for ALD references)
 */
static apex_attributes *merge_attributes(apex_attributes *base, apex_attributes *override) {
    apex_attributes *merged = create_attributes();
    if (!merged) return base;

    /* Copy base attributes */
    if (base) {
        if (base->id) merged->id = strdup(base->id);
        for (int i = 0; i < base->class_count; i++) {
            add_class(merged, base->classes[i]);
        }
        for (int i = 0; i < base->attr_count; i++) {
            add_attribute(merged, base->keys[i], base->values[i]);
        }
    }

    /* Override with new attributes */
    if (override) {
        if (override->id) {
            free(merged->id);
            merged->id = strdup(override->id);
        }
        for (int i = 0; i < override->class_count; i++) {
            add_class(merged, override->classes[i]);
        }
        for (int i = 0; i < override->attr_count; i++) {
            add_attribute(merged, override->keys[i], override->values[i]);
        }
    }

    return merged;
}

/**
 * Check if text ends with IAL pattern
 * Pattern: {: attributes} or {:.class} or {: ref-name}
 */
static bool extract_ial_from_text(const char *text, apex_attributes **attrs_out, ald_entry *alds) {
    if (!text) return false;

    /* Find {: from the end */
    const char *ial_start = strrchr(text, '{');
    if (!ial_start || ial_start[1] != ':') return false;

    /* Find closing } */
    const char *ial_end = strchr(ial_start, '}');
    if (!ial_end) return false;

    /* Check if this is at the end (only whitespace after) */
    const char *p = ial_end + 1;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p) return false; /* Not at end */

    /* Parse IAL content */
    const char *content_start = ial_start + 2;
    int content_len = ial_end - content_start;

    /* Check if it's a reference (single word, no special chars) */
    char ref_name[256];
    if (content_len > 0 && content_len < (int)sizeof(ref_name)) {
        memcpy(ref_name, content_start, content_len);
        ref_name[content_len] = '\0';

        /* Trim whitespace */
        char *trimmed = ref_name;
        while (isspace((unsigned char)*trimmed)) trimmed++;
        char *end = trimmed + strlen(trimmed) - 1;
        while (end > trimmed && isspace((unsigned char)*end)) *end-- = '\0';

        /* Check if it's a simple reference (no # . or =) */
        bool is_ref = true;
        for (char *c = trimmed; *c; c++) {
            if (*c == '#' || *c == '.' || *c == '=') {
                is_ref = false;
                break;
            }
        }

        if (is_ref && *trimmed) {
            /* Look up ALD */
            apex_attributes *found = find_ald(alds, trimmed);
            if (found) {
                /* Copy the ALD attributes */
                *attrs_out = merge_attributes(found, NULL);
                return true;
            }
        }
    }

    /* Not a reference, parse as regular IAL */
    *attrs_out = parse_ial_content(content_start, content_len);
    return *attrs_out != NULL;
}

/**
 * Apply attributes to HTML tag
 * Helper function to generate attribute string
 */
static char *attributes_to_html(apex_attributes *attrs) {
    if (!attrs) return strdup("");

    char buffer[4096];
    char *p = buffer;
    size_t remaining = sizeof(buffer);

    #define APPEND(str) do { \
        size_t len = strlen(str); \
        if (len < remaining) { \
            memcpy(p, str, len); \
            p += len; \
        remaining -= len; \
        } \
    } while(0)

    bool first_attr = true;

    /* Add ID */
    if (attrs->id) {
        char id_str[512];
        if (first_attr) {
            snprintf(id_str, sizeof(id_str), "id=\"%s\"", attrs->id);
            first_attr = false;
        } else {
            snprintf(id_str, sizeof(id_str), " id=\"%s\"", attrs->id);
        }
        APPEND(id_str);
    }

    /* Add classes */
    if (attrs->class_count > 0) {
        if (first_attr) {
            APPEND("class=\"");
            first_attr = false;
        } else {
            APPEND(" class=\"");
        }
        for (int i = 0; i < attrs->class_count; i++) {
            if (i > 0) APPEND(" ");
            APPEND(attrs->classes[i]);
        }
        APPEND("\"");
    }

    /* Add other attributes */
    for (int i = 0; i < attrs->attr_count; i++) {
        char attr_str[1024];
        const char *val = attrs->values[i];
        if (first_attr) {
            snprintf(attr_str, sizeof(attr_str), "%s=\"%s\"",
                     attrs->keys[i], val);
            first_attr = false;
        } else {
            snprintf(attr_str, sizeof(attr_str), " %s=\"%s\"",
                     attrs->keys[i], val);
        }
        APPEND(attr_str);
    }

    #undef APPEND

    *p = '\0';
    return strdup(buffer);
}

/**
 * Check if a paragraph contains IAL (possibly with other content)
 * Returns true if IAL found, extracts attributes, and modifies text to remove IAL
 */
/**
 * Extract IAL from a PURE IAL paragraph (only contains "{: ...}")
 * This is ONLY for next-line block IAL that applies to the previous element.
 * Does NOT handle inline paragraph IAL - that's not supported in standard Kramdown.
 *
 * Example:
 *   Paragraph text.
 *
 *   {: #id .class}     <-- This is a pure IAL paragraph
 */
static bool extract_ial_from_paragraph(cmark_node *para, apex_attributes **attrs_out, ald_entry *alds) {
    if (cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) return false;

    /* Must have exactly one child (a text node) - no links, no formatting */
    cmark_node *text_node = cmark_node_first_child(para);
    if (!text_node) return false;
    if (cmark_node_next(text_node) != NULL) return false;  /* More than one child - not pure IAL */
    if (cmark_node_get_type(text_node) != CMARK_NODE_TEXT) return false;

    const char *text = cmark_node_get_literal(text_node);
    if (!text) return false;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*text)) text++;
    if (*text == '\0') return false;

    /* Must start with {: */
    if (text[0] != '{' || text[1] != ':') return false;

    /* Find closing } */
    const char *close = strchr(text + 2, '}');
    if (!close) return false;

    /* Check nothing after } except whitespace */
    const char *after = close + 1;
    while (*after && isspace((unsigned char)*after)) after++;
    if (*after != '\0') return false;

    /* This is a pure IAL paragraph - extract attributes */
    return extract_ial_from_text(text, attrs_out, alds);
}

/**
 * Handle span-level IAL (inline elements with attributes)
 * Example: [Link](url){: .class} or ![Image](img){: #id}
 *
 * The IAL applies to the last inline element (link, image, emphasis, etc.)
 * before the text node containing the IAL.
 */
static bool process_span_ial(cmark_node *para, ald_entry *alds) {
    if (cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) return false;

    /* Get last child - must be a text node with IAL */
    cmark_node *last_child = cmark_node_last_child(para);
    if (!last_child || cmark_node_get_type(last_child) != CMARK_NODE_TEXT) {
        return false;
    }

    const char *text = cmark_node_get_literal(last_child);
    if (!text) return false;

    /* Check if it ends with {: ... } */
    const char *ial_start = strrchr(text, '{');
    if (!ial_start || ial_start[1] != ':') {
        return false;
    }

    const char *close = strchr(ial_start, '}');
    if (!close) {
        return false;
    }

    /* Check nothing after } except whitespace */
    const char *after = close + 1;
    while (*after && isspace((unsigned char)*after)) after++;
    if (*after != '\0') {
        return false;
    }

    /* Find the inline element before this text node */
    cmark_node *target = NULL;
    for (cmark_node *child = cmark_node_first_child(para); child; child = cmark_node_next(child)) {
        if (child == last_child) break;  /* Reached the IAL text */
        target = child;  /* Keep track of last non-IAL child */
    }

    if (!target) {
        return false;  /* No target element found */
    }

    /* Check if target is a valid inline element for IAL */
    cmark_node_type target_type = cmark_node_get_type(target);
    if (target_type != CMARK_NODE_LINK &&
        target_type != CMARK_NODE_IMAGE &&
        target_type != CMARK_NODE_EMPH &&
        target_type != CMARK_NODE_STRONG &&
        target_type != CMARK_NODE_CODE) {
        return false;  /* Not a supported inline element */
    }

    /* Extract attributes from IAL */
    apex_attributes *attrs = NULL;
    if (!extract_ial_from_text(ial_start, &attrs, alds)) return false;

    /* Apply attributes to the target inline element */
    char *attr_str = attributes_to_html(attrs);
    cmark_node_set_user_data(target, attr_str);
    apex_free_attributes(attrs);

    /* Remove the IAL from the text node */
    size_t prefix_len = ial_start - text;
    if (prefix_len > 0) {
        /* Text has content before IAL - trim it */
        char *new_text = malloc(prefix_len + 1);
        if (new_text) {
            memcpy(new_text, text, prefix_len);
            new_text[prefix_len] = '\0';

            /* Trim trailing whitespace */
            char *end = new_text + prefix_len - 1;
            while (end >= new_text && isspace((unsigned char)*end)) *end-- = '\0';

            cmark_node_set_literal(last_child, new_text);
            free(new_text);
        }
    } else {
        /* IAL is the only content in text node - remove it */
        cmark_node_unlink(last_child);
        cmark_node_free(last_child);
    }

    return true;
}

/**
 * Extract IAL from heading text (inline syntax: ## Heading {: #id})
 */
static bool extract_ial_from_heading(cmark_node *heading, apex_attributes **attrs_out, ald_entry *alds) {
    if (cmark_node_get_type(heading) != CMARK_NODE_HEADING) return false;

    /* Get the text node inside the heading */
    cmark_node *text_node = cmark_node_first_child(heading);
    if (!text_node || cmark_node_get_type(text_node) != CMARK_NODE_TEXT) return false;

    const char *text = cmark_node_get_literal(text_node);
    if (!text) return false;

    /* Look for {: at the end */
    const char *ial_start = strrchr(text, '{');
    if (!ial_start || ial_start[1] != ':') return false;

    /* Find closing } */
    const char *close = strchr(ial_start, '}');
    if (!close) return false;

    /* Check nothing after } except whitespace */
    const char *after = close + 1;
    while (*after && isspace((unsigned char)*after)) after++;
    if (*after) return false;


    /* Extract attributes */
    if (!extract_ial_from_text(ial_start, attrs_out, alds)) {
        return false;
    }

    /* Remove IAL from heading text */
    size_t prefix_len = ial_start - text;

    char *new_text = malloc(prefix_len + 1);
    if (!new_text) return false;

    if (prefix_len > 0) {
        memcpy(new_text, text, prefix_len);
        new_text[prefix_len] = '\0';

        /* Trim trailing whitespace */
        char *end = new_text + prefix_len - 1;
        while (end >= new_text && isspace((unsigned char)*end)) *end-- = '\0';
    } else {
        /* Heading was only IAL - leave empty string */
        new_text[0] = '\0';
    }


    cmark_node_set_literal(text_node, new_text);
    free(new_text);
    return true;
}

/**
 * Check if a paragraph is ONLY an IAL (should be removed entirely)
 */
static bool is_pure_ial_paragraph(cmark_node *para) {
    if (cmark_node_get_type(para) != CMARK_NODE_PARAGRAPH) return false;

    cmark_node *text_node = cmark_node_first_child(para);
    if (!text_node || cmark_node_get_type(text_node) != CMARK_NODE_TEXT) return false;
    if (cmark_node_next(text_node) != NULL) return false;

    const char *text = cmark_node_get_literal(text_node);
    if (!text) return false;

    /* Trim */
    while (isspace((unsigned char)*text)) text++;

    /* Check if it's ONLY {: ... } */
    if (text[0] != '{' || text[1] != ':') return false;

    const char *close = strchr(text + 2, '}');
    if (!close) return false;

    /* Check nothing after */
    const char *after = close + 1;
    while (*after && isspace((unsigned char)*after)) after++;

    return (*after == '\0');
}

/**
 * Process IAL for a single node
 * Check if node has inline IAL or if next sibling is IAL paragraph
 */
/**
 * Process IAL for a node
 * Returns the node to free (if any), or NULL
 * Caller must free the returned node after iteration is complete
 */
static cmark_node *process_node_ial(cmark_node *node, ald_entry *alds) {
    if (!node) return NULL;

    cmark_node_type type = cmark_node_get_type(node);

    /* Handle heading with inline IAL (## Heading {: #id}) */
    if (type == CMARK_NODE_HEADING) {
        apex_attributes *attrs = NULL;
        bool extracted = extract_ial_from_heading(node, &attrs, alds);
        if (extracted) {
            /* Store attributes in heading */
            char *attr_str = attributes_to_html(attrs);

            /* Merge with existing user_data if present */
            char *existing = (char *)cmark_node_get_user_data(node);
            if (existing) {
                /* Append to existing */
                char *combined = malloc(strlen(existing) + strlen(attr_str) + 1);
                if (combined) {
                    strcpy(combined, existing);
                    strcat(combined, attr_str);
                    cmark_node_set_user_data(node, combined);
                    free(attr_str);
                } else {
                    cmark_node_set_user_data(node, attr_str);
                }
            } else {
                cmark_node_set_user_data(node, attr_str);
            }

            apex_free_attributes(attrs);
            return NULL;  /* No node to free */
        }
        /* If no inline IAL, fall through to check for next-line IAL */
    }

    /* Handle span-level IAL (links, images, emphasis, etc. with inline attributes) */
    if (type == CMARK_NODE_PARAGRAPH) {
        if (process_span_ial(node, alds)) {
            return NULL;  /* Span IAL processed, no node to free */
        }
        /* No span IAL found, fall through to check for next-line IAL */
    }

    /* Only certain block types can have IAL after them */
    if (type != CMARK_NODE_HEADING &&
        type != CMARK_NODE_PARAGRAPH &&
        type != CMARK_NODE_BLOCK_QUOTE &&
        type != CMARK_NODE_CODE_BLOCK &&
        type != CMARK_NODE_LIST &&
        type != CMARK_NODE_ITEM &&
        type != CMARK_NODE_TABLE) {  /* Tables can have IAL */
        return NULL;  /* No node to free */
    }


    /* Look at next sibling for IAL paragraph */
    cmark_node *next = cmark_node_next(node);
    if (!next) {
        return NULL;  /* No node to free */
    }

    if (cmark_node_get_type(next) != CMARK_NODE_PARAGRAPH) {
        return NULL;  /* No node to free */
    }

    /* Check if it's a pure IAL paragraph */
    if (is_pure_ial_paragraph(next)) {
        apex_attributes *attrs = NULL;
        if (extract_ial_from_paragraph(next, &attrs, alds)) {
            /* Store attributes in this node */
            char *attr_str = attributes_to_html(attrs);
            cmark_node_set_user_data(node, attr_str);
            apex_free_attributes(attrs);

            /* Return node to be unlinked and freed after iteration completes */
            /* Don't unlink here - that invalidates the iterator */
            return next;  /* Return node to unlink and free after iteration */
        }
    }

    return NULL;  /* No node to free */
}

/**
 * Process IAL in AST
 */
void apex_process_ial_in_tree(cmark_node *node, ald_entry *alds) {
    if (!node) return;

    /* Collect nodes to unlink and free after iteration to avoid use-after-free */
    cmark_node **nodes_to_free = NULL;
    size_t free_count = 0;
    size_t free_capacity = 0;

    /* First pass: process IAL and collect nodes to remove */
    cmark_iter *iter = cmark_iter_new(node);
    cmark_event_type ev_type;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *cur = cmark_iter_get_node(iter);

        /* Only process on ENTER events */
        if (ev_type == CMARK_EVENT_ENTER) {
            /* Process node and collect any nodes that need to be freed */
            cmark_node *node_to_free = process_node_ial(cur, alds);
            if (node_to_free) {
                /* Expand array if needed */
                if (free_count >= free_capacity) {
                    free_capacity = free_capacity ? free_capacity * 2 : 16;
                    cmark_node **new_array = realloc(nodes_to_free, free_capacity * sizeof(cmark_node*));
                    if (new_array) {
                        nodes_to_free = new_array;
                    } else {
                        /* If realloc fails, unlink and free immediately */
                        cmark_node_unlink(node_to_free);
                        cmark_node_free(node_to_free);
                        continue;
                    }
                }
                nodes_to_free[free_count++] = node_to_free;
            }
        }
    }

    cmark_iter_free(iter);

    /* Second pass: unlink and free collected nodes after iteration is complete */
    for (size_t i = 0; i < free_count; i++) {
        cmark_node_unlink(nodes_to_free[i]);
        cmark_node_free(nodes_to_free[i]);
    }
    free(nodes_to_free);
}

/**
 * Check if a line is a pure IAL (starts with {: and ends with })
 */
static bool is_ial_line(const char *line, size_t len) {
    const char *p = line;
    const char *end = line + len;

    /* Skip leading whitespace */
    while (p < end && isspace((unsigned char)*p)) p++;

    /* Must start with {: */
    if (p + 2 > end || p[0] != '{' || p[1] != ':') return false;

    /* Find closing } */
    const char *close = memchr(p + 2, '}', end - (p + 2));
    if (!close) return false;

    /* Check nothing substantial after the } */
    const char *after = close + 1;
    while (after < end && isspace((unsigned char)*after)) after++;

    return (after >= end);
}

/**
 * Preprocess text to separate IAL markers from preceding content.
 * Kramdown allows IAL on the line immediately following content,
 * but cmark-gfm treats that as part of the same paragraph.
 * This inserts blank lines before IAL markers.
 */
char *apex_preprocess_ial(const char *text) {
    if (!text) return NULL;

    size_t text_len = strlen(text);
    /* Worst case: we add a newline before every line */
    size_t capacity = text_len * 2 + 1;
    char *output = malloc(capacity);
    if (!output) return NULL;

    char *out = output;
    const char *p = text;
    bool prev_line_was_content = false;
    bool prev_line_was_blank = true;  /* Start as if there was a blank line before */

    while (*p) {
        /* Find end of current line */
        const char *line_start = p;
        const char *line_end = strchr(p, '\n');
        if (!line_end) {
            line_end = p + strlen(p);
        }

        size_t line_len = line_end - line_start;

        /* Check if this line is blank */
        bool is_blank = true;
        for (size_t i = 0; i < line_len; i++) {
            if (!isspace((unsigned char)line_start[i])) {
                is_blank = false;
                break;
            }
        }

        /* Check if this line is an IAL */
        bool is_ial = is_ial_line(line_start, line_len);

        /* Special case: Kramdown-style TOC marker "{:toc ...}".
         *
         * In Kramdown/Jekyll, a pure IAL paragraph containing only "{:toc}"
         * (optionally with additional parameters) is replaced with a
         * generated table of contents. We map this syntax to Apex's
         * existing TOC marker "<!--TOC ...-->" so it is handled by the
         * TOC extension.
         */
        bool handled_toc_marker = false;
        if (is_ial) {
            const char *q = line_start;
            const char *end = line_start + line_len;

            /* Skip leading whitespace */
            while (q < end && isspace((unsigned char)*q)) q++;

            /* Must start with "{:" */
            if (q + 2 <= end && q[0] == '{' && q[1] == ':') {
                q += 2;

                /* Find closing '}' */
                const char *close = memchr(q, '}', (size_t)(end - q));
                if (close) {
                    /* Extract and trim inner content */
                    const char *inner_start = q;
                    const char *inner_end = close;
                    while (inner_start < inner_end && isspace((unsigned char)*inner_start)) {
                        inner_start++;
                    }
                    while (inner_end > inner_start && isspace((unsigned char)inner_end[-1])) {
                        inner_end--;
                    }

                    if (inner_start < inner_end) {
                        /* Check for leading "toc" (case-insensitive) */
                        const char *toc_start = inner_start;
                        const char *toc_end = toc_start + 3;
                        if ((size_t)(inner_end - inner_start) >= 3 &&
                            (toc_start[0] == 't' || toc_start[0] == 'T') &&
                            (toc_start[1] == 'o' || toc_start[1] == 'O') &&
                            (toc_start[2] == 'c' || toc_start[2] == 'C') &&
                            (toc_end == inner_end || isspace((unsigned char)*toc_end))) {
                            /* Everything after "toc" (including any whitespace) is
                             * treated as TOC options and passed through to the
                             * marker. This allows syntax like "{:toc max=3 min=2}". */
                            const char *options_start = toc_end;
                            while (options_start < inner_end &&
                                   isspace((unsigned char)*options_start)) {
                                options_start++;
                            }

                            /* If this is a TOC marker, optionally insert a blank
                             * line before it to keep block structure consistent
                             * with normal IAL handling. */
                            if (prev_line_was_content && !prev_line_was_blank) {
                                *out++ = '\n';
                            }

                            /* Build "<!--TOC [options]-->" */
                            const char *marker_prefix = "<!--TOC";
                            size_t prefix_len = strlen(marker_prefix);
                            memcpy(out, marker_prefix, prefix_len);
                            out += prefix_len;

                            if (options_start < inner_end) {
                                *out++ = ' ';
                                size_t opts_len = (size_t)(inner_end - options_start);
                                memcpy(out, options_start, opts_len);
                                out += opts_len;
                            }

                            *out++ = '-';
                            *out++ = '-';
                            *out++ = '>';

                            /* Preserve original newline if present */
                            if (*line_end == '\n') {
                                *out++ = '\n';
                            }

                            handled_toc_marker = true;
                        }
                    }
                }
            }
        }

        if (!handled_toc_marker) {
            /* If this is an IAL and previous line was content (not blank, not IAL),
             * insert a blank line before it */
            if (is_ial && prev_line_was_content && !prev_line_was_blank) {
                *out++ = '\n';
            }

            /* Copy the line */
            memcpy(out, line_start, line_len);
            out += line_len;

            /* Copy the newline if present */
            if (*line_end == '\n') {
                *out++ = '\n';
            }
        }

        /* Advance input pointer */
        p = (*line_end == '\n') ? line_end + 1 : line_end;

        /* Track state for next iteration */
        prev_line_was_blank = is_blank;
        prev_line_was_content = !is_blank && !is_ial;
    }

    *out = '\0';
    return output;
}

/**
 * URL encode a string (percent encoding)
 * Only encodes characters that need encoding (space, non-ASCII, etc.)
 * Returns newly allocated string, caller must free
 */
static char *url_encode(const char *url) {
    if (!url) return NULL;

    /* Calculate size needed (worst case: 3 chars per byte) */
    size_t len = strlen(url);
    size_t capacity = len * 3 + 1;
    char *encoded = malloc(capacity);
    if (!encoded) return NULL;

    char *out = encoded;
    for (const char *p = url; *p; p++) {
        unsigned char c = (unsigned char)*p;
        /* Characters that don't need encoding */
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '/' ||
            c == ':' || c == '?' || c == '#' || c == '[' ||
            c == ']' || c == '@' || c == '!' || c == '$' ||
            c == '&' || c == '\'' || c == '(' || c == ')' ||
            c == '*' || c == '+' || c == ',' || c == ';' ||
            c == '=' || c == '%') {
            *out++ = c;
        } else {
            /* Encode as %XX */
            snprintf(out, 4, "%%%02X", c);
            out += 3;
        }
    }
    *out = '\0';
    return encoded;
}

/**
 * Parse attributes from a string (similar to parse_ial_content but for image attributes)
 * Handles: width=300 style="float:left" "title"
 */
static apex_attributes *parse_image_attributes(const char *attr_str, int len) {
    apex_attributes *attrs = create_attributes();
    if (!attrs) return NULL;

    if (len <= 0 || !attr_str) return attrs;

    char buffer[2048];
    if (len >= (int)sizeof(buffer)) len = (int)sizeof(buffer) - 1;
    memcpy(buffer, attr_str, len);
    buffer[len] = '\0';

    char *p = buffer;
    while (*p) {
        /* Skip whitespace */
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Check for quoted title at the end ("title" or 'title') */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            char *title_start = p;
            while (*p && *p != quote) {
                if (*p == '\\' && *(p+1)) p++; /* Skip escaped char */
                p++;
            }
            if (*p == quote) {
                *p = '\0';
                add_attribute(attrs, "title", title_start);
                *p = quote;
                p++;
            }
            continue;
        }

        /* Check for key=value */
        char *key_start = p;
        while (*p && *p != '=' && !isspace((unsigned char)*p)) p++;

        if (*p == '=') {
            /* Found key=value */
            char saved = *p;
            *p = '\0';
            char *key = strdup(key_start);
            *p = saved;
            p++; /* Skip = */

            /* Parse value (could be quoted or unquoted) */
            char *value = NULL;
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                char *value_start = p;
                while (*p && *p != quote) {
                    if (*p == '\\' && *(p+1)) p++; /* Skip escaped char */
                    p++;
                }
                if (*p == quote) {
                    *p = '\0';
                    value = strdup(value_start);
                    *p = quote;
                    p++;
                }
            } else {
                /* Unquoted value */
                char *value_start = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                char saved_val = *p;
                *p = '\0';
                value = strdup(value_start);
                *p = saved_val;
            }

            if (value) {
                add_attribute(attrs, key, value);
                free(value);
            }
            free(key);
            continue;
        }

        /* Unknown token, skip */
        p++;
    }

    return attrs;
}

/**
 * Create a new image attribute entry (always creates a new entry, doesn't reuse)
 */
static image_attr_entry *create_image_attr_entry(image_attr_entry **list, const char *url, int index) {
    if (!list || !url) return NULL;

    /* Create new entry - always create new, don't reuse by URL */
    image_attr_entry *entry = calloc(1, sizeof(image_attr_entry));
    if (entry) {
        entry->url = strdup(url);
        entry->attrs = create_attributes();
        entry->index = index;
        entry->next = *list;
        *list = entry;
    }
    return entry;
}

/**
 * Free image attribute list
 */
void apex_free_image_attributes(image_attr_entry *img_attrs) {
    while (img_attrs) {
        image_attr_entry *next = img_attrs->next;
        free(img_attrs->url);
        apex_free_attributes(img_attrs->attrs);
        free(img_attrs);
        img_attrs = next;
    }
}

/**
 * Check if text starting at 'p' looks like the start of attributes (key=value pattern)
 */
static bool looks_like_attribute_start(const char *p, const char *end) {
    if (!p || p >= end) return false;

    /* Skip whitespace */
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end) return false;

    /* Look for key= pattern (attribute name followed by =) */
    const char *key_start = p;
    while (p < end && *p != '=' && *p != ' ' && *p != '\t' && *p != ')') {
        p++;
    }

    if (p < end && *p == '=' && p > key_start) {
        /* Found key= pattern - this looks like attributes */
        return true;
    }

    /* Also check for quoted title at the end ("title" or 'title') */
    if (p < end && (*p == '"' || *p == '\'')) {
        return true;
    }

    return false;
}

/**
 * Preprocess markdown to extract image attributes and URL-encode all link URLs
 */
char *apex_preprocess_image_attributes(const char *text, image_attr_entry **img_attrs, apex_mode_t mode) {
    if (!text) return NULL;

    /* Check if we should do URL encoding */
    bool do_url_encoding = (mode == APEX_MODE_UNIFIED ||
                            mode == APEX_MODE_MULTIMARKDOWN ||
                            mode == APEX_MODE_KRAMDOWN);

    /* Check if we should process image attributes */
    bool do_image_attrs = (mode == APEX_MODE_UNIFIED ||
                           mode == APEX_MODE_MULTIMARKDOWN);

    if (!do_url_encoding && !do_image_attrs) {
        /* Nothing to do */
        return NULL;
    }
    size_t text_len = strlen(text);
    size_t capacity = text_len * 3 + 1; /* Extra space for URL encoding expansion */
    char *output = malloc(capacity);
    if (!output) return NULL;

    const char *read = text;
    char *write = output;
    size_t remaining = capacity;
    image_attr_entry *local_img_attrs = NULL;

    while (*read) {
        /* Look for inline images: ![alt](url attributes) */
        if (*read == '!' && read[1] == '[') {
            const char *img_start = read;
            read += 2; /* Skip ![ */

            /* Find closing ] for alt text */
            const char *alt_end = strchr(read, ']');
            if (alt_end && alt_end[1] == '(') {
                /* Found ![alt]\( */
                const char *url_start = alt_end + 2; /* After ]( */
                const char *p = url_start;
                const char *url_end = NULL;
                const char *attr_start = NULL;
                const char *paren_end = NULL;

                /* Find the closing paren first */
                while (*p && *p != ')' && *p != '\n') p++;
                if (*p == ')') {
                    paren_end = p;
                } else {
                    /* No closing paren found - malformed, skip */
                    read = url_start;
                    continue;
                }

                /* Scan forward from url_start looking for attribute patterns */
                /* Attributes typically start with a space followed by key=value */
                p = url_start;
                while (p < paren_end) {
                    if (*p == ' ' || *p == '\t') {
                        /* Found a space - check if what follows looks like attributes */
                        const char *after_space = p;
                        while (after_space < paren_end && (*after_space == ' ' || *after_space == '\t')) after_space++;

                        if (after_space < paren_end && looks_like_attribute_start(after_space, paren_end)) {
                            /* This looks like the start of attributes */
                            attr_start = after_space;
                            url_end = p; /* URL ends before this space */
                            break;
                        }
                    }
                    p++;
                }

                /* If no attributes found, URL goes to closing paren */
                if (!url_end) {
                    url_end = paren_end;
                }

                /* If image attributes disabled, treat everything as URL */
                if (!do_image_attrs) {
                    attr_start = NULL;
                    url_end = paren_end;
                }

                if (url_end && url_end > url_start) {
                    /* Extract URL */
                    size_t url_len = url_end - url_start;
                    char *url = malloc(url_len + 1);
                    if (url) {
                        memcpy(url, url_start, url_len);
                        url[url_len] = '\0';

                        /* Extract attributes if present */
                        apex_attributes *attrs = NULL;
                        if (attr_start && attr_start < paren_end && do_image_attrs) {
                            size_t attr_len = paren_end - attr_start;
                            attrs = parse_image_attributes(attr_start, attr_len);
                        }

                        /* URL encode the URL (if enabled) */
                        char *encoded_url = do_url_encoding ? url_encode(url) : strdup(url);
                        if (encoded_url) {
                            /* Store attributes with encoded URL - always create new entry (if image attrs enabled) */
                            if (attrs && do_image_attrs) {
                                /* Count existing entries to get index */
                                int img_index = 0;
                                for (image_attr_entry *e = local_img_attrs; e; e = e->next) img_index++;

                                image_attr_entry *entry = create_image_attr_entry(&local_img_attrs, encoded_url, img_index);
                                if (entry) {
                                    /* Copy attributes (don't merge) */
                                    for (int i = 0; i < attrs->attr_count; i++) {
                                        add_attribute(entry->attrs, attrs->keys[i], attrs->values[i]);
                                    }
                                    if (attrs->id) {
                                        entry->attrs->id = strdup(attrs->id);
                                    }
                                    for (int i = 0; i < attrs->class_count; i++) {
                                        add_class(entry->attrs, attrs->classes[i]);
                                    }
                                }
                            }

                            /* Write the image syntax up to URL */
                            size_t prefix_len = url_start - img_start;
                            if (prefix_len < remaining) {
                                memcpy(write, img_start, prefix_len);
                                write += prefix_len;
                                remaining -= prefix_len;
                            }

                            /* Write encoded URL */
                            size_t encoded_len = strlen(encoded_url);
                            if (encoded_len < remaining) {
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            } else {
                                /* Buffer too small, expand */
                                size_t written = write - output;
                                capacity = (written + encoded_len + 1) * 2;
                                char *new_output = realloc(output, capacity);
                                if (!new_output) {
                                    free(output);
                                    free(url);
                                    free(encoded_url);
                                    if (attrs) apex_free_attributes(attrs);
                                    apex_free_image_attributes(local_img_attrs);
                                    return NULL;
                                }
                                output = new_output;
                                write = output + written;
                                remaining = capacity - written;
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            }

                            /* Write closing ) - attributes are NOT written back, they're stored separately */
                            if (remaining > 0) {
                                *write++ = ')';
                                remaining--;
                            }
                            read = paren_end + 1;
                            free(encoded_url);
                            if (attrs) apex_free_attributes(attrs);
                        }
                        free(url);
                        continue;
                    }
                }
            }
        }

        /* Look for reference-style link definitions: [ref]: url attributes */
        /* Only process if there are attributes (indicating it's for an image) */
        if (*read == '[') {
            const char *ref_start = read;
            const char *ref_end = strchr(ref_start, ']');
            if (ref_end && ref_end[1] == ':' && (ref_end[2] == ' ' || ref_end[2] == '\t')) {
                /* Found [ref]: */
                const char *url_start = ref_end + 2;
                /* Skip whitespace */
                while (*url_start && (*url_start == ' ' || *url_start == '\t')) url_start++;

                const char *p = url_start;
                const char *url_end = NULL;
                const char *attr_start = NULL;

                /* Find URL end (space before attributes or newline/end) */
                while (*p && *p != '\n' && *p != '\r') {
                    if ((*p == ' ' || *p == '\t') && !attr_start) {
                        attr_start = p;
                        const char *check = p;
                        while (*check && (*check == ' ' || *check == '\t')) check++;
                        if (*check && *check != '\n' && *check != '\r') {
                            url_end = p;
                            p = check;
                            break;
                        } else {
                            attr_start = NULL;
                        }
                    }
                    p++;
                }

                if (!url_end) {
                    url_end = p; /* URL ends at newline */
                }

                if (url_end > url_start) {
                    /* Extract URL */
                    size_t url_len = url_end - url_start;
                    char *url = malloc(url_len + 1);
                    if (url) {
                        memcpy(url, url_start, url_len);
                        url[url_len] = '\0';

                        /* Extract attributes if present */
                        apex_attributes *attrs = NULL;
                        if (attr_start && do_image_attrs) {
                            const char *attr_end = p;
                            while (*attr_end && *attr_end != '\n' && *attr_end != '\r') attr_end++;
                            size_t attr_len = attr_end - attr_start;
                            attrs = parse_image_attributes(attr_start, attr_len);
                        }

                        /* URL encode the URL (if enabled) */
                        char *encoded_url = do_url_encoding ? url_encode(url) : strdup(url);
                        if (encoded_url) {
                            /* If has attributes, store them (assume it's for an image) */
                            /* Reference definitions use index -1 to indicate URL-based matching */
                            if (attrs && do_image_attrs) {
                                image_attr_entry *entry = create_image_attr_entry(&local_img_attrs, encoded_url, -1);
                                if (entry) {
                                    /* Copy attributes (don't merge) */
                                    for (int i = 0; i < attrs->attr_count; i++) {
                                        add_attribute(entry->attrs, attrs->keys[i], attrs->values[i]);
                                    }
                                    if (attrs->id) {
                                        entry->attrs->id = strdup(attrs->id);
                                    }
                                    for (int i = 0; i < attrs->class_count; i++) {
                                        add_class(entry->attrs, attrs->classes[i]);
                                    }
                                }
                            }

                            /* Write the reference up to URL */
                            size_t prefix_len = url_start - ref_start;
                            if (prefix_len < remaining) {
                                memcpy(write, ref_start, prefix_len);
                                write += prefix_len;
                                remaining -= prefix_len;
                            }

                            /* Write encoded URL */
                            size_t encoded_len = strlen(encoded_url);
                            if (encoded_len < remaining) {
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            } else {
                                size_t written = write - output;
                                capacity = (written + encoded_len + 1) * 2;
                                char *new_output = realloc(output, capacity);
                                if (!new_output) {
                                    free(output);
                                    free(url);
                                    free(encoded_url);
                                    if (attrs) apex_free_attributes(attrs);
                                    apex_free_image_attributes(local_img_attrs);
                                    return NULL;
                                }
                                output = new_output;
                                write = output + written;
                                remaining = capacity - written;
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            }

                            /* Write attributes if present */
                            if (attr_start && attrs && attr_start < p) {
                                const char *attr_p = attr_start;
                                const char *attr_end = p;
                                while (*attr_end && *attr_end != '\n' && *attr_end != '\r') attr_end++;
                                while (attr_p < attr_end) {
                                    if (remaining > 0) {
                                        *write++ = *attr_p++;
                                        remaining--;
                                    } else {
                                        attr_p++;
                                    }
                                }
                            }

                            /* Find end of line */
                            while (*p && *p != '\n' && *p != '\r') p++;

                            /* Copy newline */
                            if (*p == '\n' && remaining > 0) {
                                *write++ = *p++;
                                remaining--;
                            } else if (*p == '\n') {
                                p++;
                            } else if (*p == '\r') {
                                if (p[1] == '\n' && remaining >= 2) {
                                    *write++ = *p++;
                                    *write++ = *p++;
                                    remaining -= 2;
                                } else if (remaining > 0) {
                                    *write++ = *p++;
                                    remaining--;
                                } else {
                                    p++;
                                }
                            }

                            read = p;
                            free(encoded_url);
                            if (attrs) apex_free_attributes(attrs);
                        }
                        free(url);
                        continue;
                    }
                }
            }
        }

        /* Look for regular links: [text](url) - just URL encode */
        if (*read == '[' && (read == text || read[-1] != '!')) {
            const char *link_start = read;
            const char *link_text_end = strchr(link_start, ']');
            if (link_text_end && link_text_end[1] == '(') {
                /* Found [text]\( */
                const char *url_start = link_text_end + 2; /* After ]( */
                const char *p = url_start;
                const char *url_end = NULL;

                /* Find URL end (space or ) or newline) */
                while (*p && *p != ')' && *p != '\n') {
                    if (*p == ' ' || *p == '\t') {
                        /* Regular links don't have attributes, so space means end of URL */
                        url_end = p;
                        break;
                    }
                    p++;
                }

                if (!url_end && *p == ')') {
                    url_end = p;
                } else if (!url_end) {
                    url_end = p;
                }

                if (url_end > url_start) {
                    /* Extract URL */
                    size_t url_len = url_end - url_start;
                    char *url = malloc(url_len + 1);
                    if (url) {
                        memcpy(url, url_start, url_len);
                        url[url_len] = '\0';

                        /* URL encode (if enabled) */
                        char *encoded_url = do_url_encoding ? url_encode(url) : strdup(url);
                        if (encoded_url) {
                            /* Write link prefix */
                            size_t prefix_len = url_start - link_start;
                            if (prefix_len < remaining) {
                                memcpy(write, link_start, prefix_len);
                                write += prefix_len;
                                remaining -= prefix_len;
                            }

                            /* Write encoded URL */
                            size_t encoded_len = strlen(encoded_url);
                            if (encoded_len < remaining) {
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            } else {
                                size_t written = write - output;
                                capacity = (written + encoded_len + 1) * 2;
                                char *new_output = realloc(output, capacity);
                                if (!new_output) {
                                    free(output);
                                    free(url);
                                    free(encoded_url);
                                    apex_free_image_attributes(local_img_attrs);
                                    return NULL;
                                }
                                output = new_output;
                                write = output + written;
                                remaining = capacity - written;
                                memcpy(write, encoded_url, encoded_len);
                                write += encoded_len;
                                remaining -= encoded_len;
                            }

                            /* Write rest of link (space, closing paren, etc.) */
                            while (p < url_start + url_len + 100 && *p && *p != '\n') {
                                if (*p == ')') {
                                    if (remaining > 0) {
                                        *write++ = *p++;
                                        remaining--;
                                    } else {
                                        p++;
                                    }
                                    break;
                                }
                                p++;
                            }

                            read = p;
                            free(encoded_url);
                        }
                        free(url);
                        continue;
                    }
                }
            }
        }

        /* Regular character - copy as-is */
        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            /* Expand buffer */
            size_t written = write - output;
            capacity = (written + 1) * 2;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                free(output);
                apex_free_image_attributes(local_img_attrs);
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

    /* Return the image attributes list */
    *img_attrs = local_img_attrs;

    return output;
}

/**
 * Apply image attributes to image nodes in AST
 * Uses two matching strategies:
 * 1. First tries to match by index (position) for inline images
 * 2. Then tries to match by URL for reference-style images
 * This ensures inline images with same URL get different attributes,
 * while reference-style images share attributes from their definition.
 */
void apex_apply_image_attributes(cmark_node *document, image_attr_entry *img_attrs) {
    if (!document || !img_attrs) return;

    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type event;
    int image_index = 0;  /* Track position of images in document */
    bool *used_by_index = NULL;
    int attr_count = 0;

    /* Count attributes and allocate used array */
    for (image_attr_entry *e = img_attrs; e; e = e->next) attr_count++;
    if (attr_count > 0) {
        used_by_index = calloc(attr_count, sizeof(bool));
    }

    while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (event == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_IMAGE) {
            const char *url = cmark_node_get_url(node);
            image_attr_entry *matching = NULL;

            /* First try to match by index (for inline images, index >= 0) */
            int idx = 0;
            for (image_attr_entry *e = img_attrs; e; e = e->next, idx++) {
                if (e->index >= 0 && e->index == image_index && !used_by_index[idx]) {
                    matching = e;
                    used_by_index[idx] = true;
                    break;
                }
            }

            /* If no match by index, try by URL (for reference-style images, index == -1) */
            if (!matching && url) {
                idx = 0;
                for (image_attr_entry *e = img_attrs; e; e = e->next, idx++) {
                    if (e->index == -1 && !used_by_index[idx] && e->url && strcmp(e->url, url) == 0) {
                        /* Reference-style: don't mark as used, so it can match multiple images */
                        matching = e;
                        /* Don't set used_by_index for reference-style - they can be reused */
                        break;
                    }
                }
            }

            if (matching && matching->attrs) {
                /* Apply attributes to this image */
                char *attr_str = attributes_to_html(matching->attrs);
                if (attr_str) {
                    /* Merge with existing user_data if present */
                    char *existing = (char *)cmark_node_get_user_data(node);
                    if (existing) {
                        char *combined = malloc(strlen(existing) + strlen(attr_str) + 2);
                        if (combined) {
                            strcpy(combined, existing);
                            strcat(combined, " ");
                            strcat(combined, attr_str);
                            cmark_node_set_user_data(node, combined);
                            free(attr_str);
                        } else {
                            cmark_node_set_user_data(node, attr_str);
                        }
                    } else {
                        cmark_node_set_user_data(node, attr_str);
                    }
                }
            }

            image_index++;  /* Move to next image */
        }
    }

    free(used_by_index);
    cmark_iter_free(iter);
}

