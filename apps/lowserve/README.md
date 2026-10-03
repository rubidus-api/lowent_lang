# `lowserve` — serve a folder read-only over HTTP, on the loopback interface

```sh
lowserve ./site 8080
```

`lowserve` answers `GET` and `HEAD` for the files under one folder, on `127.0.0.1` only. It is
meant for looking at a built site or a folder of documents on your own machine.

It decodes the path and then refuses what would leave the folder: a `..` segment (400) and any
path that passes through a symbolic link (403). A directory is served through its `index.html`;
there are no directory listings. One request per connection, one connection at a time.

The outside oracle is Python's `http.server` serving the same folder: `curl` gets the same
status and the same bytes for every file.

**Not for a public network**: the network layer has no read timeout yet, so a client that stops
in the middle of its request holds the server.

Details: [`doc/manual.md`](doc/manual.md).
