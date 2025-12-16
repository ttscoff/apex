
[![Version: 0.1.35](https://img.shields.io/badge/Version-0.1.35-528c9e)](https://github.com/ApexMarkdown/apex/releases/latest) ![](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)


# Apex


Apex is a unified Markdown processor that combines the best features from CommonMark, GitHub Flavored Markdown (GFM), MultiMarkdown, Kramdown, and Marked. One processor to rule them all.


![](apex-header-2-rb@2x.webp)


There are so many variations of Markdown, extending its features in all kinds of ways. But picking one flavor means giving up the features of another flavor. So I'm building Apex with the goal of making all of the most popular features of various processors available in one tool.

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
- **Wiki links**: `[[Page Name]]`, `[[Page Name|Display Text]]`, and `[[Page Name#Section]]` syntax with configurable link targets via `--wikilink-space` and `--wikilink-extension`
- **Abbreviations**: Three syntaxes (classic MMD, MMD 6 reference, MMD 6 inline)
- **Callouts**: Bear/Obsidian-style callouts with collapsible support (`> [!NOTE]`, `> [!WARNING]`, etc.)
- **GitHub emoji**: 350+ emoji support (`:rocket:`, `:heart:`, etc.)

### Document Features


- **Metadata blocks**: YAML front matter, MultiMarkdown metadata, and Pandoc title blocks
- **Metadata variables**: Insert metadata values with `[%key]` syntax
- **Metadata transforms**: Transform metadata values with `[%key:transform]` syntax - supports case conversion, string manipulation, regex replacement, date formatting, and more. See [Metadata Transforms](https://github.com/ApexMarkdown/apex/wiki/Metadata-Transforms) for complete documentation
- **Metadata control of options**: Control command-line options via metadata - set boolean flags (`indices: false`, `wikilinks: true`) and string options (`bibliography: refs.bib`, `title: My Document`, `wikilink-space: dash`, `wikilink-extension: html`) directly in document metadata for per-document configuration
- **Table of Contents**: Automatic TOC generation with depth control using HTML (`<!--TOC-->`), MMD (`{{TOC}}` / `{{TOC:2-4}}`), and Kramdown `{:toc}` markers. Headings marked with `{:.no_toc}` are excluded from the generated TOC.
- **File includes**: Three syntaxes (Marked `<<[file]`, MultiMarkdown `{{file}}`, iA Writer `/file`), with support for address ranges and wildcard/glob patterns such as `{{file.*}}`, `{{*.md}}`, and `{{c?de.py}}`.
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

### Extensibility and Plugins

Apex supports a flexible plugin system that lets you add new syntax and post-processing features in any language while keeping the core parser stable and fast. Plugins are disabled by default so there is no performance impact unless you opt in. Enable them per run with `--plugins`, or per document with a `plugins: true` (or `enable-plugins: true`) key in your metadata.

You can manage plugins from the CLI:

- Install plugins with `--install-plugin`:
  - From the central directory using an ID: `--install-plugin kbd`
  - Directly from a Git URL or GitHub shorthand: `--install-plugin https://github.com/user/repo.git` or `--install-plugin user/repo`
- Uninstall a local plugin with `--uninstall-plugin ID`.
- See installed and available plugins with `--list-plugins`.

When installing from a direct Git URL or GitHub repo name, Apex will prompt with a security warning before cloning, since plugins execute unverified code.

For a complete guide to writing, installing, and publishing plugins, see the [Plugins](https://github.com/ApexMarkdown/apex/wiki/Plugins) page in the Apex Wiki.

## Installation

### Homebrew (macOS/Linux)

```bash
brew tap ttscoff/thelab
brew install ttscoff/thelab/apex
```

### Building from Source

```bash
git clone https://github.com/ApexMarkdown/apex.git
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

Download pre-built binaries from the [latest release](https://github.com/ApexMarkdown/apex/releases/latest). Binaries are available for:

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
- `--wikilinks` - Enable wiki link syntax `[[Page]]`, `[[Page|Display]]`, and `[[Page#Section]]`
- `--wikilink-space MODE` - Control how spaces in wiki link page names are converted (`dash`, `none`, `underscore`, `space`; default: `dash`)
- `--wikilink-extension EXT` - File extension to append to wiki link URLs (e.g. `html`, `md`)

### All Options

```
Apex Markdown Processor v0.1.34
One Markdown processor to rule them all

Usage: build/apex [options] [file]

Options:
  --accept               Accept all Critic Markup changes (apply edits)
  --[no-]includes        Enable file inclusion (enabled by default in unified mode)
  --hardbreaks           Treat newlines as hard breaks
  -h, --help             Show this help message
  --header-anchors        Generate <a> anchor tags instead of header IDs
  --id-format FORMAT      Header ID format: gfm (default), mmd, or kramdown
                          (modes auto-set format; use this to override in unified mode)
  --[no-]alpha-lists     Support alpha list markers (a., b., c. and A., B., C.)
  --[no-]mixed-lists     Allow mixed list markers at same level (inherit type from first item)
  -m, --mode MODE        Processor mode: commonmark, gfm, mmd, kramdown, unified (default)
  --meta-file FILE       Load metadata from external file (YAML, MMD, or Pandoc format)
  --meta KEY=VALUE       Set metadata key-value pair (can be used multiple times, supports quotes and comma-separated pairs)
  --no-footnotes         Disable footnote support
  --no-ids                Disable automatic header ID generation
  --no-math              Disable math support
  --no-smart             Disable smart typography
  --no-tables            Disable table support
  -o, --output FILE      Write output to FILE instead of stdout
  --pretty               Pretty-print HTML with indentation and whitespace
  --[no-]autolink        Enable autolinking of URLs and email addresses
  --obfuscate-emails     Obfuscate email links/text using HTML entities
  --[no-]plugins         Enable or disable external/plugin processing (default: off)
  --list-plugins         List installed plugins and available plugins from the remote directory
  --install-plugin ID    Install plugin by id from directory, or by Git URL/GitHub shorthand (user/repo)
  --uninstall-plugin ID  Uninstall a locally installed plugin by id
  --[no-]relaxed-tables  Enable relaxed table parsing (no separator rows required)
  --[no-]sup-sub         Enable MultiMarkdown-style superscript (^text^) and subscript (~text~) syntax
  --[no-]transforms      Enable metadata variable transforms [%key:transform] (enabled by default in unified mode)
  --[no-]unsafe          Allow raw HTML in output (default: true for unified/mmd/kramdown, false for commonmark/gfm)
  --[no-]wikilinks       Enable wiki link syntax [[PageName]] (disabled by default)
  --wikilink-space MODE  Space replacement for wiki links: dash, none, underscore, space (default: dash)
  --wikilink-extension EXT  File extension to append to wiki links (e.g., html, md)
  --embed-images         Embed local images as base64 data URLs in HTML output
  --base-dir DIR         Base directory for resolving relative paths (for images, includes, wiki links)
  --bibliography FILE     Bibliography file (BibTeX, CSL JSON, or CSL YAML) - can be used multiple times
  --csl FILE              Citation style file (CSL format)
  --indices               Enable index processing (mmark and TextIndex syntax)
  --no-indices            Disable index processing
  --no-index              Suppress index generation (markers still created)
  --no-bibliography       Suppress bibliography output
  --link-citations       Link citations to bibliography entries
  --show-tooltips         Show tooltips on citations
  --reject               Reject all Critic Markup changes (revert edits)
  -s, --standalone       Generate complete HTML document (with <html>, <head>, <body>)
  --css FILE, --style FILE  Link to CSS file in document head (requires --standalone, overrides CSS metadata)
  --title TITLE          Document title (requires --standalone, default: "Document")
  -v, --version          Show version information

If no file is specified, reads from stdin.
```

### Per-Document Configuration via Metadata

Most command-line options can be controlled via document metadata, allowing different files to be processed with different settings when processing batches. Boolean options accept `true`/`false`, `yes`/`no`, or `1`/`0` (case-insensitive). String options use the value directly.

**Example:**

```yaml
---
indices: false
wikilinks: true
bibliography: references.bib
title: My Research Paper
pretty: true
standalone: true
---
```

This allows you to process multiple files with `apex *.md` and have each file use its own configuration. You can also use `--meta-file` to specify a shared configuration file that applies to all processed files.

## Documentation

For complete documentation, see the [Apex Wiki](https://github.com/ApexMarkdown/apex/wiki).

Key documentation pages:

- [Citations and Bibliography](https://github.com/ApexMarkdown/apex/wiki/Citations) - Complete guide to citations and bibliographies
- [Command Line Options](https://github.com/ApexMarkdown/apex/wiki/Command-Line-Options) - All CLI flags explained
- [Syntax Reference](https://github.com/ApexMarkdown/apex/wiki/Syntax) - Complete syntax reference

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE]([LICENSE](https://github.com/ApexMarkdown/apex/blob/main/LICENSE)) file for details.
