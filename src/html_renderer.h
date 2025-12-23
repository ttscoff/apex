/**
 * Custom HTML Renderer for Apex
 * Extends cmark-gfm's HTML renderer to support IAL attributes
 */

#ifndef APEX_HTML_RENDERER_H
#define APEX_HTML_RENDERER_H

#include "cmark-gfm.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Render document to HTML with IAL attribute support
 * This is a wrapper around cmark_render_html that injects attributes
 */
char *apex_render_html_with_attributes(cmark_node *document, int options);

/**
 * Inject header IDs into HTML output
 * @param html The HTML output
 * @param document The AST document
 * @param generate_ids Whether to generate IDs
 * @param use_anchors Whether to use <a> anchor tags instead of header IDs
 * @param id_format 0=GFM (with dashes), 1=MMD (no dashes)
 * @return Newly allocated HTML with IDs injected
 */
char *apex_inject_header_ids(const char *html, cmark_node *document, bool generate_ids, bool use_anchors, int id_format);

/**
 * Clean up HTML tag spacing
 * - Compresses multiple spaces in tags to single spaces
 * - Removes spaces before closing >
 * @param html The HTML to clean
 * @return Newly allocated cleaned HTML (must be freed)
 */
char *apex_clean_html_tag_spacing(const char *html);

/**
 * Collapse newlines and surrounding whitespace *between* adjacent tags in
 * non-pretty HTML. For example:
 *   </table>\n\n<figure>  ->  </table><figure>
 *
 * Only affects whitespace between a closing '>' and the next '<' where there
 * is at least one newline, leaving text content and code blocks untouched.
 * @param html The HTML to process
 * @return Newly allocated HTML with inter-tag newlines collapsed (must be freed)
 */
char *apex_collapse_intertag_newlines(const char *html);

/**
 * Convert thead to tbody for relaxed tables
 * Converts <thead><tr><th>...</th></tr></thead> to <tbody><tr><td>...</td></tr></tbody>
 * for tables that were created from relaxed table input (no separator rows)
 * @param html The HTML to process
 * @return Newly allocated HTML with relaxed table thead converted to tbody (must be freed)
 */
char *apex_convert_relaxed_table_headers(const char *html);

/**
 * Remove blank lines within tables
 * Removes lines containing only whitespace/newlines between <table> and </table> tags
 * @param html The HTML to process
 * @return Newly allocated HTML with blank lines removed from tables (must be freed)
 */
char *apex_remove_table_blank_lines(const char *html);

/**
 * Remove table rows that contain only em dashes (separator rows incorrectly rendered as data rows)
 * This happens when smart typography converts --- to — in separator rows
 * @param html The HTML to process
 * @return Newly allocated HTML with separator rows removed (must be freed)
 */
char *apex_remove_table_separator_rows(const char *html);

/**
 * Adjust header levels in HTML based on Base Header Level metadata
 * Shifts all headers by the specified offset (e.g., Base Header Level: 2 means h1->h2, h2->h3, etc.)
 * @param html The HTML to process
 * @param base_header_level The base header level (1-6, or 0 to disable)
 * @return Newly allocated HTML with adjusted header levels (must be freed)
 */
char *apex_adjust_header_levels(const char *html, int base_header_level);

/**
 * Adjust quote styles in HTML based on Quotes Language metadata
 * Replaces default English quote entities with language-specific quotes
 * @param html The HTML to process
 * @param quotes_language The quotes language (dutch/nl, english/en, french/fr, german/de, germanguillemets, spanish/es, swedish/sv, or NULL for default)
 * @return Newly allocated HTML with adjusted quotes (must be freed)
 */
char *apex_adjust_quote_language(const char *html, const char *quotes_language);

/**
 * Apply ARIA labels and accessibility attributes to HTML output
 * Adds aria-label to TOC nav elements, role attributes to figures and tables,
 * and aria-describedby linking tables to their captions
 * @param html The HTML to process
 * @param document The AST document (currently unused but kept for API consistency)
 * @return Newly allocated HTML with ARIA attributes injected (must be freed)
 */
char *apex_apply_aria_labels(const char *html, cmark_node *document);

#ifdef __cplusplus
}
#endif

#endif /* APEX_HTML_RENDERER_H */

