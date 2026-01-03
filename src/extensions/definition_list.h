/**
 * Definition List Extension for Apex
 *
 * Supports Kramdown/PHP Markdown Extra style definition lists:
 * Term
 * : Definition 1
 * : Definition 2
 */

#ifndef APEX_DEFINITION_LIST_H
#define APEX_DEFINITION_LIST_H

#include <stdbool.h>
#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Custom node types for definition lists */
/* Note: APEX_NODE_DEFINITION_* are defined as enum values in parser.h, not as variables */

/**
 * Process definition lists via preprocessing
 * Converts : syntax to HTML before main parsing
 * @param text The markdown text to process
 * @param unsafe If true, allow raw HTML in output (pass CMARK_OPT_UNSAFE)
 */
char *apex_process_definition_lists(const char *text, bool unsafe);

/**
 * Create and return the definition list extension
 */
cmark_syntax_extension *create_definition_list_extension(void);

#ifdef __cplusplus
}
#endif

#endif /* APEX_DEFINITION_LIST_H */

