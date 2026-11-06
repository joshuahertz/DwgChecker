# Local LibreDWG patches

`scripts/build_dwgread.sh` applies every `*.patch` here, in file-name order, with
`patch -p1` against the pinned release tarball (`third_party/libredwg.version`).
Each patch must apply cleanly or the build fails. Changing a patch changes the CI
cache key, so the binary is rebuilt.

| Patch | Why |
|---|---|
| `0001-json-emit-minsert-num-cols-rows.patch` | The JSON writer drops every field whose name starts with `num_` (array counts). For `MINSERT` that loses `num_cols`/`num_rows`, so the array cannot be exploded. The patch emits those two fields. Without it, the loader reports MINSERT as Unsupported. |
| `0002-json-roundtrip-doubles.patch` | The JSON writer prints doubles with `%.14f`, which is not round-trip precision. Bit-exact geometry (duplicate detection signal 6) needs the exact bits, so the patch prints `%.17g`. Without it, "bit-exact" means "equal to 14 decimal places". |

Both were checked with `git apply --check` and `patch --dry-run -p1` against
`libredwg-0.14.tar.xz` (SHA-256 in the version file).

`dwgread --version` still prints `dwgread 0.14` after patching, so the app also
logs the SHA-256 of the binary it runs.
