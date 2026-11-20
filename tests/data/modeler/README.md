# Modeler interoperability fixtures

Reference: acadrust 0.6.8, commit `99d5f48800b1220f7a0a3f12fa46656cde995f3d`.
`tests/upstream/src/modeler.rs` constructs five modeler kinds (three before
R2007), model/paper-space controls, nonidentity initial placements, Chinese
layers and owned dictionary/XRECORD/reactor graphs.

Regenerate the reference files with the upstream checker:

```sh
cargo run --locked --manifest-path tests/upstream/Cargo.toml -- \
  generate-modeler tests/data/modeler
```

The upstream binary writer promotes versions older than R2007 to R2010.
For actual R13/R14/R2000/R2004 binary inputs, convert the corresponding
reference ASCII files with the native test utility after building it:

```sh
for version in AC1012 AC1014 AC1015 AC1018; do
  build/modeler_test --binary-fixture \
    "tests/data/modeler/$version-ascii.dxf" \
    "tests/data/modeler/$version-binary.dxf"
done
```

The independent Rust verifier checks each binary input's actual header and
geometry against its reference ASCII counterpart before verifying edits.
These files test serialized modeler payloads and do not establish complete
CAD-kernel validity, surface construction history or DWG support.
