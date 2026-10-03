# `lowpack` — create, list, check and safely extract ustar archives

```sh
lowpack create site.tar public/
lowpack list site.tar
lowpack check site.tar
lowpack extract site.tar /srv/www
```

`lowpack` handles the plain, uncompressed `ustar` format for regular files and directories.
Archives it creates are reproducible: owners and times are zero and modes are fixed, so the
same tree always gives the same bytes.

Extraction is the part that has to be careful. `lowpack extract` refuses absolute paths,
`..`, paths that pass through a symbolic link that already exists in the destination, and
existing files; it removes a file it could not finish writing. Links, devices and PAX or GNU
extension headers are refused rather than guessed at.

The outside oracle is GNU tar: `tar -tf` and `tar -xf` agree with what `lowpack` writes, and
`lowpack` lists and extracts archives made by `tar --format=ustar` to the same bytes.

Details: [`doc/manual.md`](doc/manual.md).
