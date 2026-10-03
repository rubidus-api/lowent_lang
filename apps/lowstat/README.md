# `lowstat` — filter, count, sum and group a CSV file in one pass

```sh
lowstat access.csv --where status=500 --count
lowstat access.csv --group-by path --count --top 20
lowstat access.csv --group-by service --sum bytes
```

`lowstat` reads a CSV file (or standard input) once, keeps only the rows that match one
`--where` condition, and counts them or sums one integer column — overall, or per value of
one `--group-by` column. The answer is CSV, in a fixed order, so a program can compare it.

Memory follows the number of groups, not the size of the input: a 70 MB file with 1,000
groups runs in about 11 MiB. Every limit is reported, never silently truncated.

The outside oracle is Python's `csv` module: the same aggregation written independently
must print the same bytes.

Details: [`doc/manual.md`](doc/manual.md).
