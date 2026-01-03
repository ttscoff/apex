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

@end

NS_ASSUME_NONNULL_END

