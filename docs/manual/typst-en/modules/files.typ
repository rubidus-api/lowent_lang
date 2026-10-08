#import "../../typst-ko/lib.typ": *

= `files` --- file and directory streams where forgetting to close is a compile error <mod-files>

#modhead(file: "lib/file.low", layer: [L2 --- the outside world], caps: [`cap file_system`])

Opens, closes, reads and writes files and directories. Every op takes `cap file_system` first --- the right to reach the file system is handed to `main` once, and only ops that
receive it can touch files (#chref("capabilities"), #chref("io-files")). The builtin ops (`file_open`, `file_read`, `dir_open`, `file_type` and so on) only reach the kernel
and know only integer handles. Holding a raw fd lets a forgotten close go unnoticed, so this module wraps it in *a value that cannot be forgotten* (`owned handle`).

```lowent
let n result u64 files.file_error files.slurp fs "data.txt" buf .
```

#dtable(
  columns: 2,
  id: "mod-files-answers",
  caption: [Answer shapes differ honestly per op],
  [*op*], [*Answer and why*],
  [`open` · `open_dir`], [`result X file_error` --- opening has no "end". It opens or fails],
  [`read` · `read_dir`], [`result (option u64) file_error` --- `ok (some n)` read · *`ok none` the end (not failure)* · `error e` failure],
  [`write` · `seek_to` · `slurp`], [`result u64 file_error` --- no notion of "end"],
  [`type_of` · `link_type_of` · `is_dir` · `is_file` · `is_symlink`], [`option u64` · `bool` --- the leaf gives "absent" and "stat failed" as one bit. No distinction that does not exist is promised by type],
)

`file_error` variants are *only what can be distinguished* --- `open_failed`, `read_failed`, `write_failed`, `seek_failed`, `close_failed`, plus `buffer_too_small`, which the
library counts itself. The leaves give no errno, so the honest resolution stops at "which operation failed".

#dtable(
  columns: 2,
  id: "mod-files-ops",
  caption: [Ops of `files` --- all `effects io`, first argument `fs cap file_system`],
  [*op*], [*What it does*],
  [`handle` · `dir_handle`], [Unforgettable handles (one scalar `fd`) --- files and directories are different types],
  [`open fs path mode`], [Opens. mode `0` read · `1` write (truncate and create) · `2` append],
  [`read fs h buf` · `write fs h bytes`], [Reads one piece (short reads are normal) · bytes written],
  [`seek_to fs h off whence`], [Moves the cursor. whence `0` absolute · `1` from current · `2` from end. `seek_to fs h 0 2` is the file size],
  [`close fs h`], [Consumes `owned handle`, returns `result void file_error`],
  [`open_dir` · `read_dir fs h buf` · `close_dir`], [Directory listing. `read_dir` puts the next name in the buffer (`.` and `..` included)],
  [`type_of` · `link_type_of`], [`some 0` file · `some 1` directory · `some 2` other · `none` if absent (`link_type_of` does not follow links)],
  [`is_dir` · `is_file` · `is_symlink`], [Conveniences --- `false` if the path does not exist],
  [`slurp fs path buf`], [Whole file into the buffer. `error buffer_too_small` if it does not fit --- never truncates],
)

*Forgetting to close does not compile.* `close` and `close_dir` take `owned` and return `result`, so silently dropping a handle is `E-OWN-INCOMPLETE`. And closing really fails
(NFS, full disks) --- the `result` is not decoration. The handle parameter of `read` and `write` is non-owning, so reading many times and closing at the end is expressed as is.

*Failure and EOF are different answers.* `slurp` once just stopped its loop when a read failed, so callers could not tell complete reads from failures midway, and a line-counting
program reported a read failure as the success "0 lines". A fault injector showed it concretely. Now failure answers `error` (the handle is still closed --- releasing resources
is separate from the answer).

```lowent
proc main input fs cap file_system . input al cap allocator . input a cap args . output u8 . effects alloc io . do
  let dpath option slice u8 arg a 0 .
  guard is_some dpath else return 64 .
  let g option mut slice u8 alloc_bytes al capacity 256 .
  guard is_some g else return 70 .
  let buf mut slice u8 some_value g .
  let di result files.dir_handle files.file_error files.open_dir fs (some_value dpath) .
  guard is_ok di else return 71 .
  var dh owned files.dir_handle ok_value di .
  var total u64 0 .
  var going bool true .
  while going do
    let n result (option u64) files.file_error files.read_dir fs dh buf .
    if eq (is_ok n) false do set going false . end
    if is_ok n do
      let nvo option u64 ok_value n .
      if eq (is_some nvo) false do set going false . end
      if is_some nvo do set total (add total (some_value nvo)) . end
    end
  end
  let c result void files.file_error files.close_dir fs dh .
  guard is_ok c else return 72 .
  return (narrow u8 total) .
end
```

*Summoning failure yourself.* `LOW_HOST_FAULT="open:err"` (every open fails), `"read:err@2"` (the second read fails), `"read:short@1=4"` (the first read cut to 4 bytes),
`"close:err"`. VM and native use the same injector, so both back ends must answer the same.

#antipattern[Not closing on error paths][
  Once opened, early-return paths must close too. Missing it is `E-OWN-INCOMPLETE`, caught by the compiler.
]

#antipattern[Not filtering `read_dir` results][
  `.` and `..` come mixed in. Recursive traversal without filtering re-enters the same directory forever.
]

#antipattern[Treating `ok none` as failure · a short read as the end][
  `ok none` is the *fact* of the end. Conversely `ok (some n)` with fewer bytes than asked is not the end --- keep reading (as `slurp` does).
]

*Cautions.* Paths are `slice u8` byte strings with no encoding interpretation (UTF-8 by convention). Reading the fd with `field h fd` and calling leaves directly loses what the owned
discipline protects. If the buffer size is unknown, measure first with `seek_to fs h 0 2` or switch to a `read` loop. File system changes (making, removing, renaming) are stateless
path → bool leaves and are not wrapped --- pure forwarding would just be a synonym. Sockets belong to #modref("net")[`net`]. Input and output that can be interrupted while waiting (`wait`) does not exist yet --- it can only be built once a reactor stands. Why `seek_to`, not `seek` --- `seek` collided with
another name.
