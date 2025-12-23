#include "apex/apex.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <sys/time.h>

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
#include "extensions/inline_tables.h"
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
#include "extensions/index.h"
#include "plugins.h"

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
    bool in_liquid = false;

    while (*r) {
        const char *loop_start = r;

        /* Handle Liquid tags: copy {% ... %} without processing */
        if (!in_liquid && *r == '{' && r[1] == '%') {
            in_liquid = true;
            *w++ = *r++;
            *w++ = *r++;
            continue;
        }
        if (in_liquid) {
            if (*r == '%' && r[1] == '}') {
                *w++ = *r++;
                *w++ = *r++;
                in_liquid = false;
            } else {
                *w++ = *r++;
            }
            continue;
        }

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

/* ------------------------------------------------------------------------- */
/* Liquid tag protection                                                     */
/*                                                                           */
/* Any text between {% and %} is Liquid templating syntax and should be      */
/* ignored by Apex processing (math, autolinks, etc.). We implement this by  */
/* temporarily replacing Liquid tags with unique placeholder tokens before   */
/* parsing, then restoring the original tags in the final HTML.              */
/* ------------------------------------------------------------------------- */

static char *apex_protect_liquid_tags(const char *text,
                                      char ***out_tags,
                                      size_t *out_count) {
    if (!text || !out_tags || !out_count) {
        return NULL;
    }

    size_t len = strlen(text);
    /* Start with a modest expansion factor; we can grow as needed. */
    size_t capacity = (len > 0) ? len + 64 : 64;
    char *output = malloc(capacity);
    if (!output) {
        return NULL;
    }

    const char *read = text;
    char *write = output;
    size_t remaining = capacity;

    char **tags = NULL;
    size_t tag_count = 0;
    size_t tag_capacity = 0;

    const char *ph_prefix = "APEX_LIQUID_TAG_";
    size_t ph_prefix_len = strlen(ph_prefix);

    while (*read) {
        const char *start = strstr(read, "{%");
        if (!start) {
            /* No more Liquid tags, copy remainder */
            size_t chunk_len = strlen(read);
            if (chunk_len >= remaining) {
                size_t used = (size_t)(write - output);
                capacity = used + chunk_len + 1;
                char *new_output = realloc(output, capacity);
                if (!new_output) {
                    /* Cleanup on failure */
                    for (size_t i = 0; i < tag_count; i++) {
                        free(tags[i]);
                    }
                    free(tags);
                    free(output);
                    return NULL;
                }
                output = new_output;
                write = output + used;
                remaining = capacity - used;
            }
            memcpy(write, read, chunk_len);
            write += chunk_len;
            remaining -= chunk_len;
            break;
        }

        /* Copy text before the tag */
        size_t prefix_copy_len = (size_t)(start - read);
        if (prefix_copy_len >= remaining) {
            size_t used = (size_t)(write - output);
            capacity = used + prefix_copy_len + 64;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                for (size_t i = 0; i < tag_count; i++) {
                    free(tags[i]);
                }
                free(tags);
                free(output);
                return NULL;
            }
            output = new_output;
            write = output + used;
            remaining = capacity - used;
        }
        memcpy(write, read, prefix_copy_len);
        write += prefix_copy_len;
        remaining -= prefix_copy_len;

        /* Find end of Liquid tag */
        const char *end = strstr(start + 2, "%}");
        if (!end) {
            /* No closing %}; copy rest as-is and stop */
            size_t chunk_len = strlen(start);
            if (chunk_len >= remaining) {
                size_t used = (size_t)(write - output);
                capacity = used + chunk_len + 1;
                char *new_output = realloc(output, capacity);
                if (!new_output) {
                    for (size_t i = 0; i < tag_count; i++) {
                        free(tags[i]);
                    }
                    free(tags);
                    free(output);
                    return NULL;
                }
                output = new_output;
                write = output + used;
                remaining = capacity - used;
            }
            memcpy(write, start, chunk_len);
            write += chunk_len;
            remaining -= chunk_len;
            break;
        }

        size_t tag_len = (size_t)((end + 2) - start);

        /* Store original Liquid tag */
        if (tag_count == tag_capacity) {
            size_t new_cap = tag_capacity ? tag_capacity * 2 : 8;
            char **new_tags = realloc(tags, new_cap * sizeof(char *));
            if (!new_tags) {
                for (size_t i = 0; i < tag_count; i++) {
                    free(tags[i]);
                }
                free(tags);
                free(output);
                return NULL;
            }
            tags = new_tags;
            tag_capacity = new_cap;
        }

        tags[tag_count] = malloc(tag_len + 1);
        if (!tags[tag_count]) {
            for (size_t i = 0; i < tag_count; i++) {
                free(tags[i]);
            }
            free(tags);
            free(output);
            return NULL;
        }
        memcpy(tags[tag_count], start, tag_len);
        tags[tag_count][tag_len] = '\0';

        /* Build placeholder: APEX_LIQUID_TAG_<index> */
        char placeholder[64];
        int ph_len = snprintf(placeholder, sizeof(placeholder), "%s%zu", ph_prefix, tag_count);
        if (ph_len <= 0 || (size_t)ph_len >= sizeof(placeholder)) {
            /* Cleanup on formatting error */
            for (size_t i = 0; i <= tag_count; i++) {
                free(tags[i]);
            }
            free(tags);
            free(output);
            return NULL;
        }

        size_t placeholder_len = (size_t)ph_len;
        if (placeholder_len >= remaining) {
            size_t used = (size_t)(write - output);
            capacity = used + placeholder_len + 64;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                for (size_t i = 0; i <= tag_count; i++) {
                    free(tags[i]);
                }
                free(tags);
                free(output);
                return NULL;
            }
            output = new_output;
            write = output + used;
            remaining = capacity - used;
        }

        memcpy(write, placeholder, placeholder_len);
        write += placeholder_len;
        remaining -= placeholder_len;

        tag_count++;
        read = end + 2;
    }

    *write = '\0';
    *out_tags = tags;
    *out_count = tag_count;

    /* If we didn't actually find any Liquid tags, clean up and signal no-op */
    if (tag_count == 0) {
        free(output);
        *out_tags = NULL;
        *out_count = 0;
        return NULL;
    }

    (void)ph_prefix_len; /* silence unused warning */
    return output;
}

/**
 * Preprocess table captions to normalize different syntaxes before parsing.
 *
 * Handles two cases:
 * 1. Contiguous [Caption] lines immediately following a table row:
 *    - Detects a caption line like "[Caption]" directly after a line
 *      containing table cells (with '|').
 *    - Inserts a blank line between the table row and the caption line so
 *      cmark-gfm parses the caption as a separate paragraph, which our
 *      existing caption logic already understands.
 *
 * 2. Pandoc-style captions using "Table: Caption" after a table:
 *    - When a line starting with "Table:" immediately follows a table row,
 *      converts it to a MultiMarkdown-style caption "[Caption]" and inserts
      a blank line before it.
 *    - The caption text is trimmed of surrounding whitespace.
 *
 * NOTE: This is a text-level transform that runs before any table parsing
 * or advanced table processing. It intentionally skips fenced code blocks.
 */
static char *apex_preprocess_table_captions(const char *text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    /* Allow room for extra blank lines and brackets when converting captions */
    size_t cap = len * 2 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;

    const char *read = text;
    char *write = out;
    bool prev_line_was_table_row = false;
    bool in_code_block = false;

    while (*read) {
        const char *line_start = read;
        const char *line_end = strchr(read, '\n');
        bool has_newline = (line_end != NULL);
        if (!line_end) {
            line_end = read + strlen(read);
        }

        /* Determine line properties */
        const char *p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) {
            p++;
        }

        /* Track fenced code blocks (``` or ~~~) and skip caption transforms inside */
        if (!in_code_block &&
            (line_end - p) >= 3 &&
            ((p[0] == '`' && p[1] == '`' && p[2] == '`') ||
             (p[0] == '~' && p[1] == '~' && p[2] == '~'))) {
            in_code_block = true;
        } else if (in_code_block &&
                   (line_end - p) >= 3 &&
                   ((p[0] == '`' && p[1] == '`' && p[2] == '`') ||
                    (p[0] == '~' && p[1] == '~' && p[2] == '~'))) {
            in_code_block = false;
        }

        bool is_table_row_line = false;
        bool is_bracket_caption_line = false;
        bool is_pandoc_caption_line = false;
        bool is_blank_line = false;

        if (!in_code_block) {
            /* Treat any line containing '|' as a candidate table row */
            for (const char *q = line_start; q < line_end; q++) {
                if (*q == '|') {
                    is_table_row_line = true;
                    break;
                }
            }

            if (p >= line_end) {
                is_blank_line = true;
            } else if (*p == '[') {
                is_bracket_caption_line = true;
            } else if ((size_t)(line_end - p) >= 6 &&
                       strncmp(p, "Table:", 6) == 0) {
                is_pandoc_caption_line = true;
            }
        }

        size_t line_len = (size_t)(line_end - line_start);

        if (!in_code_block &&
            prev_line_was_table_row &&
            is_bracket_caption_line) {
            /* Case 1: [Caption] immediately after table row -> insert blank line */
            *write++ = '\n';
            memcpy(write, line_start, line_len);
            write += line_len;
            if (has_newline) {
                *write++ = '\n';
            }
        } else if (!in_code_block &&
                   prev_line_was_table_row &&
                   is_pandoc_caption_line) {
            /* Case 2: Pandoc-style 'Table: Caption' -> convert to '[Caption]' */
            const char *caption_start = p + 6; /* after 'Table:' */
            while (caption_start < line_end &&
                   (*caption_start == ' ' || *caption_start == '\t')) {
                caption_start++;
            }
            const char *caption_end = line_end;
            while (caption_end > caption_start &&
                   (caption_end[-1] == ' ' || caption_end[-1] == '\t' ||
                    caption_end[-1] == '\r')) {
                caption_end--;
            }

            if (caption_end > caption_start) {
                size_t caption_len = (size_t)(caption_end - caption_start);
                *write++ = '\n';      /* blank line between table and caption */
                *write++ = '[';
                memcpy(write, caption_start, caption_len);
                write += caption_len;
                *write++ = ']';
                if (has_newline) {
                    *write++ = '\n';
                }
            } else {
                /* Empty caption text - fall back to copying the line as-is */
                memcpy(write, line_start, line_len);
                write += line_len;
                if (has_newline) {
                    *write++ = '\n';
                }
            }
        } else {
            /* Default: copy line unchanged */
            memcpy(write, line_start, line_len);
            write += line_len;
            if (has_newline) {
                *write++ = '\n';
            }
        }

        read = has_newline ? line_end + 1 : line_end;
        if (!in_code_block) {
            if (is_table_row_line) {
                /* Remember that the last non-blank, table-looking line was a row */
                prev_line_was_table_row = true;
            } else if (!is_blank_line) {
                /* Any non-blank, non-table line clears the table-row context */
                prev_line_was_table_row = false;
            }
            /* Blank lines preserve prev_line_was_table_row so that
             * 'Table: Caption' can appear after one or more blank lines. */
        }
    }

    *write = '\0';
    return out;
}

static char *apex_restore_liquid_tags(const char *html,
                                      char **tags,
                                      size_t tag_count) {
    if (!html || !tags || tag_count == 0) {
        return NULL;
    }

    const char *ph_prefix = "APEX_LIQUID_TAG_";
    size_t ph_prefix_len = strlen(ph_prefix);
    size_t html_len = strlen(html);

    /* Allocate generously: HTML + space for tags being longer than placeholders */
    size_t capacity = html_len + tag_count * 64;
    if (capacity < html_len + 1) {
        capacity = html_len + 1;
    }

    char *output = malloc(capacity);
    if (!output) {
        return NULL;
    }

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;

    while (*read) {
        const char *ph_start = strstr(read, ph_prefix);
        if (!ph_start) {
            /* Copy remainder */
            size_t chunk_len = strlen(read);
            if (chunk_len >= remaining) {
                size_t used = (size_t)(write - output);
                capacity = used + chunk_len + 1;
                char *new_output = realloc(output, capacity);
                if (!new_output) {
                    free(output);
                    return NULL;
                }
                output = new_output;
                write = output + used;
                remaining = capacity - used;
            }
            memcpy(write, read, chunk_len);
            write += chunk_len;
            remaining -= chunk_len;
            break;
        }

        /* Copy text before the placeholder */
        size_t prefix_copy_len = (size_t)(ph_start - read);
        if (prefix_copy_len >= remaining) {
            size_t used = (size_t)(write - output);
            capacity = used + prefix_copy_len + 64;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                free(output);
                return NULL;
            }
            output = new_output;
            write = output + used;
            remaining = capacity - used;
        }
        memcpy(write, read, prefix_copy_len);
        write += prefix_copy_len;
        remaining -= prefix_copy_len;

        /* Parse index after prefix */
        const char *idx_start = ph_start + ph_prefix_len;
        size_t idx = 0;
        const char *p = idx_start;
        if (!(*p >= '0' && *p <= '9')) {
            /* Not actually a placeholder, copy prefix char and continue */
            if (remaining > 0) {
                *write++ = *ph_start;
                remaining--;
            }
            read = ph_start + 1;
            continue;
        }
        while (*p >= '0' && *p <= '9') {
            idx = idx * 10 + (size_t)(*p - '0');
            p++;
        }

        if (idx >= tag_count) {
            /* Out-of-range index, treat as normal text */
            size_t placeholder_len = (size_t)(p - ph_start);
            if (placeholder_len >= remaining) {
                size_t used = (size_t)(write - output);
                capacity = used + placeholder_len + 64;
                char *new_output = realloc(output, capacity);
                if (!new_output) {
                    free(output);
                    return NULL;
                }
                output = new_output;
                write = output + used;
                remaining = capacity - used;
            }
            memcpy(write, ph_start, placeholder_len);
            write += placeholder_len;
            remaining -= placeholder_len;
            read = p;
            continue;
        }

        /* Insert original Liquid tag */
        size_t tag_len = strlen(tags[idx]);
        if (tag_len >= remaining) {
            size_t used = (size_t)(write - output);
            capacity = used + tag_len + 64;
            char *new_output = realloc(output, capacity);
            if (!new_output) {
                free(output);
                return NULL;
            }
            output = new_output;
            write = output + used;
            remaining = capacity - used;
        }
        memcpy(write, tags[idx], tag_len);
        write += tag_len;
        remaining -= tag_len;

        read = p;
    }

    *write = '\0';
    (void)ph_prefix_len; /* silence unused warning */
    return output;
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

    /* Early exit: check if alpha list markers exist before processing */
    if (strstr(html, "[apex-alpha-list:") == NULL) {
        return NULL;  /* No alpha list markers, skip processing */
    }

    size_t html_len = strlen(html);
    size_t output_capacity = html_len + 1024;  /* Extra space for style attributes */
    char *output = malloc(output_capacity);
    if (!output) return NULL;

    const char *read = html;
    char *write = output;
    size_t remaining = output_capacity;

    /* Helper macro for buffer expansion */
    #define ENSURE_SPACE(needed) do { \
        if (remaining < (needed)) { \
            size_t used = write - output; \
            size_t new_capacity = (used + (needed) + 1) * 2; \
            char *new_output = realloc(output, new_capacity); \
            if (!new_output) { \
                free(output); \
                return NULL; \
            } \
            output = new_output; \
            write = output + used; \
            remaining = new_capacity - used; \
        } \
    } while(0)

    /* Single-pass optimization: look for markers as we go */
    const char *read_start = read;  /* Track where we started reading from */
    while (*read) {
        /* Check for marker patterns */
        if (read[0] == '<' && read[1] == 'p' && read[2] == '>' && read[3] == '[') {
            /* Potential marker start */
            if (strncmp(read + 4, "apex-alpha-list:", 16) == 0) {
                bool is_upper = false;
                const char *marker_end = NULL;

                /* Check for lower or upper */
                if (read + 20 < html + html_len && strncmp(read + 20, "lower]</p>", 9) == 0) {
                    marker_end = read + 29;  /* "<p>[apex-alpha-list:lower]</p>" */
                    is_upper = false;
                } else if (read + 20 < html + html_len && strncmp(read + 20, "upper]</p>", 9) == 0) {
                    marker_end = read + 29;  /* "<p>[apex-alpha-list:upper]</p>" */
                    is_upper = true;
                }

                if (marker_end) {
                    /* Found a marker - copy everything up to it */
                    size_t copy_len = read - read_start;
                    ENSURE_SPACE(copy_len);
                    if (copy_len > 0) {
                        memcpy(write, read_start, copy_len);
                        write += copy_len;
                        remaining -= copy_len;
                    }
                    read_start = marker_end + 1;
                    read = marker_end + 1;

                    /* Skip whitespace (but copy it) */
                    const char *whitespace_start = read;
                    while (*read && (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r')) {
                        read++;
                    }
                    /* Copy whitespace in batch */
                    size_t whitespace_len = read - whitespace_start;
                    if (whitespace_len > 0) {
                        ENSURE_SPACE(whitespace_len);
                        memcpy(write, whitespace_start, whitespace_len);
                        write += whitespace_len;
                        remaining -= whitespace_len;
                    }
                    read_start = read;

                    /* Look for <ol> tag (single character check, then verify) */
                    if (read[0] == '<' && read[1] == 'o' && read[2] == 'l' &&
                        (read[3] == '>' || read[3] == ' ' || read[3] == '\t' || read[3] == '\n')) {
                        const char *ol_start = read;
                        const char *tag_end = strchr(ol_start, '>');

                        if (tag_end) {
                            /* Check if already has style attribute */
                            bool has_style = false;
                            for (const char *p = ol_start; p < tag_end; p++) {
                                if (strncmp(p, "style=", 6) == 0) {
                                    has_style = true;
                                    break;
                                }
                            }

                            if (!has_style) {
                                /* Copy "<ol" and attributes */
                                size_t tag_len = tag_end - ol_start;
                                ENSURE_SPACE(tag_len + 50);  /* Extra for style attribute */
                                memcpy(write, ol_start, tag_len);
                                write += tag_len;
                                remaining -= tag_len;

                                /* Add style attribute */
                                const char *style = is_upper
                                    ? " style=\"list-style-type: upper-alpha\">"
                                    : " style=\"list-style-type: lower-alpha\">";
                                size_t style_len = strlen(style);
                                memcpy(write, style, style_len);
                                write += style_len;
                                remaining -= style_len;
                                read = tag_end + 1;
                                read_start = read;
                                continue;
                            }
                        }
                    }
                }
            }
        }

        /* Normal character - just advance */
        read++;
    }

    /* Copy any remaining characters */
    size_t remaining_len = read - read_start;
    if (remaining_len > 0) {
        ENSURE_SPACE(remaining_len);
        memcpy(write, read_start, remaining_len);
        write += remaining_len;
        remaining -= remaining_len;
    }

    #undef ENSURE_SPACE

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

    /* Enable all features by default in unified mode; plugins are opt-in */
    opts.enable_plugins = false;
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
    opts.caption_position = 1;  /* Default: below (1=below, 0=above) */

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

    /* Index options */
    opts.enable_indices = false;
    opts.enable_mmark_index_syntax = false;
    opts.enable_textindex_syntax = false;
    opts.suppress_index = false;
    opts.group_index_by_letter = true;

    /* Wiki link options */
    opts.wikilink_space = 0;  /* Default: dash (0=dash, 1=none, 2=underscore, 3=space) */
    opts.wikilink_extension = NULL;  /* Default: no extension */

    /* Script injection options */
    opts.script_tags = NULL;

    /* Stylesheet embedding options */
    opts.embed_stylesheet = false;

    /* ARIA accessibility options */
    opts.enable_aria = false;

    /* Source file information (used by plugins via APEX_FILE_PATH) */
    opts.input_file_path = NULL;

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
            opts.enable_indices = false;  /* Indices disabled by default - use --indices to enable */
            opts.enable_mmark_index_syntax = false;  /* Disabled by default - use --indices to enable */
            opts.enable_textindex_syntax = false;  /* Disabled by default - use --indices to enable */
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
            /* Enable Marked-style extensions (including TOC) so that
             * Kramdown documents can use <!--TOC--> and {:toc} syntax
             * for table-of-contents generation. */
            opts.enable_marked_extensions = true;
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
            opts.enable_indices = true;  /* Unified: indices enabled */
            opts.enable_mmark_index_syntax = true;  /* Unified: mmark index syntax */
            opts.enable_textindex_syntax = true;  /* Unified: TextIndex syntax enabled */
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
/* Profiling helpers */
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

char *apex_markdown_to_html(const char *markdown, size_t len, const apex_options *options) {
    if (!markdown || len == 0) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    PROFILE_START(total);

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

    /* Discover plugins once per conversion. This currently supports
     * text-level pre-parse plugins described by simple YAML manifests
     * in project and global plugin directories.
     */
    apex_plugin_manager *plugin_manager = NULL;
    if (options->enable_plugins) {
        plugin_manager = apex_plugins_load(options);
    }

    /* Optional pre-parse plugin hook: run all configured pre_parse plugins
     * over the raw markdown before any Apex-specific preprocessing.
     */
    if (plugin_manager) {
        char *plugin_text = apex_plugins_run_text_phase(plugin_manager,
                                                        APEX_PLUGIN_PHASE_PRE_PARSE,
                                                        working_text,
                                                        options);
        if (plugin_text) {
            free(working_text);
            working_text = plugin_text;
            len = strlen(working_text);
        }
    }

    apex_metadata_item *metadata = NULL;
    abbr_item *abbreviations = NULL;
    ald_entry *alds = NULL;
    char *text_ptr = working_text;
    /* Liquid tag placeholders (for {% ... %} tags) */
    char **liquid_tags = NULL;
    size_t liquid_tag_count = 0;
    char *liquid_protected = NULL;


    if (options->mode == APEX_MODE_MULTIMARKDOWN ||
        options->mode == APEX_MODE_KRAMDOWN ||
        options->mode == APEX_MODE_UNIFIED) {
        /* Extract metadata FIRST */
        PROFILE_START(metadata);
        metadata = apex_extract_metadata(&text_ptr);
        PROFILE_END(metadata);

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
        PROFILE_START(metadata_replace_pre);
        metadata_replaced = apex_metadata_replace_variables(text_ptr, metadata, options);
        PROFILE_END(metadata_replace_pre);
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
        PROFILE_START(bibliography_load);
        bibliography = apex_load_bibliography((const char **)options->bibliography_files, options->base_directory);
        PROFILE_END(bibliography_load);
    }

    /* Also check metadata for bibliography (merge with CLI bibliography if both exist) */
    if (metadata) {
        const char *bib_value = apex_metadata_get(metadata, "bibliography");
        if (bib_value) {
            PROFILE_START(bibliography_load_meta);
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
            PROFILE_END(bibliography_load_meta);
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
        PROFILE_START(citations);
        citations_processed = apex_process_citations(text_ptr, &citation_registry, options);
        PROFILE_END(citations);
        if (citations_processed) {
            text_ptr = citations_processed;
        }
    }

    /* Process index entries (preprocessing) */
    apex_index_registry index_registry = {0};
    char *indices_processed = NULL;
    if (options->enable_indices) {
        PROFILE_START(indices);
        indices_processed = apex_process_index_entries(text_ptr, &index_registry, options);
        PROFILE_END(indices);
        if (indices_processed) {
            text_ptr = indices_processed;
        }
    }

    /* Preprocess autolinks to convert <https://...> to [https://...](https://...)
     * This must happen after citation processing so @ symbols in citations aren't autolinked
     * Note: Even with autolink extension enabled, preprocessing ensures compatibility
     */
    char *autolinks_processed = NULL;
    if (options->enable_autolink) {
        PROFILE_START(autolinks);
        autolinks_processed = apex_preprocess_autolinks(text_ptr, options);
        PROFILE_END(autolinks);
        if (autolinks_processed) {
            text_ptr = autolinks_processed;
        }
    }

    /* Preprocess image attributes and URL-encode all link URLs */
    image_attr_entry *img_attrs = NULL;
    char *image_attrs_processed = NULL;
    if (options->mode == APEX_MODE_UNIFIED ||
        options->mode == APEX_MODE_MULTIMARKDOWN ||
        options->mode == APEX_MODE_KRAMDOWN) {
        PROFILE_START(image_attrs_preprocess);
        image_attrs_processed = apex_preprocess_image_attributes(text_ptr, &img_attrs, options->mode);
        PROFILE_END(image_attrs_preprocess);
        if (image_attrs_processed) {
            text_ptr = image_attrs_processed;
        }
    }

    /* Preprocess IAL markers (insert blank lines before them so cmark parses correctly) */
    char *ial_preprocessed = NULL;
    if (options->mode == APEX_MODE_KRAMDOWN || options->mode == APEX_MODE_UNIFIED) {
        PROFILE_START(ial_preprocess);
        ial_preprocessed = apex_preprocess_ial(text_ptr);
        PROFILE_END(ial_preprocess);
        if (ial_preprocessed) {
            text_ptr = ial_preprocessed;
        }
    }

    /* Process file includes before parsing (preprocessing) */
    char *includes_processed = NULL;
    if (options->enable_file_includes) {
        PROFILE_START(includes);
        includes_processed = apex_process_includes(text_ptr, options->base_directory, metadata, 0);
        PROFILE_END(includes);
        if (includes_processed) {
            text_ptr = includes_processed;
        }
    }

    /* Process special markers (^ end-of-block marker) and inline tables BEFORE alpha lists */
    /* This ensures ^ markers and inline table markers are converted before alpha list processing */
    char *markers_processed_early = NULL;
    if (options->enable_marked_extensions) {
        PROFILE_START(special_markers);
        markers_processed_early = apex_process_special_markers(text_ptr);
        PROFILE_END(special_markers);
        if (markers_processed_early) {
            text_ptr = markers_processed_early;
        }
    }

    /* Process inline table fences and <!--TABLE--> markers before parsing */
    char *inline_tables_processed = NULL;
    PROFILE_START(inline_tables);
    inline_tables_processed = apex_process_inline_tables(text_ptr);
    PROFILE_END(inline_tables);
    if (inline_tables_processed) {
        text_ptr = inline_tables_processed;
    }

    /* Process alpha lists before parsing (preprocessing) */
    char *alpha_lists_processed = NULL;
    if (options->allow_alpha_lists) {
        PROFILE_START(alpha_lists);
        alpha_lists_processed = apex_preprocess_alpha_lists(text_ptr);
        PROFILE_END(alpha_lists);
        if (alpha_lists_processed) {
            text_ptr = alpha_lists_processed;
        }
    }

    /* Process inline footnotes before parsing (Kramdown ^[...] and MMD [^... ...]) */
    char *inline_footnotes_processed = NULL;
    if (options->enable_footnotes) {
        PROFILE_START(inline_footnotes);
        inline_footnotes_processed = apex_process_inline_footnotes(text_ptr);
        PROFILE_END(inline_footnotes);
        if (inline_footnotes_processed) {
            text_ptr = inline_footnotes_processed;
        }
    }

    /* Process ==highlight== syntax before parsing */
    PROFILE_START(highlights);
    char *highlights_processed = apex_process_highlights(text_ptr);
    PROFILE_END(highlights);
    if (highlights_processed) {
        text_ptr = highlights_processed;
    }

    /* Process superscript and subscript syntax before parsing */
    char *sup_sub_processed = NULL;
    if (options->enable_sup_sub) {
        PROFILE_START(sup_sub);
        sup_sub_processed = apex_process_sup_sub(text_ptr);
        PROFILE_END(sup_sub);
        if (sup_sub_processed) {
            text_ptr = sup_sub_processed;
        }
    }

    /* Process relaxed tables before parsing (preprocessing) */
    char *relaxed_tables_processed = NULL;
    if (options->relaxed_tables && options->enable_tables) {
        PROFILE_START(relaxed_tables);
        relaxed_tables_processed = apex_process_relaxed_tables(text_ptr);
        PROFILE_END(relaxed_tables);
        if (relaxed_tables_processed) {
            text_ptr = relaxed_tables_processed;
        }
    }

    /* Process headerless tables before parsing (preprocessing)
     * Detect separator rows without header rows and insert dummy headers
     * This must run after relaxed tables processing
     */
    char *headerless_tables_processed = NULL;
    if (options->enable_tables) {
        PROFILE_START(headerless_tables);
        headerless_tables_processed = apex_process_headerless_tables(text_ptr);
        PROFILE_END(headerless_tables);
        if (headerless_tables_processed) {
            text_ptr = headerless_tables_processed;
        }
    }

    /* Normalize table captions before parsing (preprocessing)
     * - Ensure contiguous [Caption] lines become separate paragraphs
     * - Convert Pandoc-style 'Table: Caption' lines to [Caption]
     */
    char *table_captions_processed = NULL;
    if (options->enable_tables) {
        PROFILE_START(table_captions_preprocess);
        table_captions_processed = apex_preprocess_table_captions(text_ptr);
        PROFILE_END(table_captions_preprocess);
        if (table_captions_processed) {
            text_ptr = table_captions_processed;
        }
    }

    /* Process definition lists before parsing (preprocessing) */
    char *deflist_processed = NULL;
    if (options->enable_definition_lists) {
        PROFILE_START(definition_lists);
        deflist_processed = apex_process_definition_lists(text_ptr, options->unsafe);
        PROFILE_END(definition_lists);
        if (deflist_processed) {
            text_ptr = deflist_processed;
        }
    }

    /* Process HTML markdown attributes before parsing (preprocessing) */
    PROFILE_START(html_markdown);
    char *html_markdown_processed = NULL;
    html_markdown_processed = apex_process_html_markdown(text_ptr);
    PROFILE_END(html_markdown);
    if (html_markdown_processed) {
        text_ptr = html_markdown_processed;
    }

    /* Process Critic Markup before parsing (preprocessing) */
    char *critic_processed = NULL;
    if (options->enable_critic_markup) {
        PROFILE_START(critic);
        critic_mode_t critic_mode = (critic_mode_t)options->critic_mode;
        critic_processed = apex_process_critic_markup_text(text_ptr, critic_mode);
        PROFILE_END(critic);
        if (critic_processed) {
            text_ptr = critic_processed;
        }
    }

    /* Protect Liquid {% ... %} tags so they are not modified by later
     * processing (including parsing, math, and autolinks). We'll restore
     * them after rendering the final HTML.
     */
    liquid_protected = apex_protect_liquid_tags(text_ptr, &liquid_tags, &liquid_tag_count);
    if (liquid_protected) {
        text_ptr = liquid_protected;
    }

    /* Convert options to cmark-gfm format */
    int cmark_opts = apex_to_cmark_options(options);

    /* Create parser */
    PROFILE_START(parsing);
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
    PROFILE_END(parsing);

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
            /* Create wiki link configuration from options */
            wiki_link_config wiki_config;
            wiki_config.base_path = "";
            wiki_config.extension = options->wikilink_extension ? options->wikilink_extension : "";
            wiki_config.space_mode = (wikilink_space_mode_t)options->wikilink_space;
            apex_process_wiki_links_in_tree(document, &wiki_config);
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
        /* Fast path: skip AST walk if no IAL markers present */
        if (strstr(text_ptr, "{:") != NULL) {
            PROFILE_START(ial);
            apex_process_ial_in_tree(document, alds);
            PROFILE_END(ial);
        }
    }

    /* Apply image attributes to image nodes */
    if (img_attrs) {
        PROFILE_START(image_attrs);
        apex_apply_image_attributes(document, img_attrs);
        PROFILE_END(image_attrs);
    }

    /* Merge lists with mixed markers if enabled */
    if (options->allow_mixed_list_markers) {
        apex_merge_mixed_list_markers(document);
    }

    /* Note: Critic Markup is now handled via preprocessing (before parsing) */

    /* Render to HTML
     * Use custom renderer when we have attributes (IAL, ALDs, or image attributes)
     * Otherwise use standard renderer
     */
    PROFILE_START(rendering);
    char *html;
    if (img_attrs || alds || options->mode == APEX_MODE_KRAMDOWN || options->mode == APEX_MODE_UNIFIED) {
        /* Use custom renderer to inject attributes */
        html = apex_render_html_with_attributes(document, cmark_opts);
    } else {
        html = cmark_render_html(document, cmark_opts, NULL);
    }
    PROFILE_END(rendering);

    /* Restore any protected Liquid tags in the rendered HTML */
    if (html && liquid_tags && liquid_tag_count > 0) {
        char *restored_html = apex_restore_liquid_tags(html, liquid_tags, liquid_tag_count);
        if (restored_html) {
            free(html);
            html = restored_html;
        }
    }

    /* Post-process HTML for advanced table attributes (rowspan/colspan) */
    if (options->enable_tables && html) {
        extern char *apex_inject_table_attributes(const char *html, cmark_node *document, int caption_position);
        char *processed_html = apex_inject_table_attributes(html, document, options->caption_position);
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
            PROFILE_START(adjust_header_levels);
            char *adjusted_html = apex_adjust_header_levels(html, base_header_level);
            PROFILE_END(adjust_header_levels);
            if (adjusted_html) {
                free(html);
                html = adjusted_html;
            }
        }

        if (quotes_lang_metadata) {
            PROFILE_START(adjust_quotes);
            char *adjusted_quotes = apex_adjust_quote_language(html, quotes_lang_metadata);
            PROFILE_END(adjust_quotes);
            if (adjusted_quotes) {
                free(html);
                html = adjusted_quotes;
            }
        }
    }

    /* Inject header IDs if enabled */
    if (options->generate_header_ids && html) {
        PROFILE_START(header_ids);
        char *processed_html = apex_inject_header_ids(html, document, true, options->header_anchors, options->id_format);
        PROFILE_END(header_ids);
        if (processed_html && processed_html != html) {
            free(html);
            html = processed_html;
        }
    }

    /* Obfuscate email links if requested */
    if (options->obfuscate_emails && html) {
        PROFILE_START(obfuscate_emails);
        char *obfuscated = apex_obfuscate_email_links(html);
        PROFILE_END(obfuscate_emails);
        if (obfuscated) {
            free(html);
            html = obfuscated;
        }
    }

    /* Embed images as base64 data URLs if requested (local images only) */
    if (options->embed_images && html) {
        PROFILE_START(embed_images);
        char *embedded = apex_embed_images(html, options, options->base_directory);
        PROFILE_END(embed_images);
        if (embedded) {
            free(html);
            html = embedded;
        }
    }

    /* Apply metadata variable replacement if enabled (post-processing for HTML attributes, etc.)
     * Note: Most replacements happen in preprocessing, but this handles edge cases in HTML
     */
    if (metadata && options->enable_metadata_variables && html) {
        PROFILE_START(metadata_replace);
        char *replaced = apex_metadata_replace_variables(html, metadata, options);
        PROFILE_END(metadata_replace);
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
        PROFILE_START(toc);
        char *with_toc = apex_process_toc(html, document, options->id_format);
        PROFILE_END(toc);
        if (with_toc) {
            free(html);
            html = with_toc;
        }
    }

    /* Apply ARIA labels if enabled */
    if (options->enable_aria && html) {
        PROFILE_START(aria_labels);
        char *aria_html = apex_apply_aria_labels(html, document);
        PROFILE_END(aria_labels);
        if (aria_html && aria_html != html) {
            free(html);
            html = aria_html;
        }
    }

    /* Replace abbreviations if any were found */
    if (abbreviations && html) {
        PROFILE_START(abbreviations);
        char *with_abbrs = apex_replace_abbreviations(html, abbreviations);
        PROFILE_END(abbreviations);
        if (with_abbrs) {
            free(html);
            html = with_abbrs;
        }
    }

    /* Replace GitHub emoji if in GFM or Unified mode */
    if ((options->mode == APEX_MODE_GFM || options->mode == APEX_MODE_UNIFIED) && html) {
        PROFILE_START(emoji);
        char *with_emoji = apex_replace_emoji(html);
        PROFILE_END(emoji);
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
            PROFILE_START(citations_render);
            char *with_citations = apex_render_citations(html, &citation_registry, options);
            PROFILE_END(citations_render);
            if (with_citations) {
                free(html);
                html = with_citations;
            }
        }

        /* Insert bibliography at marker or end of document (even if no citations, if bibliography loaded) */
        if (html && !options->suppress_bibliography && citation_registry.bibliography) {
            PROFILE_START(bibliography);
            char *with_bibliography = apex_insert_bibliography(html, &citation_registry, options);
            PROFILE_END(bibliography);
            if (with_bibliography) {
                free(html);
                html = with_bibliography;
            }
        }
    }

    /* Render index markers and insert index */
    if (options->enable_indices && html && index_registry.count > 0) {
        PROFILE_START(index_render);
        char *with_index_markers = apex_render_index_markers(html, &index_registry, options);
        PROFILE_END(index_render);
        if (with_index_markers) {
            free(html);
            html = with_index_markers;
        }

        /* Insert index at marker or end of document */
        if (html) {
            PROFILE_START(index_insert);
            char *with_index = apex_insert_index(html, &index_registry, options);
            PROFILE_END(index_insert);
            if (with_index) {
                free(html);
                html = with_index;
            }
        }
    }

    /* Clean up index registry */
    apex_free_index_registry(&index_registry);

    /* Clean up HTML tag spacing (compress multiple spaces, remove spaces before >) */
    if (html) {
        PROFILE_START(html_clean);
        char *cleaned = apex_clean_html_tag_spacing(html);
        PROFILE_END(html_clean);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* In non-pretty mode, collapse extra newlines between adjacent tags so that
     * sequences like </table>\n\n<figure> become </table><figure>. This keeps
     * compact HTML output while still letting pretty mode control layout.
     */
    if (html && !local_opts.pretty) {
        PROFILE_START(collapse_intertag_newlines);
        char *collapsed = apex_collapse_intertag_newlines(html);
        PROFILE_END(collapse_intertag_newlines);
        if (collapsed) {
            free(html);
            html = collapsed;
        }
    }

    /* Convert thead to tbody for relaxed tables and remove empty thead from headerless tables */
    /* Only run this when relaxed_tables is enabled, otherwise keep thead as-is */
    if (html && options->enable_tables && options->relaxed_tables) {
        PROFILE_START(relaxed_tables_convert);
        char *converted = apex_convert_relaxed_table_headers(html);
        PROFILE_END(relaxed_tables_convert);
        if (converted) {
            free(html);
            html = converted;
        }
    }

    /* Post-process HTML to add style attributes to alpha lists */
    if (options->allow_alpha_lists && html) {
        PROFILE_START(alpha_lists_postprocess);
        char *processed_html = apex_postprocess_alpha_lists_html(html);
        PROFILE_END(alpha_lists_postprocess);
        if (processed_html && processed_html != html) {
            free(html);
            html = processed_html;
        }
    }

    /* Remove empty paragraphs created by ^ marker (zero-width space only) */
    if (html && options->enable_marked_extensions) {
        PROFILE_START(remove_empty_paragraphs);
        char *cleaned = apex_remove_empty_paragraphs(html);
        PROFILE_END(remove_empty_paragraphs);
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
    if (headerless_tables_processed) free(headerless_tables_processed);
    if (table_captions_processed) free(table_captions_processed);
    if (deflist_processed) free(deflist_processed);
    if (metadata_replaced) free(metadata_replaced);
    if (autolinks_processed) free(autolinks_processed);
    if (html_markdown_processed) free(html_markdown_processed);
    if (critic_processed) free(critic_processed);
    if (liquid_protected) free(liquid_protected);
    if (liquid_tags) {
        for (size_t i = 0; i < liquid_tag_count; i++) {
            free(liquid_tags[i]);
        }
        free(liquid_tags);
    }
    apex_free_metadata(metadata);
    apex_free_abbreviations(abbreviations);
    apex_free_alds(alds);
    apex_free_image_attributes(img_attrs);
    apex_free_citation_registry(&citation_registry);

    /* Post-render plugin phase: allow plugins to transform the final HTML
     * fragment before standalone wrapping and pretty-printing.
     */
    if (plugin_manager && html) {
        char *plugin_html = apex_plugins_run_text_phase(plugin_manager,
                                                        APEX_PLUGIN_PHASE_POST_RENDER,
                                                        html,
                                                        &local_opts);
        if (plugin_html) {
            free(html);
            html = plugin_html;
        }
    }

    /* Build script HTML (if any) from script_tags before wrapping or appending */
    char *scripts_html = NULL;
    if (local_opts.script_tags) {
        /* Join script tag snippets with newlines */
        size_t total_len = 0;
        size_t count = 0;
        for (char **p = local_opts.script_tags; *p; ++p) {
            size_t len = strlen(*p);
            if (len == 0) continue;
            total_len += len + 1; /* +1 for newline */
            count++;
        }

        if (count > 0 && total_len > 0) {
            scripts_html = malloc(total_len + 1); /* +1 for null terminator */
            if (scripts_html) {
                size_t pos = 0;
                for (char **p = local_opts.script_tags; *p; ++p) {
                    size_t len = strlen(*p);
                    if (len == 0) continue;
                    memcpy(scripts_html + pos, *p, len);
                    pos += len;
                    scripts_html[pos++] = '\n';
                }
                scripts_html[pos] = '\0';
            }
        }
    }

    /* Undefine the macro */
    #undef options

    /* Free plugin manager after all phases complete */
    if (plugin_manager) {
        apex_plugins_free(plugin_manager);
    }

    /* Wrap in complete HTML document if requested */
    if (local_opts.standalone && html) {
        /* CSS precedence: CLI flag (--css/--style) overrides metadata */
        const char *css_path = local_opts.stylesheet_path;
        if (!css_path) {
            css_path = css_metadata;  /* Use extracted metadata value */
        }
        /* Combine any existing HTML footer metadata with scripts (footer first, then scripts) */
        char *footer_with_scripts = NULL;
        if (html_footer_metadata || scripts_html) {
            size_t footer_len = html_footer_metadata ? strlen(html_footer_metadata) : 0;
            size_t scripts_len = scripts_html ? strlen(scripts_html) : 0;
            size_t extra_newline = (footer_len > 0 && scripts_len > 0) ? 1 : 0;

            footer_with_scripts = malloc(footer_len + extra_newline + scripts_len + 1);
            if (footer_with_scripts) {
                size_t pos = 0;
                if (footer_len > 0) {
                    memcpy(footer_with_scripts + pos, html_footer_metadata, footer_len);
                    pos += footer_len;
                }
                if (extra_newline) {
                    footer_with_scripts[pos++] = '\n';
                }
                if (scripts_len > 0) {
                    memcpy(footer_with_scripts + pos, scripts_html, scripts_len);
                    pos += scripts_len;
                }
                footer_with_scripts[pos] = '\0';
            }
        }

        const char *footer_to_use = footer_with_scripts ? footer_with_scripts : html_footer_metadata;

        PROFILE_START(standalone_wrap);
        char *document = apex_wrap_html_document(html, local_opts.document_title, css_path,
                                                 html_header_metadata, footer_to_use,
                                                 language_metadata);
        PROFILE_END(standalone_wrap);
        if (document) {
            free(html);
            html = document;
        }

        if (footer_with_scripts) {
            free(footer_with_scripts);
        }

        /* If requested, replace stylesheet link with embedded CSS contents */
        if (html && css_path && local_opts.embed_stylesheet) {
            /* Attempt to read CSS file from css_path, falling back to base_directory/css_path */
            char *css_content = NULL;
            size_t css_len = 0;

            /* Helper lambda-like block to read file into memory */
            {
                FILE *css_fp = fopen(css_path, "rb");
                if (!css_fp && local_opts.base_directory && local_opts.base_directory[0] != '\0') {
                    /* Try base_directory + "/" + css_path */
                    size_t base_len = strlen(local_opts.base_directory);
                    size_t path_len = strlen(css_path);
                    size_t full_len = base_len + 1 + path_len + 1;
                    char *full_path = malloc(full_len);
                    if (full_path) {
                        snprintf(full_path, full_len, "%s/%s", local_opts.base_directory, css_path);
                        css_fp = fopen(full_path, "rb");
                        free(full_path);
                    }
                }

                if (css_fp) {
                    if (fseek(css_fp, 0, SEEK_END) == 0) {
                        long fsize = ftell(css_fp);
                        if (fsize >= 0 && fsize < 10 * 1024 * 1024) { /* 10MB safety limit */
                            rewind(css_fp);
                            css_content = malloc((size_t)fsize + 1);
                            if (css_content) {
                                css_len = fread(css_content, 1, (size_t)fsize, css_fp);
                                css_content[css_len] = '\0';
                            }
                        }
                    }
                    fclose(css_fp);
                }
            }

            if (css_content && css_len > 0) {
                /* Build the exact link line we expect from apex_wrap_html_document */
                char link_line[2048];
                int link_n = snprintf(link_line, sizeof(link_line),
                                      "  <link rel=\"stylesheet\" href=\"%s\">\n",
                                      css_path);
                if (link_n > 0 && (size_t)link_n < sizeof(link_line)) {
                    char *pos = strstr(html, link_line);
                    if (pos) {
                        size_t html_len = strlen(html);
                        size_t link_len = (size_t)link_n;
                        const char *style_prefix = "  <style>\n";
                        const char *style_suffix = "\n  </style>\n";
                        size_t prefix_len = strlen(style_prefix);
                        size_t suffix_len = strlen(style_suffix);

                        size_t new_len = html_len - link_len + prefix_len + css_len + suffix_len;
                        char *embedded = malloc(new_len + 1);
                        if (embedded) {
                            size_t before_len = (size_t)(pos - html);
                            memcpy(embedded, html, before_len);
                            size_t offset = before_len;

                            memcpy(embedded + offset, style_prefix, prefix_len);
                            offset += prefix_len;

                            memcpy(embedded + offset, css_content, css_len);
                            offset += css_len;

                            memcpy(embedded + offset, style_suffix, suffix_len);
                            offset += suffix_len;

                            size_t after_len = html_len - before_len - link_len;
                            if (after_len > 0) {
                                memcpy(embedded + offset, pos + link_len, after_len);
                                offset += after_len;
                            }

                            embedded[offset] = '\0';
                            free(html);
                            html = embedded;
                        }
                    }
                }
            }

            if (css_content) {
                free(css_content);
            }
        }
    } else if (html && scripts_html) {
        /* Snippet mode: append scripts to the end of the HTML fragment */
        size_t html_len = strlen(html);
        size_t scripts_len = strlen(scripts_html);
        size_t extra_newline = (html_len > 0 && scripts_len > 0 && html[html_len - 1] != '\n') ? 1 : 0;

        char *combined = malloc(html_len + extra_newline + scripts_len + 1);
        if (combined) {
            size_t pos = 0;
            if (html_len > 0) {
                memcpy(combined + pos, html, html_len);
                pos += html_len;
            }
            if (extra_newline) {
                combined[pos++] = '\n';
            }
            if (scripts_len > 0) {
                memcpy(combined + pos, scripts_html, scripts_len);
                pos += scripts_len;
            }
            combined[pos] = '\0';

            free(html);
            html = combined;
        }
    }

    if (scripts_html) {
        free(scripts_html);
    }

    /* Free duplicated metadata strings */
    if (css_metadata) free(css_metadata);
    if (html_header_metadata) free(html_header_metadata);
    if (html_footer_metadata) free(html_footer_metadata);
    if (language_metadata) free(language_metadata);
    if (quotes_lang_metadata) free(quotes_lang_metadata);

    /* Remove blank lines within tables (applies to both pretty and non-pretty) */
    if (html) {
        PROFILE_START(remove_table_blank_lines);
        char *cleaned = apex_remove_table_blank_lines(html);
        PROFILE_END(remove_table_blank_lines);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* Remove table separator rows that were incorrectly rendered as data rows */
    /* This happens when smart typography converts --- to — in separator rows */
    if (html && local_opts.enable_tables) {
        PROFILE_START(remove_table_separator_rows);
        extern char *apex_remove_table_separator_rows(const char *html);
        char *cleaned = apex_remove_table_separator_rows(html);
        PROFILE_END(remove_table_separator_rows);
        if (cleaned) {
            free(html);
            html = cleaned;
        }
    }

    /* Pretty-print HTML if requested */
    if (local_opts.pretty && html) {
        PROFILE_START(pretty_print);
        char *pretty = apex_pretty_print_html(html);
        PROFILE_END(pretty_print);
        if (pretty) {
            free(html);
            html = pretty;
        }
    }

    PROFILE_END(total);

    if (profiling_enabled()) {
        fprintf(stderr, "[PROFILE] %-30s: %8s\n", "---", "---");
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

    /* Strip any existing </body></html> tags from end of content to avoid duplicates */
    size_t content_len = strlen(content);
    const char *html_end = NULL;
    const char *body_end = NULL;

    /* Find last occurrence of </html> near the end */
    const char *search = content + content_len;
    while (search > content) {
        search--;
        if (strncmp(search, "</html>", 7) == 0) {
            html_end = search;
            break;
        }
    }

    /* If we found </html>, look for </body> before it */
    if (html_end) {
        search = html_end;
        while (search > content) {
            search--;
            if (strncmp(search, "</body>", 7) == 0) {
                body_end = search;
                break;
            }
            /* Stop if we hit a non-whitespace character before finding </body> */
            if (!isspace((unsigned char)*search) && *search != '>') {
                break;
            }
        }

        /* If we found both tags at the end, strip them */
        if (body_end) {
            /* Check that there's only whitespace/newlines between </body> and </html> */
            const char *between = body_end + 7;
            bool only_whitespace = true;
            while (between < html_end) {
                if (!isspace((unsigned char)*between)) {
                    only_whitespace = false;
                    break;
                }
                between++;
            }

            if (only_whitespace) {
                /* Strip everything from </body> to end */
                content_len = body_end - content;
            }
        }
    }
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
            "    tfoot td { background: #e8e8e8; }\n"
            "    figure.table-figure { width: fit-content; margin: 1em 0; }\n"
            "    figure.table-figure table { width: auto; }\n"
            "    figcaption { text-align: center; font-weight: bold; font-size: 0.8em; }\n"
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
