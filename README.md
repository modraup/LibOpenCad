# LibOpenCad

**A native C++17 library for reading, editing, and creating DXF drawings.**

LibOpenCad is an incremental port of the Rust [acadrust](https://github.com/hakanaktt/acadrust) library. It brings CAD file processing to C++ applications without requiring an installed CAD application. The library uses C++, iconv and zlib. Rust is used for reference tests during development. See [Build and distribute the SDK](#build-and-distribute-the-sdk) for third-party integration.

The project is licensed under [MPL-2.0](LICENSE). It is in early development, with basic ASCII and binary DXF I/O and document operations available. It does not yet cover the full upstream feature set. The native DWG reader supports LINE, CIRCLE, ARC and TEXT across the nine release signatures from AC1009 to AC1032. It exposes symbol names, object handles, block membership and layout references. The DWG document API uses the same LINE, CIRCLE, ARC and TEXT types as the DXF document. It edits geometry and assigns an existing layer while preserving handles and ownership. New drawings can contain LINE, CIRCLE, ARC and TEXT in model and paper space on layer 0. Creation uses release-specific empty document defaults compiled into the library. [DWG document editing](include/libopencad/dwg_document.hpp) · [DWG records and bit streams](include/libopencad/dwg_r2000.hpp) · [DWG creation](include/libopencad/dwg_create.hpp)

To update an existing drawing, retrieve a geometry record by its numeric handle, edit the returned value and apply it to the document. Open both streams in binary mode.

```cpp
auto drawing = libopencad::DwgDocument::read(input);
auto arc = drawing.arc(handle);
arc.set_radius(12.5);
arc.set_layer("DETAILS");
drawing.update(arc);
drawing.write(output);
```

Use `drawing.move_entity(handle, "DETAIL")` to append a LINE, CIRCLE, ARC, TEXT or complete traditional compound entity to an existing named block. The same call accepts `"*Model_Space"` and `"*Paper_Space"`. For R13 and later the numeric overload accepts a block-record handle or its layout handle, including a non-default paper-space layout. AC1009 resolves owners by block name and uses its native extra entity stream. A successful move preserves the entity handle and geometry. A failed move leaves the document unchanged.

Compound moves cover traditional 2D/3D POLYLINE, Polygon Mesh, Polyface Mesh and INSERT/MINSERT with ATTRIB/SEQEND records. Child handles, order and payloads remain attached to the parent. R13–R2000 repairs the affected block chains; R2004+ updates block member arrays. AC1009 relocates the complete sequence through its EXTRA stream and rewrites the SEQEND parent address. Moving a child record separately is rejected. These operations preserve stored geometry; they do not explode blocks or transform coordinates between spaces.

Create a native drawing with UTF-8 text using `DwgTextGeometry`:

```cpp
#include <libopencad/dwg_create.hpp>

std::string bytes = libopencad::create_dwg(libopencad::DxfVersion::AC1032, {
    {libopencad::DwgTextGeometry{"Survey 测量", {3.0, -4.0, 2.0}, 2.5}},
    {libopencad::DwgTextGeometry{"Sheet A", {0.0, 0.0, 0.0}, 4.0}, true}
});
```

New TEXT uses layer 0, Standard style and default formatting. The initializer accepts a value, insertion point and positive height; `DwgPrimitive` also supplies thickness and normal. After loading the bytes with `DwgDocument::read`, use `text(handle)` and `update` for further formatting, style or common-property changes. Legacy releases use the default drawing's code page and Unicode escapes; R2007+ uses UTF-16. Empty text is valid; invalid UTF-8, embedded NULs, excessive encoded length, a zero creation normal and non-finite geometry refuse creation.

DWG verification compares native output with the pinned Rust library across nine signatures. Independent AutoCAD/BricsCAD open and AUDIT validation has not been performed. Opaque records are retained during supported edits; their presence does not imply typed editing support or exhaustive DWG compatibility.


For R2018 drawings measured in inches, `create_dwg(DwgTemplate::R2018Inches, primitives)` sets the insertion units in the file header. Use `DwgFaceGeometry` for a 3DFACE, repeating the third corner for a triangle. The primitive carries its layer name and entity color. Read a face with `drawing.face3d(handle)` for handles in `drawing.objects().faces`. The inch seed uses the same standard tables as the other seeds and can be regenerated with the upstream fixture command `generate-dwg-inch-blank src/dwg_defaults/AC1032_inches.dwg`.

To write reusable R2018 blocks, pass their names as the third argument to `create_dwg(DwgTemplate::R2018Inches, primitives, block_names)`. Set `DwgPrimitive::block_name` to the owning definition for local geometry. A primitive containing an `InsertDefinition` references its target by `InsertDefinition::block_name` and carries its own insertion point, rotation, signed scales and extrusion normal. A nested reference sets both names. Empty primitive owner names select drawing space.


Create a named layer with `drawing.add_layer("Survey", 3)`, then use `set_layer("Survey")` and `update` on an entity. The optional ACI color defaults to 7. The layer uses the drawing's Continuous linetype. The return value is the new DWG object handle for R13 and later or the zero-based physical slot for R11/R12. Duplicate names use the same case-insensitive matching as lookup. AC1009 names must fit in 32 bytes after code-page encoding. AC1009 LINE/CIRCLE/ARC geometry updates preserve the existing record size and addresses. An update that needs a new optional elevation, thickness, normal or Z field in an existing AC1009 record reports an error. The document retains the previous bytes if an update fails.

To assign a named linetype to an existing DWG layer, create the pattern and set the layer's reference before saving the drawing.

```cpp
drawing.add_linetype("SurveyDash", {4.0, -1.25, 0.0, -0.75}, "Survey dash-dot");
drawing.set_layer_linetype("Survey", "SurveyDash");
drawing.write(output);
```

Lengths are in drawing units. Positive values draw a dash, negative values leave a gap, and zero places a dot. This pattern repeats every 6 units. An empty pattern draws a continuous line. R11/R12 supports up to 12 segments and 48 encoded description bytes. R13 through R2018 support up to 255 segments. The API creates simple patterns and matches layer and linetype names case-insensitively.

To override the linetype on a single DWG entity, set it on the returned LINE, CIRCLE, ARC or TEXT and apply the edit. The name can be `ByLayer`, `ByBlock`, `Continuous`, or an existing named linetype. An empty name also selects `ByLayer`. The getter returns an empty string for the implicit ByLayer setting.

```cpp
auto line = drawing.line(handle);
line.set_linetype("SurveyDash");
drawing.update(line);
drawing.write(output);
```

Set the color of a DWG LINE, CIRCLE, ARC or TEXT with `set_color` and `update`. Pass `Color::by_layer()`, `Color::by_block()`, or `Color::from_index(n)` with an ACI index from 1 through 255. R2004 and later store an exact RGB value. Earlier versions select the nearest ACI color when supplied with RGB.

```cpp
auto circle = drawing.circle(handle);
circle.set_color(libopencad::Color::from_rgb(12, 34, 56));
drawing.update(circle);
drawing.write(output);
```

Setting a color on an entity that references a color book replaces its book reference and retains its transparency. On AC1009, LibOpenCad reads and writes the full unsigned ACI byte. The pinned Rust reader treats that byte as signed, so it reports ACI 255 as 1.

Use `set_invisible(true)` to hide a LINE, CIRCLE, ARC or TEXT, and `set_invisible(false)` to show it again. DWG stores entity visibility from R13 and entity lineweight from R2000. A lineweight value is in hundredths of a millimeter. The following edit hides a line and assigns a 0.53 mm lineweight in R2000 or later.

```cpp
auto line = drawing.line(handle);
line.set_invisible(true);
line.set_line_weight(libopencad::LineWeight::from_value(53));
drawing.update(line);
drawing.write(output);
```

The DWG writer also accepts `LineWeight::by_layer()`, `by_block()` and `default_weight()`. Unlisted numeric weights map to Default, following acadrust. When saving to the original DWG version, R13/R14 omit lineweight changes and R11/R12 omit both properties. Other supported edits in the same update still apply.

Use `linetype_scale()` and `set_linetype_scale(value)` on a DWG LINE, CIRCLE, ARC or TEXT to read or edit the entity's line pattern scale.

```cpp
auto arc = drawing.arc(handle);
arc.set_linetype_scale(3.75);
drawing.update(arc);
drawing.write(output);
```

R13 through R2018 store this per-entity value. R11/R12 omit the edit and read the entity scale as 1.0. The drawing header's global scale is preserved. The setter accepts finite values, including zero and negative values, following the upstream stored-field behavior.

Read a DWG entity's transparency with `transparency()` and edit it with `set_transparency()`. R2004 and later support `Transparency::by_layer()`, `Transparency::by_block()` and `Transparency::explicit_alpha(n)`. The explicit amount ranges from 0 (opaque) through 255 (fully transparent).

```cpp
auto line = drawing.line(handle);
line.set_transparency(libopencad::Transparency::explicit_alpha(128));
drawing.update(line);
drawing.write(output);
```

A transparency-only edit retains the entity's color and color-book reference. R11/R12 through R2000 omit this unsupported field when saving to the original version and read its value as ByLayer. Other supported changes in the same update still apply.

Read an existing DWG TEXT with `drawing.text(handle)`. Its value uses UTF-8 in C++; the writer uses the drawing's code page through R2004 and UTF-16 from R2007. Characters outside an older code page use the same Unicode escapes as the upstream writer.

```cpp
auto text = drawing.text(handle);
text.set_text(u8"Updated note: 中文");
text.set_insertion_point({12.0, 8.0, 3.0});
drawing.update(text);
drawing.write(output);
```

TEXT edits support the value, insertion point, height, rotation, width factor, alignment, oblique angle, generation/mirror flags, thickness, normal and existing text-style assignment across AC1009–AC1032. Use `definition()` and `set_definition()` for the formatting fields. Rotation is in radians; horizontal alignment values are 0–5 (Left, Center, Right, Aligned, Middle, Fit) and vertical values are 0–3 (Baseline, Bottom, Middle, Top).

```cpp
auto text = drawing.text(handle);
auto definition = text.definition();
definition.height = 3.5;
definition.rotation = 0.5;
definition.width_factor = 0.8;
definition.oblique_angle = 0.25;      // Radians
definition.generation_flags = 6;      // Backward (2) and upside-down (4)
definition.thickness = -1.5;
definition.normal = {0.0, 1.0, 0.0};
definition.horizontal_alignment = 1; // Center
definition.vertical_alignment = 2;   // Middle
definition.alignment_point = libopencad::Point3{20.0, 10.0, definition.insertion.z};
definition.style = "NOTES";          // Must already exist in the drawing
text.set_definition(definition);
drawing.update(text);
drawing.write(output);
```

Style names match case-insensitively, including Unicode names. A same-style spelling change preserves the original bytes. Unknown or ambiguous names reject the update. R11/R12 TEXT uses a one-byte physical STYLE table slot: slot 255 is supported and slot 256 is rejected. R13 and later use a STYLE handle and have no corresponding one-byte slot limit.

DWG stores one elevation shared by insertion and alignment, so alignment Z follows insertion Z. Independent alignment Z is not written. Resetting `alignment_point` clears its X/Y; AC1009 can also omit the point entirely. These setters change stored fields without generating glyph geometry or recalculating alignment layout. Unedited fields, common properties, handles and ownership are retained. TEXT supports the same ownership moves as the primitive entities.

TEXT also supports the common entity setters for an existing layer or linetype, color, visibility, lineweight, linetype scale and transparency. They can be combined with text, placement and formatting changes in one atomic update.

```cpp
auto text = drawing.text(handle);
text.set_layer("DETAILS");
text.set_linetype("Continuous");
text.set_color(libopencad::Color::from_rgb(12, 34, 56));
text.set_invisible(false);
text.set_line_weight(libopencad::LineWeight::from_value(53));
text.set_linetype_scale(2.25);
text.set_transparency(libopencad::Transparency::explicit_alpha(128));
drawing.update(text);
drawing.write(output);
```

Layer and linetype names resolve case-insensitively, including Chinese names. Unknown names refuse the complete update. Linetypes accept `ByLayer` (also an empty string), `ByBlock`, `Continuous` and existing named patterns. Visibility and linetype scale start at R13, lineweight at R2000, and RGB colors and transparency at R2004. Older versions omit unsupported display fields and approximate RGB with ACI. An update containing only unsupported display fields preserves the source bytes. TEXT uses the same inherited and explicit color, weight and transparency modes described above for other entities.

A transparency-only change retains a TEXT color-book reference. Explicitly setting an ordinary color, including its current fallback ACI, removes that reference while leaving the book object and neighboring users intact. The C++ getter exposes the stored fallback ACI for book-colored entities. As with other AC1009 entities, C++ retains unsigned ACI 255 while the pinned Rust bridge reads it as ACI 1.

Oblique angles use radians in `TextDefinition`. Generation flag 2 mirrors text backward and flag 4 makes it upside-down; combine them as 6 or clear them with 0. R11/R12 store the low eight bits of `generation_flags`, matching the Rust writer (for example, 262 becomes 6 and -1 becomes 255). R13 and later retain the signed 16-bit field. Other stored bits are not discarded in those modern versions.

Thickness accepts finite positive, negative and zero values. Normals retain their supplied finite components, including non-unit and zero vectors, following the upstream stored-field behavior. Changing the normal does not transform the insertion or alignment coordinates. Zero thickness and the default normal `(0, 0, 1)` restore compact default encodings where the version provides them. These setters do not render text or perform a geometric mirror transformation.

Empty strings and length-changing updates are supported, including AC1009 relocation into its extra entity stream. Formatting-only updates retain the original encoded string. Invalid UTF-8, embedded NUL, non-finite numbers, invalid alignment values and oversized records reject the update without changing the document. A combined text, position, format and style update is atomic.

### DWG Read Statistics and Recovery

`DwgDocument::read_with_stats(input)` returns the document and `DwgReadStatistics`, including the source version, record stream counts, output counts, and diagnostics. A clean read preserves the original bytes. Set `DwgReadOptions::failsafe` to inspect the records that survived decoding. A failsafe outcome with `writable == false` exposes those records for inspection. Strict decoding errors carry the available statistics in `DwgReadError::stats()`.

```cpp
libopencad::DwgReadOptions options;
options.failsafe = true;
auto read = libopencad::DwgDocument::read_with_stats(input, options);
auto skipped = read.stats.skipped_source_records;
```

For R13 and later, `decoded_source_records` includes the mapped records whose framing and native decoders succeeded, including records retained as opaque. `output_records()` sums the entity, object, and table record counts. The AC1009 output counts reflect the native drawing and its physical table rows.

For an AutoCAD 2004 or later drawing protected with the `AcDb:Security` password verifier, pass the UTF-8 password as `DwgReadOptions::password`. `read`, `read_with_stats`, `from_mmap`, and `recover` accept the option. Strict statistics reads report missing or incorrect passwords as `decryption-failed`. Failsafe reading reports the failure and can inspect any unencrypted sections the file contains. An authenticated, writable document can copy the original encrypted file bytes through `write()`.

`DwgDocument::from_mmap(path, options)` accepts a filesystem path on POSIX and Windows and returns the same `DwgReadOutcome` as `read_with_stats`. It decodes from a read-only mapping. The document and its copies keep the mapping alive for byte-preserving output. Keep the source file unchanged for that lifetime and save edits to a separate file. File-open and mapping failures surface as `DwgReadError`.

`DwgDocument::recover(input)` returns the recovered document, read statistics, and a before/after repair report. On R13 and later it uses the handle map to isolate corrupt records and repair block membership where the surviving references identify the repair. The AC1009 reader validates the legacy entity stream as a unit.

```cpp
auto recovered = libopencad::DwgDocument::recover(input);
if (recovered.writable)
    recovered.document.write(output);
```

`stats.diagnostics` reports skipped handles and identifies whether their offsets refer to file bytes or the decompressed object section. `DwgRecoveryError::stats()` supplies the diagnostics when no usable drawing survives. A usable document may be available for inspection with `writable == false` when the container cannot be rebuilt safely. Read-only recovery retains access to the surviving native records through `document.objects()`.

## Quick Start

### Requirements

Building the SDK requires a C++17 compiler, CMake 3.16 or later, and development files for iconv and zlib. With Apple libc++, the floating-point `std::from_chars` overload used by the library requires a macOS 26.0 or later deployment target. Rust and Cargo are optional tools for upstream interoperability tests.

### Build and distribute the SDK

From the repository root, build the shared library and install the SDK into a local directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix "$PWD/build/sdk"
cmake --build build --config Release --target package
```

The ZIP in `build/` contains the compiled library, public headers, CMake package, license notices and a buildable example. Distribute the ZIP built for the consumer's OS, architecture and compatible C++ ABI. The shared build uses the platform's iconv and zlib runtimes. On Windows, make the SDK's `bin` directory and any dependency DLLs available to the executable. For static linking, configure a separate build with `-DBUILD_SHARED_LIBS=OFF`. The static package locates iconv and zlib development files when the application configures.

### Link from a third-party project

Point CMake at the installed or extracted SDK root (the directory containing `include/`). Application code includes headers such as `<libopencad/dxf_document.hpp>`.

```cmake
cmake_minimum_required(VERSION 3.16)
project(cad_example LANGUAGES CXX)
find_package(LibOpenCad 0.1 CONFIG REQUIRED)
add_executable(cad_example main.cpp)
target_link_libraries(cad_example PRIVATE LibOpenCad::libopencad)
```

For a host application that loads plugins, implement the host's entry point in `extension.cpp` and link the SDK target into the module. Select the static SDK prefix to include LibOpenCad in the plugin binary, or the shared SDK prefix to load the LibOpenCad runtime separately.

```cmake
find_package(LibOpenCad 0.1 CONFIG REQUIRED)
add_library(cad_extension MODULE extension.cpp)
target_link_libraries(cad_extension PRIVATE LibOpenCad::libopencad)
```

The macOS static SDK includes a [buildable plugin and host](examples/macos_static_plugin/README.md) that write a DWG and verify its geometry after reopening it.

To run the example shipped in the SDK on macOS or Linux, use the default installation layout:

```sh
sdk="$PWD/build/sdk"
cmake -S "$sdk/share/doc/LibOpenCad/examples" -B build/consumer -DCMAKE_PREFIX_PATH="$sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build/consumer --config Release
(cd build/consumer && ctest -C Release --output-on-failure)
```

The example writes `build/consumer/drawing.dxf`. For a deployed application, install the shared library where its runtime loader searches. A source-based application can instead use the same target with `add_subdirectory(LibOpenCad)`. Embedded builds skip the library's tests by default and preserve the parent project's test settings.

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

The following entity types support creation, editing of common geometry fields, copying, and deletion by handle:

- `LINE`, `CIRCLE`, `ARC`, `ELLIPSE`, and `TEXT`.
- `POINT`: location, thickness, normal, and the point display angle field.
- `RAY` and `XLINE`: base point and normalized direction, including zero-vector behavior matching the pinned Rust implementation.
- `SOLID` and `TRACE`: stored and boundary corner order, triangle detection, thickness, and normal.
- `3DFACE`: corners, area, and invisible-edge flags.
- `SPLINE` and `HELIX`: knots, weights, control/fit points, tangents, tolerances, and independent helix parameters. The writer expands compact periodic cubics and interpolates fit-only cubics with chord, centripetal or uniform parameters.
- `LEADER` and `TOLERANCE`: annotation geometry, dimension style references, linked annotations, and extension dictionary copy/deletion.
- `HATCH`: polyline, line, circular arc, elliptic arc, and rational spline boundaries; holes, pattern lines, gradients, seed points, and boundary references. Pattern setters update stored lines by angle delta and scale ratio, including base points, offsets and dashes. Full creation currently requires R2010+.
- `DIMENSION`, `ARC_DIMENSION`, and `LARGE_RADIAL_DIMENSION`: all nine geometry variants, text overrides, measurement fields, style references, and scoped editing. Full creation requires R2010+. The pinned Rust reader omits some diameter and extended-dimension fields; C++ retains them, and tests record those differences explicitly.
- `MLINE` and `MLINESTYLE`: ordered vertices, direction/miter vectors, per-element parameters and fill breaks, style offsets/colors/linetypes, and cap angles in radians. New full styles require R2004+.
- `MULTILEADER` and `MLEADERSTYLE`: text/block contexts, nested roots and lines, multiple break segments, arrow overrides, block attributes, colors, columns, and style definitions. Unknown fields remain scoped within the definition's `extra_fields`. Full construction requires R2013+; high-level annotation layout and rendering are not implemented.
- `ACAD_TABLE` and legacy `TABLESTYLE`: row/column sizes, text/numeric/binary/point cell values, merged ranges, legacy cell formatting, row styles, and named style lookup. Creation requires R2007+. Copy/delete remaps owned extension objects. Automatic graphics regeneration remains pending.
- `TABLECONTENT` and `TABLETEMPLATE`: multiple value/block/field contents per cell, content and table/row/column/cell formats, margins, inside/outside borders, custom data, merged ranges, and style references. Document APIs synchronize the linked content object with the first effective content in each legacy `ACAD_TABLE` cell. Modern format colors use the upstream writer's nearest ACI conversion.
- `TABLEGEOMETRY`: row-major cell caches with multiple content geometries, top-left/center offsets, inner/outer dimensions, flags and references. Typed cache edits participate in table synchronization, independent copying and cascading deletion; geometry calculation and rendering remain pending.
- `MTEXT`: Unicode text chunks, geometry, background fill, and version-specific column data (R2007 ACAD XDATA and R2018 embedded objects).
- `INSERT`: named and nested block references, array dimensions, scaling, rotation, and ordered `ATTRIB`/`SEQEND` children. `ATTDEF` and attributes include alignment and R2018 embedded multiline text.
- `LWPOLYLINE`: closure, vertices, widths, and bulge arc segments.
- Traditional 2D and 3D `POLYLINE`: ordered `VERTEX` records and `SEQEND`, handled as complete sequences.
- Polygon Mesh: M/N dimensions, closure flags, and ordered vertices.
- Polyface Mesh: coordinate vertices, triangular and quadrilateral face indices, hidden edges represented by negative indices, and face colors. Polyline APIs also expose header flags, thickness, normals, vertex widths, tangent data and IDs where the record supports them.
- `SHAPE`, `MPOLYGON`, `IMAGE`, `WIPEOUT`, and PDF/DWF/DGN underlays: stored geometry, clipping, definitions and references.
- Extended records: `OLE2FRAME`/`OLEFRAME` payloads, `LIGHT` and photometric fields, `SECTIONLINE`, `SECTIONOBJECT`, `DRAWINGVIEW`, `CAMERA`, `ARCALIGNEDTEXT`, `RTEXT`, `POSITIONMARKER`, coordination models, and point clouds. These APIs edit the stored DXF definitions; they do not load external media, execute embedded content, or render the drawing.

Additional document interfaces cover named layouts and their paper-space blocks, plot settings, VIEWPORT entities, typed UCS/VIEW/VPORT definitions, named text styles, and DIMSTYLE fields. Text and dimension styles retain unrelated application XDATA when font or annotative properties change. The typed XDATA API supports every upstream DXF value kind, application-specific updates/removal, APPID registration, and handle remapping on copy. `DxfDocument::unused_app_ids()` lists APPID records that no extended data names and that do not own objects; `ACAD`, `AcadAnnotative`, `AcCmTransparency` and `AcAecLayerStandard` stay registered because writers derive those from modelled fields.

Full table-definition setters currently require R2000+ for UCS, R2007+ for VIEW/VPORT, and R2010+ for DIMSTYLE. Reading preserves older records; comprehensive version conversion remains pending. Anonymous shape-file STYLE entries have separate creation and handle-addressed editing APIs; complex linetypes can refer to them by handle. DWG-only table fields are outside the DXF setters.

The independent `<libopencad/mtext_format.hpp>` API parses and serializes MTEXT paragraphs and styled spans, including fonts, colors, fractions, height factors, tabs, and column breaks. It also supports the distinct TEXT `%%` control-code rules. `mtext::scale_inline_heights` multiplies absolute `\H<h>;` codes by a uniform transform factor; relative `\H<k>x;` codes follow the entity height and stay unchanged.

### Geometry queries and bounding boxes

The typed LINE, CIRCLE, ARC, ELLIPSE and LWPOLYLINE interfaces provide `bounding_box()` following the pinned Rust implementation. `<libopencad/bounds.hpp>` exposes `BoundingBox3D` with `min`/`max`, dimensions, center, inclusive containment, expansion, merging and OCS-to-WCS conversion. `from_points` returns `std::nullopt` for an empty input; the default box is the origin. Construction preserves the supplied corner order.

LINE also provides `length()`, `direction()` and `midpoint()`. CIRCLE provides diameter, circumference, area, `center_wcs()`, `axes_wcs()` and `point_at_angle_wcs(angle)`. ARC adds sweep/length, start/end/midpoint queries and their `_wcs` variants. Angles are radians; the ARC methods without `_wcs` return OCS coordinates. ELLIPSE exposes major/minor axis lengths and `is_full()`; LWPOLYLINE exposes `vertex_count()` alongside its existing vertices.

```cpp
drawing.for_each<libopencad::DxfArc>([](const libopencad::DxfArc& arc) {
    const auto box = arc.bounding_box();
    const auto midpoint = arc.midpoint_wcs();
    // box.min / box.max and midpoint use world coordinates.
});
```

The queries retain these upstream boundaries:

- CIRCLE/ARC bounds include arbitrary-axis orientation and signed thickness. ARC tests the coordinate extrema within its sweep. As in Rust, a negative end-minus-start difference receives one added turn; arbitrary multi-turn angles are not fully normalized.
- LINE bounds use its endpoints and omit thickness.
- ELLIPSE bounds use a simplified XY radius box at the center Z, ignoring normal and trim parameters. They can omit the Z extent of a tilted ellipse.
- LWPOLYLINE maps its vertex-only OCS box into WCS. Bulge, widths and thickness do not expand it, so curved/wide segments can extend outside the result. Empty polylines return the origin regardless of elevation/normal.

These are read-only geometric queries. They preserve source records and do not transform entities. The native and Rust tests use the same 200-entity inputs in both ASCII and binary DXF; see [geometry tests](tests/geometry.cpp) and [Rust comparison](tests/upstream/src/geometry.rs).

Common properties include layer, ByLayer / ByBlock / ACI / RGB color, linetype, lineweight, visibility, linetype scale, transparency, color-book names, material/visual-style/plot-style references, and stored graphics bytes. DXF group 380 plot-style flags are 0 ByLayer, 1 ByBlock, 2 named/dictionary without a 390 handle, and 3 by handle; `set_display_properties` (R2010+) writes 380 only for flags 1 and 2, matching the rust writer, and the reader accepts `0..=2` when no 390 is present. CTB/STB `TTBVER1` table files are decoded by `PlotStyleTable` in `<libopencad/plot_style.hpp>`. The library also provides 2D and 3D vectors, `Matrix3`, `Matrix4`, and `Transform` for geometry calculations. These do not yet provide a unified transformation API for all entity types.

### Entity transforms, mesh construction and decomposition

`translate(offset)`, `apply_transform(transform)` and `apply_mirror(transform)` are available on LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE, traditional 2D/3D POLYLINE, Polygon Mesh, Polyface Mesh, POINT, RAY, XLINE, SOLID/TRACE, 3DFACE, SPLINE and MESH. Offsets and mirror planes are in WCS. Each edit updates geometric XDATA positions, displacements, directions and distance/scale values atomically; handles, owners and unrelated properties remain attached to the entity. A nonfinite general transform throws; an identity or nonfinite mirror leaves the entity unchanged, following Rust's mirror dispatch.

These operations preserve upstream geometric contracts. Circle/arc radii and polyline widths use the transformed world-X axis length even under nonuniform scaling. They do not convert circles into ellipses. Ellipse transforms reparameterize the two axes; spline transforms update tangents, distance tolerances and inverse-transpose normals. Reflected traditional meshes retain the upstream winding and hidden-edge rules. MESH transforms change vertices without reversing face indices. Polyface mesh normals are stored by C++ but omitted by the pinned Rust DXF reader/writer, so validation also compares the stored normal against Rust's in-memory result.

`<libopencad/mesh.hpp>` provides `MeshDefinition`, `MeshFace` and `MeshEdge`. Construct boxes, subdivided planes and cylinders; merge meshes, reverse face winding, compute bounds and edit edge creases. The cylinder axis is +Y, matching upstream. Recomputed edges are sorted for deterministic C++ output; upstream uses unspecified hash iteration order. Both recomputations clear creases. Zero subdivision counts are rejected. `DxfDocument::add_mesh` creates a native MESH in R2010+ drawings; `DxfMesh::definition()` and `set_definition()` read and atomically edit vertices, faces, edges, creases and subdivision settings. Zero crease values read back as absent. Nonzero subentity override payloads are explicitly unsupported by this structured API.

```cpp
auto drawing = libopencad::DxfDocument::create();
auto mesh = libopencad::MeshDefinition::create_box({0, 0, 0}, {10, 20, 5});
mesh.subdivision_level = 2;
drawing.add_mesh(mesh);
drawing.for_each<libopencad::DxfMesh>([](auto& entity) {
    entity.apply_transform(libopencad::Transform::from_translation({3, 4, 5}));
});
```

### Mesh import

`<libopencad/import.hpp>` ports acadrust `io/import`. `detect_format` / `import_file` recognise `.stl`, `.dae`, `.obj`, `.gltf`, `.glb` and `.fbx`. Each format writes an AC1032 `DxfDocument` of MESH entities with the shared vertex weld, layer prefix and material colours:

- STL: ASCII/binary, including a `solid` header whose length is `84 + 50 * triangle_count`; VisCAM/SolidView facet colours.
- OBJ: fan-triangulated faces, `usemtl` groups, and `mtllib` when importing from a file path.
- COLLADA: geometries, effect diffuse colours, and nested node `translate`/`rotate`/`scale`/`matrix` transforms.
- glTF 2.0 / GLB: triangle/strip/fan primitives, PBR `baseColorFactor`, node TRS/matrix, `data:` buffers, and external URIs that stay inside the source directory.
- FBX: ASCII and binary (including zlib arrays capped at 64 MB), polygon `!idx` faces, and parent `Lcl Translation`/`Rotation`/`Scaling`.

Empty meshes are omitted. See [native tests](tests/import_stl.cpp) and the local-checkout Rust oracle [tests/upstream/local-import](tests/upstream/local-import).

### Layer states

`DxfDocument` ports acadrust `layer_state`: named states live as XRecords under the LAYER table extension dictionary (`ACAD_LAYERSTATES` / `ACAD_LAYERSTATE`). `capture_layer_state` / `store_layer_state` write the rust payload (mask, description, current layer, per-layer flags/colour/linetype/lineweight/plot style/transparency). Existing names update in place and keep the XRECORD handle. `restore_layer_state` applies the saved mask, `rename_layer_state` / `delete_layer_state` change only the dictionary key. See [native tests](tests/layer_state.cpp) and the local-checkout Rust oracle [tests/upstream/local-layer-state](tests/upstream/local-layer-state).

```cpp
auto drawing = libopencad::DxfDocument::create();
drawing.add_layer("Walls", 1);
drawing.capture_layer_state("Permit set", "issued");
drawing.set_symbol_properties(libopencad::SymbolTable::Layer, "Walls", {{62, "3"}});
drawing.restore_layer_state("Permit set");
```

### Plot-style tables

`<libopencad/plot_style.hpp>` ports acadrust `plot_style`: CTB/STB `TTBVER1` zlib tables (`PIAFILEVERSION_2.0,{CTB,STB,TTB}VER1,compress`). `identity` / `from_bytes` / `to_bytes` round-trip ACI and named styles; `resolve_lineweight` applies `scale_factor` when `apply_factor` is set. A damaged Adler-32 is a load warning. See [native tests](tests/plot_style.cpp) and the local-checkout Rust oracle [tests/upstream/local-plot-style](tests/upstream/local-plot-style).

```cpp
#include <libopencad/plot_style.hpp>

auto table = libopencad::PlotStyleTable::identity("scaled.ctb");
table.apply_factor = true;
table.scale_factor = 0.25f;
auto again = libopencad::PlotStyleTable::from_bytes(table.name, table.to_bytes());
```

### Sheet sets

`<libopencad/sheet_set.hpp>` ports acadrust `sheet_set`. DST files use a per-byte cipher, not zlib. `SheetSetDatabase` reads and writes XML subsets, sheets, layout references and custom properties. `DxfDocument::sheet_set_data` / `set_sheet_set_data` store `AcSheetSetData` on the named-object dictionary. See [native tests](tests/sheet_set.cpp) and [tests/upstream/local-sheet-set](tests/upstream/local-sheet-set).

```cpp
auto db = libopencad::SheetSetDatabase::make("Demo Set", "A & B");
auto set = std::string(db.sheet_set().id());
auto sheet = db.add_sheet(set, "A-101", "Ground Floor Plan", "");
libopencad::SheetSetData data;
data.sheet_set_file_name = "demo.dst";
drawing.set_sheet_set_data(data);
```

### Fields

`<libopencad/fields.hpp>` ports acadrust `fields` evaluation for DIESEL, `AcVar`, `_text` children, sheet-set placeholders, COUNT, `AcExpr` and constructible `AcObjProp` (including INSERT `InsUnits` / Effective scale, Material flags, and versioned `AcObjProp.*` UCS points). `FieldContext` is host-supplied (`now_julian` is required). Unknown evaluators return no value and keep the cached field text. `AcExpr` invalid results are `####`. Nonempty `\f` pictures use rust `Picture` / `format_number` (`%lu`, `%pr`, `%bl`, …); empty pictures stay six decimals. `cell_field_text` evaluates a table-owned FIELD. `inline_code` expands `%<_FldIdx n>%` children; `field_spans` returns UTF-8 ranges of filled children. `DxfDocument::set_table_cell_field` / `remove_table_cell_field` / `update_table_cell_fields` / `refresh_table_block` / `build_table_block` author table cells and regenerate the anonymous `*T` block. `set_text_field` / `remove_text_field` / `update_fields` / `stamp_plot_fields` attach FIELD objects to MTEXT/TEXT/ATTDEF/ATTRIB hosts. `attach_attribute_fields` copies ATTDEF fields onto INSERT ATTRIB. `orphan_table_fields` / `prune_table_fields` / `remove_fields` drop leftover table-owned FIELD objects. `create_group` / `create_unnamed_group` / `add_entity_to_group` / `remove_entity_from_group` / `remove_group` register `GROUP` objects in NOD `ACAD_GROUP` and keep entity `{ACAD_REACTORS}` in sync. `DxfDocument::recover` is rust `DxfReader::recover` (failsafe read, usability gate, then `repair()`). `mesh_texture_coordinates`, `set_layer_viewport_override` and `viewport_annotation_scale` store Autodesk XRecords on the mesh, layer or viewport extension dictionary. `begin_entity_change_recording` snapshots first-touch entity type names. DXF `write()` refreshes the named object dictionary’s `ACDB_RECOMPOSE_DATA` entry on the output copy. It lists table styles with linked cell-style maps and tables with a linked round-trip record. Use `prepare_table_recompose_data()` to apply the same update in memory. Data-link objects remain typed ClassObject storage; there is no spreadsheet engine. See [native tests](tests/stage7_remaining.cpp) and [tests/upstream/local-stage7-remaining](tests/upstream/local-stage7-remaining).

```cpp
#include <libopencad/dxf_document.hpp>
#include <libopencad/fields.hpp>

struct Host : libopencad::FieldContext {
    double now_julian() const override { return 2460000.5; }
    std::optional<std::string> login() const override { return std::string("builder"); }
};
libopencad::FieldDefinition expr;
expr.evaluator_id = "AcExpr";
expr.code = "\\AcExpr 2+3";
drawing.set_text_field(host_handle, "%<\\_FldIdx 0>%", {expr});
auto text = libopencad::resolve_field(drawing, host_handle, Host{});
```

### Semantic inventory

`<libopencad/semantic_inventory.hpp>` ports acadrust `semantic_inventory_v1`. `DxfDocument::semantic_inventory_v1()` visits header, symbol-table records, classes, entities, objects, summary info, ownership/extension-dictionary/reactor relationships and object XDATA without exposing raw file bytes or caches. `resolve(0)` is the document node. `summary_info()` reads HEADER drawing-property variables. See [native tests](tests/stage7_remaining.cpp) and [tests/upstream/local-stage7-remaining](tests/upstream/local-stage7-remaining).

```cpp
#include <libopencad/dxf_document.hpp>

auto inventory = drawing.semantic_inventory_v1();
std::size_t entities = 0;
inventory.visit([&](const libopencad::SemanticPartV1& part) {
    if (std::holds_alternative<libopencad::SemanticEntityV1>(part))
        ++entities;
});
```

### Nested copy, COUNT and notifications

`<libopencad/nested_copy.hpp>` maps xref `prefix|local` names for Insert (local after `|`) and Bind (`prefix$index$local`). `DxfDocument::localize_nested_copy_symbols` copies simple layers/linetypes and rewrites entity groups 8/6. `<libopencad/count.hpp>` counts model-space INSERT instances unique-by-transform, matches similar polylines/lines/splines/hatches, outlines inserts and hatch loops, parses `CountQuery` JSON, and evaluates `\AcCount` including block `key` and `boundaryObjectHandle`. `<libopencad/notification.hpp>` is a bounded message collector (1000 items, 2000 characters). See [native tests](tests/stage7_optional.cpp), [tests/stage7_remaining.cpp](tests/stage7_remaining.cpp), [tests/upstream/local-stage7-optional](tests/upstream/local-stage7-optional) and [tests/upstream/local-stage7-remaining](tests/upstream/local-stage7-remaining).

### Compound storage and VBA

`<libopencad/compound_file.hpp>` ports OLE compound files (magic `D0 CF 11 E0 A1 B1 1A E1`) and `StructuredStoragePayload` wrappers. `CompoundPropertySetStream` decodes and encodes typed OLE property sets, including unpadded `VT_LPSTR` vectors. Unknown variants keep original bytes; streams that are not property sets stay binary. `<libopencad/vba.hpp>` decompresses and compresses VBA containers. See [native tests](tests/stage7_optional.cpp) and [tests/stage7_remaining.cpp](tests/stage7_remaining.cpp).

```cpp
#include <libopencad/import.hpp>

auto drawing = libopencad::StlImporter::from_file("part.stl").import();
libopencad::ImportConfig config;
config.scale_factor = 0.001;
auto millimetres = libopencad::import_file("part.stl", config);
auto scene = libopencad::GltfImporter::from_file("model.glb").import();
```

`explode_entity(handle)` previews detached LINE/ARC/POINT/3DFACE results for lightweight and traditional polylines, Polygon Mesh, Polyface Mesh and MESH. It preserves line/arc geometry and visible properties, leaves identities unset, and does not clone extension dictionaries or XDATA. Width is not represented by the resulting centerline primitives. MESH decomposition retains triangles/quads and skips larger faces, following upstream standalone EXPLODE. Primitive types that Rust refuses to explode return an empty list; other unimplemented types throw. This is distinct from the approximation algorithms used by version conversion.

`explode_entity_into(handle, target, remove_source)` allocates and inserts all parts in a selected space/block/layout; `target` is an `EntityOptions`. The source remains by default. Optional source removal uses the existing reference and extension-dictionary cleanup rules; any insertion or deletion failure rolls back the whole operation. Successful insertion invalidates existing record pointers.

TEXT, MTEXT, ATTRIB/ATTDEF and INSERT/MINSERT also support translation, matrix transforms and `apply_mirror(transform, MirrorOptions)`. Uniform transforms also scale absolute inline `\H<h>;` height codes on MTEXT contents, embedded attribute MTEXT, and MULTILEADER `context.text_string`; relative `\H<k>x;` codes are left unchanged. Text mirroring defaults to readable text (`mirror_text = false`); an optional measured `text_width` overrides the Unicode width estimate used for alignment. INSERT transforms include attached attributes, OCS orientation, signed scales and array spacing. `get_transform()` returns the local-to-world affine transform and `array_points()` returns row-major OCS insertion points (at most 1,000,000 cells). Text operations follow upstream sizing and serialization behavior; they do not render glyphs.

LEADER, MULTILEADER, MLINE and TOLERANCE support atomic `translate`, `apply_transform` and `apply_mirror`, including geometric XDATA. Leader offsets rotate and scale without translation. MLINE reflection reverses its scale sign; a general matrix transform retains the authored multiline scale and segment parameters, following Rust.

Their `bounding_box()` queries retain upstream approximations: LEADER and MLINE use path vertices; MULTILEADER includes its content base, root connections and line vertices; TOLERANCE estimates an axis-aligned text rectangle using UTF-8 byte length and `^J` line count. They exclude rendered arrow, font, line-width and style extents. MLINE returns an empty optional for no vertices; an empty LEADER returns a zero box. `length()` measures the LEADER path or MLINE centerline, including the MLINE closing segment.

Some transformed values are in-memory fields rather than DXF fields: LEADER origin, TOLERANCE height/gap, MULTILEADER top-level text height and inactive content geometry. DXF reload restores upstream defaults (LEADER origin comes from its first vertex). Tests compare these values before serialization as well as comparing saved geometry. The pinned Rust transform updates a legacy MULTILEADER line-break cache while its DXF writer uses the unchanged per-segment break records; C++ preserves that serialized behavior. Root break points do transform. These annotation types are not yet included in recursive block expansion.

All nine DIMENSION variants support atomic translation, matrix transforms and mirrors, plus `measurement()`, `stored_measurement()`, `refresh_measurement()` and `bounding_box()`. Angular measurements return degrees; the stored/cache value uses radians. Linear and ordinate measurements project onto their OCS axes. Arc-length dimensions retain full-turn sweeps. Bounds include definition geometry, excluding rendered text and arrows.

General transforms update OCS insertion points, inverse-transpose normals, subtype geometry and measurement caches. Reflections preserve readable text-angle semantics; ordinate dimensions transform their own datum and relative text axes. Direct `translate` follows Rust's separate path and leaves the OCS insertion point and authored measurement cache alone (ordinate measurements are derived). `DimensionDefinition::insertion_rotation` survives in memory but is omitted by DXF, as upstream does. These operations do not regenerate dimension graphics blocks or add DIMENSION to recursive block expansion. See [dimension operation tests](tests/dimension_transform.cpp) and [Rust comparison](tests/upstream/src/dimension_transform.rs).

HATCH supports atomic `translate`, `apply_transform` and `apply_mirror(transform, MirrorOptions)`. Operations update OCS boundary coordinates, elevation, seeds, spline tangents, pattern lines and enabled gradient angles. Circular edges become elliptic edges under nonuniform scaling; reflected edges retain stored sweeps and reverse direction/bulge signs. Spline control-point Z values are weights and remain unchanged.

`MirrorOptions::mirror_hatch` defaults to `false` (MIRRHATCH=0): reflecting the boundary preserves explicit pattern lines, scale, gradient angle and recorded origin. Set it to `true` to reflect the pattern too. Both reflection modes retain the authored top-level pattern angle; individual line angles carry the reflected direction. Non-reflecting matrices passed to `apply_mirror` follow the ordinary transform rules.

`pattern_origin()` reads the first top-level ACAD XDATA 1010 point, defaulting to zero; nested payloads remain intact. `record_pattern_origin()` changes metadata only, while `set_pattern_origin()` translates pattern line bases by the origin delta. Invalid origin values return `false`. Solid hatch angle/scale values survive in memory but reset to DXF defaults after reload because solid output omits them.

Hatch transforms retain Rust's geometry limits: normals use the transformed direction, nonuniform transforms retain polyline bulge magnitudes, and ellipse axes follow the upstream length-based approximation under shear. They do not regenerate associative boundary entities or expand HATCH inside INSERT. A collapsed circular OCS axis that produces nonfinite ellipse geometry refuses atomically. See [HATCH operation tests](tests/hatch_transform.cpp) and [Rust comparison](tests/upstream/src/hatch_transform.rs).

SHAPE and PDF/DWF/DGN UNDERLAY support atomic `translate`, `apply_transform` and `apply_mirror`. SHAPE insertion is in OCS; UNDERLAY insertion is in WCS. General transforms update rotation and normalized direction normals; SHAPE size and all UNDERLAY scale components use the transformed world-X length, following Rust even for nonuniform scaling.

SHAPE reflection adjusts rotation, size, signed width factor and oblique angle, retaining thickness. UNDERLAY mirrors project the transformed local axes back into the new frame to recover signed scales; local clipping vertices, definition references and display settings remain unchanged. `DxfUnderlay::world_clip_boundary()` returns the corresponding WCS clipping vertices. A second reflection restores UNDERLAY world clipping geometry for valid planar frames. SHAPE retains Rust's oblique-size adjustments and does not promise this same involution.

These operations transform geometric XDATA, reject nonfinite/overflowing general edits atomically and leave identity/nonfinite mirrors untouched. They do not load SHX fonts or external PDF/DWF/DGN files, render content, regenerate bounds, or add these entities to recursive block expansion. General nonuniform/sheared transforms retain the upstream parameter-based approximation rather than an exact affine rendering. See [SHAPE/UNDERLAY tests](tests/shape_underlay_transform.cpp) and [Rust comparison](tests/upstream/src/shape_underlay_transform.rs).

IMAGE and WIPEOUT share `DxfRasterImage::translate`, `apply_transform` and `apply_mirror`. Insertion is a WCS point; U/V vectors carry orientation and per-pixel size and receive only the matrix's linear part. Pixel dimensions and local clipping coordinates remain unchanged. Scaling the vectors a second time would square the requested factor, so no extra scale multiplier is applied.

`corners()` and `bounding_box()` describe the full image rectangle in WCS, independent of clipping. `wipeout_world_boundary_vertices()` maps WIPEOUT pixel-space boundary vertices using its half-size offset and reversed Y convention; it rejects IMAGE, whose stored clipping coordinates are verified separately. Rectangular clipping keeps its two authored endpoints; this query does not expand them into four corners.

Operations update geometric XDATA atomically while preserving image definitions/reactors, display settings, clipping flags and reference handles. Identity/nonfinite mirrors do nothing; nonfinite/overflowing general transforms reject without partial edits. No image file decoding, pixel resampling, visible-region rendering or recursive INSERT expansion is added. See [raster transform tests](tests/raster_transform.cpp) and [Rust comparison](tests/upstream/src/raster_transform.rs).

VIEWPORT supports atomic `translate`, `apply_transform` and `apply_mirror`, including geometric XDATA. Direct `translate` moves only the paper-space center. A matrix transform also moves the view target and UCS origin, normalizes the transformed view direction/UCS axes, and scales both width and height by the transformed world-X length. A translation matrix therefore intentionally differs from direct translation, matching Rust.

View height, DCS view center, snap/grid settings, lens length, clipping planes, twist/elevation, status flags, frozen layers, clipping-boundary references and named UCS references remain unchanged. The referenced UCS table records, clipping entities and layout metadata are not transformed. Nonuniform/sheared transforms retain the upstream parameter-based behavior and do not promise an exact affine camera projection. Identity/nonfinite mirrors are no-ops; nonfinite or overflowing general edits reject atomically. See [VIEWPORT tests](tests/viewport_transform.cpp) and [Rust comparison](tests/upstream/src/viewport_transform.rs).

ACAD_TABLE supports `translate`, `apply_transform` and `apply_mirror`. Direct translation moves the insertion point; a matrix transform also normalizes the horizontal direction and normal and scales every row height and column width by the transformed world-X length. Nonuniform scale and shear follow Rust's parameter-based approximation. Cell contents, text heights, formats, merged ranges, cached cell geometry and reference handles remain unchanged. Geometric XDATA transforms once; nonfinite/overflowing general edits reject atomically, and identity/nonfinite mirrors are no-ops.

After changing table dimensions, call `document.synchronize_table_dimensions(handle)` **outside** `for_each` to update an existing TABLECONTENT adjunct. This atomic operation updates only row/column sizes, preserves modern multi-content cells and references, and rejects mismatched dimensions. A missing adjunct is a no-op. Record references must be reacquired after synchronization. Transform operations preserve stored cell geometry and anonymous block geometry; they do not recalculate table layout or render content. Call `refresh_table_block(handle)` separately to regenerate the `*T` SOLID/MTEXT/LINE graphics. See [table transform tests](tests/table_transform.cpp) and [Rust comparison](tests/upstream/src/table_transform.rs).

OLE2FRAME geometry is available through `DxfExtendedEntity::translate`, `apply_transform` and `apply_mirror`. Both stored corner points receive the transform, with no reordering or axis-aligned rectangle reconstruction. Geometric XDATA follows the entity. Nonfinite/overflowing general edits reject atomically; identity/nonfinite mirrors do nothing. Missing coordinate components use the upstream defaults `(1,1,0)` / `(0,0,0)` independently.

The edit preserves raw OLE payload bytes, including compound-file sector padding, unknown wrapper data, size/chunk fields, application/type/aspect flags, ownership, extension dictionaries and reactors. The authoritative DXF corner fields change; embedded wrapper bytes remain verbatim, including any historical geometry metadata. Rust's writer may refresh that wrapper metadata when it serializes, so validation compares decoded geometry/storage as well as original payload bytes. This API does not render OLE, edit compound storage, regenerate previews, or add DWG/recursive-block support. See [OLE transform tests](tests/ole_transform.cpp) and [Rust readback](tests/upstream/src/ole_transform.rs).

The same extended-entity methods now cover LIGHT, SECTIONLINE, DRAWINGVIEW, SECTIONOBJECT, ARCALIGNEDTEXT, RTEXT, POSITIONMARKER, COORDINATION_MODEL and both point-cloud classes. `bounding_box()` provides the upstream stored-geometry bounds. `<libopencad/extended_geometry.hpp>` also exposes detached definition transforms/bounds and `point_cloud_world_corners` for point-cloud placements.

SECTIONLINE and DRAWINGVIEW direct translation is intentionally a no-op, including XDATA, matching upstream EntityType dispatch; use a matrix translation to move them. Matrix operations transform section points/label offsets, recompute view-border XY bounds/rotation, use OCS for arc-aligned/remote text, and transform marker embedded text fields retained by DXF. COORDINATION_MODEL matrix operations pre-multiply placement; direct translation leaves placement unchanged. Old point clouds transform world extents and UCS; extended point clouds transform placement while retaining local extents/crops. Metadata-only CAMERA, OLEFRAME, LAYOUTPRINTCONFIG and Format records retain their payloads while geometric XDATA follows the operation.

LIGHT photometric vectors are evaluated without normalization. Upstream maps their Z component to integer DXF group 63 and its point reader falls back to `(1,1,1)` for these nonstandard coordinate codes. Native numeric APIs retain the actual stored vector; record edits reject a nonintegral or out-of-int16-range Z atomically instead of truncating it. Detached definitions can represent the full numeric result. Old point clouds with source-file lists omit extents/UCS fields on save under the upstream schema; the stored-record query therefore reflects the saved projection. These are explicitly tested boundaries, not a general rendering or geometry-regeneration implementation. See [extended geometry tests](tests/extended_transform.cpp) and [Rust comparisons](tests/upstream/src/extended_transform.rs).

`expand_insert_into(handle, target, remove_source, expand_arrays, max_entities)` recursively expands supported block members into model space, paper space or a named block. It composes nested transforms with each block's base point, inherits layer 0 and ByBlock properties, measures circle/arc scaling in each curve's OCS plane, converts nonuniform circles/arcs to ellipses, and decomposes bulged polylines where necessary. Arrays require explicit opt-in. Instance ATTRIB values are excluded, while definition ATTDEF records remain definitions. Source entities and definitions remain unchanged unless source deletion is requested. Cycles, xrefs, non-explodable blocks, unsupported member types and resource limits refuse atomically. Nonuniform curve expansion requires R13+ because R12 has no ELLIPSE.

Ordinary expanded members receive fresh handles and cloned extension graphs. Native expansion writes final leaf geometry directly; Rust uses an intermediate anonymous block for nested shear. The resulting leaves follow Rust's graph boundary: shear fallbacks detach extension dictionaries, while ordinary expansion clones/remaps them. Persistent entity reactors are cleared from detached copies. Expansion also accepts LEADER/TOLERANCE/MLINE/MULTILEADER, dimensions, HATCH/MPOLYGON, SHAPE/UNDERLAY, IMAGE/WIPEOUT, VIEWPORT, ACAD_TABLE and the currently typed extended entities. It remaps owned payload references, preserves geometrical XDATA values, synchronizes copied table dimensions and retains newly assigned viewport IDs. Nested shear of these specialized members rejects atomically when a member owns auxiliary objects; its detached ownership policy is not implemented. Modeler and untyped application-specific members remain unsupported. See [specialized expansion tests](tests/expand_specialized.cpp).

`DxfDocument::audit()` reports identities, stored indexes, common reference/ownership problems, handle-seed problems and basic geometry invariants without changing the drawing. `repair()` returns before/after reports, performed actions and unresolved issues. It assigns missing identities, repairs unambiguous missing entity owners, clears stale block attribute flags, rebuilds derived indexes and synchronizes the allocator. APPID entries sharing a handle receive fresh identities while other records retain theirs, because application references use names. Ordinary reading preserves raw identities; call `repair()` explicitly for this correction (Rust performs it during document finalization). Other duplicate or invalid identities, uncertain references, owner cycles and invalid geometry remain unresolved. Repairs are atomic and do not discard geometry. See `<libopencad/audit.hpp>` for report types.

Audit uses the native record model: it also inspects compound child identities and raw reference fields. Rust's independent object-map and table-key mismatch categories have no separate mutable counterpart in this model. This is a structural audit and a basic geometry check, not an AutoCAD AUDIT substitute or a topology validator.

### Read-only dynamic block properties

DXF documents expose `is_dynamic_block`, `dynamic_block_definition`, `dynamic_definition_for_insert`, `dynamic_block_graph`, `block_visibility_parameter`, `dynamic_visibility_for_insert` and `dynamic_block_properties`. Handles are numeric `std::uint64_t` values. With no INSERT argument, property queries return definition defaults. Supplying an INSERT overlays its saved XRECORD cache; the library does not evaluate dynamic actions or regenerate geometry.

The projection includes linear, polar, rotation, XY, point, flip, lookup, visibility and user parameters, plus supported distance constraints. It retains property types, labels, units, choices, display/read-only flags and selected-state visibility. Uniform reflected scales use absolute distance scale and squared area scale; nonuniform references return an empty collection, following upstream. Missing nodes, invalid lookup tables, malformed caches and unsupported constraints reject explicitly. Queries leave stored records unchanged. Evaluation graph nodes/edges and visibility membership have typed read-only views in `<libopencad/dynamic_block.hpp>`.

DXF coverage is ASCII/binary saved-state queries. `DwgDocument` exposes the same query names for R2004+ drawings, plus typed object access through `dynamic_record` / `insert` and in-place `update_dynamic_record`. Neither path evaluates actions or regenerates geometry. Empty and angular-constraint *entity* records can be rewritten or allocated; other dynamic entity families refuse rewrite. The pinned Rust DXF reader loses some USERPARAMETER short/point values because of repeated value-code fields; the native projection follows that reader while preserving the original raw record. Native graph/cache validation rejects incomplete records that Rust may partially default. See [native DXF tests](tests/dynamic_properties.cpp), [Rust DXF oracle](tests/upstream/src/dynamic_properties.rs), [native DWG tests](tests/dwg_dynamic.cpp) and [Rust DWG readback](tests/upstream/src/dwg_dynamic.rs).

### Typed DXF dynamic-block objects

`DxfDocument` exposes `dynamic_object`, `update_dynamic_object`, `dynamic_object_references`, `dynamic_objects_referencing` and `remap_dynamic_handles`. Handles are numeric `std::uint64_t` values. The codec ports the pinned acadrust OBJECTS families (parameters, grips, actions, visibility, lookup/stretch, constraint parameters, representation/proxy nodes and properties tables). Owner, reactor, xdictionary and payload handles are visited together. Unknown families, name/handle mismatches and CLASS entity conflicts refuse rewrite without changing the document. Evaluation graphs, ACSH history and `*PARAMETERENTITY` / `*GRIPENTITY` records are not part of this API.

Polar-stretch extra ids have no DXF group: writes emit 77 as 0, and a nonzero 77 on read throws. USERPARAMETER repeated 70/90 fields last-win, matching the pinned Rust reader. See `<libopencad/dynamic_block.hpp>`, [native DXF tests](tests/dynamic_objects.cpp) and [Rust DXF oracle](tests/upstream/src/dynamic_objects.rs). DWG typed rewrite remains `update_dynamic_record`.

### Typed DXF associative objects

`DxfDocument` and `DwgDocument` expose `associative_object`, `update_associative_object`, `associative_references`, `associative_objects_referencing`, `remap_associative_handles` and `associative_owned_network`. Handles are numeric `std::uint64_t` values. The codec ports the pinned acadrust inheritance families (networks, actions, dependencies, 2D constraint groups, array/view-representation/smart-center bodies and related parameters). Owner, reactor, xdictionary and payload handles are visited together. Owned-network walks detect cycles. Unknown families, name/handle mismatches and unsupported constraint node classes refuse rewrite. Empty vs handle-at-330 value parameters remain lossy on DXF, matching the Rust reader. DXF SAT inside `AssocAsmBodyActionParam` is stored as text; wires/silhouettes are not authored. DWG `ASSOCASMBODYACTIONPARAM` now reads and writes inline SAT/SAB (R2004+ parseable SAT is converted to SAB), handle-stream materials, the R2013 revision block, and a SoftPointer history handle for binary payloads. There is no `insert_associative_object`; action evaluation and constraint solving stay saved-state only.

DXF coverage is AC1024/AC1032 ASCII and binary. DWG coverage is AC1021/AC1024/AC1027/AC1032. Constraint groups use the R2010 inline layout before AC1027 and the registry/root layout from R2013. See `<libopencad/associative.hpp>`, [native DXF tests](tests/associative.cpp), [Rust DXF oracle](tests/upstream/src/associative.rs), [native DWG tests](tests/dwg_associative.cpp) and [Rust DWG readback](tests/upstream/src/dwg_associative.rs).

### AEC/Arch constructors and geometry

`DwgDocument::create_wall` / `create_door` / `create_window` author AC1032 drawings with the pinned style dictionaries, Arch layers and type-6 proxy graphics. Same-version DWG I/O keeps opaque class bodies. `set_wall` / `set_door` / `set_window` / `set_opening`, `set_stair` / `set_member`, leftover bounds setters (`set_window_assembly` through `set_ceiling_grid`), `translate_architecture`, `set_profile_bounds`, `reshape_rectangle`, `add_wall`, `continuous_walls` and `set_continuous_wall` follow the upstream straight-wall and opening layouts. Stair and leftover-kind bounds setters match rust `setters.rs`: finite positive values then `UnsupportedLayout`. `set_member` requires a decoded rectangular structural member. `decode()` / `decode_fields()` expose the shared AEC envelope and named numbers. Doors reject translation and profile-bounds edits. Failed geometry edits leave the document unchanged.

```cpp
auto drawing = libopencad::DwgDocument::create_wall({0, 0, 0}, {1000, 0, 0}, 240, 3000);
auto wall = drawing.objects().arch_entities.begin()->first;
drawing.translate_architecture(wall, {10, 20, 0});
drawing.add_wall({1010, 20, 0}, {1010, 2020, 0}, 240, 3000);
```

Public constructors remain wall/door/window only (`create_opening` / `create_stair` / `create_member` are not invented). Cross-version AEC authoring, sample-drawing-only stair/member success paths, action evaluation and constraint solving are not implemented. DXF writes omit Arch objects that have no class body, skip NOD entries that named those objects, rewrite XDATA group 1005 of omitted handles to `0`, convert graphics-only entities to `ACAD_PROXY_ENTITY`, and refuse R12 before any bytes. See `<libopencad/arch.hpp>`, [native tests](tests/architecture.cpp) and [Rust readback](tests/upstream/src/architecture.rs).

### Civil COGO points, point groups, siteless alignments and TIN surfaces

`DwgDocument::ensure_civil`, `add_cogo_point` / `set_cogo_point` / `remove_cogo_point`, `cogo_points` and `add_point_group` / `set_point_group` author AC1032 DWG Civil 3D COGO points and number-list point groups from named fields, matching acadrust `classes/civil/create`. COGO construction requires the drawing's Civil settings/style tree (`AeccPoints` last two references = default label/point styles, `AeccCreatePoints`, and the `AeccDbCogoPoint` class node). A new drawing without that tree errors and leaves the drawing unchanged. `add_alignment` writes siteless alignments of fixed lines (`AECC_ALIGNMENT`) plus a companion `AECC_FEATURE_STYLE` under the siteless node. `add_alignment_with_curves` adds a free arc or spiral-curve-spiral at each inner vertex (`VertexCurve`). A drawing that lacks the alignment style collection (style-tree key 93) or the siteless node (root key 336) gets them; a drawing without any style tree first gets the point-style/class/settings records a new drawing needs. `alignment_name` / `alignment_start_station` / `alignment_polylines` / `alignment_elements` / `alignment_pieces` read those entities back. `add_profile` / `add_profile_with_curves` write layout profiles (`AECC_PROFILE`) of fixed tangents with free symmetric parabolas, circular curves and asymmetric parabolas onto an alignment this library wrote, and rewrite that alignment's profile list. A drawing without a profile style collection (style-tree key 74) gets one, including the default `AECC_PROFILE_STYLE` and its `AECC_VALIGNMENT_STYLE_EXTENSION`; that is a side effect of `add_profile`, not a public style constructor. `profile_name` / `profile_pieces` read those entities back. `add_tin_surface` triangulates three or more finite points that are distinct in plan into an `AECC_TIN_SURFACE` with four bounding corners and hidden corner triangles. A drawing without a surface style collection (style-tree key 77) gets one, including the default `AECC_SURFACE_STYLE`; that is a side effect of `add_tin_surface`, not a public style constructor. `tin_surface` reads the stored triangulation. `add_point_group_query` / `set_point_group_query` write number-list, elevation and raw-description groups (`PointQuery`); later COGO edits refresh plain groups whose query this library wrote. Description patterns are whole-string `*` / `?` matches, case ignored. Public style constructors and name/full-description queries are not written. Failed edits leave the document unchanged. DXF create is not implemented. `AECC_COGO_POINT`, `AECC_ALIGNMENT`, `AECC_PROFILE` and `AECC_TIN_SURFACE` are stored as architecture entities. See `<libopencad/civil.hpp>`, [native tests](tests/civil.cpp) and the local-checkout Rust oracle [tests/upstream/local-civil](tests/upstream/local-civil).

### Render, background and section-manager class objects

`DxfClassObject` / `DwgDocument` typed codecs cover `RENDERSETTINGS`, `MENTALRAYRENDERSETTINGS`, `RAPIDRTRENDERSETTINGS`, `SECTION_MANAGER`, the six background families, `RENDERENTRY` / `RENDERENVIRONMENT` / `RENDERGLOBAL`, `LIGHTLIST`, `SUN`, `ACDBMOTIONPATH` / `ACDBCURVEPATH` / `ACDBPOINTPATH` (decode aliases `MOTIONPATH` / `CURVEPATH` / `POINTPATH`), `TVDEVICEPROPERTIES`, `LAYERFILTER`, `PARTIAL_VIEWING_INDEX`, `SECTION_SETTINGS`, `SUNSTUDY`, `DATATABLE` (decode alias `ACDBDATATABLE`), `GEOMAPIMAGE`, `ACDBDETAILVIEWSTYLE` (decode alias `DETAILVIEWSTYLE`), `ACDBSECTIONVIEWSTYLE` (decode alias `SECTIONVIEWSTYLE`) and `DATALINK`, plus Object Data `IRD_DSC_DICT` / `IRD_DSC_RECORD` / `IRD_OBJ_RECORD` and ViewRep `ACDBVIEWREPSOURCEMGR` / `ACDBVIEWREPSTANDARD` / `ACDBVIEWREPORIENTATIONDEF` / `ACDBVIEWREPORIENTATION` / `ACDBVIEWREPSECTIONDEFINITION` / `ACDBSYMODELSPACEVIEWSELSET` / `ACDBVIEWREPMODELSPACESOURCE` / `ACDBVIEWREP`. Construction requires R2004+. Plain RENDERSETTINGS stores `class_version+1` and the predefined flag from R2013; MentalRay has no predefined bit in the base layout; RapidRT stores its class version as-is and keeps the predefined flag after its own fields. DXF RapidRT uses group 70 for target/lighting/filter; DWG uses `bit_long`. Section-manager handles are DXF 330 / DWG HardPointer. IBL construction writes `RAPIDRTRENDERENVIRONMENT` (`IBL_BACKGROUND` is a decode alias). Light-list entries use DXF group 5. Image-background DXF stores offset/scale as sequential 140–143 (native round-trip); the pinned rust DXF reader still uses 140/150 so rust DXF Y values are dropped. Path construction writes the `ACDB*` names. Section-settings destination blocks are DXF 331 / DWG SoftPointer; type count is at most 4. Sun-study sheet-set fields are written only when `output_type == 0`; range times are DXF-always-read and DWG-conditional. Data-table cells are Integer/Double/Text/Point/ObjectId only. GeoMap origin/size are DXF 10/13 and DWG raw doubles. Detail/section view styles write `AcDbModelDocViewStyle` then the style subclass; base display name and flags are AC1032+ (DXF and DWG). DXF and DWG field order differ: DXF puts detail arrows with the identifier group and section identifier/arrow positions before the plane group; DWG puts those fields later. Hatch background color is a second sequential DXF 62. Data-link custom targets are DXF 330 / DWG SoftPointer; the hard owner is DXF 360 / DWG HardOwnership. DXF always writes `CUSTOMDATA`/`DATAMAP`; DWG writes the map only when nonempty and forces option bit 8 to match. Object Data dictionary entries are DXF 3+350/360; descriptor/record payloads are 1004 chunks of at most 127 bytes. DWG dictionary keys are sorted and use SoftOwnership; descriptor/record owners are relative handles. ViewRep ModelSpaceSource.model is DXF group 5 / DWG MAIN Undefined. ViewRep DWG owners are relative; sibling ViewRep types stay absolute SoftPointer. Convert keeps the existing RENDERSETTINGS wire rules, drops view-style 300/90 below AC1032, and rewrites DATALINK ACVALUE frames below/above AC1021 without calling `set_definition`. Generic `add_registered_object` refuses these names. `DxfDocument` / `DwgDocument` also expose the rust CadDocument Object Data graph APIs: `object_data_tables`, `add_object_data_table`, `attach_object_data` and `object_data`. The first table creates NOD `IrdDscDictionary` with Map 3D's five built-in type descriptors; records live on the entity extension dictionary under `*A1`, `*A2`, … keys that never reuse a deleted index. SpatialIndex and VBA stay generic or unimplemented; they are recorded as deferred Stage 6 leftovers rather than invented APIs. See `<libopencad/class_object.hpp>`, `<libopencad/object_data.hpp>`, [native tests](tests/class_objects.cpp), [Object Data graph tests](tests/object_data.cpp) and [Rust readback](tests/upstream/src/class_objects.rs).

### Native ACIS SAT and SAB

`<libopencad/acis.hpp>` exposes standalone C++ APIs in `libopencad::acis`. `SatDocument::parse` and `to_sat_string` read/write normalized SAT records, including legacy headers and v400 topology layouts. For v700+ record strings, counted payloads retain leading/trailing whitespace, line breaks, `#` and end-marker text; malformed lengths reject. Typed value views expose topology links and analytic geometry. `placement` / `set_placement` use the SAT row-vector convention; `complete_brep_links`, `strip_for_sab` and `split_bodies` maintain the supported reference graphs. `validate` checks pointer bounds and does not establish complete B-rep validity.

`SabReader::read_with_consumed` reads ACIS/ASM packets and reports their byte boundary. Numeric widths, string encodings and coordinate payloads remain available as raw tagged tokens. `SabWriter::write` emits typed binary records, infers numeric roles in authored spline subtypes and upgrades legacy analytic/interpolated edge parameters. Unsupported binary tags report an offset. Legacy spline edges use the same bounded nearest-parameter search and reversal rules as Rust.

`SatIntCurve`, `SatPCurve` and `SatSplineSurface` expose rational/nonrational control nets, shared subtype references, curve-on-surface support and offsets. Curve samples use De Boor evaluation; supported swept descriptions recover exact extrusion, rational revolution and linear-sum control nets. Unsupported descriptions return an empty optional. Sampling and expanded control/knot storage have resource limits; constructors reject invalid dimensions and nonfinite values before modifying the document.

`add_spline_curve`, `add_spline_surface`, `add_offset_spline_surface` and `add_pcurve` construct native SAT/SAB records. `SplineNet` supplies tensor-product surface controls and full knot multiplicities. Views own snapshots; document-wide subtype lookups observe subsequent record edits.

Native primitive constructors create boxes, wedges, pyramids, cylinders, cones, spheres, tori and closed planar bodies. They are compared with Rust topology and SAT/SAB output. For example:

```cpp
#include <libopencad/acis.hpp>

auto solid = libopencad::acis::build_box({1, 2, 3}, 4, 6, 8);
auto placement = solid.placement();
placement.translation = {10, 20, 30};
solid.set_placement(placement);
auto sat = solid.to_sat_string();
auto sab = libopencad::acis::SabWriter::write(solid);
```

`DxfDocument` integrates native `3DSOLID`, `REGION`, `BODY`, generic `SURFACE`, `PLANESURFACE`, `EXTRUDEDSURFACE`, `LOFTEDSURFACE`, `REVOLVEDSURFACE`, `SWEPTSURFACE` and `NURBSURFACE` geometry. `modeler_geometry` decodes inline SAT before R2013 or the entity-associated ACDSDATA SAB payload from R2013. `set_modeler_geometry` and `transform_modeler` update the payload atomically; surface isoline counts are accessible through `DxfModelerEntity`. Missing geometry returns an empty optional; ambiguous handles, conflicting payloads and malformed lengths are rejected.

```cpp
#include <libopencad/dxf_document.hpp>

auto document = libopencad::DxfDocument::create();
auto handle = document.add_modeler_entity(libopencad::ModelerKind::Solid3D,
    libopencad::acis::build_box({1, 2, 3}, 4, 6, 8));
document.transform_modeler(handle,
    libopencad::Transform::from_translation({10, 20, 30}));
```

Same-version ASCII/binary saves cover R13–R2018 for solids/regions/bodies and R2007–R2018 for all seven surface kinds. Creation strips unsupported modeler metadata when preparing a new SAB, following upstream; edits to existing SAB retain unknown tagged records. Copy/delete operations include the associated ACDSDATA record and the existing owned-object graph rules. No Rust runtime is required.

`DxfModelerEntity::surface_definition` / `set_surface_definition` expose typed extrusion, loft, revolution, sweep and NURBS construction parameters from `<libopencad/surface.hpp>`. Matrices use column-major storage and are transposed at the DXF boundary. Edits preserve embedded profile records and unrelated fields. `transform_modeler` composes the loft construction matrix as well as the ACIS placement. Construction-parameter edits do not regenerate the B-rep; new surface entities require a caller-supplied ACIS body. The surface-definition interface preserves its embedded profiles; the document interface below edits their geometry. History evaluation is not implemented.

`DxfDocument::surface_profiles(handle)` and `set_surface_profiles(handle, inputs)` read and edit stored inputs of EXTRUDEDSURFACE, SWEPTSURFACE, REVOLVEDSURFACE and LOFTEDSURFACE in R2007–R2018 DXF. `SurfaceProfiles` contains the extrusion/revolution/sweep `profile`, sweep/loft `path`, and ordered loft `sections` and `guides`. They use the same `EmbeddedProfile` curve, inline REGION, SAT BODY and opaque variants as history geometry. For example:

```cpp
auto inputs = document.surface_profiles(surface_handle);
auto& outline = std::get<libopencad::ProfilePolyline>(*inputs.profile);
outline.vertices[0].location.x += 5;
outline.vertices.back().bulge = -0.5;
document.set_surface_profiles(surface_handle, inputs);
```

The document query resolves the R2018-encoded `CADCODEC_LOFT_CURVES_V1` fallback only when native loft inputs are empty. Edits synchronize an existing owned fallback, including clearing it, so removed profiles cannot reappear on Rust readback. SAT bodies and opaque inputs from older versions cannot form a safe R2018 fallback; those edits retain authoritative native inputs and empty the optional fallback. Dictionary/XRECORD handles and loft database-reference roles remain intact. The API edits stored inputs and does not materialize referenced entities; a subsequent Rust save may materialize empty input roles from database references. `DxfModelerEntity::surface_profiles()` inspects native subclass fields only. Use the document setter for edits.

Profile edits preserve scalar parameters, placement matrices, isolines, the ACIS body, history links and neighboring entities. They retain legacy sweep/revolution header fields and distinguish exact loft bit lengths from byte-padded profile bodies. Malformed framing, non-finite geometry, unsafe opaque transcoding, and shared/private metadata writes reject atomically. Editing construction inputs does not rebuild the surface B-rep. See [native profile tests](tests/surface_profiles.cpp) and [Rust readback comparisons](tests/upstream/src/surface_profiles.rs).

`explode_modeler(handle)` returns detached LINE/CIRCLE/ELLIPSE/SPLINE boundaries for REGIONs, including homogeneous weights, trimmed fit-point samples and body placement. For SAT-backed solids, bodies and plane surfaces it returns `ModelerWireFace` values, matching Rust's wire-only planar/curved face representation. These face results contain no new ACIS body. The upstream face-only path produces no results for SAB-only solids/bodies/plane surfaces; REGION accepts both SAT and SAB. Generic and other specialized surfaces have no decomposition in that upstream path. Cyclic topology walks terminate and unsupported edges are skipped.

`DwgDocument::modeler(handle)` exposes R13–R2018 REGION, BODY and 3DSOLID data: resolved owner, SAT/SAB encoding, wireframes, silhouettes, material bindings, revision fields, secondary modeler data and the stored history handle. `modeler_geometry(handle)` parses the native ACIS body; `modeler_reference_point(handle)` returns the stored anchor or derives the placed point-bounds centre, following Rust. R2013+ reads attach external AcDs bodies by full 64-bit record owner, including single-row tables and multi-page blob references. Indexed containers use validated segment-directory bounds; SAB payloads and unindexed trailers cannot introduce data tables or shadow blob pages. Conflicting bodies for the same owner produce a diagnostic instead of overwriting geometry; byte-identical duplicates are accepted. A parsed table keeps its explicit ownership and length checks even when only one body is present. SAB boundaries follow typed tokens and declared body lengths, so signature/end-marker bytes inside string payloads are preserved. Missing or ambiguous bodies produce per-handle diagnostics in `objects().modeler_errors`; original records remain available for unchanged saves. Corrupt explicitly framed bodies are excluded from orphan recovery, and damaged outer SAB framing cannot be replaced by a nested payload.

```cpp
auto drawing = libopencad::DwgDocument::read(input);
auto replacement = libopencad::acis::build_torus({9, -4, 7}, 8, 2);
drawing.set_modeler_geometry(handle, replacement);
drawing.write(output);
```

Geometry replacement supports R13–R2010 inline bodies, including previously empty bodies, and existing R2013/R2018 bodies with an indexed AcDs record table. Primary and embedded REGION SAT streams share counted-string-aware framing: record separators and end markers never split a modern string payload. SAT transport accepts printable ASCII plus tab/CR/LF; non-ASCII or binary SAT bytes are diagnosed on read and rejected before mutation on write. R2004+ SAB modeler geometry preserves Unicode. An implicit SAT 700-to-400 conversion rejects string fields that cannot round-trip through the legacy schema instead of silently dropping them. Contiguous length-prefixed bodies and verified external-page references are editable. A changed external-page body is promoted to inline storage; its old pages remain verbatim to preserve potentially shared data. The latter updates record offsets, segment sizes, segment-index locations and the paged container while retaining neighboring records and unrelated section bytes. R2013+ empty bodies can receive new records, including creation of a missing AcDs section. New records rebuild the data, handle-search, data-index and segment-index tables. This authoring path validates the known native schema and limits new owner handles to the upstream reader's 32-bit range; unknown or inconsistent layouts reject atomically. `clear_modeler_geometry(handle)` removes the primary geometry and its material bindings, retaining the entity, cached display geometry, secondary body and history. Cleared bodies can be filled again. Recognizable corrupt page references are rejected instead of entering the upstream's order-based recovery path. Page offsets must agree with the assembled payload position; duplicate page IDs, including damaged duplicate frames, reject. Reading and replacement use the same page validation, and a corrupt payload length cannot extend a fully sized segment into its neighbor. Replacing a B-rep does not regenerate wireframe caches or evaluate attached history. R13–R2000 normalize authored SAT 700 headers to SAT 400 as in the upstream DWG writer.

`DwgDocument::surface(handle)` and `update_surface(handle, value)` expose R2004–R2018 SURFACE, PLANESURFACE, EXTRUDEDSURFACE, LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE and NURBSURFACE construction inputs. Class lookup follows the positional CLASSES table. Isolines, transforms, construction options and typed embedded profiles can be edited without changing the primary B-rep or ownership. NURB control-hull fields require R2013+. Legacy DXF-only profile prefixes, incompatible parameter variants and nonfinite values are rejected before commit. Malformed surface data produces a per-handle error while neighboring entities and original bytes remain available. See [native surface tests](tests/dwg_surface.cpp) and [Rust surface readback](tests/upstream/src/dwg_surface.rs).

`DwgDocument::history_record(handle)` exposes R2007–R2018 history roots, the 15 operation families and evaluation graphs, including common metadata, owners, reactors, materials, evaluation values, ordered edges and typed embedded inputs. `modeler_history_graph(entity_handle)` follows a 3DSOLID's stored history handle and returns its owned nodes in step/handle order. `update_history_inputs(handle, parameters, profiles)` replaces operation parameters and embedded inputs transactionally while preserving the common/reference streams and primary entity geometry. BREP operations can replace their standalone inline body; cached/revision-bearing BREP operation payloads require a later adapter. Embedded fit-point spline flags follow the upstream writer's normalization.

DWG surface-history queries resolve `CADCODEC_SURFACE_HISTORY_V1` links, including the XRECORD handle stream's precedence over its raw data. `set_surface_history(surface, root)` detaches or restores an existing owned root link. R2007–R2018 `history_loft_settings` / `set_history_loft_settings` expose versioned loft options, section counts and the R2018-encoded metadata path. Edits synchronize native draft parameters and transcode supported paths into the drawing version. `update_history_inputs` also synchronizes existing loft metadata when replacing native inputs. Queries can fall back to the native path when metadata has no path. Passing an empty path to `set_history_loft_settings` explicitly clears both the metadata and native path; section and guide profiles remain unchanged.

`set_history_active_node`, `set_history_evaluation_graph`, `set_history_node_expression` and `set_history_material` edit stored references to existing, appropriately owned objects. Active selection uses the operation evaluation ID (or step-ID fallback), independently of evaluation-graph node IDs. They preserve IDs, edges, parameter data, neighboring objects and the primary B-rep. They do not execute the graph. R2004–R2018 loft surface reads restore valid fallback profiles only when native inputs are empty; surface edits synchronize an existing `CADCODEC_LOFT_CURVES_V1` record. Clearing all inputs clears that fallback. SAT bodies or opaque older-version inputs disable the optional R2018 fallback while retaining authoritative native inputs.

Known shared dictionaries/records, foreign owners, unknown metadata schema versions, invalid numeric/path data and unsafe opaque transcoding reject before changing the drawing. `set_surface_history` and `set_history_loft_settings` now allocate a missing owned DICTIONARY/XRECORD when the owner is otherwise valid. Tessellation and kernel operations remain separate work. See [native metadata tests](tests/dwg_history_metadata.cpp), [independent Rust metadata comparisons](tests/upstream/src/dwg_history_metadata.rs), [native history tests](tests/dwg_history.cpp) and [Rust history comparisons](tests/upstream/src/dwg_history.rs). These interfaces do not add cross-version modeler conversion. See [DWG modeler tests](tests/dwg_modeler.cpp) and [independent Rust readback](tests/upstream/src/dwg_modeler.rs).

`DwgDocument::add_modeler(kind, geometry, target)` creates REGION, BODY or 3DSOLID objects in R13–R2018 DWG. `add_surface(inputs, geometry, target)` creates all seven supported surface classes in R2004–R2018, registering a missing class when necessary. A target of zero selects model space; an existing block-record or layout handle selects another space. Empty drawings can be obtained with `create_dwg(version, {})`. These operations allocate unique handles using the header seed and existing objects, update block members/legacy links, and create modern AcDs associations when required.

`copy_modeler(handle, target, &remapping)` copies the entity and its supported owned history and DICTIONARY/XRECORD graph. The optional map contains every old/new object handle. Internal references are remapped while external material and other shared references remain unchanged. `move_entity` also accepts modelers/surfaces, retaining their identity and AcDs association. `remove_modeler` removes the entity, supported owned graph and AcDs row, repairs block lists and old first/last links, and clears supported surviving reactor arrays. Other surviving references in object handle streams or recognized XRECORD fields refuse deletion rather than leaving those references dangling. For XRECORD codes 330–369, deletion checks the corresponding handle-stream reference first and falls back to the raw value only when no corresponding entry exists. Native XRECORD strings retain their per-string code-page byte on R2004 and UTF-16 encoding on R2007+.

```cpp
auto solid = drawing.add_modeler(libopencad::ModelerKind::Solid3D,
    libopencad::acis::build_box({1, 2, 3}, 4, 5, 6));
std::map<std::uint64_t, std::uint64_t> copied_handles;
auto copy = drawing.copy_modeler(solid, drawing.objects().paper_space, &copied_handles);
drawing.move_entity(copy, "*Model_Space");
drawing.remove_modeler(solid);
```

Lifecycle edits are transactional and retain unrelated records, section gaps, geometry caches and metadata. They maintain class instance counts without removing unused class definitions. Copies currently reject XDATA-bearing owned records and private/foreign dictionary children whose references cannot be remapped safely. Unknown AcDs schemas, xref destinations and unclassifiable legacy reference streams also reject. Creating a modeler does not automatically create construction history or extension metadata; it stores the supplied B-rep and surface inputs without evaluation. See [native lifecycle tests](tests/dwg_lifecycle.cpp), [Rust lifecycle comparisons](tests/upstream/src/dwg_lifecycle.rs) and [fixture provenance](tests/data/dwg/lifecycle/README.md).

`DwgDocument::create_solid_history(entity, operation, profiles)` allocates a history root, evaluation graph and first operation, registering missing `ACSH_*` / `ACAD_EVALUATION_GRAPH` classes. 3DSOLID objects (and REGION/BODY on R2007–R2010) store a SoftPointer history handle. Surfaces attach through `CADCODEC_SURFACE_HISTORY_V1`. `append_solid_history` assigns the next step/evaluation IDs and links to the latest result; an earlier node with outgoing edges is rejected. R2013+ REGION/BODY, R2004 and earlier, and graphless appends reject without changing the document. Optional `SurfaceProfiles` carry DWG typed embedded inputs; loft `settings` are stored as `CADCODEC_LOFT_OPTIONS_V1`. Replacing an owned graph removes it; failures leave the original bytes unchanged. These APIs edit stored construction history and leave entity ACIS geometry unchanged. See [native DWG authoring tests](tests/dwg_history_author.cpp) and [independent Rust readback](tests/upstream/src/dwg_history_author.rs).

`DwgDocument` also exposes the DXF dynamic-block saved-state queries on R2004+ drawings. `dynamic_record(handle)` and `insert(handle)` return typed objects/INSERT entities; malformed payloads are isolated in `objects().dynamic_errors` and `objects().insert_errors`. `update_dynamic_record` rewrites an existing typed object or supported dynamic entity while preserving its handle and registered class. The supplied name, kind, entity flag and payload must agree; empty-entity records require an empty payload. Invalid changes reject without changing the document. Evaluation graphs remain history records (`ACAD_EVALUATION_GRAPH`). Queries overlay a saved INSERT XRECORD cache when an insertion handle is supplied; they do not evaluate actions. See [native DWG dynamic tests](tests/dwg_dynamic.cpp), [independent Rust readback](tests/upstream/src/dwg_dynamic.rs) and [fixture provenance](tests/data/dwg/dynamic/README.md).


### Modeler history graphs and references

`DxfDocument::solid_history_graph(entity)` resolves a modeler's history root, owned operation nodes and evaluation graph. Surface history also resolves the upstream `CADCODEC_SURFACE_HISTORY_V1` extension-dictionary metadata. `solid_history_root` and `solid_history_node` expose typed root/common node data: operation kind, step/evaluation IDs, matrix, color, material, evaluation value and Boolean operand IDs. All 15 upstream history operation kinds are recognized. `solid_history_definition(node)` additionally returns typed operation parameters, including dimensions, radii, cone axes, fillet/chamfer edge arrays, sweep transforms/options, loft settings, revolution parameters and B-rep SAT data.

`solid_history_operation`, `solid_history_operations` and `solid_history_tree` return the selected operation, first-parent chain and recursive operand tree. Modern relationships follow evaluation-graph edges rather than object storage order or step numbering. Boolean trees honor the declared first operand. Cycles, missing links and ambiguous active IDs produce no chain/tree; traversal limits throw `std::length_error`. DXF does not persist the legacy parent-ID field, so graphless records cannot reconstruct a missing multi-step chain.

`solid_history_references` reports source/target handles and roles for owners, reactors, extension dictionaries, history roots, evaluation graphs, expressions, materials and evaluation-handle values, including surface fallback attachment records. `set_solid_history_active_node`, `set_solid_history_node_material` and `set_solid_history_evaluation_graph` edit stored metadata transactionally. They do not evaluate history or regenerate ACIS geometry. Graph replacement validates owned expressions, unique IDs, edge endpoints, incoming chains and cycles. Existing modeler copy/delete operations remap or remove owned history objects while preserving shared material references and adjacent histories.

R2007/R2010/R2013/R2018 ASCII and binary fixtures compare queries, reference traversal, graph edits and solid/surface copy/delete with Rust. The nonstandard upstream evaluation-handle form using numeric DXF group 91 is exposed for inspection, but history copy/delete and graph/active-node edits reject it because its handle encoding cannot be safely remapped across encodings. Invalid ownership and failed edits leave the document unchanged. Native DWG history reading, input editing and create/append authoring are documented above. See [native history tests](tests/solid_history.cpp) and [Rust history comparisons](tests/upstream/src/solid_history.rs).

`create_solid_history(entity, operation)` replaces an existing owned history with a new root/evaluation graph and first operation. `append_solid_history` allocates a new operation and connects it to the latest active result. Appending to an earlier result with outgoing edges is rejected. Graphless histories are promoted only when their complete chain can be recovered. Both APIs maintain handles, ownership, CLASS records and the active selection; failures leave the original document unchanged.

`update_solid_history_step` edits a step in the active operand tree while preserving its identity and the active selection. `update_solid_history` follows Rust's current-operation replacement rules: nonpositive IDs are filled from the previous step and the root selects the replacement's **step ID**. Supplying different positive step/evaluation IDs can therefore leave no resolvable active tree, exactly as in Rust; use the step-edit API for ordinary parameter changes.

```cpp
using namespace libopencad;
SolidHistoryOperation box(SolidHistoryKind::Box);
box.parameters = HistoryBox{10, 20, 30};
auto history = doc.create_solid_history(solid_handle, box);

SolidHistoryOperation fillet(SolidHistoryKind::Fillet);
HistoryFillet edges;
edges.edges = {1, 3};
edges.radii = {0.5, 0.5};
fillet.parameters = edges;
doc.append_solid_history(solid_handle, fillet);

auto first = *doc.solid_history_definition(history.nodes.front());
std::get<HistoryBox>(first.parameters).height = 40;
doc.update_solid_history_step(solid_handle, first);
```

Authoring supports R2007 and newer DXF 3DSOLIDs and surfaces. Attaching new histories to REGION/BODY is rejected because the upstream DXF writer does not persist their history links. These APIs edit stored construction history and leave entity ACIS geometry unchanged; they do not execute Boolean operations or regenerate solids. `HistoryProfiles` retains the raw version-bound profile records. The typed interface below reads, constructs and edits their geometry. Loft settings synchronize `CADCODEC_LOFT_OPTIONS_V1` metadata with native options and transcode supported metadata paths into the document version. Foreign/shared metadata and private data inside an operation's subclass are rejected when rewriting would be unsafe. See [history authoring tests](tests/history_author.cpp) and [independent Rust operations](tests/upstream/src/history_author.rs).



### Boolean history graph merging

`merge_solid_history_boolean(target, tool, operation)` joins two R2007+ modeler histories under a new stored Boolean step (`0` union, `1` intersect, `2` subtract). The first operand is the target result and the second is the tool result. Both entities and their ACIS geometry remain present; this API does not execute a geometric Boolean.

```cpp
auto merged = doc.merge_solid_history_boolean(target_handle, tool_handle, 2);
auto expression = doc.solid_history_tree(target_handle);
// expression->operands[0] is the former target result; [1] is the former tool result.
```

Existing operation handles survive. Tool nodes move to the target evaluation graph; their positive step/evaluation IDs and nested Boolean operand IDs shift past the target's IDs. Graph node/edge IDs and links are shifted independently. Only the new Boolean node receives a new handle (recoverable graphless histories also need evaluation-graph handles). The target root remains, the tool root/evaluation graph are retired, and the tool history link—including surface fallback metadata—is cleared. Node-owned dictionaries, shared materials, scalar/embedded-profile parameters and unrelated histories are preserved.

Merge requires distinct histories with usable current results and valid graph identities/links. Recoverable graphless histories are promoted. Missing/foreign references, cycles, duplicate IDs, nonlatest results, integer overflow, private subclass groups and deletion blocked by external non-reactor references reject without changing the document. Complete reactor entries referencing retired graph objects are cleaned; unrelated references survive. Private children owned directly by a retiring tool root/evaluation graph block the merge rather than becoming orphaned. [Native merge tests](tests/history_merge.cpp) and [Rust verification](tests/upstream/src/history_merge.rs) cover four versions, both DXF encodings, nested/repeated merges, surface ownership and graphless migration.

### Embedded history geometry

`read_history_profiles(operation, version)` and `set_history_profiles(operation, geometry, version)` expose sweep/extrusion profiles and paths, revolution profiles, and ordered loft sections, guides and paths. The `EmbeddedProfile` variant supports POINT, LINE, ARC, CIRCLE, ELLIPSE, SPLINE, LWPOLYLINE, RAY, XLINE and inline REGION. Sweep/extrusion also accept SAT wire-body profiles. Angles use radians; circle and arc centers use OCS and are converted at the embedded WCS boundary.

```cpp
#include <libopencad/embedded_profile.hpp>

auto operation = *doc.solid_history_definition(node_handle);
auto geometry = libopencad::read_history_profiles(operation, doc.version());
geometry.sweep = libopencad::ProfileCircle{{1, 2, 3}, 5, {0, 0, 1}};
libopencad::set_history_profiles(operation, geometry, doc.version());
doc.update_solid_history_step(solid_handle, operation);
```

`ProfilePolyline` includes closure, vertex IDs, widths and bulges. `ProfileSpline` includes control/fit points, knots, weights, tangents and version-dependent storage flags. Knot-less construction generates clamped knots with the effective degree. `ProfileRegion` exposes inline SAT/SAB, secondary ACIS data, reference point, transformed wire caches, silhouettes, material handles and revision fields. Native ACIS parsing/builders can edit the REGION payload. These history interfaces support R2007–R2018 with ASCII/binary readback tests; they do not rebuild the owning modeler's B-rep.

Loft `metadata_path` uses R2018 encoding and overrides the native path when present; the native copy is regenerated in the document version. R2007 omits embedded polyline vertex IDs, and pre-R2013 fit splines omit modern flags, matching Rust. Empty metadata paths retain the native-path fallback. Unknown/truncated bodies and noncanonical REGION layouts use `ProfileOpaque`, preserving bytes and meaningful bit counts in the same version. Cross-version opaque conversion is rejected. Invalid framing/counts, nonfinite geometry and unsupported SAT wire profiles in loft/revolve are rejected before assigning an operation. Silhouette IDs exceeding the upstream BLL 56-bit capacity are rejected on R2010+. See [native profile tests](tests/history_profiles.cpp) and [Rust comparison](tests/upstream/src/history_profiles.rs).


### Documents, Tables, and Objects

- Create AC1032 documents with default tables, model space, default paper space, and required objects.
- Allocate handles, maintain `$HANDSEED`, look up records by handle, detect duplicate identities, and inspect owner chains.
- Query entity membership in model and paper space; select a named block or layout through `EntityOptions` when creating or copying entities. Copied viewports receive unique IDs in the target layout.
- Look up, add, edit, rename, and safely remove entries in nine symbol tables: `LAYER`, `LTYPE`, `STYLE`, `DIMSTYLE`, `APPID`, `UCS`, `VIEW`, `VPORT`, and `BLOCK_RECORD`.
- Create named blocks with `BLOCK` / `ENDBLK`, edit base points and descriptions, and add supported entities using `EntityOptions::block_name`. Nested INSERT references participate in symbol renaming. Direct removal of nonempty or referenced blocks is rejected.
- Create extension dictionaries, nested `DICTIONARY` objects, and `XRECORD` data. Entity copying remaps internal references; deletion cascades through verified owned objects and cleans up reactor references.
- Inspect recognized handle, name, and owner references, repair them using explicit mappings, and fill missing entity owners where membership can be determined.
- `CELLSTYLEMAP`: named nested cell formats, style IDs, TABLESTYLE associations and starting TABLETEMPLATE links.
- `MATERIAL`: colors, factors, map transforms, recursive procedural textures, advanced parameters, and checker asset overlays. `FIELD`/`FIELDLIST`: stored values, child fields, references and custom data; expression evaluation is not implemented.
- Image/underlay definitions and owned image reactors, raster settings, GROUP, SCALE, SORTENTSTABLE, SPATIAL_FILTER, DICTIONARYVAR, DBCOLOR and WIPEOUTVARIABLES.
- `GEODATA`, `VISUALSTYLE`, annotation context data (including nested MTEXT, dimensions, leaders and hatch loops), break/index data, point-cloud definitions/reactors/color ramps, coordination-model definitions and context managers.
- Proxy entity/object payloads, typed proxy-graphics commands and `CLASSES` metadata. Registered application classes expose ordered `SemanticProperty` values with explicit DXF numeric widths, subclass scopes, handles and binary chunks. This generic interface does not implement each application's private behavior or replace specialized CAD object models.

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
- `DxfPoint::x_axis_angle()` follows the pinned upstream reader/writer: it exposes DXF group 50 directly, with no degree/radian conversion. This differs from the ARC angle API.
- RAY/XLINE constructors and direction setters normalize their input. Direction getters also normalize existing file values, with +X as the missing-direction default and zero remaining zero. Reading a direction does not rewrite its original group values.
- `for_each<T>` visits typed records. Supported entity types inside BLOCKS are typed; unsupported records remain accessible by handle and are preserved.
- Copying is limited to a single document. Conflicting ownership, ambiguous identities, private data whose references cannot be safely remapped, or external references that cannot be safely cleaned up cause the operation to fail.

## Format Support and Limitations

The version API recognizes `AC1009`, `AC1012`, `AC1014`, `AC1015`, `AC1018`, `AC1021`, `AC1024`, `AC1027`, and `AC1032`. **Recognizing a version does not mean all of its entities, objects, and output paths are fully supported.**

New documents currently use AC1032 (AutoCAD 2018 format). Ordinary saves retain the source version. Dedicated R12 ASCII export and controlled R13–R2018 target conversion are available for the documented scopes below. Creating ELLIPSE, RAY, or XLINE is restricted to R13 or later; adding an LWPOLYLINE requires R14 or later.

Missing or incomplete areas include DWG editing beyond the documented primitives, comprehensive version conversion, entity transforms/explode and geometry generation, complete dynamic-block action evaluation, associative constraint solving, leftover AEC/Arch constructors beyond wall/door/window and sample-drawing stair/member success paths, complete ACIS/modeler integration, remaining SpatialIndex ClassObject families, and rendering. Stored block, layout and viewport edits do not regenerate CAD display geometry. Generic registered-class properties are an editable DXF value interface, not complete semantic implementations of those specialized models; embedded group-101 payloads require their dedicated typed interface or raw preservation. Reference checks are not a comprehensive CAD audit and repair system.

Interoperability tests identify pinned-upstream limitations explicitly. Examples include omitted polyline metadata and common material/graphics fields, photometric LIGHT writer issues, and procedural material packet ordering. Native tests check fields that Rust drops; passing those checks is not reported as Rust semantic verification. Creating advanced definitions is conservatively gated by DXF version, usually R2007–R2013 or later.

`dxf_output_plan` in [dxf_version.hpp](include/libopencad/dxf_version.hpp) reports either the native version-preserving policy or the pinned Rust writer's effective version. It distinguishes implicit R12 promotion, legacy binary promotion to AC1024, and the separate explicit R12 ASCII path. **A plan does not convert the drawing.** Existing save APIs retain their version-preserving behavior. The dedicated `export_r12` / `write_r12` methods perform R12 conversion; the planner does not invoke them.

An additive diagnostic reader is available through `DxfDocument::read(input, options, statistics)`, using [DxfReadOptions](include/libopencad/dxf_read.hpp). Strict mode reports malformed groups/framing with source byte offsets and ASCII line numbers. Set `options.failsafe = true` to retain complete records around recoverable errors. Damaged compound sequences are skipped together; broken binary framing stops recovery. Statistics distinguish retained, skipped and opaque records and whether the source reached EOF. This interface validates stored values/framing and does not repair CAD object relationships. The original `read(input)` behavior remains unchanged.

In failsafe mode a malformed HEADER variable can be discarded at the next group 9 or record boundary. An unrecognized `$ACADVER` is diagnosed and retained as an unknown version. A new named SECTION or an explicit EOF can close a section missing its ENDSEC. A physically truncated file keeps a false `stream_completed` flag even when the recovered drawing can be saved with valid framing. A missing SEQEND is reported at the compound parent, and a child of the wrong type is reported at that child. The [recovery fixtures](tests/dxf_read_diagnostics.cpp) exercise these locations in both encodings.

For legacy files with a known pre-R2007 version and no `$DWGCODEPAGE`, set `DxfReadOptions::default_encoding` (for example, `"ANSI_936"`). The diagnostic reader records `fallback-encoding-applied` and stores the effective declaration for subsequent saves. An unknown or empty legacy declaration selects Windows-1252. Missing HEADER or $ACADVER selects R2018 and records a diagnostic. Undeclared text uses the configured fallback or the upstream transport-specific decoding rules.

For legacy text, strict diagnostics identify the first byte of an invalid code-page sequence or the backslash of a malformed CIF escape. ASCII diagnostics also identify the group-code line. Failsafe reading retains the entity and follows the upstream text decoder: a bad GBK sequence becomes U+FFFD, a short or nonhex `\U+` escape displays literally, and an isolated UTF-16 surrogate is omitted. The original and recovered labels are compared by handle in the [Rust read-back tests](tests/upstream/src/version_matrix.rs).

HATCH spline boundaries retain knots, weighted control points and boundary references across modern target versions. Pre-R2010 output omits spline fit points/tangents with an explicit loss report; the reader distinguishes their version-dependent group-97 layout.

The current validation environment is macOS with Apple Clang. Other platforms require independent build and test verification; cross-platform support has not yet been established.

### Export to R12 ASCII

`DxfDocument::export_r12(options)` returns a buffered ASCII drawing and a structured report. It rebuilds AC1009 sections and tables, removes modern handles/owners/subclasses and OBJECTS/CLASSES data, maps symbol names, converts RGB to ACI and expands LWPOLYLINE into POLYLINE/VERTEX/SEQEND. Supported legacy primitives, traditional polylines/meshes, nested INSERT/attributes and seven native DIMENSION kinds are retained. Both ASCII and binary inputs are accepted; output uses the source legacy code page for text.

```cpp
libopencad::R12ExportOptions options;
auto preview = doc.export_r12(options); // Strict preview: losses refuse output.
// Inspect preview.report.issues and preview.report.names in your application.
options.allow_loss = true;             // Explicitly accept reported format losses.
auto result = doc.export_r12(options);
if (!result.report.can_write)
    throw libopencad::R12ExportError(result.report);
// Open the destination only after conversion succeeds.
std::ofstream output("legacy-r12.dxf", std::ios::binary);
output.exceptions(std::ios::failbit | std::ios::badbit);
output << result.ascii;
output.close();
```

Losses include renamed symbols, removed metadata/extensions, approximated colors, simplified/truncated linetypes and omitted fields. `allow_loss` alone never permits dropping unsupported entities. A separate `omit_unsupported_entities` option permits reported omissions only when losses are also accepted. Invalid ownership, mesh references, identities or block definitions still refuse output. `write_r12(stream, options)` provides the same conversion and throws a report-bearing error before writing bytes on refusal.

R12 geometric fallbacks use the pinned Rust algorithms: ELLIPSE and SPLINE/HELIX become sampled polylines; RAY/XLINE become finite lines based on header extents; HATCH becomes an anonymous block of SOLID strips or clipped LINE/POINT pattern geometry; MTEXT becomes TEXT using approximate font metrics. Excessively dense or empty hatch patterns fall back to boundary geometry with a separate loss report. These are explicitly reported approximations, not font rendering or analytic geometry preservation. Subdivision MESH control faces become triangle fans with a crease/subdivision loss report. Sampling, intersection work and output have limits; invalid or excessive inputs refuse atomically. LEADER becomes one LINE per adjacent pair of stored vertices. As in the Rust R12 writer, this omits enabled arrows and hook lines, linearizes spline paths and drops annotation links. These losses identify the source LEADER; a referenced annotation is exported independently. MULTILEADER export emits default SOLID arrowheads, broken LINE shafts or sampled spline polylines, a shared landing per root and independent TEXT. The Rust R12 flattener omits custom arrow blocks and block annotations; both losses identify the source handle. Standard MLINEs with at least two elements become two tracks at half the scale on either side of the stored path, joined using the vertex miter directions. Other styles become a centerline. The exporter reports omitted cap, fill, joint and element styling; no cap geometry is generated. The exporter follows the pinned Rust POINT-angle conversion and reports it for nonzero values. Font filenames can be retained, but fonts are not converted to SHX. See the [R12 API](include/libopencad/dxf_r12.hpp), [native example/tests](tests/dxf_r12_export.cpp) and [Rust comparison](tests/upstream/src/r12_export.rs). This export path is separate from the loss-preserving save APIs.

R12 supports seven native DIMENSION types. Arc-length and jogged-radius dimensions use a local graphics block when available. Without one, an arc-length dimension produces two radial extension lines and its optional leader, and a jogged radius produces two lines joined at the jog point. Displayed measurements are computed from geometry. A single space suppresses the label. The loss report identifies the source handle for omitted arrowheads, the missing circular dimension line on arc-length dimensions, and loss of dimension editability.

### Convert to an explicit R13–R2018 target

`export_version(target, binary, options)` converts a staged document and returns bytes plus a structured report. `write_version(stream, target, binary, options)` throws `DxfConversionError` before writing if conversion is refused. These methods accept R12–R2018 sources within the supported conversion scopes; use the separate R12 exporter for AC1009 output.

```cpp
libopencad::DxfConversionOptions options; // Reject reported losses by default.
auto result = doc.export_version(libopencad::DxfVersion::AC1018, false, options);
// Inspect result.report.issues. Set options.allow_loss = true only if the
// application accepts the listed losses, then export again.
if (!result.report.can_write)
    throw libopencad::DxfConversionError(result.report);
std::ofstream output("target-r2004.dxf", std::ios::binary);
output.exceptions(std::ios::failbit | std::ios::badbit);
output << result.bytes;
output.close();
```

R12 source upgrades build a modern object graph: default tables/dictionaries/layouts, BLOCK_RECORD entries, subclass markers, unique parent/child handles and model/paper/block ownership. Existing nonzero identities are listed in `report.handle_mappings`; XDATA group-1005 references are remapped with them. Handleless records receive new identities. Named and anonymous local blocks, primitive geometry, TEXT/ATTDEF/INSERT/ATTRIB, traditional polylines/meshes and the seven legacy dimension types are covered. Layer, linetype and text-style definitions are retained; table counts and `$HANDSEED` are rebuilt. An unresolved reference, duplicate identity/name, incomplete sequence or unreviewed legacy schema refuses output. R12 VIEWPORT upgrade restores ACAD/MVIEW parameters and resolves frozen layers. Anonymous shape-file STYLE entries keep distinct handles, SHAPE keeps its glyph name, and external blocks retain their path and INSERT references. Cached external geometry requires loss permission. Newly synthesized defaults pruned for an older modern target are informational; losses from source data still require `allow_loss`. See [native upgrade tests](tests/dxf_upgrade_r12.cpp) and [Rust readback](tests/upstream/src/upgrade_r12.rs).

Both encodings retain the exact requested target by default. Optional `DxfOutputPolicy::UpstreamCompatible` explicitly reports Rust's promotion of pre-R2007 binary targets to R2010 through `requested` and `effective`. Ordinary save methods keep their existing behavior. Refusal leaves the source unchanged and returns no bytes; output is buffered with a configurable 256 MiB default byte limit. I/O failures after a successful conversion are ordinary stream errors, not transactional filesystem writes.

Reviewed rules cover header fields and maintenance versions, legacy code pages/UTF-8, entity true color/transparency and graphic-cache framing, table fields, block-unit DesignCenter XDATA, DIMSTYLE arrow names/handles, complex-linetype upright flags, LWPOLYLINE vertex IDs, attribute version/lock fields and MTEXT column representations. Losses require `allow_loss`; this option never permits omitting unsupported records. Unknown cross-version schemas and unavailable decompositions return errors. XRECORD values, private groups and unrelated XDATA are preserved rather than filtered by group number alone.

When the effective target is R13, LWPOLYLINE expands into POLYLINE/VERTEX/SEQEND. The parent keeps its handle, owner, common properties, extension dictionary and application data; child records receive unique handles and `$HANDSEED` advances. Closure, plinegen, OCS elevation/normal, thickness, widths and closing-segment bulge are retained. Lightweight vertex IDs are reported as losses. Two zero endpoint widths inherit the constant width, following the upstream legacy writer; when either endpoint width is nonzero, both are written explicitly. Malformed geometry, unreviewed geometry fields, invalid block owners and handle exhaustion refuse output with the source record identified. See [native R13 conversion tests](tests/dxf_r13_polyline.cpp) and [Rust readback](tests/upstream/src/r13_polyline.rs).

Multiline ATTRIB/ATTDEF conversion follows the upstream R2000–R2013 range. It writes ordered single-line attributes (`TAG_001`, `TAG_002`, …), ACAD_MLATT data and an ACAD_MATT_HEIGHT entry in the shared ACAD_XREC_ROUNDTRIP record. The first attribute retains its identity and existing dictionary; extra lines get new handles and owned dictionaries. Conversion back to R2018 restores embedded MTEXT, preserves unrelated dictionary entries and removes only validated split records. Plain read/save operations retain the stored representation. R13/R14 multiline downsave remains rejected because the upstream splitter does not cover those targets. Unknown payloads, conflicting tags/metadata, foreign references and cleanup that would discard application data refuse output. The upstream line-pitch rule splits explicit `\P` paragraphs; it does not calculate font-dependent wrapping. Embedded normals and measured extents omitted by the upstream MLATT writer are reported losses. See [native multiline conversion tests](tests/dxf_mlatt.cpp) and [Rust comparison](tests/upstream/src/mlatt.rs).

This is a bounded implementation of the pinned Rust writer's field rules, **not full compatibility certification for every entity in every release**. It retains upstream quirks, including entity lineweight and layer true-color fields on early modern targets. DIMSTYLE arrow references are explicitly resolved during conversion; the Rust writer otherwise emits its independent name/handle slots without resolving them. Pre-R2007 MTEXT columns decompose into TEXT with explicit layout-loss reporting, fresh handles for additional pieces, and the original identity/dictionary on the first piece. R2007–R2018 TABLE/FIELD schemas and links are retained, while proxy version/length envelopes are rewritten without interpreting their original payload. Legacy proxy string streams and packed-version overflow are rejected. For R13/R14/R2000/R2004 targets the converter follows the Rust DXF writer: ACAD_TABLE cells carry display text or field/block references, and FIELD plus linked table objects use the older cached-value encoding. The report identifies discarded cell state, truncated override bits and value metadata by record handle and group. Private data inside a value body is refused even with loss permission. This preserves Rust round-trip objects and their references; it does not evaluate fields or certify that older CAD applications implement those objects. Modern targets also support registered DXF properties, VISUALSTYLE layouts, versioned render/view-style fields and DATALINK values. When upgrading legacy tables the converter synchronizes visible cell values with linked content and retains modern adjunct metadata. Same-version export preserves opaque records. See the [conversion API](include/libopencad/dxf_convert.hpp), [native matrix](tests/dxf_convert.cpp) and [Rust comparisons](tests/upstream/src/version_convert.rs). Additional [geometry/decomposition tests](tests/dxf_approximation.cpp) and [object-version tests](tests/dxf_object_versions.cpp) exercise both transports against the pinned Rust library.

## API and Examples

- [Document, entity, and symbol-table APIs](include/libopencad/dxf_document.hpp).
- [Color](include/libopencad/color.hpp), [lineweight](include/libopencad/line_weight.hpp), and [DXF versions](include/libopencad/dxf_version.hpp).
- [Vectors](include/libopencad/vector.hpp) and [matrices and transforms](include/libopencad/transform.hpp).
- [Creating multiple entity types](tests/dxf_stage2.cpp), [symbol-table operations](tests/dxf_symbol_operations.cpp), and [extension dictionaries and reference operations](tests/dxf_reference_operations.cpp).
- [POINT, RAY, and XLINE operations](tests/dxf_basic_geometry.cpp), with [Rust readback checks](tests/upstream/src/basic_geometry.rs). Use `add_point(location, options)`, `add_ray(base, direction, options)`, and `add_xline(base, direction, options)` to create them; edit via `for_each<DxfPoint>`, `for_each<DxfRay>`, or `for_each<DxfXLine>`.

[Table examples](tests/dxf_tables.cpp) use `add_table_style`, `get_table_style`, `add_table`, and `DxfTable::definition` / `set_definition`. Use `set_merged_ranges` to replace merges, including an empty list to unmerge every cell. Legacy setters edit the entity record. Call `synchronize_table_content(handle)` after those edits to update its linked modern object while retaining additional contents and modern formats. Newly created table graphics blocks are empty until a CAD application regenerates them.

[Modern table examples](tests/dxf_table_content.cpp) use `DxfTableContent` for both TABLECONTENT and TABLETEMPLATE (`is_template()` distinguishes them). `add_table_content(dictionary, name, definition, as_template)` creates a standalone object. `table_content_definition(entity)` combines current legacy values with retained modern formats; `set_table_content(entity, definition)` atomically updates both representations and creates the extension dictionary/XRECORD link if missing. Low-level object setters edit only that object and use text-style handles; document APIs resolve text-style names. These synchronization operations invalidate record pointers on success. They do not evaluate FIELD/DATALINK data or calculate display geometry.


Additional examples cover [materials](tests/dxf_materials.cpp), [images and underlays](tests/dxf_external_references.cpp), [annotation contexts](tests/dxf_object_contexts.cpp), [extended records](tests/dxf_more_extended.cpp), [proxy graphics](tests/dxf_proxies.cpp), and [registered-class properties](tests/dxf_registered_classes.cpp). The [combined drawing test](tests/dxf_stage3_integration.cpp) creates nested attributed blocks, a hatch with a hole, styled dimensions, multiple layouts/viewports, images and annotation contexts entirely in C++; [Rust readback](tests/upstream/src/stage3_integration.rs) checks both output encodings, copies and deletion results.

The tests also serve as API usage examples. A separately generated API documentation site is not yet available.

[Table geometry examples](tests/dxf_table_geometry.cpp) use `DxfTableGeometry`, `add_table_geometry`, `table_geometry_handle`, `table_geometry_definition`, `set_table_geometry`, and `synchronize_table_geometry`. The document-level content query restores linked caches into `ModernTableCell::geometry`; the content setter writes explicitly supplied caches to TABLEGEOMETRY. Omitted caches retain existing values when the grid shape is unchanged; an explicitly empty cache clears that cell. Shape changes clear unsupplied cells. Linked writes require one cache cell per table cell and create missing roundtrip links atomically. Ordinary save does not recompute or invalidate geometry.

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

The native suite includes an SDK integration check that moves the installed prefix, builds the shipped example in a separate project, and runs it to create a drawing. Run `ctest -N` in the build directory to list the checks enabled for your configuration.

## Upstream and Contributing

LibOpenCad is an independent C++ port. Source comparisons and test dependencies currently target acadrust commit [`52034a67`](https://github.com/hakanaktt/acadrust/tree/52034a673ff59bf6936ebf01ec794b07a97df246), package version 0.6.8. The inspected revision does not imply full feature parity; DWG coverage is limited to the documented operations, and Civil 3D remains outside the current scope. Source comments identify the corresponding Rust modules; further work is ported and tested module by module.

Contributions of reproducible bug reports, anonymized drawings, tests, and code improvements are welcome. Include the file version, ASCII or binary encoding, a minimal reproducer, and expected behavior when reporting a problem. New format or entity support should include native tests and, where practical, Rust readback comparisons. Preserve source attribution and license notices when incorporating external code.

## License and Acknowledgments

LibOpenCad is licensed under the [Mozilla Public License 2.0](LICENSE). Thanks to [acadrust](https://github.com/hakanaktt/acadrust) and its contributors for the source implementation and test reference. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance and retained third-party notices.

MTEXT formatting includes at-least paragraph spacing (`sa`), decimal stacks (`~.`), and tab-stop clearing (`tz`). Uniform MTEXT/MULTILEADER transforms scale absolute inline `\H<h>;` heights. `DxfDocument::unused_app_ids()` lists APPID records that no extended data names and that do not own objects. `DxfDocument::wipeout_frame()` and `set_wipeout_frame()` access the drawing-wide `ACAD_WIPEOUT_VARS` setting without changing WIPEOUT display flags. Creating a missing variables object follows the R2007+ auxiliary-object authoring boundary.

`drawing_variable` / `set_drawing_variable` read and write NOD `AcDbVariableDictionary` `DICTIONARYVAR` values (R2007+). `hatch_origin` stores `HPORIGIN` as `x;y`. `current_entity_transparency` stores packed `CETRANSPARENCY`; explicit alpha greater than 230 is rejected. Missing or malformed values use a zero origin and ByLayer transparency. Conflicting dictionary or object types are left intact.
