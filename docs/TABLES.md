# Tables

Tables contain rows, which contain cells. Each `TableCell` has one stable
node ID, a header/data flag, and `col_span` and `row_span` counts (both 1 by
default). A cell with a span remains a single piece of content.

`build_table_grid` derives occupied slots and cell positions from those
counts. Layout measures the cells at their combined column widths, determines
row heights, and draws borders only at boundaries between cells. A spanning
header's fill covers its whole rectangle. A complete row of unspanned cells,
when available, supplies the column weights and resize handles. Tables
without spans retain their existing layout path.

In HTML, `colspan` and `rowspan` on `td` and `th` map to these counts.
Invalid values become 1; the HTML limits are 1000 columns and 65534 rows.
HTML `rowspan="0"` extends to the end of its `thead`, `tbody`, `tfoot`, or
implicit row group and is resolved to a count on import. A positive row span
is also limited to its HTML row group. Row groups themselves are not stored.

The compact pipe-table syntax represents unspanned cells. Use fenced rows
and cells for spans:

```text
:::table
:::row
:::th colspan=2
Heading over both columns
:::
:::
:::row
:::th rowspan=2
Shared heading
:::
:::cell
First value
:::
:::
:::row
:::cell
Second value
:::
:::
:::
```

The equivalent S-expression attributes are `:colspan 2` and `:rowspan 2`
on `th` or `td`. Export uses fenced rows when a table has spans, so the
counts survive a Markdown or S-expression round trip. See
`examples/tour.md` for a rendered example.

Pressing Enter in a table cell inserts a row after that cell's row. A cell
whose row span crosses the insertion point grows to cover the new row;
new cells are created only in the uncovered columns.
