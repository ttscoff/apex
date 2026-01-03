/**
 * NSString+Apex.h
 * Objective-C category for integrating Apex Markdown processor into Marked
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Apex mode constants for use with convertWithApex:mode:
 */
extern NSString * const ApexModeCommonmark;
extern NSString * const ApexModeGFM;
extern NSString * const ApexModeMultiMarkdown;
extern NSString * const ApexModeKramdown;
extern NSString * const ApexModeUnified;

@interface NSString (Apex)

/**
 * Convert Markdown to HTML using Apex processor in unified mode
 * @param inputString The markdown text to convert
 * @return HTML string
 */
+ (NSString *)convertWithApex:(NSString *)inputString;

/**
 * Convert Markdown to HTML using Apex with specific processor mode
 * @param inputString The markdown text to convert
 * @param mode Processor mode: Use ApexMode* constants (ApexModeCommonmark, ApexModeGFM, ApexModeMultiMarkdown, ApexModeKramdown, or ApexModeUnified) or string values: "commonmark", "gfm", "multimarkdown", "kramdown", or "unified"
 * @return HTML string
 */
+ (NSString *)convertWithApex:(NSString *)inputString mode:(NSString *)mode;

/**
 * Convert Markdown to HTML using Apex with standalone document options
 * @param inputString The markdown text to convert
 * @param mode Processor mode: Use ApexMode* constants or string values
 * @param standalone If YES, generates a complete HTML5 document
 * @param stylesheetPath Optional path to CSS file to link in document head (nil for none)
 * @param title Optional document title (nil for default)
 * @return HTML string (complete document if standalone is YES)
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)mode
                   standalone:(BOOL)standalone
                    stylesheet:(NSString * _Nullable)stylesheetPath
                         title:(NSString * _Nullable)title;

/**
 * Convert Markdown to HTML using Apex with pretty printing option
 * @param inputString The markdown text to convert
 * @param mode Processor mode: Use ApexMode* constants or string values
 * @param pretty If YES, pretty-prints HTML with indentation
 * @return HTML string
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)mode
                        pretty:(BOOL)pretty;

/**
 * Convert Markdown to HTML using Apex with dictionary-based options
 * @param inputString The markdown text to convert
 * @param mode Processor mode: Use ApexMode* constants or string values
 * @param options Dictionary of option keys and values. Supported keys:
 *   - @"pretty": NSNumber (BOOL) - Pretty-print HTML
 *   - @"standalone": NSNumber (BOOL) - Generate complete HTML document
 *   - @"stylesheet": NSString - Path to CSS file
 *   - @"title": NSString - Document title
 *   - @"hardBreaks": NSNumber (BOOL) - Treat newlines as hard breaks
 *   - @"generateHeaderIDs": NSNumber (BOOL) - Generate IDs for headers
 *   - @"unsafe": NSNumber (BOOL) - Allow raw HTML
 *   - @"headerAnchors": NSNumber (BOOL) - Generate anchor tags instead of IDs
 *   - @"obfuscateEmails": NSNumber (BOOL) - Obfuscate email links
 *   - @"embedImages": NSNumber (BOOL) - Embed images as base64 data URLs
 * @return HTML string
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)mode
                    options:(NSDictionary<NSString *, id> * _Nullable)options;

/**
 * Convert Markdown to HTML using Apex with common options combined
 * Swift-friendly method that combines frequently used options
 * @param inputString The markdown text to convert
 * @param mode Processor mode: Use ApexMode* constants or string values
 * @param generateHeaderIDs If YES, generates IDs for headers
 * @param hardBreaks If YES, treats newlines as hard breaks
 * @param pretty If YES, pretty-prints HTML with indentation
 * @return HTML string
 */
+ (NSString *)convertWithApex:(NSString *)inputString
                         mode:(NSString *)mode
              generateHeaderIDs:(BOOL)generateHeaderIDs
                    hardBreaks:(BOOL)hardBreaks
                        pretty:(BOOL)pretty;

/**
 * Convert this string (as Markdown) to HTML using Apex in unified mode
 * Instance method for convenient usage on NSString objects
 * @return HTML string
 */
- (NSString *)apexHTML;

/**
 * Convert this string (as Markdown) to HTML using Apex with specific mode
 * Instance method for convenient usage on NSString objects
 * @param mode Processor mode: Use ApexMode* constants or string values
 * @return HTML string
 */
- (NSString *)apexHTMLWithMode:(NSString *)mode;

@end

NS_ASSUME_NONNULL_END

