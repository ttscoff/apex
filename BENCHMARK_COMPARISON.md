# Markdown Processor Comparison Benchmark

## Available Tools

Found 7 tools:
- apex
- cmark-gfm
- cmark
- pandoc
- multimarkdown
- kramdown
- marked

## Processor Comparison

**File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md` (17017 bytes, 621 lines)

| Processor | Time (ms) | Relative |
|-----------|-----------|----------|
| apex | 89.00 | 1.00x |
| cmark-gfm | 20.00 | .22x |
| cmark | 20.00 | .22x |
| pandoc | 108.00 | 1.21x |
| multimarkdown | 23.00 | .25x |
| kramdown | 353.00 | 3.96x |
| marked | 102.00 | 1.14x |

## Apex Mode Comparison

**Test File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md`

| Mode | Time (ms) | Relative |
|------|-----------|----------|
| commonmark | 79.00 | 1.00x |
| gfm | 82.00 | 1.03x |
| mmd | 93.00 | 1.17x |
| kramdown | 87.00 | 1.10x |
| unified | 96.00 | 1.21x |
| default (unified) | 90.00 | 1.13x |

## Apex Feature Overhead

| Features | Time (ms) |
|----------|-----------|
| CommonMark (minimal) | 78.00 |
| + GFM tables/strikethrough | 87.00 |
| + All Apex features | 91.00 |
| + Pretty printing | 92.00 |
| + Standalone document | 94.00 |

---

*Benchmark Complete*
