# DWG modeler lifecycle fixtures

These eight fixtures are derived from the adjacent `modeler` fixtures using the pinned acadrust writer (`52034a673ff59bf6936ebf01ec794b07a97df246`). They cover R13, R14, R2000, R2004, R2007, R2010, R2013 and R2018.

Each retains the modeler/cache/material/history inputs and adds a `MODEL_DETAIL` block, a `Model-Sheet` layout on R2000+, and a control LINE with reactor references to a modeler, its history root when available, and an unrelated block. The source generator is `tests/upstream/src/dwg_lifecycle.rs`.

Regenerate from the repository root:

```sh
cargo run --locked --manifest-path tests/upstream/Cargo.toml -- generate-dwg-lifecycle tests/data/dwg tests/data/dwg/lifecycle
```

The native test also consumes existing history and surface fixtures to cover complete owned graph copying/deletion and seven surface families. It creates additional blank drawings in C++, without a Rust runtime dependency. Rust independently checks the resulting files, ownership, remapping, preserved objects, geometry and reactor cleanup.
