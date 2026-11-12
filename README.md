# LibOpenCad

**A native C++17 library for reading, editing, and creating DXF drawings.**

LibOpenCad is an incremental port of the Rust [acadrust](https://github.com/hakanaktt/acadrust) library. It brings CAD file processing to C++ applications without requiring an installed CAD application. The library uses C++ and iconv; Rust is used only for reference tests during development.

The project is licensed under [MPL-2.0](LICENSE). It is in early development, with basic ASCII and binary DXF I/O and document operations available. It does not yet cover the full upstream feature set. **DWG support is currently limited to header version identification; reading and writing DWG drawing contents are not implemented.**

## Quick Start

### Requirements

- A C++17 compiler.
- CMake 3.16 or later.
- iconv, provided by the system or installed as libiconv. CMake discovers it through `FindIconv`.
- Optional: Rust and Cargo for upstream interoperability tests.

Build the library from the repository root:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel
```

Source integration is currently supported. Installation packages and a `find_package(LibOpenCad)` configuration are not yet provided. Place this repository in your application's `LibOpenCad/` directory and use the following application `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(cad_example LANGUAGES CXX)

set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests")
add_subdirectory(LibOpenCad)
add_executable(cad_example main.cpp)
target_link_libraries(cad_example PRIVATE libopencad)
```

### Create a Drawing

Save the following as your application's `main.cpp`. It creates a drawing in millimeters, adds a line and a circle to model space, adds a note to the default paper space, and saves an ASCII DXF file.

```cpp
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>

int main() try {
    using namespace libopencad;
    auto doc = DxfDocument::create(false, 4); // ASCII, $INSUNITS = 4 (millimeters)
    doc.add_layer("Geometry", 3);
    doc.add_layer("Annotations", 2);

    EntityOptions geometry;
    geometry.layer = "Geometry";
    doc.add_line({0, 0, 0}, {100, 50, 0}, geometry);
    doc.add_circle({50, 25, 0}, 10, geometry);

    EntityOptions note;
    note.layer = "Annotations";
    note.space = EntitySpace::Paper;
    doc.add_text("Example drawing", {10, 10, 0}, 2.5, note);

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open("drawing.dxf", std::ios::binary);
    doc.write(output);
    output.close();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

Run `cmake -S . -B build` and `cmake --build build --parallel` from your application directory, then run the resulting `cad_example` executable. To produce binary DXF, use `DxfDocument::create(true, 4)` or save with `doc.write(output, true)`.

## Features

### DXF I/O and Encoding

- Automatic ASCII and binary DXF detection with a shared document and entity API.
- Save in the original format or convert between ASCII and binary while retaining the DXF version and code page.
- UTF-8 string APIs with code-page conversion for legacy drawings. Tests cover round trips of Chinese layer names and text in GBK files.
- Preservation of unedited group codes and unknown records. Preserving raw data does not imply support for every object's semantics or guarantee byte-for-byte equality of entire files.

### Geometry

The following ten entity types support creation, editing of common geometry fields, copying, and deletion by handle:

- `LINE`, `CIRCLE`, `ARC`, `ELLIPSE`, and `TEXT`.
- `LWPOLYLINE`: closure, vertices, widths, and bulge arc segments.
- Traditional 2D and 3D `POLYLINE`: ordered `VERTEX` records and `SEQEND`, handled as complete sequences.
- Polygon Mesh: M/N dimensions, closure flags, and ordered vertices.
- Polyface Mesh: coordinate vertices, triangular and quadrilateral face indices, and hidden edges represented by negative indices.

Common properties include layer, ByLayer / ByBlock / ACI / RGB color, linetype, lineweight, and visibility. The library also provides 2D and 3D vectors, `Matrix3`, `Matrix4`, and `Transform` for geometry calculations. These do not yet provide a unified transformation API for all entity types.

### Documents, Tables, and Objects

- Create AC1032 documents with default tables, model space, default paper space, and required objects.
- Allocate handles, maintain `$HANDSEED`, look up records by handle, detect duplicate identities, and inspect owner chains.
- Query entity membership in model and paper space; select model space or default paper space when creating or copying entities.
- Look up, add, edit, rename, and safely remove entries in nine symbol tables: `LAYER`, `LTYPE`, `STYLE`, `DIMSTYLE`, `APPID`, `UCS`, `VIEW`, `VPORT`, and `BLOCK_RECORD`.
- Create empty blocks with their `BLOCK` / `ENDBLK` records and maintain names and ownership. Direct removal of nonempty blocks is rejected.
- Create extension dictionaries, nested `DICTIONARY` objects, and `XRECORD` data. Entity copying remaps internal references; deletion cascades through verified owned objects and cleans up reactor references.
- Inspect recognized handle, name, and owner references, repair them using explicit mappings, and fill missing entity owners where membership can be determined.

## Read, Edit, and Save

This complete example reads `drawing.dxf` from the previous example, sets the radius of each typed circle it visits to 15, and saves a binary DXF file.

```cpp
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() try {
    std::ifstream input("drawing.dxf", std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open drawing.dxf");
    input.exceptions(std::ios::badbit);
    auto doc = libopencad::DxfDocument::read(input);
    input.close();

    doc.for_each<libopencad::DxfCircle>([](auto& circle) {
        circle.set_radius(15);
    });

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open("edited.dxf", std::ios::binary);
    doc.write(output, true);
    output.close();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

`doc.write(output)` retains the input format; `doc.write(output, false)` writes ASCII. Open file streams with `std::ios::binary` even for ASCII DXF to avoid platform newline conversion.

### API Conventions

- Reacquire record pointers and references after successful structural edits. Do not add, copy, or remove entities inside a `for_each` callback; editing existing fields inside the callback is supported.
- Before adding entities, their layers, named linetypes, and target space must exist and resolve uniquely. Failed structural operations leave the source document and its handle seed unchanged.
- ARC angle APIs, ELLIPSE parameter APIs, and matrix rotations use radians. Values passed through the generic `SymbolProperty` interface follow the units of the corresponding DXF group codes.
- `for_each<T>` visits typed records. Traditional polyline sequences in BLOCKS are typed; other entities inside blocks are currently retained as raw records and can be accessed by handle or through supported copy and delete operations.
- Copying is limited to a single document. Conflicting ownership, ambiguous identities, private data whose references cannot be safely remapped, or external references that cannot be safely cleaned up cause the operation to fail.

## Format Support and Limitations

The version API recognizes `AC1009`, `AC1012`, `AC1014`, `AC1015`, `AC1018`, `AC1021`, `AC1024`, `AC1027`, and `AC1032`. **Recognizing a version does not mean all of its entities, objects, and output paths are fully supported.**

New documents currently use AC1032 (AutoCAD 2018 format). Existing drawings retain their source version; there is no general version upgrade or downgrade API. For example, adding an ELLIPSE requires R13 or later, and adding an LWPOLYLINE requires R14 or later.

Missing or incomplete areas include DWG drawing I/O, additional entity types, full block insertion and arbitrary layout editing, complete style models, dynamic blocks and advanced associative objects, ACIS solid modeling, external 3D format imports, and rendering. Generic symbol-table properties do not provide complete typed support for these features, and reference checks are not a comprehensive CAD audit and repair system.

The current validation environment is macOS with Apple Clang. Other platforms require independent build and test verification; cross-platform support has not yet been established.

## API and Examples

- [Document, entity, and symbol-table APIs](include/libopencad/dxf_document.hpp).
- [Color](include/libopencad/color.hpp), [lineweight](include/libopencad/line_weight.hpp), and [DXF versions](include/libopencad/dxf_version.hpp).
- [Vectors](include/libopencad/vector.hpp) and [matrices and transforms](include/libopencad/transform.hpp).
- [Creating multiple entity types](tests/dxf_stage2.cpp), [symbol-table operations](tests/dxf_symbol_operations.cpp), and [extension dictionaries and reference operations](tests/dxf_reference_operations.cpp).

The tests also serve as API usage examples. A separately generated API documentation site is not yet available.

## Building and Testing

Native tests do not require Rust:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DLIBOPENCAD_TEST_UPSTREAM=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

With upstream tests enabled, the pinned acadrust library reads drawings saved by C++ and checks geometry, common properties, space membership, and object references. Math tests compare both implementations using identical inputs:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DLIBOPENCAD_TEST_UPSTREAM=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The first run needs network access to fetch the [locked Cargo dependencies](tests/upstream/Cargo.lock). Once they are cached, run `CARGO_NET_OFFLINE=true ctest --test-dir build --output-on-failure` for offline verification. Generated drawings and test results go into the build directory; source fixtures are in [tests/data](tests/data).

The current suite has passed **227 CTest checks**: 129 native tests and 98 Rust reference tests. The native suite has also passed AddressSanitizer and UndefinedBehaviorSanitizer checks. These results cover the repository's test cases and do not guarantee compatibility with arbitrary CAD files.

## Upstream and Contributing

LibOpenCad is an independent C++ port. Source comparisons and test dependencies currently target acadrust commit [`acad0df2`](https://github.com/hakanaktt/acadrust/tree/acad0df2bc491d6ac909dc7491c508bb8e6ebed8), package version 0.6.8. This does not imply that the latest upstream changes have been incorporated. Source comments identify the corresponding Rust modules; further work is ported and tested module by module.

Contributions of reproducible bug reports, anonymized drawings, tests, and code improvements are welcome. Include the file version, ASCII or binary encoding, a minimal reproducer, and expected behavior when reporting a problem. New format or entity support should include native tests and, where practical, Rust readback comparisons. Preserve source attribution and license notices when incorporating external code.

## License and Acknowledgments

LibOpenCad is licensed under the [Mozilla Public License 2.0](LICENSE). Thanks to [acadrust](https://github.com/hakanaktt/acadrust) and its contributors for the source implementation and test reference. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance and retained third-party notices.
