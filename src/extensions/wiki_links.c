/**
 * Wiki Links Extension for Apex
 * Implementation
 */

#include "wiki_links.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#include "parser.h"
#include "inlines.h"
#include "html.h"

/* Special inline character for wiki links */
__attribute__((unused))
static const char WIKI_OPEN_CHAR = '[';

/* Default configuration */
static wiki_link_config default_config = {
    .base_path = "",
    .extension = "",
    .spaces_to_underscores = false
};

/**
 * Check if we have a wiki link at the current position
 * Returns the number of characters consumed, or 0 if not a wiki link
 */
static int scan_wiki_link(const char *input, int len) {
    if (len < 4) return 0;  /* Need at least [[x]] */

    /* Must start with [[ */
    if (input[0] != '[' || input[1] != '[') return 0;

    /* Find the closing ]] */
    for (int i = 2; i < len - 1; i++) {
        if (input[i] == ']' && input[i + 1] == ']') {
            /* Found closing ]] */
            return i + 2;  /* Return position after ]] */
        }
    }

    return 0;  /* No closing ]] found */
}

/**
 * Parse wiki link content
 * Format: PageName or PageName|DisplayText or PageName#Section
 */
static void parse_wiki_link(const char *content, int len,
                            char **page, char **display, char **section) {
    *page = NULL;
    *display = NULL;
    *section = NULL;

    if (len <= 0) return;

    /* Copy content for parsing */
    char *text = malloc(len + 1);
    if (!text) return;
    memcpy(text, content, len);
    text[len] = '\0';

    /* Check for | separator (display text) */
    char *pipe = strchr(text, '|');
    if (pipe) {
        *pipe = '\0';
        *page = strdup(text);
        *display = strdup(pipe + 1);
    } else {
        *page = strdup(text);
    }

    /* Check for # separator (section) */
    if (*page) {
        char *hash = strchr(*page, '#');
        if (hash) {
            *hash = '\0';
            *section = strdup(hash + 1);
        }
    }

    free(text);
}

/**
 * Convert page name to URL
 */
static char *page_to_url(const char *page, const char *section, wiki_link_config *config) {
    if (!page) return NULL;

    size_t url_len = strlen(config->base_path) + strlen(page) +
                     (section ? strlen(section) + 1 : 0) +
                     strlen(config->extension) + 10;

    char *url = malloc(url_len);
    if (!url) return NULL;

    char *p = url;

    /* Add base path */
    strcpy(p, config->base_path);
    p += strlen(config->base_path);

    /* Add page name (converting spaces if needed) */
    for (const char *s = page; *s; s++) {
        if (*s == ' ' && config->spaces_to_underscores) {
            *p++ = '_';
        } else {
            *p++ = *s;
        }
    }

    /* Add extension */
    strcpy(p, config->extension);
    p += strlen(config->extension);

    /* Add section anchor */
    if (section) {
        *p++ = '#';
        strcpy(p, section);
    } else {
        *p = '\0';
    }

    return url;
}

/**
 * Match function - called when we encounter [
 * Need to check for [[ specifically to avoid conflicting with standard markdown links
 */
__attribute__((unused))
static cmark_node *match_wiki_link(cmark_syntax_extension *self,
                                    cmark_parser *parser,
                                    cmark_node *parent,
                                    unsigned char character,
                                    cmark_inline_parser *inline_parser) {
    (void)self;
    (void)parent;
    if (character != '[') return NULL;

    /* Get current position and remaining input */
    int pos = cmark_inline_parser_get_offset(inline_parser);
    cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);

    if (pos >= chunk->len) return NULL;

    const char *input = (const char *)chunk->data + pos;
    int remaining = chunk->len - pos;

    /* CRITICAL: Must check for [[ to distinguish from regular markdown links [text](url) */
    if (remaining < 2 || input[0] != '[' || input[1] != '[') {
        return NULL;  /* Not a wiki link, let standard link parser handle it */
    }

    /* Check if this is a wiki link */
    int consumed = scan_wiki_link(input, remaining);
    if (consumed == 0) return NULL;

    /* Extract the content between [[ and ]] */
    const char *content = input + 2;  /* Skip [[ */
    int content_len = consumed - 4;    /* Remove [[ and ]] */

    if (content_len <= 0) return NULL;

    /* Parse the wiki link */
    char *page = NULL;
    char *display = NULL;
    char *section = NULL;
    parse_wiki_link(content, content_len, &page, &display, &section);

    if (!page) return NULL;

    /* Get configuration */
    wiki_link_config *config = (wiki_link_config *)cmark_syntax_extension_get_private(self);
    if (!config) config = &default_config;

    /* Create URL */
    char *url = page_to_url(page, section, config);
    if (!url) {
        free(page);
        free(display);
        free(section);
        return NULL;
    }

    /* Create link node using parser memory */
    cmark_node *link = cmark_node_new_with_mem(CMARK_NODE_LINK, parser->mem);
    cmark_node_set_url(link, url);

    /* Create text node for display */
    cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
    const char *link_text = display ? display : page;
    cmark_node_set_literal(text, link_text);

    /* Add text as child of link */
    cmark_node_append_child(link, text);

    /* Set line/column info */
    link->start_line = text->start_line =
        link->end_line = text->end_line = cmark_inline_parser_get_line(inline_parser);
    link->start_column = text->start_column = cmark_inline_parser_get_column(inline_parser) - 1;
    link->end_column = text->end_column = cmark_inline_parser_get_column(inline_parser) + consumed - 1;

    /* Advance the parser by setting new offset */
    cmark_inline_parser_set_offset(inline_parser, pos + consumed);

    /* Clean up */
    free(page);
    free(display);
    free(section);
    free(url);

    return link;
}

/**
 * Set wiki link configuration
 */
void wiki_links_set_config(cmark_syntax_extension *ext, wiki_link_config *config) {
    if (ext && config) {
        cmark_syntax_extension_set_private(ext, config, NULL);
    }
}

/**
 * Process wiki links in text nodes via AST walking (postprocessing approach)
 * This avoids conflicts with standard markdown link syntax
 */
void apex_process_wiki_links_in_tree(cmark_node *node, wiki_link_config *config) {
    if (!node) return;

    /* Process current node if it's text */
    if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
        const char *literal = cmark_node_get_literal(node);
        if (!literal) goto recurse;

        /* Fast path: no wiki markers present */
        const char *first_marker = strstr(literal, "[[");
        if (!first_marker) goto recurse;

        /* Default configuration if none provided */
        if (!config) config = &default_config;

        /* Rebuild this text node in a single pass to avoid repeated rescans */
        const char *cursor = literal;
        cmark_node *insert_after = NULL;
        bool changed = false;

        while (1) {
            const char *open = strstr(cursor, "[[");
            if (!open) {
                /* Append any trailing text */
                size_t tail_len = strlen(cursor);
                if (tail_len > 0) {
                    char *tail = malloc(tail_len + 1);
                    if (tail) {
                        memcpy(tail, cursor, tail_len);
                        tail[tail_len] = '\0';
                        cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                        cmark_node_set_literal(text, tail);
                        free(tail);
                        if (!insert_after) {
                            cmark_node_insert_before(node, text);
                        } else {
                            cmark_node_insert_after(insert_after, text);
                        }
                        insert_after = text;
                        changed = true;
                    }
                }
                break;
            }

            /* Copy text preceding the wiki link */
            size_t prefix_len = (size_t)(open - cursor);
            if (prefix_len > 0) {
                char *prefix = malloc(prefix_len + 1);
                if (prefix) {
                    memcpy(prefix, cursor, prefix_len);
                    prefix[prefix_len] = '\0';
                    cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(text, prefix);
                    free(prefix);
                    if (!insert_after) {
                        cmark_node_insert_before(node, text);
                    } else {
                        cmark_node_insert_after(insert_after, text);
                    }
                    insert_after = text;
                    changed = true;
                }
            }

            /* Look for closing marker after [[ */
            const char *close = strstr(open + 2, "]]");
            if (!close) {
                /* No closing marker - treat the rest as plain text */
                size_t remaining_len = strlen(open);
                char *remaining = malloc(remaining_len + 1);
                if (remaining) {
                    memcpy(remaining, open, remaining_len);
                    remaining[remaining_len] = '\0';
                    cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(text, remaining);
                    free(remaining);
                    if (!insert_after) {
                        cmark_node_insert_before(node, text);
                    } else {
                        cmark_node_insert_after(insert_after, text);
                    }
                    insert_after = text;
                    changed = true;
                }
                break;
            }

            size_t content_len = (size_t)(close - (open + 2));

            /* If empty content, keep literal text and continue */
            if (content_len == 0) {
                size_t raw_len = (size_t)((close + 2) - open);
                char *raw = malloc(raw_len + 1);
                if (raw) {
                    memcpy(raw, open, raw_len);
                    raw[raw_len] = '\0';
                    cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(text, raw);
                    free(raw);
                    if (!insert_after) {
                        cmark_node_insert_before(node, text);
                    } else {
                        cmark_node_insert_after(insert_after, text);
                    }
                    insert_after = text;
                    changed = true;
                }
                cursor = close + 2;
                continue;
            }

            /* Parse wiki link content */
            char *page = NULL;
            char *display = NULL;
            char *section = NULL;
            parse_wiki_link(open + 2, (int)content_len, &page, &display, &section);

            if (page) {
                char *url = page_to_url(page, section, config);
                if (url) {
                    cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
                    cmark_node_set_url(link, url);

                    cmark_node *link_text = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(link_text, display ? display : page);
                    cmark_node_append_child(link, link_text);

                    if (!insert_after) {
                        cmark_node_insert_before(node, link);
                    } else {
                        cmark_node_insert_after(insert_after, link);
                    }
                    insert_after = link;
                    changed = true;
                    free(url);
                } else {
                    /* Fallback: keep literal text if URL creation fails */
                    size_t raw_len = (size_t)((close + 2) - open);
                    char *raw = malloc(raw_len + 1);
                    if (raw) {
                        memcpy(raw, open, raw_len);
                        raw[raw_len] = '\0';
                        cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                        cmark_node_set_literal(text, raw);
                        free(raw);
                        if (!insert_after) {
                            cmark_node_insert_before(node, text);
                        } else {
                            cmark_node_insert_after(insert_after, text);
                        }
                        insert_after = text;
                        changed = true;
                    }
                }
            } else {
                /* Fallback: keep literal text if parsing fails */
                size_t raw_len = (size_t)((close + 2) - open);
                char *raw = malloc(raw_len + 1);
                if (raw) {
                    memcpy(raw, open, raw_len);
                    raw[raw_len] = '\0';
                    cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(text, raw);
                    free(raw);
                    if (!insert_after) {
                        cmark_node_insert_before(node, text);
                    } else {
                        cmark_node_insert_after(insert_after, text);
                    }
                    insert_after = text;
                    changed = true;
                }
            }

            free(page);
            free(display);
            free(section);

            /* Move past the current wiki link */
            cursor = close + 2;
        }

        if (changed) {
            /* Remove the original text node after rebuilding */
            /* Unlink the node first, then free it. Since we're returning immediately
             * and the parent iteration already has the next sibling, this is safe. */
            cmark_node_unlink(node);
            cmark_node_free(node);
            return;  /* Don't recurse into children after modifying tree */
        }
    }

recurse:
    /* Recursively process children */
    /* Get next sibling before processing to avoid issues if child modifies/frees tree */
    for (cmark_node *child = cmark_node_first_child(node); child; ) {
        cmark_node *next = cmark_node_next(child);
        apex_process_wiki_links_in_tree(child, config);
        child = next;
    }
}

/**
 * Create the wiki links extension (simplified - actual processing done via postprocessing)
 */
cmark_syntax_extension *create_wiki_links_extension(void) {
    /* Return NULL - we handle wiki links via postprocessing now */
    return NULL;
}

