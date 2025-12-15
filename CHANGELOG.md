# Changelog

All notable changes to Apex will be documented in this file.

## [0.1.28] - 2025-12-15

### Changed

- Default wikilink URLs now replace spaces with dashes (e.g. [[Home Page]] -> href="Home-Page").

### New

- Add --wikilink-space and --wikilink-extension flags to control how [[WikiLink]] hrefs are generated.
- Allow wikilink space and extension configuration via metadata keys wikilink-space and wikilink-extension.
- Support Kramdown-style {:toc ...} markers mapped to Apex TOC generation.
- Add tests for `{:toc}` syntaxes
- MMD includes support full glob patterns like {{*.md}} and {{c?de.py}}.
- Add plugin discovery from .apex/plugins and ~/.config/apex/plugins.
- Allow external handler plugins in any language via JSON stdin/stdout.
- Support declarative regex plugins for pre_parse and post_render phases.
- Add `--no-plugins` CLI flag to disable all plugins for a run.
- Support `plugins: true/false` metadata to enable or disable plugins.
- Initial planning for a remote plugin directory and install features

### Improved

- Exclude headings with .no_toc class from generated tables of contents for finer-grained TOC control.
- MMD-style {{file.*}} now resolves preferred extensions before globbing.
- Transclusion respects brace-style patterns such as {{{intro,part1}.md}} where supported.
- Provide `APEX_PLUGIN_DIR` and `APEX_SUPPORT_DIR` for plugin code and data.
- Add profiling (APEX_PROFILE_PLUGINS=1) for plugins

## [0.1.27] - 2025-12-15

### Changed

- Default wikilink URLs now replace spaces with dashes (e.g. [[Home Page]] -> href="Home-Page").

### New

- Add --wikilink-space and --wikilink-extension flags to control how [[WikiLink]] hrefs are generated.
- Allow wikilink space and extension configuration via metadata keys wikilink-space and wikilink-extension.
- Support Kramdown-style {:toc ...} markers mapped to Apex TOC generation.
- Add tests for `{:toc}` syntaxes
- MMD includes support full glob patterns like {{*.md}} and {{c?de.py}}.
- Add plugin discovery from .apex/plugins and ~/.config/apex/plugins.
- Allow external handler plugins in any language via JSON stdin/stdout.
- Support declarative regex plugins for pre_parse and post_render phases.
- Add `--no-plugins` CLI flag to disable all plugins for a run.
- Support `plugins: true/false` metadata to enable or disable plugins.
- Initial planning for a remote plugin directory and install features

### Improved

- Exclude headings with .no_toc class from generated tables of contents for finer-grained TOC control.
- MMD-style {{file.*}} now resolves preferred extensions before globbing.
- Transclusion respects brace-style patterns such as {{{intro,part1}.md}} where supported.
- Provide `APEX_PLUGIN_DIR` and `APEX_SUPPORT_DIR` for plugin code and data.
- Add profiling (APEX_PROFILE_PLUGINS=1) for plugins

## [0.1.26] - 2025-12-14

### Changed

- Change `--enable-includes` to `--[no-]includes`, allowing `--no-includes` to disable includes in unified mode and shortening the flag
- Integrate metadata-to-options application into CLI after metadata merging
- Preserve bibliography files array when metadata mode resets options structure

### New

- Add apex_apply_metadata_to_options() function to apply metadata values to apex_options structure
- Support controlling boolean flags via metadata (indices, wikilinks, includes, relaxed-tables, alpha-lists, mixed-lists, sup-sub, autolink, transforms, unsafe, tables, footnotes, smart, math, ids, header-anchors, embed-images, link-citations, show-tooltips, suppress-bibliography, suppress-index, group-index-by-letter, obfuscate-emails, pretty, standalone, hardbreaks)
- Support controlling string options via metadata (bibliography, csl, title, style/css, id-format, base-dir, mode)
- Boolean metadata values accept true/false, yes/no, or 1/0 (case-insensitive, downcased)
- String metadata values used directly for options that take arguments
- Metadata mode option resets options to mode defaults before applying other metadata
- Comprehensive tests for metadata control of command line options

## [0.1.25] - 2025-12-13

### New

- Add citation processing with support for Pandoc, MultiMarkdown, and mmark syntaxes
- Add bibliography loading from BibTeX, CSL JSON, and CSL YAML formats
- Add --bibliography CLI option to specify bibliography files (can be used multiple times)
- Add --csl CLI option to specify citation style file
- Add --no-bibliography CLI option to suppress bibliography output
- Add --link-citations CLI option to link citations to bibliography entries
- Add --show-tooltips CLI option for citation tooltips
- Add bibliography generation and insertion at <!-- REFERENCES --> marker
- Add support for bibliography specified in document metadata
- Added missing docs and man page for citation support
- Add support for transclude base metadata to control file transclusion paths
- Add Base Header Level and HTML Header Level metadata to adjust heading levels
- Add CSS metadata to link external stylesheets in standalone HTML documents
- Add HTML Header and HTML Footer metadata to inject custom HTML
- Add Language metadata to set HTML lang attribute in standalone documents
- Add Quotes Language metadata to control smart quote styles (French, German, Spanish, etc.)
- Add --css CLI flag as alias for --style with metadata override precedence
- Add metadata key normalization: case-insensitive matching with spaces removed (e.g., "HTML Header Level" matches "htmlheaderlevel")
- Add index extension supporting mmark syntax (!item), (!item, subitem), and (!!item, subitem) for primary entries
- Add TextIndex syntax support with {^}, [term]{^}, and {^params} patterns
- Add automatic index generation at end of document or at <!--INDEX--> marker
- Add alphabetical sorting and optional grouping by first letter for index entries
- Add hierarchical sub-item support in generated index
- Add --indices CLI flag to enable index processing
- Add --no-indices CLI flag to disable index processing
- Add --no-index CLI flag to suppress index generation while keeping markers
- Add comprehensive test suite with 40 index tests covering both syntaxes

### Improved

- Only process citations when bibliography is actually provided for better performance
- Add comprehensive tests for MultiMarkdown metadata keys
- Add comprehensive performance profiling system (APEX_PROFILE=1) to measure processing time for all extensions and CLI operations
- Add early exit checks for IAL processing when no {: markers are present
- Add early exit checks for index processing when no index patterns are found
- Add early exit checks for citation processing when no citation patterns are found
- Add early exit checks for definition list processing when no : patterns are found
- Optimize alpha lists postprocessing with single-pass algorithm replacing O(n*m) strstr() loops
- Add early exit check for alpha lists postprocessing when no markers are present
- Optimize file I/O by using fwrite() with known length instead of fputs()
- Add markdown syntax detection in definition lists to skip parser creation for plain text
- Optimize definition lists by selectively extracting only needed reference definitions instead of prepending all
- Add profiling instrumentation for all preprocessing, parsing, rendering, and post-processing steps
- Add profiling instrumentation for CLI operations (file I/O, metadata processing)

### Fixed

- Prevent autolinking of @ symbols in citation syntax (e.g., [@key])
- Handle HTML comments in autolinker to preserve citation placeholders
- Fix quote language adjustment to handle Unicode curly quotes in addition to HTML entities
- Fix bibliography_files assignment to remove unnecessary cast
- Fix heap-buffer-overflow in html_renderer.c when writing null terminator (allocate capacity+1)
- Fix use-after-free in ial.c by deferring node unlinking until after iteration completes
- Fix buffer overflow in definition_list.c HTML entity escaping (correct length calculation for &amp; and &quot;)

## [0.1.24] - 2025-12-13

### New

- Add citation processing with support for Pandoc, MultiMarkdown, and mmark syntaxes
- Add bibliography loading from BibTeX, CSL JSON, and CSL YAML formats
- Add --bibliography CLI option to specify bibliography files (can be used multiple times)
- Add --csl CLI option to specify citation style file
- Add --no-bibliography CLI option to suppress bibliography output
- Add --link-citations CLI option to link citations to bibliography entries
- Add --show-tooltips CLI option for citation tooltips
- Add bibliography generation and insertion at <!-- REFERENCES --> marker
- Add support for bibliography specified in document metadata

### Improved

- Only process citations when bibliography is actually provided for better performance

### Fixed

- Raw HTML tags and comments are now preserved in definition lists by default in unified mode. Previously, HTML content in definition list definitions was being replaced with "raw HTML omitted" even when using --unsafe or in unified mode.
- Unified mode now explicitly sets unsafe=true by default to ensure raw HTML is allowed.
- Prevent autolinking of @ symbols in citation syntax (e.g., [@key])
- Handle HTML comments in autolinker to preserve citation placeholders

## [0.1.23] - 2025-12-12

### Changed

- Remove remote image embedding support (curl dependency removed)

### New

- Add metadata variable transforms with [%key:transform] syntax
- Add --transforms and --no-transforms flags to enable/disable transforms
- Add 19 text transforms: upper, lower, title, capitalize, trim, slug, replace (with regex support), substring, truncate, default, html_escape, basename, urlencode, urldecode, prefix, suffix, remove, repeat, reverse, format, length, pad, contains
- Add array transforms: split, join, first, last, slice
- Add date/time transform: strftime with date parsing
- Add transform chaining support (multiple transforms separated by colons)
- Add --meta-file flag to load metadata from external files (YAML, MMD, or Pandoc format, auto-detected)
- Add --meta KEY=VALUE flag to set metadata from command line (supports multiple flags and comma-separated pairs)
- Add metadata merging with proper precedence: command-line > document > file
- Add --embed-images flag to embed local images as base64 data URLs in HTML output
- Add --base-dir flag to set base directory for resolving relative paths (images, includes, wiki links)
- Add automatic base directory detection from input file directory when reading from file
- Add base64 encoding utility for image data
- Add MIME type detection from file extensions (supports jpg, png, gif, webp, svg, bmp, ico)
- Add image embedding function that processes HTML and replaces local image src attributes with data URLs
- Add test suite for image embedding functionality

### Improved

- Wiki link scanner now processes all links in a text node in a single pass instead of recursively processing one at a time, significantly improving performance for documents with multiple wiki links per text node.
- Added early-exit optimization to skip wiki link AST traversal entirely when no wiki link markers are present in the document.
- Improve error handling in transform execution to return original value instead of NULL on failure
- Add comprehensive test coverage for all transforms including edge cases
- Relative path resolution for images now uses base_directory option
- Base directory is automatically set from input file location when not specified

### Fixed

- Fix bracket handling in regex patterns - properly match closing brackets in [%...] syntax when patterns contain brackets
- Fix YAML metadata parsing to strip quotes from quoted string values
- Raw HTML tags and comments are now preserved in definition lists by default in unified mode. Previously, HTML content in definition list definitions was being replaced with "raw HTML omitted" even when using --unsafe or in unified mode.
- Unified mode now explicitly sets unsafe=true by default to ensure raw HTML is allowed.

## [0.1.20] - 2025-12-11

#### NEW

- Added man page generation and installation support. Man pages can be generated from Markdown source using pandoc or go-md2man, with pre-generated man pages included in the repository as fallback. CMake build system now handles man page installation, and Homebrew formula installs the man page.
- Added comprehensive test suite for MMD 6 features including multi-line setext headers and link/image titles with different quote styles (single quotes, double quotes, parentheses). Tests verify these features work in both MultiMarkdown and unified modes.
- Added build-test man_page_copy target for man page installation.
- Added --obfuscate-emails flag to hex-encode mailto links.

#### IMPROVED

- Superscript processing now stops at sentence terminators (. , ; : ! ?) instead of including them in the superscript content. This prevents punctuation from being incorrectly included in superscripts.
- Enhanced subscript and underline detection logic. The processor now correctly differentiates between subscript (tildes within a word, e.g., H~2~O) and underline (tildes at word boundaries, e.g., ~text~) by checking if tildes are within alphanumeric words or at word boundaries.
- Expanded test coverage for superscript, subscript, underline, strikethrough, and highlight features with additional edge case tests.
- Email autolink detection trims trailing punctuation.

#### FIXED

- Autolink now only wraps real URLs/emails instead of every word.
- Email autolinks now use mailto: hrefs instead of bare text.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.19] - 2025-12-09

#### CHANGED

- HTML comments now replaced with "raw HTML omitted" in CommonMark and GFM modes by default
- Added enable_sup_sub flag to apex_options struct
- Updated mode configurations to enable sup/sub in appropriate modes
- Added sup_sub.c to CMakeLists.txt build configuration
- Removed unused variables to resolve compiler warnings
- Tag filter (GFM security feature) now only applies in GFM mode, not Unified mode, allowing raw HTML and autolinks in Unified mode as intended.
- Autolink extension registration now respects the enable_autolink option flag.

#### NEW

- Added MultiMarkdown-style superscript (^text^) and subscript (~text~) syntax support
- Added --[no-]sup-sub command-line option to enable/disable superscript/subscript
- Superscript/subscript enabled by default in unified and MultiMarkdown modes
- Created sup_sub extension (sup_sub.c and sup_sub.h) for processing ^ and ~ syntax
- Added --[no-]unsafe command-line option to control raw HTML handling
- Added test_sup_sub() function with 13 tests covering superscript and
- Added test_mixed_lists() function with 10 tests covering mixed list
- Added test_unsafe_mode() function with 8 tests covering raw HTML
- Added preprocessing for angle-bracket autolinks (<http://...>) to convert them to explicit markdown links, ensuring they work correctly with custom rendering paths.
- Added --[no-]autolink CLI option to control automatic linking of URLs and email addresses. Autolinking is enabled by default in GFM, MultiMarkdown, Kramdown, and unified modes, and disabled in CommonMark mode.
- Added enable_autolink field to apex_options structure to control autolink behavior programmatically.
- Added underline syntax support: ~text~ now renders as <u>text</u> when there's a closing ~ with no space before it.

#### IMPROVED

- Test suite now includes 36 additional tests, increasing total test
- Autolink preprocessing now skips processing inside code spans (`...`) and code blocks (```...```), preventing URLs from being converted to links when they appear in code examples.
- Metadata replacement retains HTML edge-case handling and properly cleans up intermediate buffers.

#### FIXED

- Unified mode now correctly enables mixed list markers and alpha lists by default when no --mode is specified
- ^ marker now properly separates lists by creating a paragraph break instead of just blank lines
- Empty paragraphs created by ^ marker are now removed from final HTML output
- Superscript and subscript processing now skips ^ and ~ characters
- Superscript processing now skips ^ when part of footnote reference
- Subscript processing now skips ~ when part of critic markup patterns
- Setext headers are no longer broken when followed by highlight syntax (==text==). Highlight processing now stops at line breaks to prevent interference with header parsing.
- Metadata parser no longer incorrectly treats URLs and angle-bracket autolinks as metadata. Lines containing < or URLs (http://, https://, mailto:) are now skipped during metadata extraction.
- Superscript/subscript processor now correctly differentiates between ~text~ (underline), ~word (subscript), and ~~text~~ (strikethrough). Double-tilde sequences are skipped so strikethrough extension can handle them.
- Subscript processing now stops at sentence terminators (. , ; : ! ?) instead of including them in the subscript content.
- Metadata variable replacement now runs before autolinking so [%key] values containing URLs are turned into links when autolinking is enabled.
- MMD metadata parsing no longer incorrectly rejects entries with URL values; only URL-like keys or '<' characters in keys are rejected, allowing "URL: https://example.com" as valid metadata.
- Headers starting with `#` are now correctly recognized instead of being treated as autolinks. The autolink preprocessor now skips `#` at the start of a line when followed by whitespace.
- Math processor now validates that `\(...\)` sequences contain actual math content (letters, numbers, or operators) before processing them. This prevents false positives like `\(%\)` from being treated as math when they only contain special characters.



## [0.1.18] - 2025-12-06

### Fixed
- GitHub Actions workflow now properly builds separate Linux x86_64 and ARM64 binaries

## [0.1.17] - 2025-12-06

### Fixed
- Relaxed tables now disabled by default for CommonMark, GFM, and MultiMarkdown modes (only enabled for Kramdown and Unified modes)
- Header ID extraction no longer incorrectly parses metadata variables like `[%title]` as MMD-style header IDs
- Tables with alignment/separator rows now correctly generate `<thead>` even when relaxed table mode is enabled
- Relaxed tables preprocessor preserves input newline behavior in output
- Memory management bug in IAL preprocessing removed unnecessary free call

## [0.1.16] - 2025-12-06

### Fixed
- IAL (Inline Attribute List) markers appearing immediately after content without a blank line are now correctly parsed
- Added `apex_preprocess_ial()` function to ensure Kramdown-style IAL syntax works correctly with cmark-gfm parser

## [0.1.15] - 2025-12-06

### Fixed
- Homebrew formula updated with correct version and commit hash

## [0.1.10] - 2025-12-06

### Changed
- License changed to MIT

### Added
- Homebrew formula update scripts

## [0.1.9] - 2025-12-06

### Fixed
- Shell syntax in Linux checksum step for GitHub Actions

## [0.1.8] - 2025-12-06

### Fixed
- Link order for Linux static builds

## [0.1.7] - 2025-12-06

### Fixed
- Added write permissions for GitHub releases

## [0.1.6] - 2025-12-06

### Fixed
- `.gitignore` pattern fixed to properly include apex headers (was incorrectly matching `include/apex/`)

## [0.1.5] - 2025-12-06

### Changed
- Added verbose build output for CI debugging

## [0.1.4] - 2025-12-06

### Fixed
- CMake build rules updated

## [0.1.3] - 2025-12-06

### Fixed
- CMake policy version for cmark-gfm compatibility

## [0.1.2] - 2025-12-06

### Fixed
- GitHub Actions workflow fixes

## [0.1.1] - 2025-12-04

### Added
- CMake setup documentation

## [0.1.0] - 2025-12-04

### Added

**Core Features:**
- Initial release of Apex unified Markdown processor
- Based on cmark-gfm for CommonMark + GFM support
- Support for 5 processor modes: CommonMark, GFM, MultiMarkdown, Kramdown, Unified

**Metadata:**
- YAML front matter parsing
- MultiMarkdown metadata format
- Pandoc title block format
- Metadata variable replacement with `[%key]` syntax

**Extended Syntax:**
- Wiki-style links: `[[Page]]`, `[[Page|Display]]`, `[[Page#Section]]`
- Math support: `$inline$` and `$$display$$` with LaTeX
- Critic Markup: All 5 types ({++add++}, {--del--}, {~~sub~~}, {==mark==}, {>>comment<<})
- GFM tables, strikethrough, task lists, autolinks
- Reference-style footnotes
- Smart typography (smart quotes, dashes, ellipsis)

**Build System:**
- CMake build system for cross-platform support
- Builds shared library, static library, CLI binary, and macOS framework
- Clean compilation on macOS with Apple Clang

**CLI Tool:**
- `apex` command-line binary
- Support for all processor modes via `--mode` flag
- Stdin/stdout support for Unix pipes
- Comprehensive help and version information

**Integration:**
- Objective-C wrapper (`NSString+Apex`) for Marked integration
- macOS framework with proper exports
- Detailed integration documentation and examples

**Testing:**
- Automated test suite with 31 tests
- 90% pass rate across all feature areas
- Manual testing validated

**Documentation:**
- Comprehensive user guide
- Complete API reference
- Architecture documentation
- Integration guides
- Code examples

### Known Issues

- Critic Markup substitutions have edge cases with certain inputs
- Definition lists not yet implemented
- Kramdown attributes not yet implemented
- Inline footnotes not yet implemented

### Performance

- Small documents (< 10KB): < 10ms
- Medium documents (< 100KB): < 100ms
- Large documents (< 1MB): < 1s

### Credits

- Based on [cmark-gfm](https://github.com/github/cmark-gfm) by GitHub
- Developed for [Marked](https://marked2app.com) by Brett Terpstra

[0.1.28]: https://github.com/ttscoff/apex/releases/tag/v0.1.28
[0.1.27]: https://github.com/ttscoff/apex/releases/tag/v0.1.27
[0.1.26]: https://github.com/ttscoff/apex/releases/tag/v0.1.26
[0.1.25]: https://github.com/ttscoff/apex/releases/tag/v0.1.25
[0.1.24]: https://github.com/ttscoff/apex/releases/tag/v0.1.24
[0.1.23]: https://github.com/ttscoff/apex/releases/tag/v0.1.23
[0.1.20]: https://github.com/ttscoff/apex/releases/tag/v0.1.20
[0.1.19]: https://github.com/ttscoff/apex/releases/tag/v0.1.19
[0.1.18]: https://github.com/ttscoff/apex/releases/tag/v0.1.18
[0.1.17]: https://github.com/ttscoff/apex/releases/tag/v0.1.17
[0.1.16]: https://github.com/ttscoff/apex/releases/tag/v0.1.16
[0.1.15]: https://github.com/ttscoff/apex/releases/tag/v0.1.15
[0.1.10]: https://github.com/ttscoff/apex/releases/tag/v0.1.10
[0.1.9]: https://github.com/ttscoff/apex/releases/tag/v0.1.9
[0.1.8]: https://github.com/ttscoff/apex/releases/tag/v0.1.8
[0.1.7]: https://github.com/ttscoff/apex/releases/tag/v0.1.7
[0.1.6]: https://github.com/ttscoff/apex/releases/tag/v0.1.6
[0.1.5]: https://github.com/ttscoff/apex/releases/tag/v0.1.5
[0.1.4]: https://github.com/ttscoff/apex/releases/tag/v0.1.4
[0.1.3]: https://github.com/ttscoff/apex/releases/tag/v0.1.3
[0.1.2]: https://github.com/ttscoff/apex/releases/tag/v0.1.2
[0.1.1]: https://github.com/ttscoff/apex/releases/tag/v0.1.1
[0.1.0]: https://github.com/ttscoff/apex/releases/tag/v0.1.0

