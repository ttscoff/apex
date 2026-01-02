/**
 * GitHub Emoji Extension for Apex
 * Complete implementation with 861 emoji mappings
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "emoji_data.h"

/* Forward declarations */
static void normalize_emoji_name(char *name);
static int is_table_alignment_pattern(const char *start, const char *end);

/**
 * Find emoji entry by name
 * Returns pointer to emoji_entry or NULL if not found
 */
static const emoji_entry *find_emoji_entry(const char *name, int len) {
    for (int i = 0; complete_emoji_map[i].name; i++) {
        if (strlen(complete_emoji_map[i].name) == (size_t)len &&
            strncmp(complete_emoji_map[i].name, name, len) == 0) {
            return &complete_emoji_map[i];
        }
    }
    return NULL;
}

/**
 * Check if we're inside a header tag (h1-h6)
 * Simple state machine to track if we're between <h1> and </h1> etc.
 */
static int is_in_header(const char *pos, const char *start) {
    /* Look backwards for opening header tag */
    const char *p = pos;
    int depth = 0;

    while (p >= start) {
        if (p[0] == '>' && p > start + 3) {
            /* Check if this is a closing tag */
            if (p[-1] == '/' && p[-2] == '<') {
                /* Check for </h1> through </h6> */
                if (p >= start + 5) {
                    if ((p[-3] == 'h' || p[-3] == 'H') &&
                        p[-4] >= '1' && p[-4] <= '6') {
                        depth--;
                        if (depth < 0) return 0; /* Outside header */
                    }
                }
            } else if (p > start + 2) {
                /* Check for <h1> through <h6> */
                if (p[-1] == 'h' || p[-1] == 'H') {
                    if (p >= start + 3 && p[-2] >= '1' && p[-2] <= '6' && p[-3] == '<') {
                        depth++;
                        if (depth > 0) return 1; /* Inside header */
                    }
                }
            }
        }
        p--;
    }
    return 0;
}

/**
 * Find emoji name from unicode emoji (reverse lookup)
 * Compares the unicode string against the emoji map
 */
const char *apex_find_emoji_name(const char *unicode, size_t unicode_len) {
    if (!unicode || unicode_len == 0) return NULL;

    for (int i = 0; complete_emoji_map[i].name; i++) {
        const char *emoji_unicode = complete_emoji_map[i].unicode;
        if (emoji_unicode) {
            size_t emoji_len = strlen(emoji_unicode);
            /* Check if the unicode matches (exact match) */
            if (emoji_len == unicode_len &&
                strncmp(emoji_unicode, unicode, unicode_len) == 0) {
                return complete_emoji_map[i].name;
            }
        }
    }
    return NULL;
}

/**
 * Replace :emoji: patterns in HTML
 * Handles both unicode and image-based emojis
 */
char *apex_replace_emoji(const char *html) {
    if (!html) return NULL;

    size_t capacity = strlen(html) * 3;  /* Extra space for image tags */
    char *output = malloc(capacity);
    if (!output) return strdup(html);

    const char *read = html;
    char *write = output;
    size_t remaining = capacity;

    while (*read) {
        if (*read == ':') {
            /* Look for closing : */
            const char *end = strchr(read + 1, ':');
            if (end && (end - read) < 50) {  /* Reasonable emoji name length */
                /* Extract emoji name */
                int name_len = end - (read + 1);
                const char *name_start = read + 1;

                /* Validate: must have at least one character and no spaces */
                if (name_len > 0) {
                    /* Check for spaces in the name */
                    int has_space = 0;
                    for (int i = 0; i < name_len; i++) {
                        if (name_start[i] == ' ' || name_start[i] == '\t' || name_start[i] == '\n') {
                            has_space = 1;
                            break;
                        }
                    }

                    /* Skip if it contains only table alignment characters (pipes, dashes, colons) */
                    if (!has_space && is_table_alignment_pattern(name_start, end)) {
                        /* This is a table alignment pattern like :---:, :|:, :|---:, etc. */
                        /* Copy the colon pair as-is */
                        size_t pattern_len = end - read + 1;
                        if (pattern_len < remaining) {
                            memcpy(write, read, pattern_len);
                            write += pattern_len;
                            remaining -= pattern_len;
                        }
                        read = end + 1;
                        continue;
                    }

                    if (!has_space) {
                        /* Normalize the name for comparison */
                        char normalized[64];
                        if ((size_t)name_len >= sizeof(normalized)) {
                            name_len = sizeof(normalized) - 1;
                        }
                        memcpy(normalized, name_start, name_len);
                        normalized[name_len] = '\0';
                        normalize_emoji_name(normalized);
                        size_t normalized_len = strlen(normalized);

                        const emoji_entry *entry = find_emoji_entry(normalized, (int)normalized_len);

                        if (entry) {
                            int in_header = is_in_header(read, html);

                            if (entry->unicode) {
                                /* Unicode emoji */
                                size_t emoji_len = strlen(entry->unicode);
                                if (emoji_len < remaining) {
                                    memcpy(write, entry->unicode, emoji_len);
                                    write += emoji_len;
                                    remaining -= emoji_len;
                                    read = end + 1;
                                    continue;
                                } else {
                                    /* Not enough space, copy original pattern as-is */
                                    size_t pattern_len = end - read + 1;
                                    if (pattern_len < remaining) {
                                        memcpy(write, read, pattern_len);
                                        write += pattern_len;
                                        remaining -= pattern_len;
                                    }
                                    read = end + 1;
                                    continue;
                                }
                            } else if (entry->image_url) {
                                /* Image-based emoji */
                                const char *img_tag;
                                if (in_header) {
                                    /* In header: use em units for sizing */
                                    img_tag = "<img class=\"emoji\" src=\"%s\" alt=\":%s:\" style=\"height: 1em; width: auto; vertical-align: middle;\">";
                                } else {
                                    /* Regular text: use fixed size */
                                    img_tag = "<img class=\"emoji\" src=\"%s\" alt=\":%s:\" height=\"20\" width=\"20\" align=\"absmiddle\">";
                                }

                                int needed = snprintf(write, remaining, img_tag, entry->image_url, entry->name);
                                if (needed > 0 && (size_t)needed < remaining) {
                                    write += needed;
                                    remaining -= needed;
                                    read = end + 1;
                                    continue;
                                } else {
                                    /* Not enough space, copy original pattern as-is */
                                    size_t pattern_len = end - read + 1;
                                    if (pattern_len < remaining) {
                                        memcpy(write, read, pattern_len);
                                        write += pattern_len;
                                        remaining -= pattern_len;
                                    }
                                    read = end + 1;
                                    continue;
                                }
                            }
                        } else {
                            /* No match found, copy the entire pattern as-is */
                            size_t pattern_len = end - read + 1;
                            if (pattern_len < remaining) {
                                memcpy(write, read, pattern_len);
                                write += pattern_len;
                                remaining -= pattern_len;
                            }
                            read = end + 1;
                            continue;
                        }
                    }
                }
            }
        }

        /* Not an emoji pattern, copy character */
        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            read++;
        }
    }

    *write = '\0';
    return output;
}

/**
 * Normalize emoji name: lowercase, hyphens to underscores, remove colons
 */
static void normalize_emoji_name(char *name) {
    size_t len = strlen(name);
    size_t write_pos = 0;

    for (size_t i = 0; i < len; i++) {
        if (name[i] == ':') {
            /* Skip colons */
            continue;
        } else if (name[i] == '-') {
            name[write_pos++] = '_';
        } else {
            name[write_pos++] = (char)tolower((unsigned char)name[i]);
        }
    }
    name[write_pos] = '\0';
}

/**
 * Calculate Levenshtein distance between two strings
 */
static int levenshtein_distance(const char *s1, size_t len1, const char *s2, size_t len2) {
    if (len1 == 0) return (int)len2;
    if (len2 == 0) return (int)len1;

    /* Use dynamic programming with minimal memory */
    int *prev_row = malloc((len2 + 1) * sizeof(int));
    int *curr_row = malloc((len2 + 1) * sizeof(int));
    if (!prev_row || !curr_row) {
        free(prev_row);
        free(curr_row);
        return (int)(len1 > len2 ? len1 : len2); /* Fallback: return max length */
    }

    /* Initialize first row */
    for (size_t i = 0; i <= len2; i++) {
        prev_row[i] = (int)i;
    }

    /* Fill the matrix */
    for (size_t i = 0; i < len1; i++) {
        curr_row[0] = (int)(i + 1);
        for (size_t j = 0; j < len2; j++) {
            int cost = (s1[i] == s2[j]) ? 0 : 1;
            int deletion = prev_row[j + 1] + 1;
            int insertion = curr_row[j] + 1;
            int substitution = prev_row[j] + cost;

            curr_row[j + 1] = (deletion < insertion) ?
                (deletion < substitution ? deletion : substitution) :
                (insertion < substitution ? insertion : substitution);
        }

        /* Swap rows */
        int *temp = prev_row;
        prev_row = curr_row;
        curr_row = temp;
    }

    int result = prev_row[len2];
    free(prev_row);
    free(curr_row);
    return result;
}

/**
 * Find best emoji match using fuzzy matching
 * Returns the shortest matching emoji name within max_distance, or NULL if no match
 */
static const char *find_best_emoji_match(const char *name, size_t name_len, int max_distance) {
    char normalized[64];
    if (name_len >= sizeof(normalized)) {
        name_len = sizeof(normalized) - 1;
    }
    memcpy(normalized, name, name_len);
    normalized[name_len] = '\0';
    normalize_emoji_name(normalized);
    size_t normalized_len = strlen(normalized);

    /* Check exact match first */
    const emoji_entry *exact = find_emoji_entry(normalized, (int)normalized_len);
    if (exact) {
        return exact->name;
    }

    /* Find fuzzy matches */
    int best_distance = max_distance + 1;
    size_t best_length = SIZE_MAX;
    const char *best_match = NULL;

    for (int i = 0; complete_emoji_map[i].name; i++) {
        const char *emoji_name = complete_emoji_map[i].name;
        size_t emoji_len = strlen(emoji_name);

        int distance = levenshtein_distance(normalized, normalized_len, emoji_name, emoji_len);

        if (distance <= max_distance) {
            if (distance < best_distance ||
                (distance == best_distance && emoji_len < best_length)) {
                best_distance = distance;
                best_length = emoji_len;
                best_match = emoji_name;
            }
        }
    }

    return best_match;
}

/**
 * Check if a colon pair contains only table-related characters (pipes, dashes, colons)
 * This helps identify table alignment markers like :---:, :|:, :|---:, etc.
 * Note: Patterns with spaces are already filtered out before this check.
 */
static int is_table_alignment_pattern(const char *start, const char *end) {
    if (!start || !end || start >= end) return 0;

    /* Check if content between colons contains only pipes, dashes, or colons */
    /* (spaces are already checked separately and rejected) */
    for (const char *p = start; p < end; p++) {
        if (*p != '|' && *p != '-' && *p != ':') {
            /* Found a character that's not part of table alignment syntax */
            return 0;
        }
    }

    /* If we only have pipes, dashes, or colons, it's a table alignment pattern */
    return 1;
}

/**
 * Autocorrect emoji names in markdown text
 * Processes :emoji_name: patterns and corrects typos using fuzzy matching
 */
char *apex_autocorrect_emoji_names(const char *text) {
    if (!text) return NULL;

    size_t capacity = strlen(text) * 2;
    char *output = malloc(capacity);
    if (!output) return strdup(text);

    const char *read = text;
    char *write = output;
    size_t remaining = capacity;

    while (*read) {
        if (*read == ':') {
            /* Look for closing : */
            const char *end = strchr(read + 1, ':');
            if (end && (end - read) < 50) {  /* Reasonable emoji name length */
                /* Extract emoji name */
                int name_len = end - (read + 1);
                const char *name_start = read + 1;

                    /* Validate: must have at least one character and no spaces */
                if (name_len > 0) {
                    /* Check for spaces in the name */
                    int has_space = 0;
                    for (int i = 0; i < name_len; i++) {
                        if (name_start[i] == ' ' || name_start[i] == '\t' || name_start[i] == '\n') {
                            has_space = 1;
                            break;
                        }
                    }

                    /* Skip if it contains only table alignment characters (pipes, dashes, colons) */
                    if (!has_space && is_table_alignment_pattern(name_start, end)) {
                        /* This is a table alignment pattern like :---:, :|:, :|---:, etc. */
                        /* Copy the colon pair as-is */
                        size_t pattern_len = end - read + 1;
                        if (pattern_len < remaining) {
                            memcpy(write, read, pattern_len);
                            write += pattern_len;
                            remaining -= pattern_len;
                        }
                        read = end + 1;
                        continue;
                    }

                    if (!has_space) {
                        /* Normalize the name for comparison */
                        char normalized[64];
                        if ((size_t)name_len >= sizeof(normalized)) {
                            name_len = sizeof(normalized) - 1;
                        }
                        memcpy(normalized, name_start, name_len);
                        normalized[name_len] = '\0';
                        normalize_emoji_name(normalized);
                        size_t normalized_len = strlen(normalized);

                        /* Check if it's already correct (using normalized name) */
                        const emoji_entry *entry = find_emoji_entry(normalized, (int)normalized_len);

                        if (entry) {
                            /* Already correct (after normalization), write normalized version */
                            /* Check if we have enough space for :name: */
                            if (normalized_len + 2 <= remaining) {
                                *write++ = ':';
                                remaining--;
                                memcpy(write, normalized, normalized_len);
                                write += normalized_len;
                                remaining -= normalized_len;
                                *write++ = ':';
                                remaining--;
                            } else {
                                /* Not enough space, copy original pattern as-is */
                                size_t pattern_len = end - read + 1;
                                if (pattern_len < remaining) {
                                    memcpy(write, read, pattern_len);
                                    write += pattern_len;
                                    remaining -= pattern_len;
                                }
                            }
                            read = end + 1;
                            continue;
                        } else {
                            /* Try fuzzy matching */
                            const char *best_match = find_best_emoji_match(name_start, (size_t)name_len, 4);
                            if (best_match) {
                                /* Replace with corrected name */
                                size_t match_len = strlen(best_match);
                                /* Check if we have enough space for :name: */
                                if (match_len + 2 <= remaining) {
                                    *write++ = ':';
                                    remaining--;
                                    memcpy(write, best_match, match_len);
                                    write += match_len;
                                    remaining -= match_len;
                                    *write++ = ':';
                                    remaining--;
                                } else {
                                    /* Not enough space, copy original pattern as-is */
                                    size_t pattern_len = end - read + 1;
                                    if (pattern_len < remaining) {
                                        memcpy(write, read, pattern_len);
                                        write += pattern_len;
                                        remaining -= pattern_len;
                                    }
                                }
                                read = end + 1;
                                continue;
                            }
                            /* No match found, copy the entire pattern as-is */
                            size_t pattern_len = end - read + 1;
                            if (pattern_len < remaining) {
                                memcpy(write, read, pattern_len);
                                write += pattern_len;
                                remaining -= pattern_len;
                            }
                            read = end + 1;
                            continue;
                        }
                    }
                }
            }
        }

        /* Not an emoji pattern, copy character */
        if (remaining > 0) {
            *write++ = *read++;
            remaining--;
        } else {
            read++;
        }
    }

    *write = '\0';
    return output;
}
