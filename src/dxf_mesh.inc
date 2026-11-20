// SPDX-License-Identifier: MPL-2.0
// Native SubD mesh storage, ported from acadrust's DXF mesh reader/writer.
MeshDefinition DxfMesh::definition() const {
    auto fields = subclass_fields("AcDbSubDMesh");
    std::size_t pos = 0;
    MeshDefinition d;
    auto take = [&](int code) -> std::string {
        if (pos == fields.size() || fields[pos].code != code)
            throw std::runtime_error("Malformed MESH group " + std::to_string(code));
        return fields[pos++].value;
    };
    auto count = [&](int code) {
        auto n = integer(take(code));
        if (n < 0)
            throw std::runtime_error("Negative MESH count/index");
        return std::size_t(n);
    };
    auto real = [&](int code) {
        auto value = take(code);
        std::istringstream in(value);
        in.imbue(std::locale::classic());
        double v = 0;
        if (!(in >> v) || !(in >> std::ws).eof() || !std::isfinite(v))
            throw std::runtime_error("Invalid MESH real");
        return v;
    };
    auto version = integer(take(71));
    if (version < INT16_MIN || version > INT16_MAX)
        throw std::runtime_error("Invalid MESH version");
    d.version = static_cast<std::int16_t>(version);
    d.blend_crease = integer(take(72)) != 0;
    d.subdivision_level = integer(take(91));
    auto n = count(92);
    if (n > (fields.size() - pos) / 3)
        throw std::runtime_error("Truncated MESH vertices");
    for (std::size_t i = 0; i < n; ++i) {
        auto x = real(10), y = real(20), z = real(30);
        d.vertices.push_back({x, y, z});
    }
    auto size = count(93);
    if (size > fields.size() - pos)
        throw std::runtime_error("Truncated MESH faces");
    auto end = pos + size;
    while (pos < end) {
        auto sides = count(90);
        if (sides > end - pos)
            throw std::runtime_error("Truncated MESH face");
        MeshFace f;
        for (std::size_t i = 0; i < sides; ++i)
            f.vertices.push_back(count(90));
        d.faces.push_back(std::move(f));
    }
    auto edges = count(94);
    if (edges > (fields.size() - pos) / 2)
        throw std::runtime_error("Truncated MESH edges");
    for (std::size_t i = 0; i < edges; ++i) {
        auto a = count(90), b = count(90);
        d.edges.push_back({a, b, {}});
    }
    auto creases = count(95);
    if (creases > edges || creases > fields.size() - pos)
        throw std::runtime_error("Invalid MESH creases");
    for (std::size_t i = 0; i < creases; ++i) {
        auto c = real(140);
        if (c != 0)
            d.edges[i].crease = c;
    }
    if (pos < fields.size())
        d.override_option = integer(take(90));
    if (d.override_option != 0 || pos != fields.size())
        throw std::runtime_error("MESH subentity overrides are not supported");
    return d;
}
void DxfMesh::set_definition(const MeshDefinition &d) {
    if (d.override_option)
        throw std::invalid_argument("MESH subentity overrides are not supported");
    std::vector<DxfRawField> fields;
    auto put = [&](int code, std::int64_t v) {
        if (v < INT32_MIN || v > INT32_MAX)
            throw std::overflow_error("MESH integer overflow");
        fields.push_back({code, std::to_string(v)});
    };
    auto real = [&](int code, double v) {
        finite(v);
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(17);
        out << v;
        fields.push_back({code, out.str()});
    };
    auto count = [&](int code, std::size_t v) {
        if (v > INT32_MAX)
            throw std::overflow_error("MESH count overflow");
        put(code, static_cast<std::int64_t>(v));
    };
    put(71, d.version);
    put(72, d.blend_crease ? 1 : 0);
    put(91, d.subdivision_level);
    count(92, d.vertices.size());
    for (auto v : d.vertices) {
        real(10, v.x);
        real(20, v.y);
        real(30, v.z);
    }
    std::size_t face_size = 0;
    for (const auto &f : d.faces) {
        if (f.vertices.empty())
            throw std::invalid_argument("MESH face must contain at least one index");
        if (f.vertices.size() >= INT32_MAX || face_size > std::size_t(INT32_MAX) - f.vertices.size() - 1)
            throw std::overflow_error("MESH face list overflow");
        face_size += f.vertices.size() + 1;
    }
    count(93, face_size);
    for (const auto &f : d.faces) {
        count(90, f.vertices.size());
        for (auto i : f.vertices) {
            if (i >= d.vertices.size())
                throw std::invalid_argument("MESH face index out of range");
            count(90, i);
        }
    }
    count(94, d.edges.size());
    for (const auto &e : d.edges) {
        if (e.start >= d.vertices.size() || e.end >= d.vertices.size())
            throw std::invalid_argument("MESH edge index out of range");
        count(90, e.start);
        count(90, e.end);
    }
    count(95, d.edges.size());
    for (const auto &e : d.edges)
        real(140, e.crease.value_or(0));
    put(90, 0);
    auto e = *this;
    e.set_subclass_fields("AcDbSubDMesh", fields);
    *this = std::move(e);
}
BoundingBox3D DxfMesh::bounding_box() const { return definition().bounding_box().value_or(BoundingBox3D{}); }
std::uint64_t DxfDocument::add_mesh(const MeshDefinition &d, const EntityOptions &options) {
    if (static_cast<int>(version_) < static_cast<int>(DxfVersion::AC1024))
        throw std::invalid_argument("MESH requires R2010 or later");
    DxfMesh e;
    static_cast<DxfEntity &>(e) = make_entity("MESH", "AcDbSubDMesh", options);
    e.set_definition(d);
    return insert_entity(std::move(e), options);
}
