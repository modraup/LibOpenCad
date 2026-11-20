# Embedded history profile fixtures

Generated from the `history-author` fixtures by `tests/upstream/src/history_profiles.rs`
using local acadrust `52034a673ff59bf6936ebf01ec794b07a97df246`. The relevant codec is
unchanged from pinned revision `99d5f48800b1220f7a0a3f12fa46656cde995f3d`.

Each R2007/R2010/R2013/R2018 drawing includes sweep, extrusion, revolve and loft
history objects, typed curves, an inline SAB REGION with wire/silhouette/material/
revision/secondary-ACIS data, unknown/truncated bodies, and an R2018 metadata path.
ASCII and binary input fixtures are checked separately; native edits produce both
output encodings, and Rust compares them against its own equivalent edits.

Regenerate with:

```sh
cargo run --locked --manifest-path tests/upstream/Cargo.toml -- generate-history-profiles tests/data/history-author tests/data/history-profiles
```

SPDX-License-Identifier: MPL-2.0
