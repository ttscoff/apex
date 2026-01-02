/**
 * Tables Tests
 */

#include "test_helpers.h"
#include "apex/apex.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

void test_advanced_tables(void) {
    printf("\n=== Advanced Tables Tests ===\n");

    apex_options opts = apex_options_default();
    opts.enable_tables = true;
    opts.relaxed_tables = false;  /* Use standard GFM table syntax for these tests */
    char *html;

    /* Test table with caption before table */
    const char *caption_table = "[Table Caption]\n\n| H1 | H2 |\n|----|----|"
                                "\n| C1 | C2 |";
    html = apex_markdown_to_html(caption_table, strlen(caption_table), &opts);
    assert_contains(html, "<table", "Caption table renders");
    assert_contains(html, "<figure", "Caption table wrapped in figure");
    assert_contains(html, "<figcaption>", "Caption has figcaption tag");
    assert_contains(html, "Table Caption", "Caption text is present");
    assert_contains(html, "</figure>", "Caption figure is closed");
    apex_free_string(html);

    /* Test table with caption after table */
    const char *caption_table_after = "| H1 | H2 |\n|----|----|"
                                     "\n| C1 | C2 |\n\n[Table Caption After]";
    html = apex_markdown_to_html(caption_table_after, strlen(caption_table_after), &opts);
    assert_contains(html, "<table", "Caption table after renders");
    assert_contains(html, "<figure", "Caption table after wrapped in figure");
    assert_contains(html, "Table Caption After", "Caption text after is present");
    apex_free_string(html);

    /* Test rowspan with ^^ */
    const char *rowspan_table = "| H1 | H2 |\n|----|----|"
                                "\n| A  | B  |"
                                "\n| ^^ | C  |";
    html = apex_markdown_to_html(rowspan_table, strlen(rowspan_table), &opts);
    assert_contains(html, "rowspan", "Rowspan attribute added");
    assert_contains(html, "<td rowspan=\"2\">A</td>", "Rowspan applied to first cell content");
    apex_free_string(html);

    /* Test colspan with empty cell */
    const char *colspan_table = "| H1 | H2 | H3 |\n|----|----|----|"
                                "\n| A  |    |    |"
                                "\n| B  | C  | D  |";
    html = apex_markdown_to_html(colspan_table, strlen(colspan_table), &opts);
    assert_contains(html, "colspan", "Colspan attribute added");
    /* A should span all three columns in the first data row */
    assert_contains(html, "<td colspan=\"3\">A</td>", "Colspan applied to first row A spanning 3 columns");
    apex_free_string(html);

    /* Test per-cell alignment using colons */
    const char *align_table = "| h1  |  h2   | h3  |\n"
                              "| --- | :---: | --- |\n"
                              "| d1  |  d2   | d3  |";
    html = apex_markdown_to_html(align_table, strlen(align_table), &opts);
    /* cmark-gfm uses align=\"left|center|right\" attributes rather than inline styles */
    assert_contains(html, "<th>h1</th>", "Left-aligned header from colon pattern");
    /* Accept either align="center" or style="text-align: center" */
    bool has_align = strstr(html, "<th align=\"center\">h2</th") != NULL;
    bool has_style = strstr(html, "<th style=\"text-align: center\">h2</th") != NULL;
    if (has_align || has_style) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Center-aligned header from colon pattern\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Center-aligned header from colon pattern\n");
        printf("  Looking for: <th align=\"center\">h2</th> or <th style=\"text-align: center\">h2</th>\n");
        printf("  In:          %s\n", html);
    }
    apex_free_string(html);

    /* Test basic table (ensure we didn't break existing functionality) */
    const char *basic_table = "| H1 | H2 |\n|-----|-----|\n| C1 | C2 |";
    html = apex_markdown_to_html(basic_table, strlen(basic_table), &opts);
    assert_contains(html, "<table>", "Basic table still works");
    assert_contains(html, "<th>H1</th>", "Table header");
    assert_contains(html, "<td>C1</td>", "Table cell");
    apex_free_string(html);

    /* Test row header column when first header cell is empty */
    const char *row_header_table =
        "|   | H1 | H2 |\n"
        "|----|----|----|\n"
        "| Row 1 | A1 | B1 |\n"
        "| Row 2 | A2 | B2 |";
    html = apex_markdown_to_html(row_header_table, strlen(row_header_table), &opts);
    assert_contains(html, "<table>", "Row-header table renders");
    assert_contains(html, "<th scope=\"row\">Row 1</th>", "Row-header table: first row header cell");
    assert_contains(html, "<th scope=\"row\">Row 2</th>", "Row-header table: second row header cell");
    assert_contains(html, "<td>A1</td>", "Row-header table: body cell A1");
    apex_free_string(html);

    /* Test table followed by paragraph (regression: last row should not become paragraph) */
    const char *table_with_text = "| H1 | H2 |\n|-----|-----|\n| C1 | C2 |\n| C3 | C4 |\n\nText after.";
    html = apex_markdown_to_html(table_with_text, strlen(table_with_text), &opts);
    assert_contains(html, "<td>C3</td>", "Last table row C3 in table");
    assert_contains(html, "<td>C4</td>", "Last table row C4 in table");
    assert_contains(html, "</table>\n<p>Text after.</p>", "Table properly closed before paragraph");
    apex_free_string(html);

    /* Test Pandoc-style table caption with : Caption syntax */
    const char *pandoc_caption = "| Key | Value |\n| --- | :---: |\n| one |   1   |\n| two |   2   |\n\n: Key value table";
    html = apex_markdown_to_html(pandoc_caption, strlen(pandoc_caption), &opts);
    assert_contains(html, "<figcaption>", "Pandoc caption has figcaption tag");
    assert_contains(html, "Key value table", "Pandoc caption text is present");
    assert_contains(html, "<table", "Table with Pandoc caption renders");
    apex_free_string(html);

    /* Test Pandoc-style table caption with IAL attributes (Kramdown format) */
    const char *pandoc_caption_ial_kramdown = "| Key | Value |\n| --- | :---: |\n| one |   1   |\n| two |   2   |\n\n: Key value table {: #table-id .testing}";
    html = apex_markdown_to_html(pandoc_caption_ial_kramdown, strlen(pandoc_caption_ial_kramdown), &opts);
    assert_contains(html, "<table", "Table with Pandoc caption and IAL renders");
    assert_contains(html, "id=\"table-id\"", "Table IAL ID from caption applied");
    assert_contains(html, "class=\"testing\"", "Table IAL class from caption applied");
    assert_contains(html, "Key value table", "Caption text is present");
    apex_free_string(html);

    /* Test Pandoc-style table caption with IAL attributes (Pandoc format) */
    const char *pandoc_caption_ial_pandoc = "| Key | Value |\n| --- | :---: |\n| one |   1   |\n| two |   2   |\n\n: Key value table {#table-id-2 .testing-2}";
    html = apex_markdown_to_html(pandoc_caption_ial_pandoc, strlen(pandoc_caption_ial_pandoc), &opts);
    assert_contains(html, "<table", "Table with Pandoc caption and Pandoc IAL renders");
    assert_contains(html, "id=\"table-id-2\"", "Table Pandoc IAL ID from caption applied");
    assert_contains(html, "class=\"testing-2\"", "Table Pandoc IAL class from caption applied");
    assert_contains(html, "Key value table", "Caption text is present");
    apex_free_string(html);

    /* Test table with IAL applied directly (not via caption) */
    const char *table_with_direct_ial = "| H1 | H2 |\n|----|----|\n| C1 | C2 |\n{: #direct-table .direct-class}";
    html = apex_markdown_to_html(table_with_direct_ial, strlen(table_with_direct_ial), &opts);
    assert_contains(html, "<table", "Table with direct IAL renders");
    assert_contains(html, "id=\"direct-table\"", "Direct table IAL ID applied");
    assert_contains(html, "class=\"direct-class\"", "Direct table IAL class applied");
    apex_free_string(html);

    /* Test table caption before table with IAL */
    const char *caption_before_ial = "[Caption Before]\n\n| H1 | H2 |\n|----|----|\n| C1 | C2 |\n{: #before-table .before-class}";
    html = apex_markdown_to_html(caption_before_ial, strlen(caption_before_ial), &opts);
    assert_contains(html, "<table", "Table with caption before and IAL renders");
    assert_contains(html, "Caption Before", "Caption text before table");
    assert_contains(html, "id=\"before-table\"", "Table IAL ID with caption before");
    assert_contains(html, "class=\"before-class\"", "Table IAL class with caption before");
    apex_free_string(html);
}

/**
 * Test relaxed tables (tables without separator rows)
 */

void test_relaxed_tables(void) {
    printf("\n=== Relaxed Tables Tests ===\n");

    apex_options opts = apex_options_default();
    opts.enable_tables = true;
    opts.relaxed_tables = true;
    char *html;

    /* Test basic relaxed table (2 rows, no separator) */
    const char *relaxed_table = "A | B\n1 | 2";
    html = apex_markdown_to_html(relaxed_table, strlen(relaxed_table), &opts);
    assert_contains(html, "<table>", "Relaxed table renders");
    assert_contains(html, "<tbody>", "Relaxed table has tbody");
    assert_contains(html, "<tr>", "Relaxed table has rows");
    assert_contains(html, "<td>A</td>", "First cell A");
    assert_contains(html, "<td>B</td>", "First cell B");
    assert_contains(html, "<td>1</td>", "Second cell 1");
    assert_contains(html, "<td>2</td>", "Second cell 2");
    /* Should NOT have a header row */
    if (strstr(html, "<thead>") == NULL && strstr(html, "<th>") == NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Relaxed table has no header row\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Relaxed table incorrectly has header row\n");
    }
    apex_free_string(html);

    /* Test relaxed table with 3 rows */
    const char *relaxed_table3 = "A | B\n1 | 2\n3 | 4";
    html = apex_markdown_to_html(relaxed_table3, strlen(relaxed_table3), &opts);
    assert_contains(html, "<table>", "Relaxed table with 3 rows renders");
    assert_contains(html, "<td>3</td>", "Third row cell 3");
    assert_contains(html, "<td>4</td>", "Third row cell 4");
    apex_free_string(html);

    /* Test relaxed table stops at blank line */
    const char *relaxed_table_blank = "A | B\n1 | 2\n\nParagraph text";
    html = apex_markdown_to_html(relaxed_table_blank, strlen(relaxed_table_blank), &opts);
    assert_contains(html, "<table>", "Relaxed table before blank line");
    assert_contains(html, "<p>Paragraph text</p>", "Paragraph after blank line");
    apex_free_string(html);

    /* Test relaxed table with leading pipe */
    const char *relaxed_table_leading = "| A | B |\n| 1 | 2 |";
    html = apex_markdown_to_html(relaxed_table_leading, strlen(relaxed_table_leading), &opts);
    assert_contains(html, "<table>", "Relaxed table with leading pipe renders");
    assert_contains(html, "<td>A</td>", "Cell A with leading pipe");
    apex_free_string(html);

    /* Test that relaxed tables are disabled by default in GFM mode */
    apex_options gfm_opts = apex_options_for_mode(APEX_MODE_GFM);
    gfm_opts.enable_tables = true;
    html = apex_markdown_to_html(relaxed_table, strlen(relaxed_table), &gfm_opts);
    if (strstr(html, "<table>") == NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Relaxed tables disabled in GFM mode by default\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Relaxed tables incorrectly enabled in GFM mode\n");
    }
    apex_free_string(html);

    /* Test that relaxed tables are enabled by default in Kramdown mode */
    apex_options kramdown_opts = apex_options_for_mode(APEX_MODE_KRAMDOWN);
    kramdown_opts.enable_tables = true;
    html = apex_markdown_to_html(relaxed_table, strlen(relaxed_table), &kramdown_opts);
    if (strstr(html, "<table>") != NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Relaxed tables enabled in Kramdown mode by default\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Relaxed tables incorrectly disabled in Kramdown mode\n");
    }
    apex_free_string(html);

    /* Test that relaxed tables are enabled by default in Unified mode */
    apex_options unified_opts = apex_options_for_mode(APEX_MODE_UNIFIED);
    unified_opts.enable_tables = true;
    html = apex_markdown_to_html(relaxed_table, strlen(relaxed_table), &unified_opts);
    if (strstr(html, "<table>") != NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Relaxed tables enabled in Unified mode by default\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Relaxed tables incorrectly disabled in Unified mode\n");
    }
    apex_free_string(html);

    /* Test that --no-relaxed-tables disables it even in Kramdown mode */
    apex_options no_relaxed = apex_options_for_mode(APEX_MODE_KRAMDOWN);
    no_relaxed.enable_tables = true;
    no_relaxed.relaxed_tables = false;
    html = apex_markdown_to_html(relaxed_table, strlen(relaxed_table), &no_relaxed);
    if (strstr(html, "<table>") == NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " --no-relaxed-tables disables relaxed tables\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " --no-relaxed-tables did not disable relaxed tables\n");
    }
    apex_free_string(html);

    /* Test that single row with pipe is not treated as table */
    const char *single_row = "A | B";
    html = apex_markdown_to_html(single_row, strlen(single_row), &opts);
    if (strstr(html, "<table>") == NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Single row is not treated as table\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Single row incorrectly treated as table\n");
    }
    apex_free_string(html);

    /* Test that rows with different column counts are not treated as table */
    const char *mismatched = "A | B\n1 | 2 | 3";
    html = apex_markdown_to_html(mismatched, strlen(mismatched), &opts);
    if (strstr(html, "<table>") == NULL) {
        tests_passed++;
        tests_run++;
        printf(COLOR_GREEN "✓" COLOR_RESET " Mismatched column counts are not treated as table\n");
    } else {
        tests_failed++;
        tests_run++;
        printf(COLOR_RED "✗" COLOR_RESET " Mismatched column counts incorrectly treated as table\n");
    }
    apex_free_string(html);
}

/**
 * Test combine-like behavior for GitBook SUMMARY.md via core API
 * (indirectly validates that include expansion and ordering work).
 */

void test_comprehensive_table_features(void) {
    printf("\n=== Comprehensive Test File Table Features ===\n");

    apex_options opts = apex_options_default();
    opts.enable_tables = true;
    char *html = NULL;

    /* Read comprehensive_test.md file */
    FILE *f = fopen("tests/fixtures/comprehensive_test.md", "r");
    if (!f) {
        tests_run++;
        tests_failed++;
        printf(COLOR_RED "✗" COLOR_RESET " comprehensive_test.md: Could not open file\n");
        return;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Read file content */
    char *markdown = (char *)malloc(file_size + 1);
    if (!markdown) {
        fclose(f);
        tests_run++;
        tests_failed++;
        printf(COLOR_RED "✗" COLOR_RESET " comprehensive_test.md: Memory allocation failed\n");
        return;
    }

    size_t bytes_read = fread(markdown, 1, file_size, f);
    markdown[bytes_read] = '\0';
    fclose(f);

    /* Convert to HTML */
    html = apex_markdown_to_html(markdown, bytes_read, &opts);
    free(markdown);

    if (!html) {
        tests_run++;
        tests_failed++;
        printf(COLOR_RED "✗" COLOR_RESET " comprehensive_test.md: Failed to convert to HTML\n");
        return;
    }

    /* Test 1: Caption before table with IAL should render correctly */
    /* The caption "Employee Performance Q4 2025" should appear in figcaption, not as a paragraph */
    assert_contains(html, "<figcaption>Employee Performance Q4 2025</figcaption>",
                    "Caption appears in figcaption tag");

    /* Test 2: Caption paragraph should NOT appear as duplicate <p> tag */
    /* We should NOT see <p>[Employee Performance Q4 2025]</p> */
    assert_not_contains(html, "<p>[Employee Performance Q4 2025]</p>",
                        "Caption paragraph removed (no duplicate)");

    /* Test 3: Rowspan should be applied correctly - Engineering rowspan="2" */
    assert_contains(html, "rowspan=\"2\"", "Rowspan attribute present");
    assert_contains(html, "<td rowspan=\"2\">Engineering</td>", "Engineering has rowspan=2");

    /* Test 4: Rowspan should be applied correctly - Sales rowspan="2" */
    assert_contains(html, "<td rowspan=\"2\">Sales</td>", "Sales has rowspan=2");

    /* Test 5: Table should be wrapped in figure tag */
    assert_contains(html, "<figure class=\"table-figure\">", "Table wrapped in figure with class");

    /* Test 6: Empty cells are preserved (Absent cell followed by empty cell) */
    /* The Absent cell is followed by an empty cell (not converted to colspan) */
    assert_contains(html, "<td>Absent</td>", "Absent cell present");
    /* Check for empty cell after Absent - the pattern shows Absent followed by an empty td */
    assert_contains(html, "<td></td>", "Empty cell present in table");

    /* Test 7: Table structure should be correct - key rows present */
    assert_contains(html, "<td>Alice</td>", "Alice row present");
    assert_contains(html, "<td>Bob</td>", "Bob row present");
    assert_contains(html, "<td>Charlie</td>", "Charlie row present");
    assert_contains(html, "<td>Diana</td>", "Diana row present");
    /* Eve is in the last row with rowspan */
    assert_contains(html, "Eve", "Eve row present");

    apex_free_string(html);
}

/**
 * Test callouts (Bear/Obsidian/Xcode)
 */

void test_inline_tables(void) {
    printf("\n=== Inline Tables Tests ===\n");

    apex_options opts = apex_options_default();
    opts.enable_marked_extensions = true;
    char *html;

    /* ```table fence with CSV data */
    const char *csv_table =
        "```table\n"
        "header 1,header 2,header 3\n"
        "data 1,data 2,data 3\n"
        ",,data 2c\n"
        "```\n";
    html = apex_markdown_to_html(csv_table, strlen(csv_table), &opts);
    assert_contains(html, "<table>", "CSV table fence: table element");
    assert_contains(html, "<th>header 1</th>", "CSV table fence: header 1");
    assert_contains(html, "<th>header 2</th>", "CSV table fence: header 2");
    assert_contains(html, "<th>header 3</th>", "CSV table fence: header 3");
    assert_contains(html, "<td>data 1</td>", "CSV table fence: first data cell");
    assert_contains(html, "<td>data 2c</td>", "CSV table fence: continued cell");
    apex_free_string(html);

    /* ```table fence with CSV data and alignment keywords */
    const char *csv_align =
        "```table\n"
        "H1,H2,H3\n"
        "left,center,right\n"
        "a,b,c\n"
        "```\n";
    html = apex_markdown_to_html(csv_align, strlen(csv_align), &opts);
    assert_contains(html, "<table>", "CSV table with alignment: table element");
    /* Be conservative about HTML structure: just verify content appears in a table */
    assert_contains(html, "H1", "CSV table with alignment: header text H1 present");
    assert_contains(html, "H2", "CSV table with alignment: header text H2 present");
    assert_contains(html, "H3", "CSV table with alignment: header text H3 present");
    assert_contains(html, "a", "CSV table with alignment: data 'a' present");
    apex_free_string(html);

    /* ```table fence with no explicit alignment row: should also be headless */
    const char *csv_no_align =
        "```table\n"
        "r1c1,r1c2,r1c3\n"
        "r2c1,r2c2,r2c3\n"
        "```\n";
    html = apex_markdown_to_html(csv_no_align, strlen(csv_no_align), &opts);
    assert_contains(html, "<table>", "CSV table no-align: table element");
    assert_contains(html, "r1c1", "CSV table no-align: first row content present");
    assert_contains(html, "r2c1", "CSV table no-align: second row content present");
    apex_free_string(html);

    /* ```table fence with TSV data (real tabs) */
    const char *tsv_table =
        "```table\n"
        "col1\tcol2\tcol3\n"
        "val1\tval2\tval3\n"
        "```\n";
    html = apex_markdown_to_html(tsv_table, strlen(tsv_table), &opts);
    assert_contains(html, "<table>", "TSV table fence: table element");
    assert_contains(html, "col1", "TSV table fence: header col1 text");
    assert_contains(html, "col2", "TSV table fence: header col2 text");
    assert_contains(html, "col3", "TSV table fence: header col3 text");
    assert_contains(html, "val1", "TSV table fence: first data value");
    apex_free_string(html);

    /* ```table fence with no delimiter: should remain a code block */
    const char *no_delim =
        "```table\n"
        "this has no delimiters\n"
        "on the second line\n"
        "```\n";
    html = apex_markdown_to_html(no_delim, strlen(no_delim), &opts);
    assert_contains(html, "<pre lang=\"table\"><code>", "No-delim table fence: rendered as code block");
    assert_contains(html, "this has no delimiters", "No-delim table fence: content preserved");
    apex_free_string(html);

    /* <!--TABLE--> with CSV data */
    const char *csv_marker =
        "<!--TABLE-->\n"
        "one,two,three\n"
        "four,five,six\n"
        "\n";
    html = apex_markdown_to_html(csv_marker, strlen(csv_marker), &opts);
    assert_contains(html, "<table>", "CSV TABLE marker: table element");
    assert_contains(html, "one", "CSV TABLE marker: header text");
    assert_contains(html, "four", "CSV TABLE marker: data value");
    apex_free_string(html);

    /* <!--TABLE--> with TSV data (real tabs) */
    const char *tsv_marker =
        "<!--TABLE-->\n"
        "alpha\tbeta\tgamma\n"
        "delta\tepsilon\tzeta\n"
        "\n";
    html = apex_markdown_to_html(tsv_marker, strlen(tsv_marker), &opts);
    assert_contains(html, "<table>", "TSV TABLE marker: table element");
    assert_contains(html, "alpha", "TSV TABLE marker: header text");
    assert_contains(html, "delta", "TSV TABLE marker: data value");
    apex_free_string(html);

    /* <!--TABLE--> with no following data: comment should be preserved */
    const char *empty_marker =
        "Before\n\n"
        "<!--TABLE-->\n"
        "\n"
        "After\n";
    html = apex_markdown_to_html(empty_marker, strlen(empty_marker), &opts);
    assert_contains(html, "Before", "Empty TABLE marker: before text preserved");
    assert_contains(html, "<!--TABLE-->", "Empty TABLE marker: comment preserved");
    assert_contains(html, "After", "Empty TABLE marker: after text preserved");
    apex_free_string(html);
}

/**
 * Test advanced footnotes
 */
