/**
 * Apex CLI - Command-line interface for the Apex Markdown processor
 */

#include "apex/apex.h"
#include "../src/extensions/metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>

/* Profiling helpers (same as in apex.c) */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static bool profiling_enabled(void) {
    const char *env = getenv("APEX_PROFILE");
    return env && (strcmp(env, "1") == 0 || strcmp(env, "yes") == 0 || strcmp(env, "true") == 0);
}

#define PROFILE_START(name) \
    double name##_start = 0; \
    if (profiling_enabled()) { name##_start = get_time_ms(); }

#define PROFILE_END(name) \
    if (profiling_enabled()) { \
        double name##_elapsed = get_time_ms() - name##_start; \
        fprintf(stderr, "[PROFILE] %-30s: %8.2f ms\n", #name, name##_elapsed); \
    }

#define BUFFER_SIZE 4096

static void print_usage(const char *program_name) {
    fprintf(stderr, "Apex Markdown Processor v%s\n", apex_version_string());
    fprintf(stderr, "One Markdown processor to rule them all\n\n");
    fprintf(stderr, "Usage: %s [options] [file]\n\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --accept               Accept all Critic Markup changes (apply edits)\n");
    fprintf(stderr, "  --[no-]includes        Enable file inclusion (enabled by default in unified mode)\n");
    fprintf(stderr, "  --hardbreaks           Treat newlines as hard breaks\n");
    fprintf(stderr, "  -h, --help             Show this help message\n");
    fprintf(stderr, "  --header-anchors        Generate <a> anchor tags instead of header IDs\n");
    fprintf(stderr, "  --id-format FORMAT      Header ID format: gfm (default), mmd, or kramdown\n");
    fprintf(stderr, "                          (modes auto-set format; use this to override in unified mode)\n");
    fprintf(stderr, "  --[no-]alpha-lists     Support alpha list markers (a., b., c. and A., B., C.)\n");
    fprintf(stderr, "  --[no-]mixed-lists     Allow mixed list markers at same level (inherit type from first item)\n");
    fprintf(stderr, "  -m, --mode MODE        Processor mode: commonmark, gfm, mmd, kramdown, unified (default)\n");
    fprintf(stderr, "  --meta-file FILE       Load metadata from external file (YAML, MMD, or Pandoc format)\n");
    fprintf(stderr, "  --meta KEY=VALUE       Set metadata key-value pair (can be used multiple times, supports quotes and comma-separated pairs)\n");
    fprintf(stderr, "  --no-footnotes         Disable footnote support\n");
    fprintf(stderr, "  --no-ids                Disable automatic header ID generation\n");
    fprintf(stderr, "  --no-math              Disable math support\n");
    fprintf(stderr, "  --no-smart             Disable smart typography\n");
    fprintf(stderr, "  --no-tables            Disable table support\n");
    fprintf(stderr, "  -o, --output FILE      Write output to FILE instead of stdout\n");
    fprintf(stderr, "  --pretty               Pretty-print HTML with indentation and whitespace\n");
    fprintf(stderr, "  --[no-]autolink        Enable autolinking of URLs and email addresses\n");
    fprintf(stderr, "  --obfuscate-emails     Obfuscate email links/text using HTML entities\n");
    fprintf(stderr, "  --[no-]relaxed-tables  Enable relaxed table parsing (no separator rows required)\n");
    fprintf(stderr, "  --[no-]sup-sub         Enable MultiMarkdown-style superscript (^text^) and subscript (~text~) syntax\n");
    fprintf(stderr, "  --[no-]transforms      Enable metadata variable transforms [%%key:transform] (enabled by default in unified mode)\n");
    fprintf(stderr, "  --[no-]unsafe          Allow raw HTML in output (default: true for unified/mmd/kramdown, false for commonmark/gfm)\n");
    fprintf(stderr, "  --[no-]wikilinks       Enable wiki link syntax [[PageName]] (disabled by default)\n");
    fprintf(stderr, "  --embed-images         Embed local images as base64 data URLs in HTML output\n");
    fprintf(stderr, "  --base-dir DIR         Base directory for resolving relative paths (for images, includes, wiki links)\n");
    fprintf(stderr, "  --bibliography FILE     Bibliography file (BibTeX, CSL JSON, or CSL YAML) - can be used multiple times\n");
    fprintf(stderr, "  --csl FILE              Citation style file (CSL format)\n");
    fprintf(stderr, "  --indices               Enable index processing (mmark and TextIndex syntax)\n");
    fprintf(stderr, "  --no-indices            Disable index processing\n");
    fprintf(stderr, "  --no-index              Suppress index generation (markers still created)\n");
    fprintf(stderr, "  --no-bibliography       Suppress bibliography output\n");
    fprintf(stderr, "  --link-citations       Link citations to bibliography entries\n");
    fprintf(stderr, "  --show-tooltips         Show tooltips on citations\n");
    fprintf(stderr, "  --reject               Reject all Critic Markup changes (revert edits)\n");
    fprintf(stderr, "  -s, --standalone       Generate complete HTML document (with <html>, <head>, <body>)\n");
    fprintf(stderr, "  --css FILE, --style FILE  Link to CSS file in document head (requires --standalone, overrides CSS metadata)\n");
    fprintf(stderr, "  --title TITLE          Document title (requires --standalone, default: \"Document\")\n");
    fprintf(stderr, "  -v, --version          Show version information\n\n");
    fprintf(stderr, "If no file is specified, reads from stdin.\n");
}

static void print_version(void) {
    printf("Apex %s\n", apex_version_string());
    printf("Copyright (c) 2025 Brett Terpstra\n");
    printf("Licensed under MIT License\n");
}

static char *read_file(const char *filename, size_t *len) {
    PROFILE_START(file_read);
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        fprintf(stderr, "Error: Cannot determine file size\n");
        return NULL;
    }

    /* Allocate buffer */
    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    /* Read file */
    size_t bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    PROFILE_END(file_read);

    if (len) *len = bytes_read;
    return buffer;
}

static char *read_stdin(size_t *len) {
    size_t capacity = BUFFER_SIZE;
    size_t size = 0;
    char *buffer = malloc(capacity);

    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    /* Use read() system call directly for better control with pipes */
    int fd = fileno(stdin);
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer + size, BUFFER_SIZE)) > 0) {
        size += bytes_read;
        if (size + BUFFER_SIZE > capacity) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                fprintf(stderr, "Error: Memory allocation failed\n");
                return NULL;
            }
            buffer = new_buffer;
        }
    }

    /* Check if we encountered an error (not EOF) */
    if (bytes_read < 0) {
        free(buffer);
        fprintf(stderr, "Error: Error reading from stdin\n");
        return NULL;
    }

    buffer[size] = '\0';
    if (len) *len = size;
    return buffer;
}

int main(int argc, char *argv[]) {
    apex_options options = apex_options_default();
    const char *input_file = NULL;
    const char *output_file = NULL;
    const char *meta_file = NULL;
    apex_metadata_item *cmdline_metadata = NULL;
    char *allocated_base_dir = NULL;  /* Track if we allocated base_directory */

    /* Bibliography files (NULL-terminated array) */
    char **bibliography_files = NULL;
    size_t bibliography_count = 0;
    size_t bibliography_capacity = 4;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --mode requires an argument\n");
                return 1;
            }
            if (strcmp(argv[i], "commonmark") == 0) {
                options = apex_options_for_mode(APEX_MODE_COMMONMARK);
            } else if (strcmp(argv[i], "gfm") == 0) {
                options = apex_options_for_mode(APEX_MODE_GFM);
            } else if (strcmp(argv[i], "mmd") == 0 || strcmp(argv[i], "multimarkdown") == 0) {
                options = apex_options_for_mode(APEX_MODE_MULTIMARKDOWN);
            } else if (strcmp(argv[i], "kramdown") == 0) {
                options = apex_options_for_mode(APEX_MODE_KRAMDOWN);
            } else if (strcmp(argv[i], "unified") == 0) {
                options = apex_options_for_mode(APEX_MODE_UNIFIED);
            } else {
                fprintf(stderr, "Error: Unknown mode '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --output requires an argument\n");
                return 1;
            }
            output_file = argv[i];
        } else if (strcmp(argv[i], "--no-tables") == 0) {
            options.enable_tables = false;
        } else if (strcmp(argv[i], "--no-footnotes") == 0) {
            options.enable_footnotes = false;
        } else if (strcmp(argv[i], "--no-smart") == 0) {
            options.enable_smart_typography = false;
        } else if (strcmp(argv[i], "--no-math") == 0) {
            options.enable_math = false;
        } else if (strcmp(argv[i], "--includes") == 0) {
            options.enable_file_includes = true;
        } else if (strcmp(argv[i], "--no-includes") == 0) {
            options.enable_file_includes = false;
        } else if (strcmp(argv[i], "--hardbreaks") == 0) {
            options.hardbreaks = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--standalone") == 0) {
            options.standalone = true;
        } else if (strcmp(argv[i], "--css") == 0 || strcmp(argv[i], "--style") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: %s requires an argument\n", argv[i-1]);
                return 1;
            }
            options.stylesheet_path = argv[i];
            options.standalone = true;  /* Imply standalone if CSS is specified */
        } else if (strcmp(argv[i], "--title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --title requires an argument\n");
                return 1;
            }
            options.document_title = argv[i];
        } else if (strcmp(argv[i], "--pretty") == 0) {
            options.pretty = true;
        } else if (strcmp(argv[i], "--accept") == 0) {
            options.enable_critic_markup = true;
            options.critic_mode = 0;  /* CRITIC_ACCEPT */
        } else if (strcmp(argv[i], "--reject") == 0) {
            options.enable_critic_markup = true;
            options.critic_mode = 1;  /* CRITIC_REJECT */
        } else if (strcmp(argv[i], "--id-format") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --id-format requires an argument (gfm, mmd, or kramdown)\n");
                return 1;
            }
            if (strcmp(argv[i], "gfm") == 0) {
                options.id_format = 0;  /* GFM format */
            } else if (strcmp(argv[i], "mmd") == 0) {
                options.id_format = 1;  /* MMD format */
            } else if (strcmp(argv[i], "kramdown") == 0) {
                options.id_format = 2;  /* Kramdown format */
            } else {
                fprintf(stderr, "Error: --id-format must be 'gfm', 'mmd', or 'kramdown'\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--no-ids") == 0) {
            options.generate_header_ids = false;
        } else if (strcmp(argv[i], "--header-anchors") == 0) {
            options.header_anchors = true;
        } else if (strcmp(argv[i], "--relaxed-tables") == 0) {
            options.relaxed_tables = true;
        } else if (strcmp(argv[i], "--no-relaxed-tables") == 0) {
            options.relaxed_tables = false;
        } else if (strcmp(argv[i], "--alpha-lists") == 0) {
            options.allow_alpha_lists = true;
        } else if (strcmp(argv[i], "--no-alpha-lists") == 0) {
            options.allow_alpha_lists = false;
        } else if (strcmp(argv[i], "--mixed-lists") == 0) {
            options.allow_mixed_list_markers = true;
        } else if (strcmp(argv[i], "--no-mixed-lists") == 0) {
            options.allow_mixed_list_markers = false;
        } else if (strcmp(argv[i], "--unsafe") == 0) {
            options.unsafe = true;
        } else if (strcmp(argv[i], "--no-unsafe") == 0) {
            options.unsafe = false;
        } else if (strcmp(argv[i], "--sup-sub") == 0) {
            options.enable_sup_sub = true;
        } else if (strcmp(argv[i], "--no-sup-sub") == 0) {
            options.enable_sup_sub = false;
        } else if (strcmp(argv[i], "--autolink") == 0) {
            options.enable_autolink = true;
        } else if (strcmp(argv[i], "--no-autolink") == 0) {
            options.enable_autolink = false;
        } else if (strcmp(argv[i], "--obfuscate-emails") == 0) {
            options.obfuscate_emails = true;
        } else if (strcmp(argv[i], "--wikilinks") == 0) {
            options.enable_wiki_links = true;
        } else if (strcmp(argv[i], "--no-wikilinks") == 0) {
            options.enable_wiki_links = false;
        } else if (strcmp(argv[i], "--transforms") == 0) {
            options.enable_metadata_transforms = true;
        } else if (strcmp(argv[i], "--no-transforms") == 0) {
            options.enable_metadata_transforms = false;
        } else if (strcmp(argv[i], "--embed-images") == 0) {
            options.embed_images = true;
        } else if (strcmp(argv[i], "--base-dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --base-dir requires an argument\n");
                return 1;
            }
            options.base_directory = argv[i];
        } else if (strcmp(argv[i], "--bibliography") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --bibliography requires an argument\n");
                return 1;
            }
            /* Allocate or reallocate bibliography files array */
            if (!bibliography_files) {
                bibliography_files = malloc(bibliography_capacity * sizeof(char*));
                if (!bibliography_files) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    return 1;
                }
            } else if (bibliography_count >= bibliography_capacity) {
                bibliography_capacity *= 2;
                char **new_files = realloc(bibliography_files, bibliography_capacity * sizeof(char*));
                if (!new_files) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    return 1;
                }
                bibliography_files = new_files;
            }
            bibliography_files[bibliography_count++] = argv[i];
            options.enable_citations = true;  /* Enable citations when bibliography is provided */
        } else if (strcmp(argv[i], "--csl") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --csl requires an argument\n");
                return 1;
            }
            options.csl_file = argv[i];
            options.enable_citations = true;  /* Enable citations when CSL is provided */
        } else if (strcmp(argv[i], "--no-bibliography") == 0) {
            options.suppress_bibliography = true;
        } else if (strcmp(argv[i], "--link-citations") == 0) {
            options.link_citations = true;
        } else if (strcmp(argv[i], "--show-tooltips") == 0) {
            options.show_tooltips = true;
        } else if (strcmp(argv[i], "--indices") == 0) {
            options.enable_indices = true;
            options.enable_mmark_index_syntax = true;
            options.enable_textindex_syntax = true;
        } else if (strcmp(argv[i], "--no-indices") == 0) {
            options.enable_indices = false;
        } else if (strcmp(argv[i], "--no-index") == 0) {
            options.suppress_index = true;
        } else if (strcmp(argv[i], "--meta-file") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --meta-file requires an argument\n");
                return 1;
            }
            meta_file = argv[i];
        } else if (strncmp(argv[i], "--meta", 6) == 0) {
            const char *arg_value;
            if (strlen(argv[i]) > 6 && argv[i][6] == '=') {
                /* --meta=KEY=VALUE format */
                arg_value = argv[i] + 7;
            } else {
                /* --meta KEY=VALUE format */
                if (++i >= argc) {
                    fprintf(stderr, "Error: --meta requires an argument\n");
                    return 1;
                }
                arg_value = argv[i];
            }
            apex_metadata_item *new_meta = apex_parse_command_metadata(arg_value);
            if (new_meta) {
                /* Merge with existing command-line metadata */
                if (cmdline_metadata) {
                    apex_metadata_item *merged = apex_merge_metadata(cmdline_metadata, new_meta, NULL);
                    apex_free_metadata(cmdline_metadata);
                    apex_free_metadata(new_meta);
                    cmdline_metadata = merged;
                } else {
                    cmdline_metadata = new_meta;
                }
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            /* Assume it's the input file */
            input_file = argv[i];
        }
    }

    /* Set base_directory from input file if not already set */
    if (input_file && !options.base_directory) {
        char *input_path_copy = strdup(input_file);
        if (input_path_copy) {
            char *dir = dirname(input_path_copy);
            if (dir && dir[0] != '\0' && strcmp(dir, ".") != 0) {
                allocated_base_dir = strdup(dir);
                options.base_directory = allocated_base_dir;
            }
            free(input_path_copy);
        }
    }

    /* Read input */
    size_t input_len;
    char *markdown;

    PROFILE_START(cli_total);
    if (input_file) {
        markdown = read_file(input_file, &input_len);
    } else {
        PROFILE_START(stdin_read);
        markdown = read_stdin(&input_len);
        PROFILE_END(stdin_read);
    }

    if (!markdown) {
        if (cmdline_metadata) apex_free_metadata(cmdline_metadata);
        return 1;
    }

    /* Load metadata from file if specified */
    PROFILE_START(metadata_file_load);
    apex_metadata_item *file_metadata = NULL;
    if (meta_file) {
        file_metadata = apex_load_metadata_from_file(meta_file);
        if (!file_metadata) {
            fprintf(stderr, "Warning: Could not load metadata from file '%s'\n", meta_file);
        }
    }
    PROFILE_END(metadata_file_load);

    /* Extract document metadata to merge with external sources
     * We'll extract it here and then inject the merged result */
    PROFILE_START(metadata_extract_cli);
    apex_metadata_item *doc_metadata = NULL;
    size_t doc_metadata_end = 0;

    if (options.mode == APEX_MODE_MULTIMARKDOWN ||
        options.mode == APEX_MODE_KRAMDOWN ||
        options.mode == APEX_MODE_UNIFIED) {
        /* Make a copy to extract metadata without modifying original */
        char *doc_copy = malloc(input_len + 1);
        if (doc_copy) {
            memcpy(doc_copy, markdown, input_len);
            doc_copy[input_len] = '\0';
            char *doc_ptr = doc_copy;
            doc_metadata = apex_extract_metadata(&doc_ptr);
            if (doc_metadata) {
                /* Calculate where metadata ended in original */
                doc_metadata_end = doc_ptr - doc_copy;
            }
            free(doc_copy);
        }
    }
    PROFILE_END(metadata_extract_cli);

    /* Merge metadata in priority order: file -> document -> command-line */
    PROFILE_START(metadata_merge);
    apex_metadata_item *merged_metadata = NULL;
    if (file_metadata || doc_metadata || cmdline_metadata) {
        merged_metadata = apex_merge_metadata(
            file_metadata,
            doc_metadata,
            cmdline_metadata,
            NULL
        );
    }
    PROFILE_END(metadata_merge);

    /* Build enhanced markdown with merged metadata as YAML front matter */
    PROFILE_START(metadata_yaml_build);
    char *enhanced_markdown = NULL;
    size_t enhanced_len = input_len;

    if (merged_metadata) {
        /* Build YAML front matter from merged metadata */
        size_t yaml_size = 512;
        char *yaml_buf = malloc(yaml_size);
        if (yaml_buf) {
            size_t yaml_pos = 0;
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '-';
            yaml_buf[yaml_pos++] = '\n';

            /* Use extracted metadata position if available */
            bool has_existing_metadata = (doc_metadata_end > 0);
            size_t metadata_start_pos = 0;
            size_t metadata_end_pos = doc_metadata_end;

            /* Add all merged metadata */
            apex_metadata_item *item = merged_metadata;
            while (item) {
                /* Escape value if it contains special characters */
                bool needs_quotes = strchr(item->value, ':') || strchr(item->value, '\n') ||
                                    strchr(item->value, '"') || strchr(item->value, '\\');

                size_t needed = strlen(item->key) + strlen(item->value) + (needs_quotes ? 4 : 0) + 10;
                if (yaml_pos + needed >= yaml_size) {
                    yaml_size = (yaml_pos + needed) * 2;
                    char *new_buf = realloc(yaml_buf, yaml_size);
                    if (!new_buf) {
                        free(yaml_buf);
                        yaml_buf = NULL;
                        break;
                    }
                    yaml_buf = new_buf;
                }

                if (needs_quotes) {
                    int written = snprintf(yaml_buf + yaml_pos, yaml_size - yaml_pos, "%s: \"%s\"\n", item->key, item->value);
                    if (written > 0) yaml_pos += written;
                } else {
                    int written = snprintf(yaml_buf + yaml_pos, yaml_size - yaml_pos, "%s: %s\n", item->key, item->value);
                    if (written > 0) yaml_pos += written;
                }
                item = item->next;
            }

            if (yaml_buf) {
                /* Add closing --- */
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '-';
                yaml_buf[yaml_pos++] = '\n';
                yaml_buf[yaml_pos] = '\0';

                if (has_existing_metadata) {
                    /* Replace existing metadata */
                    size_t before_len = metadata_start_pos;
                    size_t after_len = input_len - metadata_end_pos;
                    enhanced_len = before_len + yaml_pos + after_len;
                    enhanced_markdown = malloc(enhanced_len + 1);
                    if (enhanced_markdown) {
                        if (before_len > 0) {
                            memcpy(enhanced_markdown, markdown, before_len);
                        }
                        memcpy(enhanced_markdown + before_len, yaml_buf, yaml_pos);
                        if (after_len > 0) {
                            memcpy(enhanced_markdown + before_len + yaml_pos, markdown + metadata_end_pos, after_len);
                        }
                        enhanced_markdown[enhanced_len] = '\0';
                    }
                } else {
                    /* Prepend metadata */
                    enhanced_len = yaml_pos + input_len;
                    enhanced_markdown = malloc(enhanced_len + 1);
                    if (enhanced_markdown) {
                        memcpy(enhanced_markdown, yaml_buf, yaml_pos);
                        memcpy(enhanced_markdown + yaml_pos, markdown, input_len);
                        enhanced_markdown[enhanced_len] = '\0';
                    }
                }
                free(yaml_buf);
            }
        }
    }
    PROFILE_END(metadata_yaml_build);

    /* Set bibliography files in options (NULL-terminated array) */
    char **saved_bibliography_files = NULL;
    if (bibliography_count > 0) {
        bibliography_files = realloc(bibliography_files, (bibliography_count + 1) * sizeof(char*));
        if (bibliography_files) {
            bibliography_files[bibliography_count] = NULL;  /* NULL terminator */
            options.bibliography_files = bibliography_files;
            /* Save reference in case metadata mode resets options */
            saved_bibliography_files = bibliography_files;
        }
    }

    /* Apply metadata to options - allows per-document control of command-line options */
    /* Note: Bibliography file loading from metadata will be handled in citations extension */
    if (merged_metadata) {
        apex_apply_metadata_to_options(merged_metadata, &options);
        /* Restore bibliography files if they were lost (e.g., if mode was set in metadata) */
        if (saved_bibliography_files && !options.bibliography_files) {
            options.bibliography_files = saved_bibliography_files;
        }
    }

    /* Use enhanced markdown if we created it, otherwise use original */
    char *final_markdown = enhanced_markdown ? enhanced_markdown : markdown;
    size_t final_len = enhanced_markdown ? enhanced_len : input_len;

    /* Convert to HTML */
    char *html = apex_markdown_to_html(final_markdown, final_len, &options);

    /* Cleanup */
    if (enhanced_markdown) free(enhanced_markdown);
    free(markdown);
    if (file_metadata) apex_free_metadata(file_metadata);
    if (doc_metadata) apex_free_metadata(doc_metadata);
    if (cmdline_metadata) apex_free_metadata(cmdline_metadata);
    if (merged_metadata) apex_free_metadata(merged_metadata);

    if (!html) {
        fprintf(stderr, "Error: Conversion failed\n");
        return 1;
    }

    /* Write output */
    PROFILE_START(file_write);
    if (output_file) {
        FILE *fp = fopen(output_file, "w");
        if (!fp) {
            fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
            apex_free_string(html);
            return 1;
        }
        size_t html_len = strlen(html);
        fwrite(html, 1, html_len, fp);
        fclose(fp);
    } else {
        size_t html_len = strlen(html);
        fwrite(html, 1, html_len, stdout);
        /* Don't fflush - let the system buffer for better performance */
    }
    PROFILE_END(file_write);

    PROFILE_END(cli_total);

    apex_free_string(html);

    /* Free bibliography files array */
    if (bibliography_files) {
        free(bibliography_files);
    }

    /* Free base_directory if we allocated it */
    if (allocated_base_dir) {
        free(allocated_base_dir);
    }

    return 0;
}
