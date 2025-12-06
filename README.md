[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

# Apex

Apex is a unified Markdown processor that combines the best features from CommonMark, GitHub Flavored Markdown (GFM), MultiMarkdown, Kramdown, and Marked. One processor to rule them all.

## Features

- **Multiple compatibility modes**: CommonMark, GFM, MultiMarkdown, Kramdown, and Unified (all features)
- **Rich extensions**: Tables, footnotes, definition lists, smart typography, math, wiki links, task lists, and more
- **Flexible output**: Pretty-printed HTML, standalone documents, custom styling
- **Header ID generation**: Automatic or manual header IDs with multiple format options
- **Relaxed tables**: Support for tables without separator rows (Kramdown-style)

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
cmake -S . -B build
cmake --build build
```

The `apex` binary will be in the `build/` directory.

**Note:** You must run `cmake -S . -B build` first to configure the project and generate the build cache. Then use `cmake --build build` to compile. If you get a "could not load cache" error, it means the configuration step hasn't been run yet.

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

## Documentation

For complete documentation, see the [Apex Wiki](https://github.com/ttscoff/apex/wiki).

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
