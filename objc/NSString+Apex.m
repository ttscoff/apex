/**
 * NSString+Apex.m
 * Implementation of Apex Markdown processor integration
 */

#import "NSString+Apex.h"
#import <apex/apex.h>

/**
 * Apex mode constants
 */
NSString *const ApexModeCommonmark = @"commonmark";
NSString *const ApexModeGFM = @"gfm";
NSString *const ApexModeMultiMarkdown = @"multimarkdown";
NSString *const ApexModeKramdown = @"kramdown";
NSString *const ApexModeUnified = @"unified";

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
  } else if ([mode isEqualToString:@"multimarkdown"] ||
             [mode isEqualToString:@"mmd"]) {
    return APEX_MODE_MULTIMARKDOWN;
  } else if ([mode isEqualToString:@"kramdown"]) {
    return APEX_MODE_KRAMDOWN;
  } else {
    return APEX_MODE_UNIFIED; /* Default to unified */
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
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)modeString {
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

/**
 * Convert Markdown to HTML using Apex with standalone document options
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)modeString
                   standalone:(BOOL)standalone
                   stylesheet:(NSString *_Nullable)stylesheetPath
                        title:(NSString *_Nullable)title {
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

  /* Set standalone document options */
  options.standalone = standalone;
  if (stylesheetPath && [stylesheetPath length] > 0) {
    options.stylesheet_path = [stylesheetPath UTF8String];
  }
  if (title && [title length] > 0) {
    options.document_title = [title UTF8String];
  }

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

/**
 * Convert Markdown to HTML using Apex with pretty printing option
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)modeString
                       pretty:(BOOL)pretty {
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

  /* Set pretty printing option */
  options.pretty = pretty;

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

/**
 * Convert Markdown to HTML using Apex with dictionary-based options
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)modeString
                      options:
                          (NSDictionary<NSString *, id> *_Nullable)optionsDict {
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

  /* Apply dictionary options if provided */
  if (optionsDict) {
    /* Pretty printing */
    id prettyValue = optionsDict[@"pretty"];
    if (prettyValue && [prettyValue isKindOfClass:[NSNumber class]]) {
      options.pretty = [prettyValue boolValue];
    }

    /* Standalone document */
    id standaloneValue = optionsDict[@"standalone"];
    if (standaloneValue && [standaloneValue isKindOfClass:[NSNumber class]]) {
      options.standalone = [standaloneValue boolValue];
    }

    /* Stylesheet path */
    id stylesheetValue = optionsDict[@"stylesheet"];
    if (stylesheetValue && [stylesheetValue isKindOfClass:[NSString class]]) {
      NSString *stylesheet = (NSString *)stylesheetValue;
      if ([stylesheet length] > 0) {
        options.stylesheet_path = [stylesheet UTF8String];
      }
    }

    /* Document title */
    id titleValue = optionsDict[@"title"];
    if (titleValue && [titleValue isKindOfClass:[NSString class]]) {
      NSString *title = (NSString *)titleValue;
      if ([title length] > 0) {
        options.document_title = [title UTF8String];
      }
    }

    /* Hard breaks */
    id hardBreaksValue = optionsDict[@"hardBreaks"];
    if (hardBreaksValue && [hardBreaksValue isKindOfClass:[NSNumber class]]) {
      options.hardbreaks = [hardBreaksValue boolValue];
    }

    /* Generate header IDs */
    id generateHeaderIDsValue = optionsDict[@"generateHeaderIDs"];
    if (generateHeaderIDsValue &&
        [generateHeaderIDsValue isKindOfClass:[NSNumber class]]) {
      options.generate_header_ids = [generateHeaderIDsValue boolValue];
    }

    /* Unsafe HTML */
    id unsafeValue = optionsDict[@"unsafe"];
    if (unsafeValue && [unsafeValue isKindOfClass:[NSNumber class]]) {
      options.unsafe = [unsafeValue boolValue];
    }

    /* Header anchors */
    id headerAnchorsValue = optionsDict[@"headerAnchors"];
    if (headerAnchorsValue &&
        [headerAnchorsValue isKindOfClass:[NSNumber class]]) {
      options.header_anchors = [headerAnchorsValue boolValue];
    }

    /* Obfuscate emails */
    id obfuscateEmailsValue = optionsDict[@"obfuscateEmails"];
    if (obfuscateEmailsValue &&
        [obfuscateEmailsValue isKindOfClass:[NSNumber class]]) {
      options.obfuscate_emails = [obfuscateEmailsValue boolValue];
    }

    /* Embed images */
    id embedImagesValue = optionsDict[@"embedImages"];
    if (embedImagesValue && [embedImagesValue isKindOfClass:[NSNumber class]]) {
      options.embed_images = [embedImagesValue boolValue];
    }
  }

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

/**
 * Convert Markdown to HTML using Apex with common options combined
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)modeString
            generateHeaderIDs:(BOOL)generateHeaderIDs
                   hardBreaks:(BOOL)hardBreaks
                       pretty:(BOOL)pretty {
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

  /* Set common options */
  options.generate_header_ids = generateHeaderIDs;
  options.hardbreaks = hardBreaks;
  options.pretty = pretty;

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

/**
 * Convert this string (as Markdown) to HTML using Apex in unified mode
 */
- (NSString *)apexHTML {
  return [NSString convertWithApex:self];
}

/**
 * Convert this string (as Markdown) to HTML using Apex with specific mode
 */
- (NSString *)apexHTMLWithMode:(NSString *)mode {
  return [NSString convertWithApex:self mode:mode];
}

@end
