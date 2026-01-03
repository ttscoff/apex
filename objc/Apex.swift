/**
 * Apex.swift
 * Swift API wrapper for Apex Markdown processor
 * Provides idiomatic Swift interface over the Objective-C API
 */

import Foundation

/// Apex processor mode
/// Type-safe enum for processor modes
public enum ApexMode: String, CaseIterable {
    case commonmark = "commonmark"
    case gfm = "gfm"
    case multimarkdown = "multimarkdown"
    case kramdown = "kramdown"
    case unified = "unified"

    /// Convert to NSString for Objective-C API
    internal var nsString: NSString {
        return self.rawValue as NSString
    }
}

/// Apex conversion options
/// Swift struct for configuring conversion options
public struct ApexOptions {
    /// Pretty-print HTML with indentation
    public var pretty: Bool = false

    /// Generate complete HTML5 document
    public var standalone: Bool = false

    /// Path to CSS file to link in document head
    public var stylesheet: String? = nil

    /// Document title
    public var title: String? = nil

    /// Treat newlines as hard breaks
    public var hardBreaks: Bool = false

    /// Generate IDs for headers
    public var generateHeaderIDs: Bool = false

    /// Allow raw HTML in output
    public var unsafe: Bool = false

    /// Generate anchor tags instead of header IDs
    public var headerAnchors: Bool = false

    /// Obfuscate email links using HTML entities
    public var obfuscateEmails: Bool = false

    /// Embed local images as base64 data URLs
    public var embedImages: Bool = false

    /// Default options
    public static let `default` = ApexOptions()

    /// Convert to NSDictionary for Objective-C API
    internal func toDictionary() -> [String: Any] {
        var dict: [String: Any] = [:]

        if pretty {
            dict["pretty"] = true
        }
        if standalone {
            dict["standalone"] = true
        }
        if let stylesheet = stylesheet {
            dict["stylesheet"] = stylesheet
        }
        if let title = title {
            dict["title"] = title
        }
        if hardBreaks {
            dict["hardBreaks"] = true
        }
        if generateHeaderIDs {
            dict["generateHeaderIDs"] = true
        }
        if unsafe {
            dict["unsafe"] = true
        }
        if headerAnchors {
            dict["headerAnchors"] = true
        }
        if obfuscateEmails {
            dict["obfuscateEmails"] = true
        }
        if embedImages {
            dict["embedImages"] = true
        }

        return dict
    }
}

/// String extension for Apex Markdown conversion
/// Provides idiomatic Swift API
extension String {
    /**
     * Convert Markdown to HTML using Apex in unified mode
     */
    public func apexHTML() -> String {
        return NSString.convert(withApex: self) as String
    }

    /**
     * Convert Markdown to HTML using Apex with specific mode
     */
    public func apexHTML(mode: ApexMode) -> String {
        return NSString.convert(withApex: self, mode: mode.nsString) as String
    }

    /**
     * Convert Markdown to HTML using Apex with mode and options
     */
    public func apexHTML(mode: ApexMode = .unified, options: ApexOptions = .default) -> String {
        let dict = options.toDictionary()
        if dict.isEmpty {
            // No options, use simple method
            return NSString.convert(withApex: self, mode: mode.nsString) as String
        } else {
            // Use dictionary-based method
            return NSString.convert(
                withApex: self, mode: mode.nsString, options: dict as NSDictionary) as String
        }
    }

    /**
     * Convert Markdown to HTML using Apex with standalone document options
     */
    public func apexHTML(
        mode: ApexMode = .unified, standalone: Bool, stylesheet: String? = nil, title: String? = nil
    ) -> String {
        return NSString.convert(
            withApex: self,
            mode: mode.nsString,
            standalone: standalone,
            stylesheet: stylesheet as NSString?,
            title: title as NSString?
        ) as String
    }

    /**
     * Convert Markdown to HTML using Apex with pretty printing
     * Convenience method for pretty-printing only
     */
    public func apexHTML(pretty: Bool, mode: ApexMode = .unified) -> String {
        return NSString.convert(withApex: self, mode: mode.nsString, pretty: pretty) as String
    }

    /**
     * Convert Markdown to HTML using Apex with common options
     */
    public func apexHTML(
        mode: ApexMode = .unified,
        generateHeaderIDs: Bool,
        hardBreaks: Bool,
        pretty: Bool
    ) -> String {
        return NSString.convert(
            withApex: self,
            mode: mode.nsString,
            generateHeaderIDs: generateHeaderIDs,
            hardBreaks: hardBreaks,
            pretty: pretty
        ) as String
    }
}

/// Static Apex converter
/// Provides static methods for conversion
public struct Apex {
    /**
     * Convert Markdown to HTML using Apex in unified mode
     */
    public static func convert(_ markdown: String) -> String {
        return markdown.apexHTML()
    }

    /**
     * Convert Markdown to HTML using Apex with specific mode
     */
    public static func convert(_ markdown: String, mode: ApexMode) -> String {
        return markdown.apexHTML(mode: mode)
    }

    /**
     * Convert Markdown to HTML using Apex with mode and options
     */
    public static func convert(
        _ markdown: String, mode: ApexMode = .unified, options: ApexOptions = .default
    ) -> String {
        return markdown.apexHTML(mode: mode, options: options)
    }

    /**
     * Convert Markdown to HTML using Apex with standalone document options
     */
    public static func convert(
        _ markdown: String, mode: ApexMode = .unified, standalone: Bool, stylesheet: String? = nil,
        title: String? = nil
    ) -> String {
        return markdown.apexHTML(
            mode: mode, standalone: standalone, stylesheet: stylesheet, title: title)
    }

    /**
     * Convert Markdown to HTML using Apex with pretty printing
     */
    public static func convert(_ markdown: String, pretty: Bool, mode: ApexMode = .unified)
        -> String
    {
        return markdown.apexHTML(pretty: pretty, mode: mode)
    }

    /**
     * Convert Markdown to HTML using Apex with common options
     */
    public static func convert(
        _ markdown: String,
        mode: ApexMode = .unified,
        generateHeaderIDs: Bool,
        hardBreaks: Bool,
        pretty: Bool
    ) -> String {
        return markdown.apexHTML(
            mode: mode, generateHeaderIDs: generateHeaderIDs, hardBreaks: hardBreaks, pretty: pretty
        )
    }
}
