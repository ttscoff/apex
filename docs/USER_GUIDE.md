# Apex User Guide

**Version 0.1.0**

Apex is a unified Markdown processor that combines the best features of CommonMark, GitHub Flavored Markdown, MultiMarkdown, and Kramdown into a single, fast, C-based processor.

## Quick Start

### Installation

```bash
# Build from source
git clone [apex-repo-url]
cd apex
mkdir build && cd build
cmake ..
make
sudo make install
```

### Basic Usage

```bash
# Convert a file
apex document.md > output.html

# From stdin
cat document.md | apex > output.html

# Specify processor mode
apex --mode gfm document.md
apex --mode multimarkdown document.md
```

## Processor Modes

Apex supports multiple processor modes for compatibility with different Markdown flavors:

### CommonMark Mode (`--mode commonmark`)

Pure CommonMark specification compliance. No extensions.

```bash
apex --mode commonmark document.md
```

### GFM Mode (`--mode gfm`)

GitHub Flavored Markdown with:

- Tables
- Strikethrough (~~text~~)
- Task lists (- [ ] Todo)
- Autolinks
- Hard line breaks

```bash
apex --mode gfm document.md
```

### MultiMarkdown Mode (`--mode multimarkdown` or `--mode mmd`)

MultiMarkdown compatibility with:

- Metadata blocks (YAML, MMD format)
- Metadata variables `[%key]`
- Footnotes
- Tables
- Smart typography
- Math support

```bash
apex --mode mmd document.md
```

### Kramdown Mode (`--mode kramdown`)

Kramdown compatibility with:

- Attributes `{: #id .class}`
- Definition lists
- Footnotes
- Smart typography
- Math support

```bash
apex --mode kramdown document.md
```

### Unified Mode (`--mode unified`, default)

All features enabled - the superset of all modes.

```bash
apex document.md
# or explicitly:
apex --mode unified document.md
```

## Features

### Metadata Blocks

Apex supports three metadata formats:

**YAML Front Matter:**
```yaml
---
title: My Document
author: John Doe
date: 2025-12-04
---
```

**MultiMarkdown Metadata:**
```
Title: My Document
Author: John Doe
Date: 2025-12-04

```

**Pandoc Title Blocks:**
```
% My Document
% John Doe
% 2025-12-04
```

### Metadata Variables

Use `[%key]` to insert metadata values anywhere in your document:

```markdown
---
title: Apex User Guide
version: 0.1.0
---

# [%title]

Version: [%version]
```

Renders as:
```html
<h1>Apex User Guide</h1>
<p>Version: 0.1.0</p>
```

### Wiki Links

```markdown
[[Page Name]]                   # Link to page
[[Page Name|Display Text]]      # Custom display
[[Page Name#Section]]           # Link to section
```

### Math Support

**Inline math:**
```markdown
The equation $E = mc^2$ is famous.
```

**Display math:**
```markdown
$$
\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
$$
```

Math is wrapped in spans with appropriate classes for MathJax or KaTeX to render.

### Critic Markup

Track changes and annotations with three processing modes:

**Markup Mode (default)** - Show changes with HTML tags:
```markdown
{++addition++}              # Added text → <ins>addition</ins>
{--deletion--}              # Deleted text → <del>deletion</del>
{~~old text~>new text~~}    # Substitution → <del>old</del><ins>new</ins>
{==highlighted==}           # Highlighted text → <mark>highlighted</mark>
{>>comment text<<}          # Comment → <span class="comment">comment</span>
```

**Accept Mode** (`--accept`) - Apply all changes, output clean text:
```bash
apex --accept document.md
```
- Additions: kept
- Deletions: removed
- Substitutions: new text kept
- Highlights: text kept (markup removed)
- Comments: removed

**Reject Mode** (`--reject`) - Revert all changes, restore original:
```bash
apex --reject document.md
```
- Additions: removed
- Deletions: kept
- Substitutions: old text kept
- Highlights: text kept (markup removed)
- Comments: removed

**Use cases**:

- Review: `apex document.md` (show all markup)
- Publish: `apex --accept document.md` (final version)
- Rollback: `apex --reject document.md` (original version)

### Tables

GitHub Flavored Markdown tables:

```markdown
| Header 1 | Header 2 |
| -------- | -------- |
| Cell 1   | Cell 2   |
```

#### Inline tables from CSV/TSV

You can turn inline CSV/TSV text into Markdown tables by using a fenced code
block with the info string `table`. The delimiter is detected automatically:

- If any non-blank line contains a tab, the block is treated as TSV.
- Otherwise, if any non-blank line contains a comma, the block is treated as CSV.
- If no tabs or commas are found, the block is left unchanged as a normal
  `table`-info fenced code block.

```table
header 1,header 2,header 3
data 1,data 2,data 3
,,data 2c
```

Optionally, you can use an HTML comment marker to indicate that the following
non-blank lines up to the next blank line should be treated as CSV/TSV data:

```markdown
<!--TABLE-->
header 1,header 2,header 3
data 1,data 2,data 3
,,data 2c
```

The same delimiter-detection rules apply to `<!--TABLE-->` blocks.

### Task Lists

```markdown
- [ ] Todo item
- [x] Completed item
```

### Footnotes

**Reference-style footnotes**:
```markdown
Here's a footnote[^1].

[^1]: This is the footnote content.
```

**Kramdown inline footnotes**:
```markdown
This is a footnote^[inline content here] in the text.
```

**MultiMarkdown inline footnotes**:
```markdown
This is a footnote[^inline content with spaces] in the text.
```

All styles produce properly formatted footnote sections with backlinks.

### Abbreviations

**Classic MMD syntax** (automatic replacement):
```markdown
*[HTML]: Hypertext Markup Language
*[CSS]: Cascading Style Sheets

HTML and CSS are essential.
```

**MMD 6 reference syntax**:
```markdown
[>MMD]: MultiMarkdown

Using [>MMD] here and [>MMD] again.
```

**MMD 6 inline syntax** (no definition needed):
```markdown
This is [>(MD) Markdown] and [>(CSS) Cascading Style Sheets].
```

All produce `<abbr title="expansion">abbr</abbr>` tags.

### Definition Lists

Kramdown/PHP Markdown Extra syntax:
```markdown
Term
: Definition text with **Markdown** support

Apple
: A fruit
: A company
```

Renders as proper `<dl>`, `<dt>`, `<dd>` HTML.

### Inline Attribute Lists (IAL)

Kramdown syntax for adding attributes to elements:
```markdown
# Header {: #custom-id}

Paragraph with class.
{: .important}

## Another Header {: #section-2 .highlight}
```

### Callouts

Bear/Obsidian syntax:
```markdown
> [!NOTE] Title
> This is a note callout

> [!WARNING] Be Careful
> Warning content

> [!TIP] Pro Tip
> Helpful advice

> [!DANGER] Critical
> Dangerous operation
```

Collapsible callouts:
```markdown
> [!NOTE]+ Expandable
> Click to expand

> [!NOTE]- Collapsed
> Starts collapsed
```

### File Includes

**Marked syntax**:
```markdown
<<[file.md]      # Include and process Markdown
<<(code.py)      # Include as code block
<<{raw.html}     # Include raw HTML
```

**MultiMarkdown transclusion**:
```markdown
{{file.md}}      # Include file
{{*.md}}         # Wildcard include
```

**iA Writer syntax**:
```markdown
/image.png       # Intelligent include (detects type)
/code.py         # Auto-detects as code block
/document.md     # Auto-detects as Markdown
```

CSV and TSV files automatically convert to tables!

### Table of Contents

Multiple marker formats:
```markdown
<!--TOC-->              # Basic TOC
<!--TOC max2 min1-->    # With depth control
{{TOC}}                 # MMD style
{{TOC:2-4}}            # With range
```

### Advanced Tables

**Rowspan** (cells spanning multiple rows):
```markdown
| A   | B   |
| --- | --- |
| C   | D   |
| ^^  | E   |
```
Cell C spans 2 rows (using `^^` marker).

**Colspan** (cells spanning multiple columns):
```markdown
| A   | B   | C   |
| --- | --- | --- |
| D   |     |     |
```
Empty cells merge with previous cell.

**Table captions**:
```markdown
[Table Caption]

| A   | B   |
| --- | --- |
| C   | D   |
```

### Special Markers

**Page breaks**:
```markdown
<!--BREAK-->            # HTML comment style
{::pagebreak /}         # Kramdown style
```

**Autoscroll pauses** (for Marked's teleprompter mode):
```markdown
<!--PAUSE:5-->          # Pause for 5 seconds
```

**End-of-block marker**:
```markdown
- Item 1

^

- Item 2
```
Forces list separation.

### GitHub Emoji

```markdown
Success! :rocket: :tada: :sparkles:
I :heart: Markdown!
```

Supports 350+ GitHub emoji, converted to Unicode characters.

## Command-Line Options

```
apex [options] [file]

Options:
  -m, --mode MODE        Processor mode: commonmark, gfm, mmd, kramdown, unified
  -o, --output FILE      Write output to FILE
  -s, --standalone       Generate complete HTML document (with <html>, <head>, <body>)
  --style FILE           Link to CSS file in document head (implies --standalone)
  --title TITLE          Document title (for standalone mode, default: "Document")
  --pretty               Pretty-print HTML with indentation and whitespace
  --accept               Accept all Critic Markup changes (apply edits)
  --reject               Reject all Critic Markup changes (revert edits)
  --no-tables            Disable table support
  --no-footnotes         Disable footnote support
  --no-smart             Disable smart typography
  --no-math              Disable math support
  --[no-]includes       Enable file inclusion (enabled by default in unified mode)
  --hardbreaks           Treat newlines as hard breaks
  -h, --help             Show help
  -v, --version          Show version
```

### Output Modes

**Fragment Mode (default)**: Outputs HTML body content only
```bash
apex document.md  # Compact HTML fragment
```

**Pretty Mode**: Formatted HTML with indentation
```bash
apex --pretty document.md  # Readable, indented HTML
```

**Standalone Mode**: Complete HTML5 document
```bash
apex --standalone document.md
apex -s --title "My Doc" --style styles.css document.md
```

**Combined**: Beautiful complete documents
```bash
apex -s --pretty --title "Report" --style report.css doc.md
```

## Library Usage

### C API

```c
#include <apex/apex.h>

// Get default options
apex_options options = apex_options_default();

// Or get mode-specific options
apex_options gfm_opts = apex_options_for_mode(APEX_MODE_GFM);

// Convert
const char *markdown = "# Hello\n\n**World**";
char *html = apex_markdown_to_html(markdown, strlen(markdown), &options);

// Use html...
printf("%s\n", html);

// Clean up
apex_free_string(html);
```

### Objective-C (for Marked integration)

```objective-c
#import <NSString+Apex.h>

// Convert with default unified mode
NSString *html = [NSString convertWithApex:markdown];

// Convert with specific mode
NSString *gfmHtml = [NSString convertWithApex:markdown mode:@"gfm"];
NSString *mmdHtml = [NSString convertWithApex:markdown mode:@"multimarkdown"];
```

## Performance

Apex is built on cmark-gfm, which is highly optimized:

- **Small documents (< 10KB)**: < 10ms
- **Medium documents (< 100KB)**: < 100ms
- **Large documents (< 1MB)**: < 1s

## Compatibility

### With MultiMarkdown

- ✅ Metadata blocks (YAML, MMD, Pandoc formats)
- ✅ Metadata variables `[%key]`
- ✅ Tables (including advanced features)
- ✅ Smart typography
- ✅ Math support ($...$ and $$...$$)
- ✅ File includes / transclusion (`{{file}}`)
- ✅ Definition lists
- ✅ Footnotes (reference and inline)
- ✅ Abbreviations (classic and MMD 6 syntax)

### With GitHub Flavored Markdown

- ✅ Tables
- ✅ Strikethrough (~~text~~)
- ✅ Task lists (- [ ] / - [x])
- ✅ Autolinks
- ✅ Hard line breaks (in GFM mode)
- ✅ Emoji (:emoji_name:)

### With Kramdown

- ✅ Smart typography
- ✅ Math support
- ✅ Footnotes (reference and inline ^[text])
- ✅ Inline Attribute Lists `{: #id .class}`
- ✅ Attribute List Definitions `{:ref: attrs}`
- ✅ Definition lists (: syntax)
- ✅ End-of-block markers (^)

### With Marked (Special Syntax)

- ✅ Callouts (Bear/Obsidian/Xcode styles)
- ✅ File includes (<<[md], <<(code), <<{html})
- ✅ Table of Contents markers
- ✅ Page breaks (<!--BREAK-->)
- ✅ Autoscroll pauses (<!--PAUSE:N-->)
- ✅ Wiki links ([[Page]])

### With iA Writer

- ✅ File transclusion (/filename with auto-detection)
- ✅ Image includes
- ✅ Code includes
- ✅ CSV/TSV to table conversion

### With CommonMark

- ✅ Full specification compliance
- ✅ All standard syntax
- ✅ Strict parsing when in CommonMark mode

## Standalone HTML Documents

Generate complete, self-contained HTML5 documents:

```bash
# Basic standalone document
apex --standalone document.md

# With custom title and CSS
apex -s --title "My Report" --style report.css doc.md -o report.html

# Pretty-formatted complete document
apex -s --pretty --title "Beautiful Doc" doc.md
```

**Features**:

- Complete HTML5 structure (<!DOCTYPE html>, <head>, <body>)
- UTF-8 charset and responsive viewport
- Custom document titles
- External CSS linking or beautiful default inline styles
- Perfect for: documentation, reports, blogs, standalone files

**Default styles include**:

- Modern system font stack
- Responsive centered layout (800px max-width)
- Code block and table styling
- Callout formatting
- Print styles for page breaks

## Pretty-Print HTML

Format HTML with proper indentation for readability:

```bash
# Pretty fragment
apex --pretty document.md

# Pretty standalone
apex -s --pretty document.md
```

**Features**:

- 2-space indentation per nesting level
- Block elements on separate lines
- Inline elements preserved inline
- Perfect for: debugging, source viewing, version control

## Examples

### Complete Document Example

```markdown
---
title: Example Document
author: Brett Terpstra
date: 2025-12-04
---

# [%title]

By [%author], updated [%date]

## Features

This document demonstrates **bold**, *italic*, and ~~strikethrough~~.

### Math

The famous equation: $E = mc^2$

### Code

```python
def hello():
    print("Hello, World!")
```

### Task List

- [x] Complete documentation
- [ ] Add more examples

### Wiki Links

See [[HomePage]] or [[API#Methods|API Documentation]].

### Critic Markup

Original text {~~with changes~>and improvements~~}.

New {++addition++} and {--removal--}.

{==Important note==} to highlight.

### Footnotes

This has a footnote[^1].

[^1]: Footnote content here.
```

## Troubleshooting

**Math not rendering**: Ensure MathJax or KaTeX is loaded in your HTML template

**Wiki links as plain text**: Make sure `--mode unified` is set or wiki links are enabled

**Critic markup not showing**: Verify `enable_critic_markup` is true in unified mode

## Getting Help

- GitHub Issues: [repo-url]/issues
- Documentation: See `docs/` directory
- Examples: See `tests/` directory

## License

BSD 2-Clause License

Copyright (c) 2025 Brett Terpstra

