/**
 * NSString+Apex.m
 * Implementation of Apex Markdown processor integration
 */

#import "NSString+Apex.h"
#import <apex/apex.h>

/**
 * Apex mode constants
 */
NSString * const ApexModeCommonmark = @"commonmark";
NSString * const ApexModeGFM = @"gfm";
NSString * const ApexModeMultiMarkdown = @"multimarkdown";
NSString * const ApexModeKramdown = @"kramdown";
NSString * const ApexModeUnified = @"unified";

@implementation NSString (Apex)

/**
 * Convert mode string to apex_mode_t enum
 */
+ (apex_mode_t)apexModeFromString:(NSString *)modeString {
    NSString *mode = [modeString lowercaseString];

    if ([mode isEqualToString:@"commonmark"]) {
        return APEX_MODE_COMMONMARK;
    } else if ([mode isEqualToString:@"gfm"]) {
        return APEX_MODE_GFM;
    } else if ([mode isEqualToString:@"multimarkdown"] || [mode isEqualToString:@"mmd"]) {
        return APEX_MODE_MULTIMARKDOWN;
    } else if ([mode isEqualToString:@"kramdown"]) {
        return APEX_MODE_KRAMDOWN;
    } else {
        return APEX_MODE_UNIFIED;  /* Default to unified */
    }
}

/**
 * Convert Markdown to HTML using Apex (unified mode)
 */
+ (NSString *)convertWithApex:(NSString *)inputString {
    return [self convertWithApex:inputString mode:ApexModeUnified];
}

/**
 * Convert Markdown to HTML using Apex with specific mode
 */
+ (NSString *)convertWithApex:(NSString *)inputString mode:(NSString *)modeString {
    if (!inputString || [inputString length] == 0) {
        return @"";
    }

    /* Convert to C string */
    const char *markdown = [inputString UTF8String];
    if (!markdown) {
        return @"";
    }

    /* Get options for the specified mode */
    apex_mode_t mode = [self apexModeFromString:modeString];
    apex_options options = apex_options_for_mode(mode);

    /* Convert to HTML */
    char *html_c = apex_markdown_to_html(markdown, strlen(markdown), &options);

    if (!html_c) {
        return @"";
    }

    /* Convert back to NSString */
    NSString *html = [NSString stringWithUTF8String:html_c];
    apex_free_string(html_c);

    return html ? html : @"";
}

@end

