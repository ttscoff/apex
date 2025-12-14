<!--README-->
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

<!--GITHUB-->
# Apex
<!--END GITHUB-->

Apex is a unified Markdown processor that combines the best features from CommonMark, GitHub Flavored Markdown (GFM), MultiMarkdown, Kramdown, and Marked. One processor to rule them all.

<!--GITHUB-->![](apex-header-2-rb@2x.webp)<!--END GITHUB-->

<!--JEKYLL {% img alignright /uploads/2025/12/apexicon.png 300 300 "Apex Icon" %}-->There are so many variations of Markdown, extending its features in all kinds of ways. But picking one flavor means giving up the features of another flavor. So I'm building Apex with the goal of making all of the most popular features of various processors available in one tool.

## Features

### Compatibility Modes

- **Multiple compatibility modes**: CommonMark, GFM, MultiMarkdown, Kramdown, and Unified (all features)
- **Mode-specific features**: Each mode enables appropriate extensions for maximum compatibility

### Markdown Extensions

- **Tables**: GitHub Flavored Markdown tables with advanced features (rowspan, colspan, captions)
- **Relaxed tables**: Support for tables without separator rows (Kramdown-style)
- **Footnotes**: Three syntaxes supported (reference-style, Kramdown inline, MultiMarkdown inline)
- **Definition lists**: Kramdown-style definition lists with Markdown content support
- **Task lists**: GitHub-style checkboxes (`- [ ]` and `- [x]`)
- **Strikethrough**: `~~text~~` syntax from GFM
- **Smart typography**: Automatic conversion of quotes, dashes, ellipses, and more
- **Math support**: LaTeX math expressions with `$...$` (inline) and `$$...$$` (display)
- **Wiki links**: `[[Page Name]]` and `[[Page Name|Display Text]]` syntax
- **Abbreviations**: Three syntaxes (classic MMD, MMD 6 reference, MMD 6 inline)
- **Callouts**: Bear/Obsidian-style callouts with collapsible support (`> [!NOTE]`, `> [!WARNING]`, etc.)
- **GitHub emoji**: 350+ emoji support (`:rocket:`, `:heart:`, etc.)

### Document Features

- **Metadata blocks**: YAML front matter, MultiMarkdown metadata, and Pandoc title blocks
- **Metadata variables**: Insert metadata values with `[%key]` syntax
- **Metadata transforms**: Transform metadata values with `[%key:transform]` syntax - supports case conversion, string manipulation, regex replacement, date formatting, and more. See [Metadata Transforms](https://github.com/ttscoff/apex/wiki/Metadata-Transforms) for complete documentation
- **Table of Contents**: Automatic TOC generation with depth control (`<!--TOC-->`, `{{TOC}}`)
- **File includes**: Three syntaxes (Marked `<<[file]`, MultiMarkdown `{{file}}`, iA Writer `/file`)
- **CSV/TSV support**: Automatic table conversion from CSV and TSV files
- **Inline Attribute Lists (IAL)**: Kramdown-style attributes `{: #id .class}`
- **Special markers**: Page breaks (`<!--BREAK-->`), autoscroll pauses (`<!--PAUSE:N-->`), end-of-block markers

### Citations and Bibliography

- **Multiple citation syntaxes**: Pandoc (`[@key]`), MultiMarkdown (`[#key]`), and mmark (`[@RFC1234]`) styles
- **Bibliography formats**: Support for BibTeX (`.bib`), CSL JSON (`.json`), and CSL YAML (`.yml`, `.yaml`) formats
- **Automatic bibliography generation**: Bibliography automatically generated from cited entries
- **Citation linking**: Option to link citations to bibliography entries
- **Metadata support**: Bibliography can be specified in document metadata or via command-line flags
- **Multiple bibliography files**: Support for loading and merging multiple bibliography files
- **CSL style support**: Citation Style Language (CSL) files for custom citation formatting
- **Mode support**: Citations enabled in MultiMarkdown and unified modes

### Indices

- **mmark syntax**: `(!item)`, `(!item, subitem)`, `(!!item, subitem)` for primary entries
- **TextIndex syntax**: `{^}`, `[term]{^}`, `{^params}` for flexible indexing
- **Automatic index generation**: Index automatically generated at end of document or at `<!--INDEX-->` marker
- **Alphabetical sorting**: Entries sorted alphabetically with optional grouping by first letter
- **Hierarchical sub-items**: Support for nested index entries
- **Mode support**: Indices enabled by default in MultiMarkdown and unified modes

### Critic Markup

- **Change tracking**: Additions (`{++text++}`), deletions (`{--text--}`), substitutions (`{~~old~>new~~}`)
- **Annotations**: Highlights (`{==text==}`) and comments (`{>>text<<}`)
- **Accept mode**: `--accept` flag to apply all changes for final output
- **Reject mode**: `--reject` flag to revert all changes to original

### Output Options

- **Flexible output**: Compact HTML fragments, pretty-printed HTML, or complete standalone documents
- **Standalone documents**: Generate complete HTML5 documents with `<html>`, `<head>`, `<body>` tags
- **Custom styling**: Link external CSS files in standalone mode
- **Pretty-print**: Formatted HTML with proper indentation for readability
- **Header ID generation**: Automatic or manual header IDs with multiple format options (GFM, MMD, Kramdown)
- **Header anchors**: Option to generate `<a>` anchor tags instead of header IDs

### Advanced Features

- **Hard breaks**: Option to treat newlines as hard line breaks
- **Feature toggles**: Granular control to enable/disable specific features (tables, footnotes, math, smart typography, etc.)
- **Unsafe HTML**: Option to allow or block raw HTML in documents
- **Autolinks**: Automatic URL detection and linking
- **Superscript/Subscript**: Support for `^superscript^` and `~subscript~` syntax

## Installation

### Homebrew (macOS/Linux)

```bash
brew tap ttscoff/thelab
brew install ttscoff/thelab/apex
```

### Building from Source

```bash
git clone https://github.com/ttscoff/apex.git
cd apex
git submodule update --init --recursive
make
```

The `apex` binary will be in the `build/` directory.

To install the built binary and libraries system-wide:

```bash
make install
```

**Note:** The default `make` command runs both `cmake -S . -B build` (to configure the project) and `cmake --build build` (to compile). If you prefer to run cmake commands directly, you can use those instead.

### Pre-built Binaries

Download pre-built binaries from the [latest release](https://github.com/ttscoff/apex/releases/latest). Binaries are available for:

- macOS (Universal binary for arm64 and x86_64)
- Linux (x86_64 and arm64)

## Basic Usage

### Command Line

```bash
# Process a markdown file
apex input.md

# Output to a file
apex input.md -o output.html

# Generate standalone HTML document
apex input.md --standalone --title "My Document"

# Pretty-print HTML output
apex input.md --pretty
```

### Processing Modes

Apex supports multiple compatibility modes:

- `--mode commonmark` - Pure CommonMark specification
- `--mode gfm` - GitHub Flavored Markdown
- `--mode mmd` or `--mode multimarkdown` - MultiMarkdown compatibility
- `--mode kramdown` - Kramdown compatibility
- `--mode unified` - All features enabled (default)

```bash
# Use GFM mode
apex input.md --mode gfm

# Use Kramdown mode with relaxed tables
apex input.md --mode kramdown
```

### Common Options

- `--pretty` - Pretty-print HTML with indentation
- `--standalone` - Generate complete HTML document with `<html>`, `<head>`, `<body>`
- `--style FILE` - Link to CSS file in document head (requires `--standalone`)
- `--title TITLE` - Document title (requires `--standalone`)
- `--relaxed-tables` - Enable relaxed table parsing (default in unified/kramdown modes)
- `--no-relaxed-tables` - Disable relaxed table parsing
- `--id-format FORMAT` - Header ID format: `gfm`, `mmd`, or `kramdown`
- `--no-ids` - Disable automatic header ID generation
- `--header-anchors` - Generate `<a>` anchor tags instead of header IDs
- `--bibliography FILE` - Bibliography file (BibTeX, CSL JSON, or CSL YAML) - can be used multiple times
- `--csl FILE` - Citation style file (CSL format)
- `--link-citations` - Link citations to bibliography entries
- `--indices` - Enable index processing (mmark and TextIndex syntax)
- `--no-indices` - Disable index processing
- `--no-index` - Suppress index generation (markers still created)

## Documentation

For complete documentation, see the [Apex Wiki](https://github.com/ttscoff/apex/wiki).

Key documentation pages:
- [Citations and Bibliography](https://github.com/ttscoff/apex/wiki/Citations) - Complete guide to citations and bibliographies
- [Command Line Options](https://github.com/ttscoff/apex/wiki/Command-Line-Options) - All CLI flags explained
- [Syntax Reference](https://github.com/ttscoff/apex/wiki/Syntax) - Complete syntax reference

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE]([LICENSE](https://github.com/ttscoff/apex/blob/main/LICENSE)) file for details.
<!--END README-->
