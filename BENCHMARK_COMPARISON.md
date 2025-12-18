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

**File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md` (17015 bytes, 619 lines)

| Processor | Time (ms) | Relative |
|-----------|-----------|----------|
| apex | 21.00 | 1.00x |
| cmark-gfm | 18.00 | .85x |
| cmark | 17.00 | .80x |
| pandoc | 107.00 | 5.09x |
| multimarkdown | 17.00 | .80x |
| kramdown | 333.00 | 15.85x |
| marked | 102.00 | 4.85x |

## Apex Mode Comparison

**Test File:** `/Users/ttscoff/Desktop/Code/apex/tests/comprehensive_test.md`

| Mode | Time (ms) | Relative |
|------|-----------|----------|
| commonmark | 18.00 | 1.00x |
| gfm | 19.00 | 1.05x |
| mmd | 20.00 | 1.11x |
| kramdown | 20.00 | 1.11x |
| unified | 21.00 | 1.16x |
| default (unified) | 21.00 | 1.16x |

## Apex Feature Overhead

| Features | Time (ms) |
|----------|-----------|
| CommonMark (minimal) | 17.00 |
| + GFM tables/strikethrough | 19.00 |
| + All Apex features | 21.00 |
| + Pretty printing | 21.00 |
| + Standalone document | 21.00 |

---

*Benchmark Complete*
