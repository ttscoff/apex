#include "apex/apex.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

/* cmark-gfm headers */
#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm-core-extensions.h"

/* Apex extensions */
#include "extensions/metadata.h"
#include "extensions/wiki_links.h"
#include "extensions/math.h"
#include "extensions/critic.h"
#include "extensions/callouts.h"
#include "extensions/includes.h"
#include "extensions/toc.h"
#include "extensions/abbreviations.h"
#include "extensions/emoji.h"
#include "extensions/special_markers.h"
#include "extensions/ial.h"
#include "extensions/definition_list.h"
#include "extensions/advanced_footnotes.h"
#include "extensions/advanced_tables.h"
#include "extensions/html_markdown.h"
#include "extensions/inline_footnotes.h"
#include "extensions/highlight.h"
#include "extensions/sup_sub.h"
#include "extensions/header_ids.h"
#include "extensions/relaxed_tables.h"
#include "extensions/citations.h"

/* Custom renderer */
#include "html_renderer.h"

/**
 * Encode a string as hexadecimal HTML entities (&#xNN;)
 * Caller must free the returned buffer.
 */
static char *apex_encode_hex_entities(const char *text, size_t len) {
    if (!text || len == 0) return NULL;
    /* Each char becomes &#xNN; => up to 6 chars */
    size_t cap = len * 6 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;

    char *w = out;
    for (size_t i = 0; i < len; i++) {
        int written = snprintf(w, cap - (size_t)(w - out), "&#x%02X;", (unsigned char)text[i]);
        if (written <= 0 || (size_t)written >= cap - (size_t)(w - out)) {
            free(out);
            return NULL;
        }
        w += written;
    }
    *w = '\0';
    return out;
}

/**
 * Base64 encode binary data
 * Caller must free the returned buffer.
 */
static char *apex_base64_encode(const unsigned char *data, size_t len) {
    if (!data || len == 0) return NULL;

    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    /* Base64 encoding increases size by ~33% (4 chars per 3 bytes) */
    size_t encoded_len = ((len + 2) / 3) * 4;
    char *encoded = malloc(encoded_len + 1);
    if (!encoded) return NULL;

    char *out = encoded;
    size_t i = 0;

    while (i < len) {
        unsigned char byte1 = data[i++];
        unsigned char byte2 = (i < len) ? data[i++] : 0;
        unsigned char byte3 = (i < len) ? data[i++] : 0;

        unsigned int combined = (byte1 << 16) | (byte2 << 8) | byte3;

        *out++ = base64_chars[(combined >> 18) & 0x3F];
        *out++ = base64_chars[(combined >> 12) & 0x3F];

        /* Check if we have at least 2 bytes (byte1 and byte2) */
        if (i >= 2) {
            *out++ = base64_chars[(combined >> 6) & 0x3F];
        } else {
            *out++ = '=';
        }

        /* Check if we have all 3 bytes */
        if (i >= 3) {
            *out++ = base64_chars[combined & 0x3F];
        } else {
            *out++ = '=';
        }
    }

    *out = '\0';
    return encoded;
}

/**
 * Obfuscate mailto: links in rendered HTML by converting href/text
 * characters to hexadecimal HTML entities.
 */
static char *apex_obfuscate_email_links(const char *html) {
    if (!html) return NULL;

    size_t cap = strlen(html) * 6 + 1; /* generous for expansion */
    char *out = malloc(cap);
    if (!out) return NULL;

    const char *p = html;
    char *w = out;
    bool in_mailto = false;

    while (*p) {
        /* Obfuscate href="mailto:... */
        if (!in_mailto && strncmp(p, "href=\"mailto:", 13) == 0) {
            const char *addr_start = p + 6; /* keep mailto: prefix in output */
            const char *addr_end = strchr(addr_start, '"');
            if (!addr_end) {
                *w++ = *p++;
                continue;
            }

            char *encoded = apex_encode_hex_entities(addr_start, (size_t)(addr_end - addr_start));
            if (encoded) {
                size_t needed = 6 + strlen(encoded) + 1; /* href=" + encoded + closing quote */
                size_t used = (size_t)(w - out);
                if (used + needed >= cap) {
                    cap = (used + needed + 1) * 2;
                    char *new_out = realloc(out, cap);
                    if (!new_out) {
                        free(out);
                        free(encoded);
                        return NULL;
                    }
                    out = new_out;
                    w = out + used;
                }
                memcpy(w, "href=\"", 6); w += 6;
                memcpy(w, encoded, strlen(encoded)); w += strlen(encoded);
                *w++ = '"';
                free(encoded);
                p = addr_end + 1;
                in_mailto = true;
                continue;
            }
        }

        /* Encode visible link text for mailto links */
        if (in_mailto && *p == '>') {
            *w++ = *p++;
            const char *text_start = p;
            while (*p && *p != '<') p++;
            char *encoded_text = apex_encode_hex_entities(text_start, (size_t)(p - text_start));
            if (encoded_text) {
                size_t needed = strlen(encoded_text);
                size_t used = (size_t)(w - out);
                if (used + needed >= cap) {
                    cap = (used + needed + 1) * 2;
                    char *new_out = realloc(out, cap);
                    if (!new_out) {
                        free(out);
                        free(encoded_text);
                        return NULL;
                    }
                    out = new_out;
                    w = out + used;
                }
                memcpy(w, encoded_text, needed);
                w += needed;
                free(encoded_text);
                continue;
            }
        }

        /* Detect end of link */
        if (in_mailto && strncmp(p, "</a", 3) == 0) {
            in_mailto = false;
        }

        *w++ = *p++;
    }

    *w = '\0';
    return out;
}

/**
 * Detect MIME type from file extension
 * Handles URLs with query parameters by stopping at ? or #
 */
static const char *apex_detect_mime_type(const char *filepath) {
    if (!filepath) return "image/png";  /* Default */

    const char *ext = strrchr(filepath, '.');
    if (!ext) return "image/png";  /* Default */
    ext++;

    /* Find the end of the extension (stop at ? or # for URLs) */
    const char *ext_end = ext;
    while (*ext_end && *ext_end != '?' && *ext_end != '#' && *ext_end != '/' && *ext_end != ' ') {
        ext_end++;
    }
    size_t ext_len = ext_end - ext;

    /* Case-insensitive comparison */
    if (ext_len == 3 && (strncasecmp(ext, "jpg", 3) == 0 || strncasecmp(ext, "jpeg", 3) == 0)) {
        return "image/jpeg";
    } else if (ext_len == 4 && strncasecmp(ext, "jpeg", 4) == 0) {
        return "image/jpeg";
    } else if (ext_len == 3 && strncasecmp(ext, "png", 3) == 0) {
        return "image/png";
    } else if (ext_len == 3 && strncasecmp(ext, "gif", 3) == 0) {
        return "image/gif";
    } else if (ext_len == 4 && strncasecmp(ext, "webp", 4) == 0) {
        return "image/webp";
    } else if (ext_len == 3 && strncasecmp(ext, "svg", 3) == 0) {
        return "image/svg+xml";
    } else if (ext_len == 3 && strncasecmp(ext, "bmp", 3) == 0) {
        return "image/bmp";
    } else if (ext_len == 3 && strncasecmp(ext, "ico", 3) == 0) {
        return "image/x-icon";
    }

    return "image/png";  /* Default */
}

/**
 * Resolve relative path from base directory
 */
static char *apex_resolve_image_path(const char *filepath, const char *base_dir) {
    if (!filepath) return NULL;

    /* If absolute path, return as-is */
    if (filepath[0] == '/') {
        return strdup(filepath);
    }

    /* Relative path - combine with base_dir */
    if (!base_dir || !*base_dir) {
        return strdup(filepath);
    }

    size_t len = strlen(base_dir) + strlen(filepath) + 2;
    char *resolved = malloc(len);
    if (!resolved) return NULL;

    snprintf(resolved, len, "%s/%s", base_dir, filepath);
    return resolved;
}

/**
 * Read local image file and encode as base64
 */
static char *apex_read_and_encode_image(const char *filepath) {
    if (!filepath) return NULL;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 10 * 1024 * 1024) {  /* Limit to 10MB */
        fclose(fp);
        return NULL;
    }

    /* Read content */
    unsigned char *content = malloc(size);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(content, 1, size, fp);
    fclose(fp);

    if (read != (size_t)size) {
        free(content);
        return NULL;
    }

    /* Encode to base64 */
    char *encoded = apex_base64_encode(content, size);
    free(content);
    return encoded;
}

/**
 * Embed images as base64 data URLs in HTML
 * Only supports local images - remote images are not embedded
 */
static char *apex_embed_images(const char *html, const apex_options *options, const char *base_directory) {
    if (!html || !options->embed_images) {
        return html ? strdup(html) : NULL;
    }

    size_t html_len = strlen(html);
    /* Allocate generous buffer - base64 encoding increases size by ~33% */
    size_t cap = html_len * 3 + 1024;
    char *output = malloc(cap);
    if (!output) return strdup(html);

    const char *read = html;
    char *write = output;
    size_t remaining = cap;

    while (*read) {
        /* Look for <img tag */
        if (*read == '<' && strncmp(read, "<img", 4) == 0) {
            const char *img_start = read;
            const char *img_end = strchr(img_start, '>');
            if (!img_end) {
                /* Malformed tag, copy as-is */
                if (remaining > 0) {
                    *write++ = *read++;
                    remaining--;
                } else {
                    read++;
                }
                continue;
            }

            /* Check if already a data URL */
            const char *src_attr = strstr(img_start, "src=\"");
            if (!src_attr || src_attr > img_end) {
                src_attr = strstr(img_start, "src='");
            }

            if (src_attr && src_attr < img_end) {
                const char quote_char = (src_attr[4] == '"') ? '"' : '\'';
                const char *url_start = src_attr + 5;
                const char *url_end = strchr(url_start, quote_char);
                if (url_end && url_end < img_end) {
                    size_t url_len = url_end - url_start;
                    char *url = malloc(url_len + 1);
                    if (url) {
                        memcpy(url, url_start, url_len);
                        url[url_len] = '\0';

                        /* Check if already a data URL */
                        bool is_data_url = (strncmp(url, "data:", 5) == 0);
                        bool is_remote = (strncmp(url, "http://", 7) == 0 ||
                                        strncmp(url, "https://", 8) == 0 ||
                                        strncmp(url, "//", 2) == 0);

                        char *data_url = NULL;
                        const char *mime_type = NULL;

                        if (!is_data_url && !is_remote && options->embed_images) {
                            /* Local image */
                            char *resolved_path = apex_resolve_image_path(url, base_directory);
                            if (resolved_path) {
                                struct stat st;
                                if (stat(resolved_path, &st) == 0 && S_ISREG(st.st_mode)) {
                                    char *encoded = apex_read_and_encode_image(resolved_path);
                                    if (encoded) {
                                        mime_type = apex_detect_mime_type(resolved_path);
                                        size_t data_url_len = strlen("data:") + strlen(mime_type) + strlen(";base64,") + strlen(encoded) + 1;
                                        data_url = malloc(data_url_len);
                                        if (data_url) {
                                            snprintf(data_url, data_url_len, "data:%s;base64,%s", mime_type, encoded);
                                        }
                                        free(encoded);
                                    }
                                }
                                free(resolved_path);
                            }
                        }

                        if (data_url) {
                            /* Replace the src attribute */
                            size_t before_src = src_attr - img_start;
                            size_t after_url = img_end - url_end;

                            /* Calculate new size needed */
                            size_t new_len = before_src + strlen("src=\"") + strlen(data_url) + 1 + after_url + 1;
                            if (new_len > remaining) {
                                size_t written = write - output;
                                cap = (written + new_len + 1) * 2;
                                char *new_output = realloc(output, cap);
                                if (!new_output) {
                                    free(data_url);
                                    free(url);
                                    free(output);
                                    return strdup(html);
                                }
                                output = new_output;
                                write = output + written;
                                remaining = cap - written;
                            }

                            /* Copy up to src= */
                            memcpy(write, img_start, before_src);
                            write += before_src;
                            remaining -= before_src;

                            /* Write new src attribute */
                            memcpy(write, "src=\"", 5);
                            write += 5;
                            remaining -= 5;
                            size_t data_url_len = strlen(data_url);
                            memcpy(write, data_url, data_url_len);
                            write += data_url_len;
                            remaining -= data_url_len;
                            *write++ = '"';
                            remaining--;

                            /* Copy rest of tag */
                            memcpy(write, url_end + 1, after_url);
                            write += after_url;
                            remaining -= after_url;

                            read = img_end + 1;
                            free(data_url);
                            free(url);
                            continue;
                        }

                        /* No data URL created, clean up */
                        free(url);
                    }
                }
            }

            /* No replacement, copy tag as-is */
            size_t tag_len = img_end - img_start + 1;
            if (tag_len > remaining) {
                size_t written = write - output;
                cap = (written + tag_len + 1) * 2;
                char *new_output = realloc(output, cap);
                if (!new_output) {
                    free(output);
                    return strdup(html);
                }
                output = new_output;
                write = output + written;
                remaining = cap - written;
            }
            memcpy(write, img_start, tag_len);
            write += tag_len;
            remaining -= tag_len;
            read = img_end + 1;
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

    *write = '\0';
    return output;
}

/**
 * Preprocess angle-bracket autolinks (<http://...>) into explicit links
 * and convert bare URLs/emails to explicit links so they survive
 * custom rendering paths.
 * Skips processing inside code spans (`...`) and code blocks (```...```).
 */
static char *apex_preprocess_autolinks(const char *text, const apex_options *options) {
    if (!text || !options || !options->enable_autolink) return NULL;

    size_t len = strlen(text);
    /* Worst case: every character becomes part of a [url](url) */
    size_t cap = len * 4 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;

    const char *r = text;
    char *w = out;
    bool in_code_block = false;
    bool in_inline_code = false;
    int code_block_backticks = 0;  /* Count of consecutive backticks for code blocks */

    while (*r) {
        const char *loop_start = r;

        /* Check if we're at the start of a reference link definition: [id]: URL */
        if (r == text || r[-1] == '\n') {
            const char *line_start = r;
            /* Skip leading whitespace */
            while (*line_start == ' ' || *line_start == '\t') {
                line_start++;
            }
            /* Check for [id]: pattern */
            if (*line_start == '[') {
                const char *id_end = strchr(line_start + 1, ']');
                if (id_end && id_end[1] == ':') {
                    /* This is a reference link definition - skip autolinking for this line */
                    const char *line_end = strchr(r, '\n');
                    if (!line_end) line_end = r + strlen(r);
                    /* Copy the entire line without processing */
                    size_t line_len = line_end - r;
                    if ((size_t)(w - out) + line_len + 1 > cap) {
                        size_t used = (size_t)(w - out);
                        cap = (used + line_len + 1) * 2;
                        char *new_out = realloc(out, cap);
                        if (!new_out) { free(out); return NULL; }
                        out = new_out;
                        w = out + used;
                    }
                    memcpy(w, r, line_len);
                    w += line_len;
                    r = line_end;
                    continue;
                }
            }
        }
        /* Track code blocks (```...```) */
        if (*r == '`') {
            int backtick_count = 1;
            const char *p = r + 1;
            while (*p == '`' && backtick_count < 10) {
                backtick_count++;
                p++;
            }

            if (backtick_count >= 3) {
                /* Code block marker */
                if (!in_code_block) {
                    in_code_block = true;
                    code_block_backticks = backtick_count;
                } else if (backtick_count >= code_block_backticks) {
                    /* End of code block */
                    in_code_block = false;
                    code_block_backticks = 0;
                }
                /* Copy the backticks */
                for (int i = 0; i < backtick_count; i++) {
                    *w++ = *r++;
                }
                continue;
            } else if (backtick_count == 1) {
                /* Inline code span - toggle state and copy the backtick */
                in_inline_code = !in_inline_code;
                *w++ = *r++;
                continue;
            } else {
                /* Multiple backticks but less than 3 - copy them as-is (shouldn't happen in valid markdown) */
                for (int i = 0; i < backtick_count; i++) {
                    *w++ = *r++;
                }
                continue;
            }
        }

        /* Skip processing inside code blocks or inline code */
        if (in_code_block || in_inline_code) {
            *w++ = *r++;
            continue;
        }

        /* Handle angle-bracket autolink */
        if (*r == '<') {
            const char *start = r + 1;
            const char *end = strchr(start, '>');
            if (end && start != end) {
                size_t url_len = (size_t)(end - start);
                if ((url_len > 6 && strncmp(start, "http://", 7) == 0) ||
                    (url_len > 7 && strncmp(start, "https://", 8) == 0) ||
                    (url_len > 7 && strncmp(start, "mailto:", 7) == 0)) {
                    size_t needed = 2 + url_len + 3 + url_len + 2; /* [url](url) */
                    if ((size_t)(w - out) + needed + 1 > cap) {
                        size_t used = (size_t)(w - out);
                        cap = (used + needed + 1) * 2;
                        char *new_out = realloc(out, cap);
                        if (!new_out) { free(out); return NULL; }
                        out = new_out;
                        w = out + used;
                    }
                    *w++ = '[';
                    memcpy(w, start, url_len); w += url_len;
                    *w++ = ']'; *w++ = '(';
                    memcpy(w, start, url_len); w += url_len;
                    *w++ = ')';
                    r = end + 1;
                    continue;
                }
            }
        }

        /* Skip HTML comments (like citation placeholders <!--CITE:key-->) */
        if (*r == '<' && r[1] == '!' && r[2] == '-' && r[3] == '-') {
            /* Find end of comment --> */
            const char *comment_end = strstr(r, "-->");
            if (comment_end) {
                size_t comment_len = (comment_end + 3) - r;
                if ((size_t)(w - out) + comment_len + 1 > cap) {
                    size_t used = (size_t)(w - out);
                    cap = (used + comment_len + 1) * 2;
                    char *new_out = realloc(out, cap);
                    if (!new_out) { free(out); return NULL; }
                    out = new_out;
                    w = out + used;
                }
                memcpy(w, r, comment_len);
                w += comment_len;
                r = comment_end + 3;
                continue;
            }
        }

        /* Handle bare URL or mailto/email */
        bool is_url_start = false;
        bool is_email_start = false;

        if (!isspace((unsigned char)*r)) {
            /* Check for URL protocols */
            if (strncmp(r, "http://", 7) == 0 || strncmp(r, "https://", 8) == 0 || strncmp(r, "mailto:", 7) == 0) {
                is_url_start = true;
            }
            /* Check for email address: must start at word boundary and have @ in current token */
            /* Skip if @ is part of a citation placeholder (<!--CITE:...) or citation syntax [@ */
            else if ((r == text || isspace((unsigned char)r[-1]) || r[-1] == '(' || r[-1] == '[') &&
                     !(r > text && r[-1] == '[' && *r == '@')) {  /* Skip [@ which is citation syntax */
                /* Scan forward to find end of current token */
                const char *token_end = r;
                while (*token_end && !isspace((unsigned char)*token_end) && *token_end != '<' && *token_end != '>') {
                    token_end++;
                }
                /* Check if @ exists within this token (not just anywhere in text) */
                const char *at_pos = r;
                while (at_pos < token_end && *at_pos != '@') {
                    at_pos++;
                }
                /* If @ found within token, and it's not at start/end, it might be an email */
                /* Also skip if @ is immediately after [ (citation syntax) */
                if (at_pos < token_end && at_pos > r && (at_pos + 1) < token_end &&
                    !(at_pos > text && at_pos[-1] == '[')) {  /* Not [@ */
                    /* Basic validation: has chars before @ and after @ */
                    is_email_start = true;
                }
            }
        }

        if (is_url_start || is_email_start) {
            const char *start = r;
            /* simple token end: whitespace or angle bracket */
            const char *end = start;
            while (*end && !isspace((unsigned char)*end) && *end != '<' && *end != '>') end++;
            size_t url_len = (size_t)(end - start);

            /* Trim trailing punctuation that should not be part of the link */
            while (url_len > 0 &&
                   (start[url_len - 1] == '.' ||
                    start[url_len - 1] == ',' ||
                    start[url_len - 1] == ';' ||
                    start[url_len - 1] == ':')) {
                url_len--;
                end--;
            }

            /* Prepare link and href text */
            const char *link_text = start;
            size_t link_text_len = url_len;
            const char *href_text = start;
            size_t href_len = url_len;
            bool needs_mailto_prefix = is_email_start &&
                                       !(url_len >= 7 && strncmp(start, "mailto:", 7) == 0);
            char *mailto_buf = NULL;

            if (needs_mailto_prefix) {
                href_len += 7; /* "mailto:" */
                mailto_buf = malloc(href_len + 1);
                if (mailto_buf) {
                    memcpy(mailto_buf, "mailto:", 7);
                    memcpy(mailto_buf + 7, start, url_len);
                    mailto_buf[href_len] = '\0';
                    href_text = mailto_buf;
                }
            }

            /* Heuristic: skip if preceded by '(' or '[' (likely already a link) */
            /* Also skip if this is a single '#' at start of line (header marker) */
            if (url_len > 0 &&
                !(r > text && (r[-1] == '(' || r[-1] == '[')) &&
                !(r == text && *r == '#' && (r[1] == ' ' || r[1] == '\t' || r[1] == '\n'))) {
                size_t needed = 2 + link_text_len + 3 + href_len + 2; /* [text](href) */
                if ((size_t)(w - out) + needed + 1 > cap) {
                    size_t used = (size_t)(w - out);
                    cap = (used + needed + 1) * 2;
                    char *new_out = realloc(out, cap);
                    if (!new_out) { free(out); return NULL; }
                    out = new_out;
                    w = out + used;
                }
                *w++ = '[';
                memcpy(w, link_text, link_text_len); w += link_text_len;
                *w++ = ']'; *w++ = '(';
                memcpy(w, href_text, href_len); w += href_len;
                *w++ = ')';
                r = end;
                free(mailto_buf);
                continue;
            }
            free(mailto_buf);
        }

        *w++ = *r++;

        /* Safety: ensure we always advance */
        if (r == loop_start) {
            r++;
        }
    }
    *w = '\0';
    return out;
}

/**
 * Preprocess alpha list markers (a., b., c. and A., B., C.)
 * Converts them to numbered markers (1., 2., 3.) and adds markers for post-processing
 */
static char *apex_preprocess_alpha_lists(const char *text) {
    if (!text) return NULL;

    size_t text_len = strlen(text);
    size_t output_capacity = text_len * 3;  /* Extra capacity for markers and conversions */
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = text;
    char *write = output;
    size_t remaining = output_capacity;
    bool in_alpha_list = false;
    char expected_lower = 'a';
    char expected_upper = 'A';
    bool is_upper = false;
    int item_number = 1;
    int blank_lines_since_alpha = 0;  /* Track blank lines to detect list breaks */

    while (*read) {
        const char *line_start = read;
        const char *line_end = strchr(read, '\n');
        if (!line_end) line_end = read + strlen(read);
        bool has_newline = (*line_end == '\n');

        /* Check for line start */
        const char *p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) {
            p++;
        }

        /* Check if line starts with alpha marker */
        bool is_alpha_marker = false;
        char alpha_char = 0;
        bool alpha_is_upper = false;

        if (p < line_end) {
            if (*p >= 'a' && *p <= 'z' && p + 1 < line_end && p[1] == '.' &&
                (p + 2 >= line_end || (p[2] == ' ' || p[2] == '\t'))) {
                is_alpha_marker = true;
                alpha_char = *p;
                alpha_is_upper = false;
            } else if (*p >= 'A' && *p <= 'Z' && p + 1 < line_end && p[1] == '.' &&
                       (p + 2 >= line_end || (p[2] == ' ' || p[2] == '\t'))) {
                is_alpha_marker = true;
                alpha_char = *p;
                alpha_is_upper = true;
            }
        }

        if (is_alpha_marker) {
            /* Check if this continues an existing alpha list */
            bool continues_list = false;
            if (in_alpha_list) {
                if (alpha_is_upper == is_upper) {
                    if (alpha_is_upper) {
                        if (alpha_char == expected_upper) {
                            continues_list = true;
                        }
                    } else {
                        if (alpha_char == expected_lower) {
                            continues_list = true;
                        }
                    }
                }
            }

            if (!continues_list) {
                /* Start new alpha list */
                in_alpha_list = true;
                is_upper = alpha_is_upper;
                item_number = 1;
                blank_lines_since_alpha = 0;
                if (alpha_is_upper) {
                    expected_upper = alpha_char;
                } else {
                    expected_lower = alpha_char;
                }
                /* Add marker paragraph before the list (will be rendered as <p data-apex-alpha-list="lower|upper"></p>) */
                int needed = snprintf(write, remaining, "[apex-alpha-list:%s]\n\n", alpha_is_upper ? "upper" : "lower");
                if (needed > 0 && needed < (int)remaining) {
                    write += needed;
                    remaining -= needed;
                }
            } else {
                blank_lines_since_alpha = 0;  /* Reset on continuation */
            }

            /* Convert alpha marker to numbered marker */
            int needed = snprintf(write, remaining, "%.*s%d. ", (int)(p - line_start), line_start, item_number);
            if (needed > 0 && needed < (int)remaining) {
                write += needed;
                remaining -= needed;
            }

            /* Advance past the alpha marker and copy the rest of the line */
            const char *line_rest = p + 2;  /* Skip "a." or "A." */
            size_t line_rest_len = line_end - line_rest;
            if (has_newline) {
                line_rest_len++;  /* Include newline */
            }
            if (line_rest_len > remaining) {
                /* Buffer too small, but try to copy what we can */
                line_rest_len = remaining;
            }
            if (line_rest_len > 0) {
                memcpy(write, line_rest, line_rest_len);
                write += line_rest_len;
                remaining -= line_rest_len;
            }
            read = line_end;
            if (has_newline && *read == '\n') read++;
            item_number++;

            /* Update expected next character */
            if (alpha_is_upper) {
                expected_upper++;
                if (expected_upper > 'Z') expected_upper = 'A';  /* Wrap around */
            } else {
                expected_lower++;
                if (expected_lower > 'z') expected_lower = 'a';  /* Wrap around */
            }
            continue;  /* Skip the else block since we've handled this line */
        } else {
            /* Not an alpha marker - check if we should end the list */
            if (in_alpha_list) {
                /* Blank line - count it */
                if (line_end == line_start || (p >= line_end)) {
                    blank_lines_since_alpha++;
                    /* If we have 2+ blank lines, end the alpha list */
                    if (blank_lines_since_alpha >= 2) {
                        in_alpha_list = false;
                    }
                } else {
                    /* Check if it's a numbered list marker starting with "1." after blank lines */
                    bool had_blank_lines = (blank_lines_since_alpha > 0);
                    /* Reset blank line counter on non-blank line */
                    blank_lines_since_alpha = 0;

                    if (*p >= '0' && *p <= '9') {
                        /* Parse the number */
                        int num = 0;
                        const char *num_p = p;
                        while (num_p < line_end && *num_p >= '0' && *num_p <= '9') {
                            num = num * 10 + (*num_p - '0');
                            num_p++;
                        }
                        /* If it's "1. " after blank lines (from ^ marker), end alpha list */
                        if (num == 1 && num_p < line_end && *num_p == '.' &&
                            (num_p + 1 >= line_end || num_p[1] == ' ' || num_p[1] == '\t') &&
                            had_blank_lines) {
                            in_alpha_list = false;
                            /* Insert a paragraph with a space to force block separation */
                            /* The parser will see this as a block break and create separate lists */
                            int needed = snprintf(write, remaining, "\n\n \n\n");
                            if (needed > 0 && needed < (int)remaining) {
                                write += needed;
                                remaining -= needed;
                            }
                        } else {
                            /* Other numbered markers also end alpha list */
                            in_alpha_list = false;
                        }
                    } else if (*p == '*' || *p == '-' || *p == '+') {
                        /* Bullet list marker - ends alpha list */
                        in_alpha_list = false;
                    } else {
                        /* Non-list content ends the alpha list */
                        in_alpha_list = false;
                    }
                }
            }

            /* Copy line as-is */
            size_t line_len = line_end - line_start;
            if (line_end < read + strlen(read)) line_len++;  /* Include newline */
            if (line_len > remaining) line_len = remaining;
            memcpy(write, line_start, line_len);
            write += line_len;
            remaining -= line_len;
            read = line_end;
            if (*read == '\n') read++;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Post-process HTML to add style attributes to alpha lists
 * Finds HTML comments like <!-- apex-alpha-list-lower --> or <!-- apex-alpha-list-upper -->
 * and adds style="list-style-type: lower-alpha" or style="list-style-type: upper-alpha"
 * to the following <ol> tag.
 */
static char *apex_postprocess_alpha_lists_html(const char *html) {
    if (!html) return NULL;

    size_t html_len = strlen(html);
    size_t output_capacity = html_len + 1024;  /* Extra space for style attributes */
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = output_capacity;

    while (*read) {
        /* Look for alpha list marker paragraph: <p>[apex-alpha-list:lower]</p> or <p>[apex-alpha-list:upper]</p> */
        const char *marker_lower = strstr(read, "<p>[apex-alpha-list:lower]</p>");
        const char *marker_upper = strstr(read, "<p>[apex-alpha-list:upper]</p>");
        const char *marker = NULL;
        bool is_upper = false;

        if (marker_lower && (!marker_upper || marker_lower < marker_upper)) {
            marker = marker_lower;
            is_upper = false;
        } else if (marker_upper) {
            marker = marker_upper;
            is_upper = true;
        }

        if (marker) {
            /* Copy everything up to the marker */
            size_t copy_len = marker - read;
            if (copy_len > remaining) copy_len = remaining;
            memcpy(write, read, copy_len);
            write += copy_len;
            remaining -= copy_len;

            /* Skip the marker paragraph */
            read = marker + (is_upper ? 30 : 30);  /* Both are 30 chars: "<p>[apex-alpha-list:lower]</p>" or upper */

            /* Skip whitespace and newlines */
            while (*read && (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r')) {
                *write++ = *read++;
                remaining--;
            }

            /* Look for the next <ol> tag */
            const char *ol_start = strstr(read, "<ol");
            if (ol_start) {
                /* Copy everything up to and including "<ol" */
                size_t copy_len = ol_start - read;
                if (copy_len > remaining) copy_len = remaining;
                memcpy(write, read, copy_len);
                write += copy_len;
                remaining -= copy_len;
                read = ol_start;

                /* Find the end of the <ol> tag */
                const char *tag_end = strchr(read, '>');
                if (tag_end) {
                    /* Check if there's already a style attribute */
                    bool has_style = false;
                    for (const char *p = read; p < tag_end; p++) {
                        if (strncmp(p, "style=", 6) == 0) {
                            has_style = true;
                            break;
                        }
                    }

                    if (!has_style) {
                        /* Copy "<ol" and attributes up to '>' */
                        size_t tag_len = tag_end - read;
                        if (tag_len > remaining) tag_len = remaining;
                        memcpy(write, read, tag_len);
                        write += tag_len;
                        remaining -= tag_len;

                        /* Add style attribute before the closing '>' */
                        const char *style = is_upper
                            ? " style=\"list-style-type: upper-alpha\">"
                            : " style=\"list-style-type: lower-alpha\">";
                        size_t style_len = strlen(style);
                        if (style_len <= remaining) {
                            memcpy(write, style, style_len);
                            write += style_len;
                            remaining -= style_len;
                        }
                        read = tag_end + 1;
                    } else {
                        /* Already has style, copy as-is */
                        size_t copy_len = tag_end - read + 1;
                        if (copy_len > remaining) copy_len = remaining;
                        memcpy(write, read, copy_len);
                        write += copy_len;
                        remaining -= copy_len;
                        read = tag_end + 1;
                    }
                } else {
                    /* No closing '>', copy as-is */
                    *write++ = *read++;
                    remaining--;
                }
            } else {
                /* No <ol> found, continue normally */
                continue;
            }
        } else {
            *write++ = *read++;
            remaining--;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Remove empty paragraphs that contain only zero-width spaces (from ^ markers)
 */
static char *apex_remove_empty_paragraphs(const char *html) {
    if (!html) return NULL;

    size_t html_len = strlen(html);
    size_t output_capacity = html_len + 1;
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = output_capacity;

    while (*read) {
        /* Look for <p> with zero-width space or empty content followed by </p> */
        if (strncmp(read, "<p>", 3) == 0) {
            const char *p_end = strstr(read, "</p>");
            if (p_end) {
                const char *content_start = read + 3;
                size_t content_len = p_end - content_start;

                /* Check if content is just zero-width space entity, whitespace, or empty */
                bool is_empty = true;
                const char *c = content_start;
                while (c < p_end) {
                    if (strncmp(c, "&#8203;", 7) == 0) {
                        c += 7;  /* Skip the entity */
                        continue;
                    }
                    if (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\r') {
                        /* Check for UTF-8 zero-width space (E2 80 8B) */
                        if ((unsigned char)*c == 0xE2 && c + 2 < p_end &&
                            (unsigned char)c[1] == 0x80 && (unsigned char)c[2] == 0x8B) {
                            c += 3;
                            continue;
                        }
                        is_empty = false;
                        break;
                    }
                    c++;
                }

                if (is_empty && content_len > 0) {
                    /* Skip this paragraph */
                    read = p_end + 4;  /* Skip </p> */
                    continue;
                }
            }
        }

        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            break;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Merge adjacent lists with mixed markers at the same level
 * When allow_mixed_list_markers is true, lists with different marker types
 * at the same indentation level should inherit the type from the first list.
 */
static void apex_merge_mixed_list_markers(cmark_node *node) {
    if (!node) return;

    /* Process children first (depth-first) */
    /* Get next sibling before processing to avoid issues if child modifies tree */
    /* Note: If child merges with its next sibling during recursive processing,
     * that sibling will be freed. So we need to get the next sibling from the
     * parent's perspective after the recursive call, not use the pre-call next. */
    for (cmark_node *child = cmark_node_first_child(node); child; ) {
        /* Get next sibling BEFORE recursive call as a hint, but we'll re-get it after
         * because the recursive call might have merged child with next and freed it. */
        apex_merge_mixed_list_markers(child);
        /* After recursive call, get next sibling from parent's perspective.
         * If child was merged with its next sibling, that sibling is now freed,
         * but child's position in the parent's child list hasn't changed, so
         * we can safely get the next sibling. */
        child = cmark_node_next(child);
    }

    /* Check if current node is a list */
    if (cmark_node_get_type(node) != CMARK_NODE_LIST) {
        return;
    }

    /* Look for adjacent lists at the same level */
    cmark_node *sibling = cmark_node_next(node);
    while (sibling) {
        /* Skip non-list nodes (paragraphs, etc.) - these indicate list separation */
        if (cmark_node_get_type(sibling) != CMARK_NODE_LIST) {
            break;  /* Non-list node means lists are separated, don't merge */
        }

        cmark_list_type first_type = cmark_node_get_list_type(node);
        cmark_list_type second_type = cmark_node_get_list_type(sibling);

        /* If they have different types, merge them */
        if (first_type != second_type) {
            /* Use the first list's type (this is what we'll use) */

            /* Move all items from the second list to the first */
            cmark_node *item = cmark_node_first_child(sibling);
            while (item) {
                cmark_node *next_item = cmark_node_next(item);
                cmark_node_unlink(item);
                cmark_node_append_child(node, item);
                item = next_item;
            }

            /* Update list start number if needed (for ordered lists) */
            /* Note: We keep the original start number from the first list.
             * The items are already numbered correctly by the parser. */

            /* Remove the now-empty second list */
            cmark_node *next_sibling = cmark_node_next(sibling);
            cmark_node_unlink(sibling);
            cmark_node_free(sibling);
            sibling = next_sibling;
        } else {
            /* Same type, move to next sibling */
            sibling = cmark_node_next(sibling);
        }
    }
}

/**
 * Get default options with all features enabled (unified mode)
 */
apex_options apex_options_default(void) {
    apex_options opts;
    opts.mode = APEX_MODE_UNIFIED;

    /* Enable all features by default in unified mode */
    opts.enable_tables = true;
    opts.enable_footnotes = true;
    opts.enable_definition_lists = true;
    opts.enable_smart_typography = true;
    opts.enable_math = true;
    opts.enable_critic_markup = true;
    opts.enable_wiki_links = false;  /* Disabled by default - use --wikilinks to enable */
    opts.enable_task_lists = true;
    opts.enable_attributes = true;
    opts.enable_callouts = true;
    opts.enable_marked_extensions = true;

    /* Critic markup mode (0=accept, 1=reject, 2=markup) */
    opts.critic_mode = 2;  /* Default: show markup */

    /* Metadata */
    opts.strip_metadata = true;
    opts.enable_metadata_variables = true;
    opts.enable_metadata_transforms = true;  /* Enabled by default in unified mode */

    /* File inclusion */
    opts.enable_file_includes = true;
    opts.max_include_depth = 10;
    opts.base_directory = NULL;

    /* Output options */
    opts.unsafe = true;
    opts.validate_utf8 = true;
    opts.github_pre_lang = true;
    opts.standalone = false;
    opts.pretty = false;
    opts.stylesheet_path = NULL;
    opts.document_title = NULL;

    /* Line breaks */
    opts.hardbreaks = false;
    opts.nobreaks = false;

    /* Header IDs */
    opts.generate_header_ids = true;
    opts.header_anchors = false;  /* Use header IDs by default, not anchor tags */
    opts.id_format = 0;  /* GFM format (with dashes) */

    /* Table options */
    opts.relaxed_tables = true;  /* Default: enabled in unified mode (can be disabled with --no-relaxed-tables) */

    /* List options */
    /* Since default mode is unified, enable these by default */
    opts.allow_mixed_list_markers = true;  /* Unified mode default: inherit type from first item */
    opts.allow_alpha_lists = true;  /* Unified mode default: support alpha lists */

    /* Superscript and subscript */
    opts.enable_sup_sub = true;  /* Default: enabled in unified mode */

    /* Autolink options */
    opts.enable_autolink = true;  /* Default: enabled in unified mode */
    opts.obfuscate_emails = false; /* Default: plaintext emails */

    /* Image embedding options */
    opts.embed_images = false;  /* Default: disabled */

    /* Citation options */
    opts.enable_citations = false;  /* Disabled by default - enable with --bibliography */
    opts.bibliography_files = NULL;
    opts.csl_file = NULL;
    opts.suppress_bibliography = false;
    opts.link_citations = false;
    opts.show_tooltips = false;
    opts.nocite = NULL;

    return opts;
}

/**
 * Get options configured for a specific processor mode
 */
apex_options apex_options_for_mode(apex_mode_t mode) {
    apex_options opts = apex_options_default();
    opts.mode = mode;

    switch (mode) {
        case APEX_MODE_COMMONMARK:
            /* Pure CommonMark - disable extensions */
            opts.enable_tables = false;
            opts.enable_footnotes = false;
            opts.enable_definition_lists = false;
            opts.enable_smart_typography = false;
            opts.enable_math = false;
            opts.enable_critic_markup = false;
            opts.enable_wiki_links = false;
            opts.enable_task_lists = false;
            opts.enable_attributes = false;
            opts.enable_callouts = false;
            opts.enable_marked_extensions = false;
            opts.enable_file_includes = false;
            opts.enable_metadata_variables = false;
            opts.enable_metadata_transforms = false;
            opts.unsafe = false;  /* CommonMark mode: replace HTML comments with "raw HTML omitted" */
            opts.hardbreaks = false;
            opts.id_format = 0;  /* GFM format (default) */
            opts.relaxed_tables = false;  /* CommonMark has no tables */
            opts.allow_mixed_list_markers = false;  /* CommonMark: current behavior (separate lists) */
            opts.allow_alpha_lists = false;  /* CommonMark: no alpha lists */
            opts.enable_sup_sub = false;  /* CommonMark: no sup/sub */
            opts.enable_autolink = false;  /* CommonMark: no autolinks */
            opts.enable_citations = false;  /* CommonMark: no citations */
            break;

        case APEX_MODE_GFM:
            /* GFM - tables, task lists, strikethrough, autolinks */
            opts.enable_tables = true;
            opts.enable_task_lists = true;
            opts.enable_footnotes = false;
            opts.enable_definition_lists = false;
            opts.enable_smart_typography = false;
            opts.enable_math = false;
            opts.enable_critic_markup = false;
            opts.enable_wiki_links = false;
            opts.enable_attributes = false;
            opts.enable_callouts = false;
            opts.enable_marked_extensions = false;
            opts.enable_file_includes = false;
            opts.enable_metadata_variables = false;
            opts.enable_metadata_transforms = false;
            opts.unsafe = false;  /* GFM mode: replace HTML comments with "raw HTML omitted" */
            opts.hardbreaks = true;  /* GFM treats newlines as hard breaks */
            opts.id_format = 0;  /* GFM format */
            opts.relaxed_tables = false;  /* GFM uses standard table syntax */
            opts.allow_mixed_list_markers = false;  /* GFM: current behavior (separate lists) */
            opts.allow_alpha_lists = false;  /* GFM: no alpha lists */
            opts.enable_sup_sub = false;  /* GFM: no sup/sub */
            opts.enable_autolink = true;  /* GFM: autolinks enabled */
            opts.enable_citations = false;  /* GFM: no citations */
            break;

        case APEX_MODE_MULTIMARKDOWN:
            /* MultiMarkdown - metadata, footnotes, tables, etc. */
            opts.enable_tables = true;
            opts.enable_footnotes = true;
            opts.relaxed_tables = false;  /* MMD uses standard table syntax */
            opts.enable_definition_lists = true;
            opts.enable_smart_typography = true;
            opts.enable_math = true;
            opts.enable_critic_markup = false;
            opts.enable_wiki_links = false;
            opts.enable_task_lists = false;
            opts.enable_attributes = false;
            opts.enable_callouts = false;
            opts.enable_marked_extensions = false;
            opts.enable_file_includes = true;
            opts.enable_metadata_variables = true;
            opts.enable_metadata_transforms = false;
            opts.hardbreaks = false;
            opts.id_format = 1;  /* MMD format */
            opts.allow_mixed_list_markers = true;  /* MultiMarkdown: inherit type from first item */
            opts.allow_alpha_lists = false;  /* MultiMarkdown: no alpha lists */
            opts.enable_citations = true;  /* MultiMarkdown: citations enabled (if bibliography provided) */
            opts.enable_sup_sub = true;  /* MultiMarkdown: support sup/sub */
            opts.enable_autolink = true;  /* MultiMarkdown: autolinks enabled */
            break;

        case APEX_MODE_KRAMDOWN:
            /* Kramdown - attributes, definition lists, footnotes */
            opts.enable_tables = true;
            opts.enable_footnotes = true;
            opts.enable_definition_lists = true;
            opts.enable_smart_typography = true;
            opts.enable_math = true;
            opts.enable_critic_markup = false;
            opts.enable_wiki_links = false;
            opts.enable_task_lists = false;
            opts.enable_attributes = true;
            opts.enable_callouts = false;
            opts.enable_marked_extensions = false;
            opts.enable_file_includes = false;
            opts.enable_metadata_variables = false;
            opts.enable_metadata_transforms = false;
            opts.hardbreaks = false;
            opts.id_format = 2;  /* Kramdown format */
            opts.relaxed_tables = true;  /* Kramdown supports relaxed tables */
            opts.allow_mixed_list_markers = false;  /* Kramdown: current behavior (separate lists) */
            opts.allow_alpha_lists = false;  /* Kramdown: no alpha lists */
            opts.enable_sup_sub = false;  /* Kramdown: no sup/sub */
            opts.enable_autolink = true;  /* Kramdown: autolinks enabled */
            opts.enable_citations = false;  /* Kramdown: no citations (different system) */
            break;

        case APEX_MODE_UNIFIED:
            /* All features enabled - already the default */
            /* Unified mode should have everything on */
            opts.enable_wiki_links = false;  /* Disabled by default - use --wikilinks to enable */
            opts.enable_math = true;
            opts.id_format = 0;  /* GFM format (default, can be overridden with --id-format) */
            opts.relaxed_tables = true;  /* Unified mode supports relaxed tables */
            opts.allow_mixed_list_markers = true;  /* Unified: inherit type from first item */
            opts.allow_alpha_lists = true;  /* Unified: support alpha lists */
            opts.enable_sup_sub = true;  /* Unified: support sup/sub (default: true) */
            opts.unsafe = true;  /* Unified mode: allow raw HTML by default */
            opts.enable_citations = true;  /* Unified: citations enabled (if bibliography provided) */
            break;
    }

    return opts;
}

/**
 * Convert cmark-gfm option flags based on Apex options
 */
static int apex_to_cmark_options(const apex_options *options) {
    int cmark_opts = CMARK_OPT_DEFAULT;

    if (options->validate_utf8) {
        cmark_opts |= CMARK_OPT_VALIDATE_UTF8;
    }

    if (options->unsafe) {
        cmark_opts |= CMARK_OPT_UNSAFE;
    }

    if (options->hardbreaks) {
        cmark_opts |= CMARK_OPT_HARDBREAKS;
    }

    if (options->nobreaks) {
        cmark_opts |= CMARK_OPT_NOBREAKS;
    }

    if (options->github_pre_lang) {
        cmark_opts |= CMARK_OPT_GITHUB_PRE_LANG;
    }

    if (options->enable_footnotes) {
        cmark_opts |= CMARK_OPT_FOOTNOTES;
    }

    if (options->enable_smart_typography) {
        cmark_opts |= CMARK_OPT_SMART;
    }

    /* Table style preference (use CSS classes instead of inline styles) */
    if (options->enable_tables) {
        /* Tables are handled via extension registration, not options */
        /* We could add CMARK_OPT_TABLE_PREFER_STYLE_ATTRIBUTES here if needed */
    }

    return cmark_opts;
}

/**
 * Register cmark-gfm extensions based on Apex options
 */
static void apex_register_extensions(cmark_parser *parser, const apex_options *options) {
    /* Ensure core extensions are registered */
    cmark_gfm_core_extensions_ensure_registered();

    /* Note: Metadata is handled via preprocessing, not as an extension */

    /* Add GFM extensions as needed */
    if (options->enable_tables) {
        cmark_syntax_extension *table_ext = cmark_find_syntax_extension("table");
        if (table_ext) {
            cmark_parser_attach_syntax_extension(parser, table_ext);
        }
    }

    if (options->enable_task_lists) {
        cmark_syntax_extension *tasklist_ext = cmark_find_syntax_extension("tasklist");
        if (tasklist_ext) {
            cmark_parser_attach_syntax_extension(parser, tasklist_ext);
        }
    }

    /* GFM strikethrough (for all modes that support it) */
    if (options->mode == APEX_MODE_GFM || options->mode == APEX_MODE_UNIFIED) {
        cmark_syntax_extension *strike_ext = cmark_find_syntax_extension("strikethrough");
        if (strike_ext) {
            cmark_parser_attach_syntax_extension(parser, strike_ext);
        }
    }

    /* GFM autolink - enable for GFM and Unified modes if autolink is enabled */
    if (options->enable_autolink && (options->mode == APEX_MODE_GFM || options->mode == APEX_MODE_UNIFIED)) {
        cmark_syntax_extension *autolink_ext = cmark_find_syntax_extension("autolink");
        if (autolink_ext) {
            cmark_parser_attach_syntax_extension(parser, autolink_ext);
        }
    }

    /* Tag filter (GFM security)
     * In Unified mode we allow raw HTML/autolinks, so only enable in GFM.
     */
    if (options->mode == APEX_MODE_GFM) {
        cmark_syntax_extension *tagfilter_ext = cmark_find_syntax_extension("tagfilter");
        if (tagfilter_ext) {
            cmark_parser_attach_syntax_extension(parser, tagfilter_ext);
        }
    }

    /* Note: Wiki links are handled via postprocessing, not as an extension */

    /* Math support (LaTeX) */
    if (options->enable_math) {
        cmark_syntax_extension *math_ext = create_math_extension();
        if (math_ext) {
            cmark_parser_attach_syntax_extension(parser, math_ext);
        }
    }

    /* Definition lists (Kramdown/PHP Extra style) */
    if (options->enable_definition_lists) {
        cmark_syntax_extension *deflist_ext = create_definition_list_extension();
        if (deflist_ext) {
            cmark_parser_attach_syntax_extension(parser, deflist_ext);
        }
    }

    /* Advanced footnotes (block-level content support) */
    if (options->enable_footnotes) {
        cmark_syntax_extension *adv_footnotes_ext = create_advanced_footnotes_extension();
        if (adv_footnotes_ext) {
            cmark_parser_attach_syntax_extension(parser, adv_footnotes_ext);
        }
    }

    /* Advanced tables (colspan, rowspan, captions) */
    if (options->enable_tables) {
        cmark_syntax_extension *adv_tables_ext = create_advanced_tables_extension();
        if (adv_tables_ext) {
            cmark_parser_attach_syntax_extension(parser, adv_tables_ext);
        }
    }
}

/**
 * Main conversion function using cmark-gfm
 */
char *apex_markdown_to_html(const char *markdown, size_t len, const apex_options *options) {
    if (!markdown || len == 0) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* Use default options if none provided, and create a mutable copy */
    apex_options default_opts;
    apex_options local_opts;
    if (!options) {
        default_opts = apex_options_default();
        local_opts = default_opts;
    } else {
        local_opts = *options;  /* Make a mutable copy */
    }
    /* Use local_opts for rest of function (mutable) - shadow the const parameter */
    #define options (&local_opts)

    /* Extract metadata if enabled (preprocessing step) */
    /* Safety check: ensure len doesn't exceed actual string length */
    size_t actual_len = strlen(markdown);
    if (len > actual_len) len = actual_len;

    char *working_text = malloc(len + 1);
    if (!working_text) return NULL;
    memcpy(working_text, markdown, len);
    working_text[len] = '\0';

    apex_metadata_item *metadata = NULL;
    abbr_item *abbreviations = NULL;
    ald_entry *alds = NULL;
    char *text_ptr = working_text;


    if (options->mode == APEX_MODE_MULTIMARKDOWN ||
        options->mode == APEX_MODE_KRAMDOWN ||
        options->mode == APEX_MODE_UNIFIED) {
        /* Extract metadata FIRST */
        metadata = apex_extract_metadata(&text_ptr);

        /* Extract ALDs for Kramdown */
        if (options->mode == APEX_MODE_KRAMDOWN || options->mode == APEX_MODE_UNIFIED) {
            alds = apex_extract_alds(&text_ptr);
        }

        /* Extract abbreviations */
        abbreviations = apex_extract_abbreviations(&text_ptr);
    }

    /* Check metadata for bibliography and enable citations if found */
    if (metadata && !local_opts.enable_citations) {
        const char *bib_value = apex_metadata_get(metadata, "bibliography");
        if (bib_value) {
            local_opts.enable_citations = true;
        }
        const char *csl_value = apex_metadata_get(metadata, "csl");
        if (csl_value) {
            local_opts.enable_citations = true;
        }
    }

    /* Apply metadata variable replacement BEFORE autolinking
     * This ensures replaced URLs get autolinked
     */
    char *metadata_replaced = NULL;
    if (metadata && options->enable_metadata_variables) {
        metadata_replaced = apex_metadata_replace_variables(text_ptr, metadata, options);
        if (metadata_replaced) {
            text_ptr = metadata_replaced;
        }
    }

    /* Load bibliography files if provided (before processing citations)
     * Check both CLI bibliography files and metadata bibliography
     * Only load bibliography if files are actually specified - this avoids
     * unnecessary file I/O and parsing when citations aren't being used
     */
    apex_bibliography_registry *bibliography = NULL;

    /* Load from CLI bibliography files if specified */
    if (options->bibliography_files) {
        bibliography = apex_load_bibliography((const char **)options->bibliography_files, options->base_directory);
    }

    /* Also check metadata for bibliography (merge with CLI bibliography if both exist) */
    if (metadata) {
        const char *bib_value = apex_metadata_get(metadata, "bibliography");
        if (bib_value) {
            /* Load bibliography from metadata */
            char *resolved_path = NULL;
            if (options->base_directory) {
                size_t base_len = strlen(options->base_directory);
                size_t bib_len = strlen(bib_value);
                resolved_path = malloc(base_len + bib_len + 2);
                if (resolved_path) {
                    strcpy(resolved_path, options->base_directory);
                    if (resolved_path[base_len - 1] != '/') {
                        resolved_path[base_len] = '/';
                        base_len++;
                    }
                    strcpy(resolved_path + base_len, bib_value);
                }
            } else {
                resolved_path = strdup(bib_value);
            }

            if (resolved_path) {
                apex_bibliography_registry *meta_bib = apex_load_bibliography_file(resolved_path);
                if (meta_bib) {
                    if (bibliography) {
                        /* Merge with existing bibliography */
                        apex_bibliography_entry *entry = meta_bib->entries;
                        while (entry) {
                            apex_bibliography_entry *next = entry->next;
                            if (!apex_find_bibliography_entry(bibliography, entry->id)) {
                                entry->next = bibliography->entries;
                                bibliography->entries = entry;
                                bibliography->count++;
                            } else {
                                apex_bibliography_entry_free(entry);
                            }
                            entry = next;
                        }
                        free(meta_bib);
                    } else {
                        /* Use metadata bibliography as the main bibliography */
                        bibliography = meta_bib;
                    }
                }
                free(resolved_path);
            }
        }
    }

    /* Process citations BEFORE autolinking to prevent @ symbols from being converted to mailto links
     * Citations like [@key] need to be processed before autolinking sees the @ symbol
     * Only process citations if bibliography is actually loaded or citations are explicitly enabled
     */
    apex_citation_registry citation_registry = {0};
    citation_registry.bibliography = bibliography;
    char *citations_processed = NULL;

    /* Check if we should process citations: bibliography loaded, CLI files specified, CSL specified, or metadata bibliography */
    bool should_process_citations = false;
    if (bibliography) {
        should_process_citations = true;
    } else if (options->bibliography_files) {
        should_process_citations = true;
    } else if (options->csl_file) {
        should_process_citations = true;
    } else if (metadata) {
        const char *bib_value = apex_metadata_get(metadata, "bibliography");
        const char *csl_value = apex_metadata_get(metadata, "csl");
        if (bib_value || csl_value) {
            should_process_citations = true;
        }
    }

    if (options->enable_citations && should_process_citations) {
        citations_processed = apex_process_citations(text_ptr, &citation_registry, options);
        if (citations_processed) {
            text_ptr = citations_processed;
        }
    }

    /* Preprocess autolinks to convert <https://...> to [https://...](https://...)
     * This must happen after citation processing so @ symbols in citations aren't autolinked
     * Note: Even with autolink extension enabled, preprocessing ensures compatibility
     */
    char *autolinks_processed = NULL;
    if (options->enable_autolink) {
        autolinks_processed = apex_preprocess_autolinks(text_ptr, options);
        if (autolinks_processed) {
            text_ptr = autolinks_processed;
        }
    }

    /* Preprocess IAL markers (insert blank lines before them so cmark parses correctly) */
    char *ial_preprocessed = NULL;
    if (options->mode == APEX_MODE_KRAMDOWN || options->mode == APEX_MODE_UNIFIED) {
        ial_preprocessed = apex_preprocess_ial(text_ptr);
        if (ial_preprocessed) {
            text_ptr = ial_preprocessed;
        }
    }

    /* Process file includes before parsing (preprocessing) */
    char *includes_processed = NULL;
    if (options->enable_file_includes) {
        includes_processed = apex_process_includes(text_ptr, options->base_directory, metadata, 0);
        if (includes_processed) {
            text_ptr = includes_processed;
        }
    }

    /* Process special markers (^ end-of-block marker) BEFORE alpha lists */
    /* This ensures ^ markers are converted to blank lines before alpha list processing */
    char *markers_processed_early = NULL;
    if (options->enable_marked_extensions) {
        markers_processed_early = apex_process_special_markers(text_ptr);
        if (markers_processed_early) {
            text_ptr = markers_processed_early;
        }
    }

    /* Process alpha lists before parsing (preprocessing) */
    char *alpha_lists_processed = NULL;
    if (options->allow_alpha_lists) {
        alpha_lists_processed = apex_preprocess_alpha_lists(text_ptr);
        if (alpha_lists_processed) {
            text_ptr = alpha_lists_processed;
        }
    }

    /* Process inline footnotes before parsing (Kramdown ^[...] and MMD [^... ...]) */
    char *inline_footnotes_processed = NULL;
    if (options->enable_footnotes) {
        inline_footnotes_processed = apex_process_inline_footnotes(text_ptr);
        if (inline_footnotes_processed) {
            text_ptr = inline_footnotes_processed;
        }
    }

    /* Process ==highlight== syntax before parsing */
    char *highlights_processed = apex_process_highlights(text_ptr);
    if (highlights_processed) {
        text_ptr = highlights_processed;
    }

    /* Process superscript and subscript syntax before parsing */
    char *sup_sub_processed = NULL;
    if (options->enable_sup_sub) {
        sup_sub_processed = apex_process_sup_sub(text_ptr);
        if (sup_sub_processed) {
            text_ptr = sup_sub_processed;
        }
    }

    /* Process relaxed tables before parsing (preprocessing) */
    char *relaxed_tables_processed = NULL;
    if (options->relaxed_tables && options->enable_tables) {
        relaxed_tables_processed = apex_process_relaxed_tables(text_ptr);
        if (relaxed_tables_processed) {
            text_ptr = relaxed_tables_processed;
        }
    }

    /* Process definition lists before parsing (preprocessing) */
    char *deflist_processed = NULL;
    if (options->enable_definition_lists) {
        deflist_processed = apex_process_definition_lists(text_ptr, options->unsafe);
        if (deflist_processed) {
            text_ptr = deflist_processed;
        }
    }

    /* Process HTML markdown attributes before parsing (preprocessing) */
    char *html_markdown_processed = NULL;
    html_markdown_processed = apex_process_html_markdown(text_ptr);
    if (html_markdown_processed) {
        text_ptr = html_markdown_processed;
    }

    /* Process Critic Markup before parsing (preprocessing) */
    char *critic_processed = NULL;
    if (options->enable_critic_markup) {
        critic_mode_t critic_mode = (critic_mode_t)options->critic_mode;
        critic_processed = apex_process_critic_markup_text(text_ptr, critic_mode);
        if (critic_processed) {
            text_ptr = critic_processed;
        }
    }

    /* Convert options to cmark-gfm format */
    int cmark_opts = apex_to_cmark_options(options);

    /* Create parser */
    cmark_parser *parser = cmark_parser_new(cmark_opts);
    if (!parser) {
        free(working_text);
        apex_free_metadata(metadata);
        return NULL;
    }

    /* Register extensions based on mode and options */
    apex_register_extensions(parser, options);

    /* Parse the markdown (without metadata) */
    cmark_parser_feed(parser, text_ptr, strlen(text_ptr));
    cmark_node *document = cmark_parser_finish(parser);

    if (!document) {
        cmark_parser_free(parser);
        free(working_text);
        apex_free_metadata(metadata);
        return NULL;
    }

    /* Postprocess wiki links if enabled */
    if (options->enable_wiki_links) {
        /* Fast path: skip AST walk if no wiki link markers present */
        if (strstr(text_ptr, "[[") != NULL) {
            apex_process_wiki_links_in_tree(document, NULL);
        }
    }

    /* Postprocess callouts if enabled */
    if (options->enable_callouts) {
        apex_process_callouts_in_tree(document);
    }

    /* Process manual header IDs (MMD [id] and Kramdown {#id}) */
    if (options->generate_header_ids) {
        cmark_iter *iter = cmark_iter_new(document);
        cmark_event_type event;
        while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
            cmark_node *node = cmark_iter_get_node(iter);
            if (event == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_HEADING) {
                apex_process_manual_header_id(node);
            }
        }
        cmark_iter_free(iter);
    }

    /* Process IAL (Inline Attribute Lists) if in Kramdown or Unified mode */
    if (alds || options->mode == APEX_MODE_KRAMDOWN || options->mode == APEX_MODE_UNIFIED) {
        apex_process_ial_in_tree(document, alds);
    }

    /* Merge lists with mixed markers if enabled */
    if (options->allow_mixed_list_markers) {
        apex_merge_mixed_list_markers(document);
    }

    /* Note: Critic Markup is now handled via preprocessing (before parsing) */

    /* Render to HTML
     * Unified mode: use standard renderer to preserve autolinks/raw HTML
     * Kramdown or ALDs: use custom renderer for attributes/IAL
     */
    char *html;
    if (options->mode == APEX_MODE_UNIFIED) {
        html = cmark_render_html(document, cmark_opts, NULL);
    } else if (alds || options->mode == APEX_MODE_KRAMDOWN) {
        html = apex_render_html_with_attributes(document, cmark_opts);
    } else {
        html = cmark_render_html(document, cmark_opts, NULL);
    }

    /* Post-process HTML for advanced table attributes (rowspan/colspan) */
    if (options->enable_tables && html) {
        extern char *apex_inject_table_attributes(const char *html, cmark_node *document);
        char *processed_html = apex_inject_table_attributes(html, document);
        if (processed_html && processed_html != html) {
            free(html);
            html = processed_html;
        }
    }

    /* Extract metadata values needed for standalone HTML and post-processing BEFORE freeing metadata */
    /* We need to duplicate strings because metadata will be freed */
    char *css_metadata = NULL;
    char *html_header_metadata = NULL;
    char *html_footer_metadata = NULL;
    char *language_metadata = NULL;
    char *quotes_lang_metadata = NULL;
    int base_header_level = 1;  /* Default is 1 */

    if (metadata) {
        /* Extract values we'll need later (before metadata is freed) and duplicate them */
        const char *css_val = apex_metadata_get(metadata, "css");
        if (css_val) css_metadata = strdup(css_val);

        const char *html_header_val = apex_metadata_get(metadata, "HTML Header");
        if (!html_header_val) {
            html_header_val = apex_metadata_get(metadata, "html header");
        }
        if (html_header_val) html_header_metadata = strdup(html_header_val);

        const char *html_footer_val = apex_metadata_get(metadata, "HTML Footer");
        if (!html_footer_val) {
            html_footer_val = apex_metadata_get(metadata, "html footer");
        }
        if (html_footer_val) html_footer_metadata = strdup(html_footer_val);

        const char *lang_val = apex_metadata_get(metadata, "language");
        if (lang_val) language_metadata = strdup(lang_val);

        /* Get quotes language */
        const char *quotes_lang_val = apex_metadata_get(metadata, "Quotes Language");
        if (!quotes_lang_val) {
            quotes_lang_val = apex_metadata_get(metadata, "quotes language");
        }
        if (!quotes_lang_val) {
            quotes_lang_val = apex_metadata_get(metadata, "quoteslanguage");
        }
        /* If language is set but quotes language is not, use language for quotes */
        if (!quotes_lang_val && lang_val) {
            quotes_lang_val = lang_val;
        }
        if (quotes_lang_val) quotes_lang_metadata = strdup(quotes_lang_val);

        /* Get header level */
        const char *header_level_str = apex_metadata_get(metadata, "HTML Header Level");
        if (!header_level_str) {
            header_level_str = apex_metadata_get(metadata, "Base Header Level");
        }
        if (header_level_str) {
            char *endptr;
            long level = strtol(header_level_str, &endptr, 10);
            if (endptr != header_level_str && level >= 1 && level <= 6) {
                base_header_level = (int)level;
            }
        }
    }

    /* Adjust header levels and quote language based on metadata */
    if (html) {
        if (base_header_level > 1) {
            char *adjusted_html = apex_adjust_header_levels(html, base_header_level);
            if (adjusted_html) {
                free(html);
                html = adjusted_html;
            }
        }

        if (quotes_lang_metadata) {
            char *adjusted_quotes = apex_adjust_quote_language(html, quotes_lang_metadata);
            if (adjusted_quotes) {
                free(html);
                html = adjusted_quotes;
            }
        }
    }

    /* Inject header IDs if enabled */
    if (options->generate_header_ids && html) {
        char *processed_html = apex_inject_header_ids(html, document, true, options->header_anchors, options->id_format);
        if (processed_html && processed_html != html) {
            free(html);
            html = processed_html;
        }
    }

    /* Obfuscate email links if requested */
    if (options->obfuscate_emails && html) {
        char *obfuscated = apex_obfuscate_email_links(html);
        if (obfuscated) {
            free(html);
            html = obfuscated;
        }
    }

    /* Embed images as base64 data URLs if requested (local images only) */
    if (options->embed_images && html) {
        char *embedded = apex_embed_images(html, options, options->base_directory);
        if (embedded) {
            free(html);
            html = embedded;
        }
    }

    /* Apply metadata variable replacement if enabled (post-processing for HTML attributes, etc.)
     * Note: Most replacements happen in preprocessing, but this handles edge cases in HTML
     */
    if (metadata && options->enable_metadata_variables && html) {
        char *replaced = apex_metadata_replace_variables(html, metadata, options);
        if (replaced && replaced != html) {
            free(html);
            html = replaced;
        } else if (replaced == html) {
            /* No replacements found, free the duplicate */
            free(replaced);
        }
    }

    /* Process TOC markers if enabled (Marked extensions) */
    if (options->enable_marked_extensions && html) {
        char *with_toc = apex_process_toc(html, document, options->id_format);
        if (with_toc) {
            free(html);
            html = with_toc;
        }
    }

    /* Replace abbreviations if any were found */
    if (abbreviations && html) {
        char *with_abbrs = apex_replace_abbreviations(html, abbreviations);
        if (with_abbrs) {
            free(html);
            html = with_abbrs;
        }
    }

    /* Replace GitHub emoji if in GFM or Unified mode */
    if ((options->mode == APEX_MODE_GFM || options->mode == APEX_MODE_UNIFIED) && html) {
        char *with_emoji = apex_replace_emoji(html);
        if (with_emoji) {
            free(html);
            html = with_emoji;
        }
    }

    /* Render citations in HTML if enabled and bibliography is available */
    bool should_render_citations = false;
    if (citation_registry.bibliography) {
        should_render_citations = true;
    } else if (options->bibliography_files) {
        should_render_citations = true;
    } else if (options->csl_file) {
        should_render_citations = true;
    } else if (metadata) {
        const char *bib_value = apex_metadata_get(metadata, "bibliography");
        const char *csl_value = apex_metadata_get(metadata, "csl");
        if (bib_value || csl_value) {
            should_render_citations = true;
        }
    }

    if (options->enable_citations && html && should_render_citations) {
        if (citation_registry.count > 0) {
            char *with_citations = apex_render_citations(html, &citation_registry, options);
            if (with_citations) {
                free(html);
                html = with_citations;
            }
        }

        /* Insert bibliography at marker or end of document (even if no citations, if bibliography loaded) */
        if (html && !options->suppress_bibliography && citation_registry.bibliography) {
            char *with_bibliography = apex_insert_bibliography(html, &citation_registry, options);
            if (with_bibliography) {
                free(html);
                html = with_bibliography;
            }
        }
    }

    /* Clean up HTML tag spacing (compress multiple spaces, remove spaces before >) */
    if (html) {
        char *cleaned = apex_clean_html_tag_spacing(html);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* Convert thead to tbody for relaxed tables (if relaxed tables were processed) */
    if (html && options->relaxed_tables && options->enable_tables && relaxed_tables_processed) {
        char *converted = apex_convert_relaxed_table_headers(html);
        if (converted) {
            free(html);
            html = converted;
        }
    }

    /* Post-process HTML to add style attributes to alpha lists */
    if (options->allow_alpha_lists && html) {
        char *processed_html = apex_postprocess_alpha_lists_html(html);
        if (processed_html && processed_html != html) {
            free(html);
            html = processed_html;
        }
    }

    /* Remove empty paragraphs created by ^ marker (zero-width space only) */
    if (html && options->enable_marked_extensions) {
        char *cleaned = apex_remove_empty_paragraphs(html);
        if (cleaned && cleaned != html) {
            free(html);
            html = cleaned;
        }
    }

    /* Clean up */
    cmark_node_free(document);
    cmark_parser_free(parser);
    free(working_text);
    if (ial_preprocessed) free(ial_preprocessed);
    if (includes_processed) free(includes_processed);
    if (markers_processed_early) {
        /* Only free if alpha_lists_processed didn't use it */
        if (!alpha_lists_processed || alpha_lists_processed != markers_processed_early) {
            free(markers_processed_early);
        }
    }
    if (inline_footnotes_processed) free(inline_footnotes_processed);
    if (highlights_processed) free(highlights_processed);
    if (alpha_lists_processed) free(alpha_lists_processed);
    if (relaxed_tables_processed) free(relaxed_tables_processed);
    if (deflist_processed) free(deflist_processed);
    if (metadata_replaced) free(metadata_replaced);
    if (autolinks_processed) free(autolinks_processed);
    if (html_markdown_processed) free(html_markdown_processed);
    if (critic_processed) free(critic_processed);
    apex_free_metadata(metadata);
    apex_free_abbreviations(abbreviations);
    apex_free_alds(alds);
    apex_free_citation_registry(&citation_registry);

    /* Undefine the macro */
    #undef options

    /* Wrap in complete HTML document if requested */
    if (local_opts.standalone && html) {
        /* CSS precedence: CLI flag (--css/--style) overrides metadata */
        const char *css_path = local_opts.stylesheet_path;
        if (!css_path) {
            css_path = css_metadata;  /* Use extracted metadata value */
        }

        char *document = apex_wrap_html_document(html, local_opts.document_title, css_path,
                                                 html_header_metadata, html_footer_metadata,
                                                 language_metadata);
        if (document) {
            free(html);
            html = document;
        }
    }

    /* Free duplicated metadata strings */
    if (css_metadata) free(css_metadata);
    if (html_header_metadata) free(html_header_metadata);
    if (html_footer_metadata) free(html_footer_metadata);
    if (language_metadata) free(language_metadata);
    if (quotes_lang_metadata) free(quotes_lang_metadata);

    /* Remove blank lines within tables (applies to both pretty and non-pretty) */
    if (html) {
        char *cleaned = apex_remove_table_blank_lines(html);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* Remove table separator rows that were incorrectly rendered as data rows */
    /* This happens when smart typography converts --- to — in separator rows */
    if (html && local_opts.enable_tables) {
        extern char *apex_remove_table_separator_rows(const char *html);
        char *cleaned = apex_remove_table_separator_rows(html);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* Pretty-print HTML if requested */
    if (local_opts.pretty && html) {
        char *pretty = apex_pretty_print_html(html);
        if (pretty) {
            free(html);
            html = pretty;
        }
    }

    return html;
}

/**
 * Wrap HTML content in complete HTML5 document structure
 */
char *apex_wrap_html_document(const char *content, const char *title, const char *stylesheet_path, const char *html_header, const char *html_footer, const char *language) {
    if (!content) return NULL;

    const char *doc_title = title ? title : "Document";
    const char *lang = language ? language : "en";

    /* Calculate buffer size */
    size_t content_len = strlen(content);
    size_t title_len = strlen(doc_title);
    size_t style_len = stylesheet_path ? strlen(stylesheet_path) : 0;
    size_t header_len = html_header ? strlen(html_header) : 0;
    size_t footer_len = html_footer ? strlen(html_footer) : 0;
    size_t lang_len = strlen(lang);
    /* Need generous space for styles (1.5KB) + structure + header + footer */
    size_t capacity = content_len + title_len + style_len + header_len + footer_len + lang_len + 4096;

    char *output = malloc(capacity + 1);  /* +1 for null terminator */
    if (!output) return strdup(content);

    char *write = output;
    size_t remaining = capacity + 1;  /* Include null terminator in remaining count */

    /* Ensure we have a valid version string */
    const char *version_str = APEX_VERSION_STRING;
    if (!version_str) version_str = "unknown";

    /* HTML5 doctype and opening */
    int n = snprintf(write, remaining, "<!DOCTYPE html>\n<html lang=\"%s\">\n<head>\n", lang);
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    /* Meta tags */
    n = snprintf(write, remaining, "  <meta charset=\"UTF-8\">\n");
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    n = snprintf(write, remaining, "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    n = snprintf(write, remaining, "  <meta name=\"generator\" content=\"Apex %s\">\n", version_str);
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    /* Title */
    n = snprintf(write, remaining, "  <title>%s</title>\n", doc_title);
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    /* Stylesheet link if provided */
    if (stylesheet_path) {
        n = snprintf(write, remaining, "  <link rel=\"stylesheet\" href=\"%s\">\n", stylesheet_path);
        if (n < 0 || (size_t)n >= remaining) {
            free(output);
            return strdup(content);
        }
        write += n;
        remaining -= n;
    } else {
        /* Include minimal default styles */
        const char *styles = "  <style>\n"
            "    body {\n"
            "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif;\n"
            "      line-height: 1.6;\n"
            "      max-width: 800px;\n"
            "      margin: 2rem auto;\n"
            "      padding: 0 1rem;\n"
            "      color: #333;\n"
            "    }\n"
            "    pre { background: #f5f5f5; padding: 1rem; overflow-x: auto; }\n"
            "    code { background: #f0f0f0; padding: 0.2em 0.4em; border-radius: 3px; }\n"
            "    blockquote { border-left: 4px solid #ddd; margin: 0; padding-left: 1rem; color: #666; }\n"
            "    table { border-collapse: collapse; width: 100%%; }\n"
            "    th, td { border: 1px solid #ddd; padding: 0.5rem; }\n"
            "    th { background: #f5f5f5; }\n"
            "    .page-break { page-break-after: always; }\n"
            "    .callout { padding: 1rem; margin: 1rem 0; border-left: 4px solid; }\n"
            "    .callout-note { border-color: #3b82f6; background: #eff6ff; }\n"
            "    .callout-warning { border-color: #f59e0b; background: #fffbeb; }\n"
            "    .callout-tip { border-color: #10b981; background: #f0fdf4; }\n"
            "    .callout-danger { border-color: #ef4444; background: #fef2f2; }\n"
            "    ins { background: #d4fcbc; text-decoration: none; }\n"
            "    del { background: #fbb6c2; text-decoration: line-through; }\n"
            "    mark { background: #fff3cd; }\n"
            "    .critic.comment { background: #e7e7e7; color: #666; font-style: italic; }\n"
            "  </style>\n";
        size_t styles_len = strlen(styles);
        if (styles_len >= remaining) {
            free(output);
            return strdup(content);
        }
        memcpy(write, styles, styles_len);
        write += styles_len;
        remaining -= styles_len;
    }

    /* HTML Header metadata - raw HTML inserted in <head> */
    if (html_header) {
        n = snprintf(write, remaining, "  %s\n", html_header);
        if (n < 0 || (size_t)n >= remaining) {
            free(output);
            return strdup(content);
        }
        write += n;
        remaining -= n;
    }

    /* Close head, open body */
    n = snprintf(write, remaining, "</head>\n<body>\n\n");
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    /* Content */
    if (content_len + 1 > remaining) {  /* +1 for null terminator at end */
        free(output);
        return strdup(content);
    }
    memcpy(write, content, content_len);
    write += content_len;
    remaining -= content_len;

    /* HTML Footer metadata - raw HTML appended before </body> */
    if (html_footer) {
        n = snprintf(write, remaining, "\n%s", html_footer);
        if (n < 0 || (size_t)n >= remaining) {
            free(output);
            return strdup(content);
        }
        write += n;
        remaining -= n;
    }

    /* Close body and html */
    n = snprintf(write, remaining, "\n</body>\n</html>\n");
    if (n < 0 || (size_t)n >= remaining) {
        free(output);
        return strdup(content);
    }
    write += n;
    remaining -= n;

    /* Null terminate - ensure we have space */
    if (remaining > 0) {
        *write = '\0';
    } else {
        /* Should never happen, but be safe */
        free(output);
        return strdup(content);
    }

    return output;
}

/**
 * Free a string allocated by Apex
 */
void apex_free_string(char *str) {
    if (str) {
        free(str);
    }
}

/**
 * Version information
 */
const char *apex_version_string(void) {
    return APEX_VERSION_STRING;
}

int apex_version_major(void) {
    return APEX_VERSION_MAJOR;
}

int apex_version_minor(void) {
    return APEX_VERSION_MINOR;
}

int apex_version_patch(void) {
    return APEX_VERSION_PATCH;
}
