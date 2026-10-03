# `lowindex` — a word index for a folder of text files

```sh
lowindex build notes.lwx ~/notes
lowindex search notes.lwx interval analysis
```

`build` reads every text file under a folder once and writes an index file. `search` answers
from the index alone: the files that contain **every** word of the query, one path per line,
in byte order. On 1,433 files (66 MB) building takes about 2 seconds and a query about 50 ms.

A word is a run of ASCII letters and digits or of bytes from 0x80 up (so UTF-8 letters stay
whole), with ASCII letters folded to lower case. There is no stemming, ranking or Unicode
normalisation — the rule is small enough to check: an independent Python implementation of the
same rule, scanning the files directly, gives the same answers.

`build` writes the new index beside the old one and renames it into place, so an interrupted
build leaves the previous index readable. The index carries a version, its length and a checksum;
a damaged index is refused, not half-read.

Details: [`doc/manual.md`](doc/manual.md).
