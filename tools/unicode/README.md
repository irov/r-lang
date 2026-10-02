# Unicode table regeneration

The R 0.1 lexer uses Unicode 17.0.0 `XID_Start`, `XID_Continue`, canonical
decomposition, canonical combining class and composition data. The generated C tables
are checked in, so the normal build has no Unicode library or Python dependency.

Regenerate from the official UCD archive:

```sh
python3 tools/generate_unicode_tables.py \
  --ucd-zip /path/to/UCD.zip \
  --header compiler/source/unicode_data.h \
  --source compiler/source/unicode_data.c
```

The generator rejects an archive whose SHA-256 differs from the value recorded in
`tools/toolchain.lock`.

