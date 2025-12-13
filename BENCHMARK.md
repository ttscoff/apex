# Apex Markdown Processor - Performance Benchmark

## Test Document

- **File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md`
- **Lines:**      621
- **Words:**     2582
- **Size:**    17017 bytes

## Output Modes

| Mode | Iterations | Average (ms) | Min (ms) | Max (ms) | Throughput (words/sec) |
|------|------------|--------------|---------|---------|------------------------|
| Fragment Mode (default HTML output) | 50 | 63 | 56 | 97 | 43033.33 |
| Pretty-Print Mode (formatted HTML) | 50 | 70 | 60 | 121 | 36885.71 |
| Standalone Mode (complete HTML document) | 50 | 73 | 60 | 134 | 36885.71 |
| Standalone + Pretty (full features) | 50 | 75 | 67 | 122 | 36885.71 |

## Mode Comparison

| Mode | Iterations | Average (ms) | Min (ms) | Max (ms) | Throughput (words/sec) |
|------|------------|--------------|---------|---------|------------------------|
| CommonMark Mode (minimal, spec-compliant) | 50 | 60 | 52 | 90 | 43033.33 |
| GFM Mode (GitHub Flavored Markdown) | 50 | 66 | 57 | 98 | 43033.33 |
| MultiMarkdown Mode (metadata, footnotes, tables) | 50 | 71 | 63 | 105 | 36885.71 |
| Kramdown Mode (attributes, definition lists) | 50 | 72 | 64 | 113 | 36885.71 |
| Unified Mode (all features enabled) | 50 | 77 | 69 | 103 | 36885.71 |
| Default Mode (unified, all features) | 50 | 77 | 68 | 126 | 36885.71 |

---

*Benchmark Complete*
