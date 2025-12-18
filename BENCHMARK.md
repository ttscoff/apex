# Apex Markdown Processor - Performance Benchmark

## Test Document

- **File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md`
- **Lines:**      619
- **Words:**     2582
- **Size:**    17015 bytes

## Output Modes

| Mode | Iterations | Average (ms) | Min (ms) | Max (ms) | Throughput (words/sec) |
|------|------------|--------------|---------|---------|------------------------|
| Fragment Mode (default HTML output) | 50 | 10 | 10 | 13 | 258200.00 |
| Pretty-Print Mode (formatted HTML) | 50 | 10 | 10 | 14 | 258200.00 |
| Standalone Mode (complete HTML document) | 50 | 10 | 10 | 11 | 258200.00 |
| Standalone + Pretty (full features) | 50 | 10 | 10 | 13 | 258200.00 |

## Mode Comparison

| Mode | Iterations | Average (ms) | Min (ms) | Max (ms) | Throughput (words/sec) |
|------|------------|--------------|---------|---------|------------------------|
| CommonMark Mode (minimal, spec-compliant) | 50 | 8 | 6 | 76 | 0.00 |
| GFM Mode (GitHub Flavored Markdown) | 50 | 8 | 7 | 9 | 0.00 |
| MultiMarkdown Mode (metadata, footnotes, tables) | 50 | 9 | 8 | 11 | 0.00 |
| Kramdown Mode (attributes, definition lists) | 50 | 9 | 9 | 12 | 0.00 |
| Unified Mode (all features enabled) | 50 | 10 | 9 | 11 | 258200.00 |
| Default Mode (unified, all features) | 50 | 10 | 10 | 12 | 258200.00 |

---

*Benchmark Complete*
