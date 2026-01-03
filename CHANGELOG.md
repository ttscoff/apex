# Changelog

All notable changes to Apex will be documented in this file.

## [0.1.47] - 2026-01-03

### Changed

- Updated Objective-C API to use mode constants instead of string literals in default implementation

### New

- Added emoji autocorrect option to enable automatic conversion of emoji names like :rocket: to Unicode emoji characters
- Added progress indicator option to show processing progress on stderr for operations longer than 1 second

### Improved

- Added mode constants (ApexModeCommonmark, ApexModeGFM, etc.) to Objective-C API for better type safety and code clarity

## [0.1.46] - 2026-01-03

## [0.1.45] - 2026-01-03

### Improved

- Table caption preprocessing now handles blank lines between tables and captions by buffering and discarding them appropriately
- Table matching in HTML renderer now uses sequential matching instead of index-based matching to correctly handle tables with attributes
- Caption detection now concatenates all text nodes in a paragraph to ensure IAL attributes are found even when split across nodes

### Fixed

- Table captions now work correctly when separated from tables by blank lines
- IAL attributes (id, class) are now correctly extracted and applied to tables with captions
- Table: caption format now works when appearing immediately after table rows without blank lines
- Table: caption format now supports case-insensitive "table:" in addition to "Table:"

## [0.1.44] - 2026-01-02

### Changed

- Updated emoji_entry structure to support both unicode and image-based emojis with separate unicode and image_url fields
- Table alignment test now accepts both align="center" and style="text-align: center" attributes to be more flexible with cmark-gfm's output format

### New

- Fenced divs now support specifying different HTML block elements using >blocktype syntax (e.g., ::: >aside {.sidebar} creates <aside> instead of <div>)
- Support for common HTML5 block elements: aside, article, section, details, summary, header, footer, nav, and custom elements
- Block type syntax works with all attribute types including IDs, classes, and custom attributes
- Added comprehensive test coverage for block type feature including nesting, multiple attributes, and edge cases
- In GFM format, emojis in headers are converted to their textual names in generated IDs (e.g., #  Support -> id="smile-support"), matching Pandoc's GFM behavior
- Added support for all 861 GitHub emojis (expanded from ~200), including 14 image-based emojis like :bowtie:, :octocat:, and :feelsgood:
- Added emoji name autocorrect feature using fuzzy matching with Levenshtein distance algorithm to correct typos and formatting errors in emoji names
- Added --emoji-autocorrect and --no-emoji-autocorrect command-line flags to control emoji autocorrection
- Emoji autocorrect is enabled by default in unified mode and can be enabled in GFM mode
- Image-based emojis in headers now use em units (height: 1em) for proper scaling instead of fixed pixel sizes

### Improved

- Fenced div block types can be nested and mixed with regular divs
- Emoji processing now validates that only complete :emoji_name: patterns are processed (requires at least one character and no spaces between colons)
- Emoji names are normalized (lowercase, hyphens to underscores) before matching to handle case variations and formatting differences
- Table processing now completes in under 30ms for large tables (previously timing out after 5+ seconds) by avoiding expensive string comparisons when no changes are made
- Added early exit in table attribute injection to skip processing for simple tables without special attributes, avoiding expensive AST traversal for tables that don't need it
- Table post-processing performance significantly improved for large tables (2600+ cells) by implementing lazy cell content extraction, only extracting content when needed for attribute matching or alignment processing
- Added timeout protection (10 seconds) to table post-processing loop to prevent hangs on extremely large tables, gracefully exiting and returning processed HTML
- Per-cell alignment processing now disabled automatically for tables with more than 1000 cells to avoid timeouts, while column alignment from delimiter rows continues to work (handled by cmark-gfm)
- Optimized alignment colon detection to only check first and last non-whitespace characters instead of scanning entire cell content, reducing character comparisons by ~50x
- Added early exit for tables with only simple attributes (no rowspan/colspan/data-remove) to skip expensive HTML processing when no complex features are needed
- Content-based cell matching now skipped for tables with more than 500 attributes to avoid performance degradation
- Added CMARK_OPT_LIBERAL_HTML_TAG option when unsafe mode is enabled to allow cmark-gfm to properly recognize inline HTML tags instead of encoding them
- MacOS binaries now use @rpath for libyaml instead of hardcoded Homebrew paths, allowing the binary to work when copied to /usr/local/bin as long as libyaml is installed in /usr/local/lib or /opt/homebrew/lib
- Emoji name resolution now prefers longer, more descriptive names (e.g., "thumbsup" over "+1")
- Header ID generation now normalizes common Latin diacritics (e, a, c, etc.)
- Table rowspan matching now extracts cell content for more accurate matching

### Fixed

- Fixed issue where partial emoji patterns or empty patterns could cause incorrect matches
- Emoji replacement now correctly ignores table alignment patterns like :---: and :|: to prevent incorrect emoji processing in table delimiter rows
- Emoji patterns (like :bowtie:) inside HTML tag attributes are now correctly ignored and not processed, preventing mangled HTML output when emojis appear in attributes like title=":emoji:"
- Autolink processing now skips URLs inside HTML tag attributes, preventing URLs in attributes like src="https://..." from being converted to markdown links
- Inline HTML tags like <img> are no longer HTML-encoded when --unsafe is enabled, preserving raw HTML in paragraphs, blockquotes, and definition lists
- Header IDs with emojis now correctly replace cmark-gfm's auto-generated IDs instead of being skipped, ensuring custom ID formats (like emoji-to-name conversion) are applied
- Emoji processing now correctly skips index placeholders (<!--IDX:...-->)
- Table processing now correctly detects and processes rowspan markers (^^)
- Table attribute processing optimization no longer incorrectly skips rowspan/colspan attributes
- Table formatting in fixture
- Remove unused function

## [0.1.43] - 2025-12-31

### Changed

- Reference image attribute expansion now converts IAL

  attributes (ID, classes) to key=value format for
  compatibility with inline image parsing

- Test suite refactored: split test_runner.c into multiple

  files (test_basic.c, test_extensions.c, test_helpers.c,
  test_ial.c, test_links.c, test_metadata.c, test_output.c,
  test_tables.c) for better organization

- Test fixtures reorganized: moved all .md test files from

  tests/ to tests/fixtures/ with subdirectories (basic/,
  demos/, extensions/, ial/, images/, output/, tables/)

### New

Support for Pandoc-style table captions using `: Caption` syntax (in addition to existing `[Caption]` and `Table: Caption` formats)

IAL attributes in table captions are now extracted and applied to the table element (e.g., `: Caption {#id .class}` applies `id` and `class` to the table)

Support for Pandoc-style IAL syntax without colon prefix (`{#id .class}` in addition to Kramdown `{:#id .class}` format)

- Support Pandoc-style IAL syntax ({#id .class}) in addition

  to Kramdown-style ({: #id .class}) for all IAL contexts
  including block-level, inline, and paragraph IALs

- Extract_ial_from_text function now recognizes both {: and

  {# or {. formats when extracting IALs from text

- Extract_ial_from_paragraph function now accepts

  Pandoc-style IALs for pure IAL paragraphs

- Process_span_ial_in_container function now processes

  Pandoc-style IALs for inline elements like links, images,
  and emphasis

- Is_ial_line function now detects Pandoc-style IAL-only

  lines in addition to Kramdown format

- Support Pandoc-style IAL syntax ({#id .class}) in addition

  to Kramdown-style ({: #id .class}) for all IAL contexts
  including block-level elements, paragraphs, inline
  elements, and headings

- Add support for Pandoc fenced divs syntax (::::: {#id

  .class} ... :::::) in unified mode, enabled by default

- Add --divs and --no-divs command-line flags to control

  fenced divs processing

- Add comprehensive test suite for Pandoc fenced divs

  covering basic divs, nested divs, attributes, and edge
  cases

- Add bracketed spans feature that converts [text]{IAL}

  syntax to HTML span elements with attributes, enabled by
  default in unified mode

- Add --spans and --no-spans command-line flags to

  enable/disable bracketed spans in other modes

- Bracketed spans support all IAL attribute types (IDs,

  classes, key-value pairs) and process markdown inside
  spans

- Reference link definitions take precedence over bracketed

  spans - if [text] matches a reference link, it remains a
  link

- Add comprehensive test suite for bracketed spans including

  reference link precedence, nested brackets, and markdown
  processing

- Add bracketed spans examples and documentation
- Add automatic width/height attribute conversion:

  percentages and non-integer/non-px values convert to style
  attributes, Xpx values convert to integer width/height
  attributes (strips px suffix), bare integers remain as
  width/height attributes

- Add support for Pandoc/Kramdown IAL syntax on images:

  inline images support IAL after closing paren like
  ![alt](url){#id .class width=50%}

- Add support for IAL syntax after titles in reference image

  definitions: [ref]: url "title" {#id .class width=50%}

- Add support for Pandoc-style IAL with space prefix: {

  width=50% } syntax works for images

- Add comprehensive test suite for width/height conversion

  covering percentages, pixels, integers, mixed cases, and
  edge cases like decimals and viewport units

- Reference image definitions now preserve and include title

  attributes from definitions like [ref]: url "title" {#id}

### Improved

- Table caption preprocessing now converts `: Caption`

  format to `[Caption]` format before definition list
  processing to avoid conflicts

- HTML renderer now extracts and injects IAL attributes from

  table captions while excluding internal attributes like
  `data-caption`

- IAL parsing automatically detects format and adjusts

  content offset accordingly (2 chars for {: format, 1 char
  for {# or {. format)

- HTML markdown extension now uses CMARK_OPT_UNSAFE to allow

  raw HTML including nested divs in processed content

- Width/height conversion properly merges with existing

  style attributes when both are present

- IAL detection now properly handles whitespace before IAL

  syntax for both inline and reference images

- Reference image expansion now correctly skips IAL even

  when closing paren is not found

### Fixed

- Table caption test assertions now correctly match table

  tags with attributes by using <table instead of <table>

- Extract_ial_from_paragraph now allows newline character

  after closing brace in IAL syntax

- List items with key:value format (e.g., "- Foo: Bar") are

  no longer incorrectly parsed as MMD metadata in unified
  mode

- Fenced divs now add markdown="1" attribute so content

  inside divs is properly parsed as markdown

- HTML markdown extension now preserves all attributes (id,

  class, custom attributes) when processing divs with
  markdown="1"

- HTML markdown extension now recursively processes nested

  divs with markdown="1" attributes

- HTML markdown extension now adds newline after closing div

  tags to ensure following markdown headers and content are
  parsed correctly

- Bracketed spans now correctly handle nested brackets by

  matching outer brackets instead of first closing bracket

- Remove test assertions checking for markdown attribute on

  bracketed spans which is correctly removed by
  html_markdown extension

- IAL syntax with spaces (e.g., { width=50% }) now correctly

  detected and processed for images

- IAL syntax is now properly stripped from output even when

  parsing fails, preventing raw IAL from appearing in HTML

- Reference image definitions with IAL after URL (no title)

  now correctly detected and processed

- Test string length issues: replaced hardcoded lengths with

  strlen() calls to ensure full IAL syntax is processed in
  width/height conversion tests

- Removed unused style_attr_index variable to eliminate

  compiler warning

## [0.1.42] - 2025-12-30

### New

- ALD references can now be combined with additional

  attributes in the same IAL (e.g., {:id .class3} where id
  is an ALD reference and .class3 is an additional class)

### Improved

- When merging ALD attributes with additional attributes,

  duplicate key-value pairs are now replaced instead of
  duplicated (e.g., if ALD defines rel="x" and IAL includes
  rel="y", the result is rel="y")

- Classes from additional attributes are appended to ALD

  classes, and IDs in IALs override ALD IDs when specified

- Enhanced merge_attributes function to properly handle

  attribute key conflicts by replacing existing values
  rather than creating duplicates

## [0.1.41] - 2025-12-30

### New

- Inline Attribute Lists (IALs) can now appear immediately

  after inline elements within paragraphs, not just at the
  end of paragraphs

- IALs can be applied to strong (bold), emphasis (italic),

  and code elements in addition to links and images

- IALs now work with nested inline elements, allowing

  attributes to be applied to italic text inside bold text
  and similar nested structures

- Added comprehensive test suite for inline IAL

  functionality covering links, strong, emphasis, code,
  multiple IALs, and edge cases

- Added IAL demo markdown file (tests/ial_demo.md)

  demonstrating all supported IAL features

- Added script (tests/generate_ial_demo.sh) to automatically

  generate an HTML file with interactive attribute
  inspection tooltips

### Fixed

- IALs (Inline Attribute Lists) are now correctly applied to

  the intended link element when multiple links in a
  document share the same URL

- IALs are now correctly applied to the intended element

  when multiple elements share the same URL or content,
  using separate element counters for each inline element
  type

- Block-level HTML elements now correctly include a space

  between the tag name and attributes when IAL attributes
  are injected

## [0.1.40] - 2025-12-23

### Changed

- Table captions now default to below the table instead of

  above

- Tfoot row detection in AST now separates the logic for

  marking the === row itself versus rows that come after it,
  improving accuracy of tfoot section identification.

- Disallow using --combine and --mmd-merge together to avoid

  ambiguous multi-file behavior

- Update CSV include and inline table handling so both share

  the same CSV-to-table conversion and alignment behavior

### New

- Added --captions option to control caption position (above

  or below)

- Added default CSS styling for figcaption elements in

  standalone output (centered, bold, 0.8em)

- Added CSS styling for table figures to align captions with

  tables (fit-content width)

- Support for tables that start with alignment rows

  (separator rows) without header rows. Column alignment
  specified in the separator row is automatically applied to
  all data columns.

- Support for individual cell alignment in tables using

  colons, similar to Jekyll Spaceship. Cells can be aligned
  independently with :Text (left), Text: (right), or :Text:
  (center). Colons are removed from output and alignment is
  applied via CSS text-align styles.

- Support per-cell alignment markers inside table cells
- Support multiline table cells using trailing backslash

  markers

- Support header and footer colspans based on empty cells
- Add --combine CLI mode to concatenate Markdown files with

  include expansion and GitBook-style SUMMARY.md index
  support.

- Add --mmd-merge CLI mode to merge MultiMarkdown index

  files into a single Markdown stream

- Support indentation-based header level shifting when

  merging mmd_merge index entries

- Support inline CSV/TSV tables using ```table fenced blocks

  with automatic CSV/TSV delimiter detection

- Support <!--TABLE--> markers that convert following

  CSV/TSV lines into Markdown tables until a blank line

- Add --aria command-line option to enable ARIA labels and

  accessibility attributes in HTML output

- Add aria-label="Table of contents" to TOC navigation

  elements when --aria is enabled

Add role="figure" to figure elements when --aria is enabled

- Add role="table" to table elements when --aria is enabled
- Generate id attributes for figcaption elements in table

  figures when --aria is enabled

- Add aria-describedby attributes linking tables to their

  captions when --aria is enabled

### Improved

- Removed unused variables to eliminate compiler warnings
- Empty thead sections from headerless tables are now

  removed from HTML output instead of rendering empty header
  cells.

- Table row mapping now better handles the relationship

  between HTML row indices and AST row indices, accounting
  for separator rows that are removed from HTML output.

- Added safeguards to prevent rows that should be in tbody

  from being skipped, including protection for the first few
  rows (header and first two data rows) when a === separator
  is present.

- Make table captions positionable above or below tables
- Center and style figcaptions in standalone HTML output
- Support optional alignment keyword rows (left, right,

  center, auto) and headless tables for both included CSV
  files and inline CSV/TSV data

- Preserve ```table fences without commas or tabs by leaving

  them as code blocks so users can show literal CSV/TSV
  without conversion

### Fixed

- Removed unused variable 'row_idx' from advanced_tables.c

  to eliminate compiler warnings.

- Rows before the === separator are now correctly placed in

  tbody instead of being incorrectly placed in tfoot or
  skipped entirely. The fix includes HTML position
  verification to ensure rows that appear before the === row
  in the rendered HTML are always in tbody, regardless of
  AST marking.

- Prevent === separator rows from appearing as table content

Ensure footer rows render in tfoot without losing body rows

- Preserve legitimate empty cells such as missing Q4 values
- Apply ^^ rowspans correctly for all table sections
- Apply ^^ rowspans correctly across table sections without

  leaking into unrelated rows

- Support footer colspans so footer cells can span multiple

  columns like headers and body rows

- Preserve legitimate empty table cells that are not part of

  colspans or rowspans

- KaTeX auto-render now properly configures delimiters and

  manually renders math spans to prevent plain text from
  appearing after rendered equations

- Relaxed table header conversion now only runs when

  relaxed_tables option is enabled

- HTML document wrapping now strips existing </body></html>

  tags to prevent duplicates when content already contains
  them

- KaTeX auto-render now properly configures delimiters and

  manually renders math spans to prevent plain text from
  appearing after rendered equations

- Relaxed table header conversion now only runs when

  relaxed_tables option is enabled

- HTML document wrapping now strips existing </body></html>

  tags to prevent duplicates when content already contains
  them

- Table captions appearing after their tables now correctly

  link via aria-describedby attributes

## [0.1.39] - 2025-12-19

### Changed

- Table post-processing now tracks all cells (including

  removed ones) to correctly map HTML column positions to
  original AST column indices, ensuring attributes are
  applied to the correct cells.

- Added content-based detection for ^^ marker cells during

  HTML post-processing to ensure they are properly removed
  even when attribute matching fails.

### New

- Added content verification to prevent false matches when

  cells are covered by rowspans

- Added tracking of previous cell's colspan to detect and

  remove empty cells after colspan

- Added detection and removal of === marker rows in tfoot

  sections

### Improved

- Rowspan cell tracking now uses a per-column active cell

  approach (inspired by Jekyll Spaceship) for more reliable
  rowspan calculation across complex table structures.

- Cell matching now uses position-based fallback to previous

  row when current row match fails

- Row mapping accounts for rowspan coverage to correctly

  identify visible columns

- Tfoot row detection now uses AST row indices instead of

  HTML row indices for accuracy

### Fixed

- Table rowspan rendering now correctly handles rows where

  most cells use ^^ markers. Previously, cells like "Beta"
  and "Gamma" in rows with multiple ^^ cells would be
  missing or appear in the wrong position. The fix includes
  proper mapping between HTML row indices and AST row
  indices, accounting for separator rows that are removed
  from HTML output.

- Missing cells after colspan (e.g., "92.00" cell was

  missing when "Absent" had colspan="2")

- Rowspan not applying correctly when HTML row mapping was

  off by one

- Footer alignment rows (=== markers) appearing in output

  instead of being removed

- Empty cells after colspan not being removed from rendered

  HTML

## [0.1.38] - 2025-12-18

### Changed

- In standalone mode, insert script tags just before </body>
- In snippet mode, append script tags at the end of the HTML

  fragment

- When --embed-css is used with --css, replace the

  stylesheet <link> tag with an inline <style> block
  containing the CSS file contents

### New

- Support Pandoc-style "Table: Caption" syntax and
- Add --script CLI flag to inject scripts into HTML output
- Support shorthands for common JS libraries (mermaid,

  mathjax, katex, highlightjs, prism, htmx, alpine)

- Add --embed-css option to inline CSS files into the

  standalone document head

### Improved

- Compress extraneous newlines between HTML elements
- Remove unused apex_remote_trim helper to eliminate

  compiler warnings

### Fixed

- Prevent caption paragraphs from being reused across
- Skip URL encoding for footnote definitions ([^id]: ...) so

  footnote

## [0.1.37] - 2025-12-17

### Changed

- Image attributes are mode-dependent: work in Unified and

  MultiMarkdown modes only

- URL encoding is mode-dependent: works in Unified,

  MultiMarkdown, and Kramdown modes

- Improved caption detection to check all table rows for

  caption markers, not just the last row, to handle cases
  where captions come after tfoot rows.

### New

- Support for MultiMarkdown-style image attributes in

  unified and MultiMarkdown modes

Inline image attributes: ![alt](url width=300 style="float:left" "title")

- Reference-style image attributes: ![][ref] with [ref]: url

  width=300

- Automatic URL encoding for links with spaces in unified,

  MultiMarkdown, and Kramdown modes

- URLs with spaces are automatically percent-encoded (e.g.,

  "path with spaces.png" becomes "path%20with%20spaces.png")

- Added support for MultiMarkdown-style image attributes in

  reference-style images. Reference definitions can now
  include attributes: [img1]: image.png width=300
  style="float:left"

Added support for inline image attributes: ![alt](url width=300 style="...")

- Added automatic URL encoding for all link URLs (images and

  regular links). URLs with spaces are automatically
  percent-encoded (e.g., "path to/image.png" becomes
  "path%20to/image.png")

- Added detection and removal of table alignment separator

  rows that were incorrectly being rendered as table rows.

- Added test cases for table captions appearing before and

  after tables.

Added support for tfoot sections in tables using `===` row markers. Rows containing `===` markers are now placed in `<tfoot>` sections, and all subsequent rows after the first `===` row are also placed in tfoot.

- Added comprehensive table feature tests that validate

  rowspan,

### Improved

- Improved attribute injection in HTML renderer to correctly

  place attributes before closing > or /> in img and link
  tags

- Enhanced URL parsing to distinguish between spaces within

  URLs vs spaces before attributes using forward-scanning
  pattern detection

- Self-closing img tags now consistently use " />" (space

  before slash) when attributes are injected, matching the
  format used by cmark-gfm for img tags without injected
  attributes

- Rowspan and colspan attribute handling now properly

  appends to existing attributes instead of replacing them,
  allowing multiple attributes to coexist on table cells.

- Alignment rows (rows containing only '' characters) are

  now detected and marked for removal, preventing them from
  appearing in HTML output.

### Fixed

- Fixed bug where image prefix "![" was incorrectly removed

  during preprocessing of expanded reference-style images

- URL encoding now only encodes unsafe characters (space,

  control chars, non-ASCII). Valid URL characters like /, :,
  ?, #, ~, etc. are preserved and no longer incorrectly
  encoded.

- Titles in links and images are now correctly detected and

  excluded from URL encoding. Supports quoted titles
  ("title", 'title') and parentheses titles ((title)). URLs
  with parentheses (like Wikipedia links) are correctly
  distinguished from titles based on whether a space
  precedes the opening parenthesis.

- Reference-style images with attributes now render

  correctly. Reference definitions with image attributes are
  removed from output, while those without attributes are
  preserved (with URL encoding) so cmark can resolve the
  references.

- Spacing between attributes in HTML output. Attributes

  injected into img and link tags now have proper spacing,
  preventing malformed HTML like alt="text"width="100".

- Table attributes now render correctly with proper spacing.

  Fixed missing space in table tag when id attribute
  immediately follows (e.g., <tableid="..." now renders as
  <table id="...").

- Rowspan and colspan injection now works correctly in all

  cases. Fixed bug where table tracking variables weren't
  set when fixing missing space in table tag (e.g.,
  <tableid="..."), causing row and cell processing to be
  skipped. Table tracking is now properly initialized even
  when correcting tag spacing.

- Captions after tables were not being detected when tables

  had IAL attributes, as IAL processing replaced the caption
  data stored in user_data. Added fallback logic to check
  for caption paragraphs directly in the AST when user_data
  lookup fails.

- Rows containing only `===` markers are now properly

  skipped entirely rather than rendering as empty cells in
  tfoot sections.

- Caption paragraphs before tables are now properly removed,

## [0.1.36] - 2025-12-16

### Fixed

- Resolve CMake error when building framework where

  file(GLOB) returns multiple dylib files, causing
  semicolon-concatenated paths in file(COPY) command. Now
  extracts first file from glob result before copying.

- Homebrew installation now correctly links to system

  libyaml instead of hardcoded CI path

## [0.1.35] - 2025-12-16

### Changed

- Update Homebrew formula to install from a precompiled

  macOS universal binary instead of building from source
  with cmake.

- Allow --install-plugin to accept a Git URL or GitHub

  shorthand (user/repo) in addition to directory IDs when
  installing plugins.

### Improved

- Simplify Homebrew installation so users no longer need

  cmake or Xcode build tools to install apex.

- Add an interactive security confirmation when installing

  plugins from a direct Git URL or GitHub repo name,
  reminding users that plugins execute unverified code.

## [0.1.34] - 2025-12-16

## [0.1.33] - 2025-12-16

## [0.1.32] - 2025-12-16

## [0.1.31] - 2025-12-16

## [0.1.30] - 2025-12-16

### Changed

- Make --list-plugins show installed plugins before remote

  ones.

- Prevent remote plugins that are already installed from

  being listed under Available Plugins.

- Build system now detects libyaml via multiple methods

  (yaml-0.1, yaml, libyaml) for better cross-platform
  support.

- Homebrew formula now includes libyaml as a dependency to

  ensure full YAML support.

- Suppressed unused-parameter warnings from vendored

  cmark-gfm extensions to reduce build noise.

### New

Add --uninstall-plugin CLI flag to remove installed plugins.

- Run optional post_install command from plugin.yml after

  cloning a plugin.

- Full YAML parsing support using libyaml for arrays and

  nested structures in metadata and plugin manifests.

- Plugin bundle support allowing multiple plugins to be

  defined in a single plugin.yml manifest.

- Expose APEX_FILE_PATH to external plugins so scripts can

  see the original input path or base directory when
  processing.

### Improved

- Split() metadata transform now accepts regular expressions

  as delimiters (for example split(,\s*)).

- YAML arrays are automatically normalized to

  comma-separated strings for backward compatibility with
  existing metadata transforms.

- External plugin environment now includes the source file

  path (when available) alongside APEX_PLUGIN_DIR and
  APEX_SUPPORT_DIR.

### Fixed

- Tighten mutual-exclusion checks between install and

  uninstall plugin flags.

- Ensure CMake policy version is compatible with vendored

  cmark-gfm on newer CMake releases.

- Install the Apex framework with its public apex.h header

  correctly embedded in Apex.framework/Headers for Xcode
  use.

- Bundle libcmark-gfm and libcmark-gfm-extensions dylibs

  into Apex.framework so dependent apps no longer hit
  missing library errors at runtime.

## [0.1.29] - 2025-12-15

### Changed

- Make --list-plugins show installed plugins before remote

  ones.

- Prevent remote plugins that are already installed from

  being listed under Available Plugins.

### New

- Initial planning for a remote plugin directory and install

  features

Add --uninstall-plugin CLI flag to remove installed plugins.

### Fixed

- Superscript/subscript no longer process content inside

  Liquid {% %} tags.

- Autolink detection skips Liquid {% %} tags so emails and

  URLs are not rewritten there.

- Fix directory url for `--list-plugins`

## [0.1.28] - 2025-12-15

### Changed

- Default wikilink URLs now replace spaces with dashes (e.g.

  [[Home Page]] -> href="Home-Page").

### New

- Add --wikilink-space and --wikilink-extension flags to

  control how [[WikiLink]] hrefs are generated.

- Allow wikilink space and extension configuration via

  metadata keys wikilink-space and wikilink-extension.

- Support Kramdown-style {:toc ...} markers mapped to Apex

  TOC generation.

- Add tests for `{:toc}` syntaxes
- MMD includes support full glob patterns like {{*.md}} and

  {{c?de.py}}.

- Add plugin discovery from .apex/plugins and

  ~/.config/apex/plugins.

- Allow external handler plugins in any language via JSON

  stdin/stdout.

- Support declarative regex plugins for pre_parse and

  post_render phases.

- Add `--no-plugins` CLI flag to disable all plugins for a

  run.

- Support `plugins: true/false` metadata to enable or

  disable plugins.

- Initial planning for a remote plugin directory and install

  features

### Improved

- Exclude headings with .no_toc class from generated tables

  of contents for finer-grained TOC control.

- MMD-style {{file.*}} now resolves preferred extensions

  before globbing.

- Transclusion respects brace-style patterns such as

  {{{intro,part1}.md}} where supported.

- Provide `APEX_PLUGIN_DIR` and `APEX_SUPPORT_DIR` for

  plugin code and data.

- Add profiling (APEX_PROFILE_PLUGINS=1) for plugins

## [0.1.27] - 2025-12-15

### Changed

- Default wikilink URLs now replace spaces with dashes (e.g.

  [[Home Page]] -> href="Home-Page").

### New

- Add --wikilink-space and --wikilink-extension flags to

  control how [[WikiLink]] hrefs are generated.

- Allow wikilink space and extension configuration via

  metadata keys wikilink-space and wikilink-extension.

- Support Kramdown-style {:toc ...} markers mapped to Apex

  TOC generation.

- Add tests for `{:toc}` syntaxes
- MMD includes support full glob patterns like {{*.md}} and

  {{c?de.py}}.

- Add plugin discovery from .apex/plugins and

  ~/.config/apex/plugins.

- Allow external handler plugins in any language via JSON

  stdin/stdout.

- Support declarative regex plugins for pre_parse and

  post_render phases.

- Add `--no-plugins` CLI flag to disable all plugins for a

  run.

- Support `plugins: true/false` metadata to enable or

  disable plugins.

- Initial planning for a remote plugin directory and install

  features

### Improved

- Exclude headings with .no_toc class from generated tables

  of contents for finer-grained TOC control.

- MMD-style {{file.*}} now resolves preferred extensions

  before globbing.

- Transclusion respects brace-style patterns such as

  {{{intro,part1}.md}} where supported.

- Provide `APEX_PLUGIN_DIR` and `APEX_SUPPORT_DIR` for

  plugin code and data.

- Add profiling (APEX_PROFILE_PLUGINS=1) for plugins

## [0.1.26] - 2025-12-14

### Changed

- Change `--enable-includes` to `--[no-]includes`, allowing

  `--no-includes` to disable includes in unified mode and
  shortening the flag

- Integrate metadata-to-options application into CLI after

  metadata merging

- Preserve bibliography files array when metadata mode

  resets options structure

### New

- Add apex_apply_metadata_to_options() function to apply

  metadata values to apex_options structure

- Support controlling boolean flags via metadata (indices,

  wikilinks, includes, relaxed-tables, alpha-lists,
  mixed-lists, sup-sub, autolink, transforms, unsafe,
  tables, footnotes, smart, math, ids, header-anchors,
  embed-images, link-citations, show-tooltips,
  suppress-bibliography, suppress-index,
  group-index-by-letter, obfuscate-emails, pretty,
  standalone, hardbreaks)

- Support controlling string options via metadata

  (bibliography, csl, title, style/css, id-format, base-dir,
  mode)

- Boolean metadata values accept true/false, yes/no, or 1/0

  (case-insensitive, downcased)

- String metadata values used directly for options that take

  arguments

- Metadata mode option resets options to mode defaults

  before applying other metadata

- Comprehensive tests for metadata control of command line

  options

## [0.1.25] - 2025-12-13

### New

- Add citation processing with support for Pandoc,

  MultiMarkdown, and mmark syntaxes

- Add bibliography loading from BibTeX, CSL JSON, and CSL

  YAML formats

- Add --bibliography CLI option to specify bibliography

  files (can be used multiple times)

- Add --csl CLI option to specify citation style file
- Add --no-bibliography CLI option to suppress bibliography

  output

- Add --link-citations CLI option to link citations to

  bibliography entries

- Add --show-tooltips CLI option for citation tooltips
- Add bibliography generation and insertion at <!--

  REFERENCES --> marker

Add support for bibliography specified in document metadata

- Added missing docs and man page for citation support
- Add support for transclude base metadata to control file

  transclusion paths

- Add Base Header Level and HTML Header Level metadata to

  adjust heading levels

- Add CSS metadata to link external stylesheets in

  standalone HTML documents

- Add HTML Header and HTML Footer metadata to inject custom

  HTML

- Add Language metadata to set HTML lang attribute in

  standalone documents

- Add Quotes Language metadata to control smart quote styles

  (French, German, Spanish, etc.)

- Add --css CLI flag as alias for --style with metadata

  override precedence

- Add metadata key normalization: case-insensitive matching

  with spaces removed (e.g., "HTML Header Level" matches
  "htmlheaderlevel")

- Add index extension supporting mmark syntax (!item),

  (!item, subitem), and (!!item, subitem) for primary
  entries

- Add TextIndex syntax support with {^}, [term]{^}, and

  {^params} patterns

- Add automatic index generation at end of document or at

  <!--INDEX--> marker

- Add alphabetical sorting and optional grouping by first

  letter for index entries

- Add hierarchical sub-item support in generated index
- Add --indices CLI flag to enable index processing
- Add --no-indices CLI flag to disable index processing
- Add --no-index CLI flag to suppress index generation while

  keeping markers

- Add comprehensive test suite with 40 index tests covering

  both syntaxes

### Improved

- Only process citations when bibliography is actually

  provided for better performance

- Add comprehensive tests for MultiMarkdown metadata keys
- Add comprehensive performance profiling system

  (APEX_PROFILE=1) to measure processing time for all
  extensions and CLI operations

- Add early exit checks for IAL processing when no {:

  markers are present

- Add early exit checks for index processing when no index

  patterns are found

- Add early exit checks for citation processing when no

  citation patterns are found

- Add early exit checks for definition list processing when

  no : patterns are found

- Optimize alpha lists postprocessing with single-pass

  algorithm replacing O(n*m) strstr() loops

- Add early exit check for alpha lists postprocessing when

  no markers are present

- Optimize file I/O by using fwrite() with known length

  instead of fputs()

- Add markdown syntax detection in definition lists to skip

  parser creation for plain text

- Optimize definition lists by selectively extracting only

  needed reference definitions instead of prepending all

- Add profiling instrumentation for all preprocessing,

  parsing, rendering, and post-processing steps

- Add profiling instrumentation for CLI operations (file

  I/O, metadata processing)

### Fixed

- Prevent autolinking of @ symbols in citation syntax (e.g.,

  [@key])

- Handle HTML comments in autolinker to preserve citation

  placeholders

- Fix quote language adjustment to handle Unicode curly

  quotes in addition to HTML entities

Fix bibliography_files assignment to remove unnecessary cast

- Fix heap-buffer-overflow in html_renderer.c when writing

  null terminator (allocate capacity+1)

- Fix use-after-free in ial.c by deferring node unlinking

  until after iteration completes

- Fix buffer overflow in definition_list.c HTML entity

  escaping (correct length calculation for &amp; and &quot;)

## [0.1.24] - 2025-12-13

### New

- Add citation processing with support for Pandoc,

  MultiMarkdown, and mmark syntaxes

- Add bibliography loading from BibTeX, CSL JSON, and CSL

  YAML formats

- Add --bibliography CLI option to specify bibliography

  files (can be used multiple times)

- Add --csl CLI option to specify citation style file
- Add --no-bibliography CLI option to suppress bibliography

  output

- Add --link-citations CLI option to link citations to

  bibliography entries

- Add --show-tooltips CLI option for citation tooltips
- Add bibliography generation and insertion at <!--

  REFERENCES --> marker

Add support for bibliography specified in document metadata

### Improved

- Only process citations when bibliography is actually

  provided for better performance

### Fixed

- Raw HTML tags and comments are now preserved in definition

  lists by default in unified mode. Previously, HTML content
  in definition list definitions was being replaced with
  "raw HTML omitted" even when using --unsafe or in unified
  mode.

- Unified mode now explicitly sets unsafe=true by default to

  ensure raw HTML is allowed.

- Prevent autolinking of @ symbols in citation syntax (e.g.,

  [@key])

- Handle HTML comments in autolinker to preserve citation

  placeholders

## [0.1.23] - 2025-12-12

### Changed

- Remove remote image embedding support (curl dependency

  removed)

### New

- Add metadata variable transforms with [%key:transform]

  syntax

- Add --transforms and --no-transforms flags to

  enable/disable transforms

- Add 19 text transforms: upper, lower, title, capitalize,

  trim, slug, replace (with regex support), substring,
  truncate, default, html_escape, basename, urlencode,
  urldecode, prefix, suffix, remove, repeat, reverse,
  format, length, pad, contains

- Add array transforms: split, join, first, last, slice
- Add date/time transform: strftime with date parsing
- Add transform chaining support (multiple transforms

  separated by colons)

- Add --meta-file flag to load metadata from external files

  (YAML, MMD, or Pandoc format, auto-detected)

- Add --meta KEY=VALUE flag to set metadata from command

  line (supports multiple flags and comma-separated pairs)

- Add metadata merging with proper precedence: command-line
  > document > file
- Add --embed-images flag to embed local images as base64

  data URLs in HTML output

- Add --base-dir flag to set base directory for resolving

  relative paths (images, includes, wiki links)

- Add automatic base directory detection from input file

  directory when reading from file

- Add base64 encoding utility for image data
- Add MIME type detection from file extensions (supports

  jpg, png, gif, webp, svg, bmp, ico)

- Add image embedding function that processes HTML and

  replaces local image src attributes with data URLs

- Add test suite for image embedding functionality

### Improved

- Wiki link scanner now processes all links in a text node

  in a single pass instead of recursively processing one at
  a time, significantly improving performance for documents
  with multiple wiki links per text node.

- Added early-exit optimization to skip wiki link AST

  traversal entirely when no wiki link markers are present
  in the document.

- Improve error handling in transform execution to return

  original value instead of NULL on failure

- Add comprehensive test coverage for all transforms

  including edge cases

- Relative path resolution for images now uses

  base_directory option

- Base directory is automatically set from input file

  location when not specified

### Fixed

- Fix bracket handling in regex patterns - properly match

  closing brackets in [%...] syntax when patterns contain
  brackets

- Fix YAML metadata parsing to strip quotes from quoted

  string values

- Raw HTML tags and comments are now preserved in definition

  lists by default in unified mode. Previously, HTML content
  in definition list definitions was being replaced with
  "raw HTML omitted" even when using --unsafe or in unified
  mode.

- Unified mode now explicitly sets unsafe=true by default to

  ensure raw HTML is allowed.

## [0.1.20] - 2025-12-11

#### NEW

- Added man page generation and installation support. Man

  pages can be generated from Markdown source using pandoc
  or go-md2man, with pre-generated man pages included in the
  repository as fallback. CMake build system now handles man
  page installation, and Homebrew formula installs the man
  page.

- Added comprehensive test suite for MMD 6 features

  including multi-line setext headers and link/image titles
  with different quote styles (single quotes, double quotes,
  parentheses). Tests verify these features work in both
  MultiMarkdown and unified modes.

- Added build-test man_page_copy target for man page

  installation.

- Added --obfuscate-emails flag to hex-encode mailto links.

#### IMPROVED

- Superscript processing now stops at sentence terminators

  (. , ; : ! ?) instead of including them in the superscript
  content. This prevents punctuation from being incorrectly
  included in superscripts.

- Enhanced subscript and underline detection logic. The

  processor now correctly differentiates between subscript
  (tildes within a word, e.g., H~2~O) and underline (tildes
  at word boundaries, e.g., ~text~) by checking if tildes
  are within alphanumeric words or at word boundaries.

- Expanded test coverage for superscript, subscript,

  underline, strikethrough, and highlight features with
  additional edge case tests.

- Email autolink detection trims trailing punctuation.

#### FIXED

- Autolink now only wraps real URLs/emails instead of every

  word.

Email autolinks now use mailto: hrefs instead of bare text.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.19] - 2025-12-09

#### CHANGED

- HTML comments now replaced with "raw HTML omitted" in

  CommonMark and GFM modes by default

- Added enable_sup_sub flag to apex_options struct
- Updated mode configurations to enable sup/sub in

  appropriate modes

- Added sup_sub.c to CMakeLists.txt build configuration
- Removed unused variables to resolve compiler warnings
- Tag filter (GFM security feature) now only applies in GFM

  mode, not Unified mode, allowing raw HTML and autolinks in
  Unified mode as intended.

- Autolink extension registration now respects the

  enable_autolink option flag.

#### NEW

- Added MultiMarkdown-style superscript (^text^) and

  subscript (~text~) syntax support

- Added --[no-]sup-sub command-line option to enable/disable

  superscript/subscript

- Superscript/subscript enabled by default in unified and

  MultiMarkdown modes

- Created sup_sub extension (sup_sub.c and sup_sub.h) for

  processing ^ and ~ syntax

- Added --[no-]unsafe command-line option to control raw

  HTML handling

- Added test_sup_sub() function with 13 tests covering

  superscript and

- Added test_mixed_lists() function with 10 tests covering

  mixed list

- Added test_unsafe_mode() function with 8 tests covering

  raw HTML

- Added preprocessing for angle-bracket autolinks

  (<http://...>) to convert them to explicit markdown links,
  ensuring they work correctly with custom rendering paths.

- Added --[no-]autolink CLI option to control automatic

  linking of URLs and email addresses. Autolinking is
  enabled by default in GFM, MultiMarkdown, Kramdown, and
  unified modes, and disabled in CommonMark mode.

- Added enable_autolink field to apex_options structure to

  control autolink behavior programmatically.

- Added underline syntax support: ~text~ now renders as

  <u>text</u> when there's a closing ~ with no space before
  it.

#### IMPROVED

- Test suite now includes 36 additional tests, increasing

  total test

- Autolink preprocessing now skips processing inside code

  spans (`...`) and code blocks (```...```), preventing URLs
  from being converted to links when they appear in code
  examples.

- Metadata replacement retains HTML edge-case handling and

  properly cleans up intermediate buffers.

#### FIXED

- Unified mode now correctly enables mixed list markers and

  alpha lists by default when no --mode is specified

- ^ marker now properly separates lists by creating a

  paragraph break instead of just blank lines

- Empty paragraphs created by ^ marker are now removed from

  final HTML output

- Superscript and subscript processing now skips ^ and ~

  characters

- Superscript processing now skips ^ when part of footnote

  reference

- Subscript processing now skips ~ when part of critic

  markup patterns

- Setext headers are no longer broken when followed by

  highlight syntax (==text==). Highlight processing now
  stops at line breaks to prevent interference with header
  parsing.

- Metadata parser no longer incorrectly treats URLs and

  angle-bracket autolinks as metadata. Lines containing < or
  URLs (http://, https://, mailto:) are now skipped during
  metadata extraction.

- Superscript/subscript processor now correctly

  differentiates between ~text~ (underline), ~word
  (subscript), and ~~text~~ (strikethrough). Double-tilde
  sequences are skipped so strikethrough extension can
  handle them.

- Subscript processing now stops at sentence terminators (.

  , ; : ! ?) instead of including them in the subscript
  content.

- Metadata variable replacement now runs before autolinking

  so [%key] values containing URLs are turned into links
  when autolinking is enabled.

- MMD metadata parsing no longer incorrectly rejects entries

  with URL values; only URL-like keys or '<' characters in
  keys are rejected, allowing "URL: https://example.com" as
  valid metadata.

Headers starting with `#` are now correctly recognized instead of being treated as autolinks. The autolink preprocessor now skips `#` at the start of a line when followed by whitespace.

Math processor now validates that `\(...\)` sequences contain actual math content (letters, numbers, or operators) before processing them. This prevents false positives like `\(%\)` from being treated as math when they only contain special characters.



## [0.1.18] - 2025-12-06

### Fixed
- GitHub Actions workflow now properly builds separate Linux

  x86_64 and ARM64 binaries

## [0.1.17] - 2025-12-06

### Fixed
- Relaxed tables now disabled by default for CommonMark,

  GFM, and MultiMarkdown modes (only enabled for Kramdown
  and Unified modes)

- Header ID extraction no longer incorrectly parses metadata

  variables like `[%title]` as MMD-style header IDs

- Tables with alignment/separator rows now correctly

  generate `<thead>` even when relaxed table mode is enabled

- Relaxed tables preprocessor preserves input newline

  behavior in output

- Memory management bug in IAL preprocessing removed

  unnecessary free call

## [0.1.16] - 2025-12-06

### Fixed
- IAL (Inline Attribute List) markers appearing immediately

  after content without a blank line are now correctly
  parsed

Added `apex_preprocess_ial()` function to ensure Kramdown-style IAL syntax works correctly with cmark-gfm parser

## [0.1.15] - 2025-12-06

### Fixed
- Homebrew formula updated with correct version and commit

  hash

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
`.gitignore` pattern fixed to properly include apex headers (was incorrectly matching `include/apex/`)

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
- Support for 5 processor modes: CommonMark, GFM,

  MultiMarkdown, Kramdown, Unified

**Metadata:**

- YAML front matter parsing
- MultiMarkdown metadata format
- Pandoc title block format
- Metadata variable replacement with `[%key]` syntax

**Extended Syntax:**

- Wiki-style links: `[[Page]]`, `[[Page|Display]]`,

  `[[Page#Section]]`

- Math support: `$inline$` and `$$display$$` with LaTeX
- Critic Markup: All 5 types ({++add++}, {--del--},

  {~~sub~~}, {==mark==}, {>>comment<<})

- GFM tables, strikethrough, task lists, autolinks
- Reference-style footnotes
- Smart typography (smart quotes, dashes, ellipsis)

**Build System:**

- CMake build system for cross-platform support
- Builds shared library, static library, CLI binary, and

  macOS framework

- Clean compilation on macOS with Apple Clang

**CLI Tool:**

- `apex` command-line binary
- Support for all processor modes via `--mode` flag
- Stdin/stdout support for Unix pipes
- Comprehensive help and version information

**Integration:**

Objective-C wrapper (`NSString+Apex`) for Marked integration

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

- Critic Markup substitutions have edge cases with certain

  inputs

- Definition lists not yet implemented
- Kramdown attributes not yet implemented
- Inline footnotes not yet implemented

### Performance

- Small documents (< 10KB): < 10ms
- Medium documents (< 100KB): < 100ms
- Large documents (< 1MB): < 1s

### Credits

Based on [cmark-gfm](https://github.com/github/cmark-gfm) by GitHub

Developed for [Marked](https://marked2app.com) by Brett Terpstra

[0.1.47]: https://github.com/ttscoff/apex/releases/tag/v0.1.47
[0.1.46]: https://github.com/ttscoff/apex/releases/tag/v0.1.46
[0.1.45]: https://github.com/ttscoff/apex/releases/tag/v0.1.45
[0.1.44]: https://github.com/ttscoff/apex/releases/tag/v0.1.44
[0.1.43]:
https://github.com/ttscoff/apex/releases/tag/v0.1.43
[0.1.42]:
https://github.com/ttscoff/apex/releases/tag/v0.1.42
[0.1.41]:
https://github.com/ttscoff/apex/releases/tag/v0.1.41
[0.1.40]:
https://github.com/ttscoff/apex/releases/tag/v0.1.40
[0.1.39]:
https://github.com/ttscoff/apex/releases/tag/v0.1.39
[0.1.38]:
https://github.com/ttscoff/apex/releases/tag/v0.1.38
[0.1.37]:
https://github.com/ttscoff/apex/releases/tag/v0.1.37
[0.1.36]:
https://github.com/ttscoff/apex/releases/tag/v0.1.36
[0.1.35]:
https://github.com/ttscoff/apex/releases/tag/v0.1.35
[0.1.34]:
https://github.com/ttscoff/apex/releases/tag/v0.1.34
[0.1.33]:
https://github.com/ttscoff/apex/releases/tag/v0.1.33
[0.1.32]:
https://github.com/ttscoff/apex/releases/tag/v0.1.32
[0.1.31]:
https://github.com/ttscoff/apex/releases/tag/v0.1.31
[0.1.30]:
https://github.com/ttscoff/apex/releases/tag/v0.1.30
[0.1.29]:
https://github.com/ttscoff/apex/releases/tag/v0.1.29
[0.1.28]:
https://github.com/ttscoff/apex/releases/tag/v0.1.28
[0.1.27]:
https://github.com/ttscoff/apex/releases/tag/v0.1.27
[0.1.26]:
https://github.com/ttscoff/apex/releases/tag/v0.1.26
[0.1.25]:
https://github.com/ttscoff/apex/releases/tag/v0.1.25
[0.1.24]:
https://github.com/ttscoff/apex/releases/tag/v0.1.24
[0.1.23]:
https://github.com/ttscoff/apex/releases/tag/v0.1.23
[0.1.20]:
https://github.com/ttscoff/apex/releases/tag/v0.1.20
[0.1.19]:
https://github.com/ttscoff/apex/releases/tag/v0.1.19
[0.1.18]:
https://github.com/ttscoff/apex/releases/tag/v0.1.18
[0.1.17]:
https://github.com/ttscoff/apex/releases/tag/v0.1.17
[0.1.16]:
https://github.com/ttscoff/apex/releases/tag/v0.1.16
[0.1.15]:
https://github.com/ttscoff/apex/releases/tag/v0.1.15
[0.1.10]:
https://github.com/ttscoff/apex/releases/tag/v0.1.10
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

