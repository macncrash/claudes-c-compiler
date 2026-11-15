# Real-program tests

`run.py` builds real C libraries, each with a driver from `drivers/` that
exercises it and prints deterministic results, once with ccc and once with
a reference compiler (clang), and compares what the two programs print.

```sh
python3 tests/projects/run.py --ccc target/release/ccc
python3 tests/projects/run.py --ccc target/release/ccc --only sqlite,zstd
```

| project | library | driver |
|---|---|---|
| `zstd` | zstd (lib/common, compress, decompress) | one-shot at levels -5 to 19 and streaming with checksums, 4 kinds of data, 0 B to 300 KB; sizes, hashes of the compressed bytes, round trips |
| `lz4` | lz4, lz4hc, lz4frame, xxhash | default, accelerated, HC 1 to 12 and frame (block and content checksums); sizes, hashes, round trips |
| `blake3` | BLAKE3 portable C | hash, keyed hash, derived key, extended and seeked output of the official test-vector inputs, whole and in odd chunks |
| `blake3-neon` | BLAKE3 with its NEON code (arm64) | the same |
| `onig` | Oniguruma (48 files, with the `config.h` its crate's build writes) | ~40 patterns in Ruby, Perl and POSIX syntax over several subjects: backreferences, named groups, lookaround, lazy and possessive quantifiers, Unicode properties and case folding, Shift_JIS and ISO-8859-1; every match and group |
| `ring` | ring's portable C (8 files, `OPENSSL_NO_ASM`) | X25519 (RFC 7748 vectors, the 1000-iteration chain, public keys), Poly1305 (RFC 8439), AES-128/256 without hardware instructions (FIPS-197) and in CTR mode, P-256 base and variable point multiplication, doubling, addition and Montgomery multiplication on random inputs |
| `sqlite` | the SQLite amalgamation, with FTS5 | tables, indexes, joins, aggregates, window functions, recursive CTEs (the Mandelbrot query), string, date, JSON and printf functions, floating point, full-text search |

The library sources are not in this repository. Each project is looked up
under `--src-root`, by default the cargo registry
(`~/.cargo/registry/src/*`), where the `zstd-sys`, `lz4-sys`, `blake3`,
`onig_sys`, `ring` and `libsqlite3-sys` crates keep the C they build; the newest
version found is
used, and a project whose sources are missing is skipped. Both builds are
linked by the reference compiler, so the comparison is of the code each
compiler generated.

A project passes only if the reference build runs successfully and prints
something, and the ccc build prints exactly the same.
