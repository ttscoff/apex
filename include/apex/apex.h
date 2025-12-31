/**
 * Apex - Unified Markdown Processor
 *
 * A comprehensive Markdown processor with support for CommonMark, GFM,
 * MultiMarkdown, Kramdown, and Marked's special syntax extensions.
 */

#ifndef APEX_H
#define APEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

#define APEX_VERSION_MAJOR 0
#define APEX_VERSION_MINOR 1
#define APEX_VERSION_PATCH 42
#define APEX_VERSION_STRING "0.1.42"

/**
 * Processor compatibility modes
 */
#ifndef APEX_MODE_DEFINED
#define APEX_MODE_DEFINED
typedef enum {
    APEX_MODE_COMMONMARK = 0,      /* Pure CommonMark spec */
    APEX_MODE_GFM = 1,              /* GitHub Flavored Markdown */
    APEX_MODE_MULTIMARKDOWN = 2,    /* MultiMarkdown compatibility */
    APEX_MODE_KRAMDOWN = 3,         /* Kramdown compatibility */
    APEX_MODE_UNIFIED = 4           /* All features enabled */
} apex_mode_t;
#endif

/**
 * Configuration options for the parser and renderer
 */
typedef struct {
    apex_mode_t mode;

    /* Feature flags */
    bool enable_plugins;  /* Enable external/plugin processing */
    bool enable_tables;
    bool enable_footnotes;
    bool enable_definition_lists;
    bool enable_smart_typography;
    bool enable_math;
    bool enable_critic_markup;
    bool enable_wiki_links;
    bool enable_task_lists;
    bool enable_attributes;
    bool enable_callouts;
    bool enable_marked_extensions;
    bool enable_divs;  /* Enable Pandoc fenced divs (Unified mode only) */
    bool enable_spans;  /* Enable bracketed spans [text]{IAL} (Pandoc-style) */

    /* Critic markup mode */
    int critic_mode;  /* 0=markup (default), 1=accept, 2=reject */

    /* Metadata handling */
    bool strip_metadata;
    bool enable_metadata_variables;  /* [%key] replacement */
    bool enable_metadata_transforms; /* [%key:transform] transforms */

    /* File inclusion */
    bool enable_file_includes;
    int max_include_depth;
    const char *base_directory;

    /* Output options */
    bool unsafe;  /* Allow raw HTML */
    bool validate_utf8;
    bool github_pre_lang;  /* Use GitHub code block language format */
    bool standalone;  /* Generate complete HTML document */
    bool pretty;      /* Pretty-print HTML with indentation */
    const char *stylesheet_path;  /* Path to CSS file to link in head */
    const char *document_title;   /* Title for HTML document */

    /* Line break handling */
    bool hardbreaks;  /* Treat newlines as hard breaks (GFM style) */
    bool nobreaks;    /* Render soft breaks as spaces */

    /* Header ID generation */
    bool generate_header_ids;  /* Generate IDs for headers */
    bool header_anchors;  /* Generate <a> anchor tags instead of header IDs */
    int id_format;  /* 0=GFM (with dashes), 1=MMD (no dashes) */

    /* Table options */
    bool relaxed_tables;  /* Support tables without separator rows (kramdown/unified only) */
    int caption_position;  /* 0=above, 1=below (default: 1=below) */

    /* List options */
    bool allow_mixed_list_markers;  /* Allow mixed list markers at same level (inherit type from first item) */
    bool allow_alpha_lists;  /* Support alpha list markers (a., b., c. and A., B., C.) */

    /* Superscript and subscript */
    bool enable_sup_sub;  /* Support MultiMarkdown-style ^text^ and ~text~ syntax */

    /* Autolink options */
    bool enable_autolink;  /* Enable autolinking of URLs and email addresses */
    bool obfuscate_emails; /* Obfuscate email links/text using HTML entities */

    /* Image embedding options */
    bool embed_images;  /* Embed local images as base64 data URLs */

    /* Citation options */
    bool enable_citations;  /* Enable citation processing */
    char **bibliography_files;  /* NULL-terminated array of bibliography file paths */
    const char *csl_file;  /* CSL style file path */
    bool suppress_bibliography;  /* Suppress bibliography output */
    bool link_citations;  /* Link citations to bibliography entries */
    bool show_tooltips;  /* Show tooltips on citations */
    const char *nocite;  /* Comma-separated citation keys to include without citing, or "*" for all */

    /* Index options */
    bool enable_indices;  /* Enable index processing */
    bool enable_mmark_index_syntax;  /* Enable mmark (!item) syntax */
    bool enable_textindex_syntax;  /* Enable TextIndex {^} syntax */
    bool suppress_index;  /* Suppress index output */
    bool group_index_by_letter;  /* Group index entries by first letter */

    /* Wiki link options */
    int wikilink_space;  /* Space replacement: 0=dash, 1=none, 2=underscore, 3=space */
    const char *wikilink_extension;  /* File extension to append (e.g., "html") */

    /* Script injection options */
    /* Raw <script>...</script> HTML snippets to inject either:
     * - Before </body> when generating standalone HTML
     * - At the end of the HTML fragment in snippet mode
     *
     * This is typically populated by the CLI --script flag.
     */
    char **script_tags;  /* NULL-terminated array of script tag strings (may be NULL for none) */

    /* Stylesheet embedding options */
    /* When true and a stylesheet path is provided, Apex will attempt to
     * read the CSS file and embed it directly into a <style> block in the
     * document head instead of emitting a <link rel="stylesheet"> tag.
     * This is typically enabled via the CLI --embed-css flag.
     */
    bool embed_stylesheet;

    /* ARIA accessibility options */
    bool enable_aria;  /* Add ARIA labels and accessibility attributes to HTML output */

    /* Source file information for plugins */
    /* When Apex is invoked on a file, this is the full path to that file. */
    /* When reading from stdin, this is either the base directory (if set) or empty. */
    const char *input_file_path;
} apex_options;

/**
 * Get default options for a specific mode
 */
apex_options apex_options_default(void);
apex_options apex_options_for_mode(apex_mode_t mode);

/**
 * Main conversion function: Markdown to HTML
 *
 * @param markdown Input markdown text
 * @param len Length of input text
 * @param options Processing options (NULL for defaults)
 * @return Newly allocated HTML string (must be freed with apex_free_string)
 */
char *apex_markdown_to_html(const char *markdown, size_t len, const apex_options *options);

/**
 * Wrap HTML content in complete HTML5 document structure
 *
 * @param content HTML content to wrap
 * @param title Document title (NULL for default)
 * @param stylesheet_path Path to CSS file to link (NULL for none)
 * @param html_header Raw HTML to insert in <head> section (NULL for none)
 * @param html_footer Raw HTML to append before </body> (NULL for none)
 * @param language Language code for <html lang> attribute (NULL for "en")
 * @return Newly allocated HTML document string (must be freed with apex_free_string)
 */
char *apex_wrap_html_document(const char *content, const char *title, const char *stylesheet_path, const char *html_header, const char *html_footer, const char *language);

/**
 * Pretty-print HTML with proper indentation
 *
 * @param html HTML to format
 * @return Newly allocated formatted HTML string (must be freed with apex_free_string)
 */
char *apex_pretty_print_html(const char *html);

/**
 * Free a string allocated by Apex
 */
void apex_free_string(char *str);

/**
 * Get version information
 */
const char *apex_version_string(void);
int apex_version_major(void);
int apex_version_minor(void);
int apex_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif /* APEX_H */
