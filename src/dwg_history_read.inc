// SPDX-License-Identifier: MPL-2.0
// Bounded object envelope and typed acadrust history/evaluation payloads.
const char *history_dxf_name(SolidHistoryKind kind) {
    static constexpr const char *names[] = {
        "ACSH_BOX_CLASS",       "ACSH_WEDGE_CLASS",   "ACSH_SPHERE_CLASS",  "ACSH_CYLINDER_CLASS",
        "ACSH_CONE_CLASS",      "ACSH_PYRAMID_CLASS", "ACSH_TORUS_CLASS",   "ACSH_BOOLEAN_CLASS",
        "ACSH_BREP_CLASS",      "ACSH_FILLET_CLASS",  "ACSH_CHAMFER_CLASS", "ACSH_SWEEP_CLASS",
        "ACSH_EXTRUSION_CLASS", "ACSH_LOFT_CLASS",    "ACSH_REVOLVE_CLASS"};
    require(std::size_t(kind) < std::size(names), "Unknown history kind");
    return names[std::size_t(kind)];
}
const char *history_cpp_name(SolidHistoryKind kind) {
    static constexpr const char *names[] = {
        "AcDbShBox",     "AcDbShWedge", "AcDbShSphere",    "AcDbShCylinder", "AcDbShCone",
        "AcDbShPyramid", "AcDbShTorus", "AcDbShBoolean",   "AcDbShBrep",     "AcDbShFillet",
        "AcDbShChamfer", "AcDbShSweep", "AcDbShExtrusion", "AcDbShLoft",     "AcDbShRevolve"};
    require(std::size_t(kind) < std::size(names), "Unknown history kind");
    return names[std::size_t(kind)];
}
std::optional<SolidHistoryKind> history_kind(std::string_view name) {
    for (std::size_t i = 0; i < 15; ++i)
        if (history_dxf_name(static_cast<SolidHistoryKind>(i)) == name)
            return static_cast<SolidHistoryKind>(i);
    return {};
}
bool history_class(std::string_view name) {
    return history_kind(name).has_value() || name == "ACSH_HISTORY_CLASS" || name == "EVALUATION_GRAPH" ||
           name == "ACAD_EVALUATION_GRAPH";
}
DwgHistoryRecord read_history_record(std::string_view body, const DwgR2000Record &record, std::uint64_t h,
                                     unsigned v, const char *page, const std::string &type) {
    require(v >= 1021 || (v == 1018 && (type == "DICTIONARY" || type == "XRECORD")),
            "Native DWG history requires R2007+");
    auto end = record.main_bits;
    DwgBitReader strings(body, end);
    bool text = false;
    if (v >= 1021) {
        require(end > record.handle_end_bit, "Missing history text flag");
        strings.seek(--end);
        text = strings.bits(1);
    }
    if (text) {
        auto trailer = [&]() {
            require(end >= 16, "Invalid history text trailer");
            end -= 16;
            strings.seek(end);
            return strings.little_endian(2);
        };
        auto n = trailer();
        if (n & 32768)
            n = (n & 32767) + (trailer() << 15);
        require(n <= end, "Invalid history string length");
        strings = DwgBitReader(body, end);
        end -= n;
        strings.seek(end);
    }
    DwgBitReader main(body, end), refs(body);
    main.seek(record.handle_end_bit);
    refs.seek(record.main_bits);
    skip_entity_prefix(main, end, false, v);
    DwgHistoryRecord out;
    out.handle = h;
    out.type = type;
    out.owner = refs.handle(h).value;
    auto count = [&]() {
        auto n = main.bit_long();
        require(n >= 0 && n <= 1000000 && std::size_t(n) <= (end - main.position()) / 2,
                "Invalid history count");
        return std::size_t(n);
    };
    auto reactor_count = main.bit_long();
    require(reactor_count >= 0 && reactor_count <= 1000000, "Invalid history reactor count");
    auto reactors = std::size_t(reactor_count);
    require(reactors <= (body.size() * 8 - refs.position()) / 8, "Invalid history reactor count");
    for (std::size_t i = 0; i < reactors; ++i)
        out.reactors.push_back(refs.handle(h).value);
    if (!main.bits(1))
        out.extension_dictionary = refs.handle(h).value;
    if (v >= 1027)
        require(!main.bits(1), "History object datastore not supported");
    auto real = [&]() {
        double x = main.bit_double();
        require(std::isfinite(x), "Nonfinite history value");
        return x;
    };
    auto string = [&]() {
        return v < 1021 ? symbol_text(main, false, page)
               : text   ? symbol_text(strings, true, page)
                        : std::string();
    };
    if (type == "DICTIONARY") {
        auto n = main.bit_long();
        require(n >= 0 && std::size_t(n) <= (body.size() * 8 - refs.position()) / 8,
                "Invalid dictionary count");
        main.bit_short();
        main.bits(8);
        for (int i = 0; i < n; ++i) {
            auto key = string();
            auto value = refs.handle(h).value;
            out.dictionary_entries.emplace_back(key, value);
        }
    } else if (type == "XRECORD") {
        out.parameters_begin_bit = main.position();
        auto n = main.bit_long();
        require(n >= 0 && std::size_t(n) <= (end - main.position()) / 8, "Invalid XRECORD size");
        for (int i = 0; i < n; ++i)
            out.metadata += char(main.bits(8));
        out.parameters_end_bit = main.position();
        main.bit_short();
        while (body.size() * 8 - refs.position() >= 8) {
            auto start = refs.position();
            auto ref = refs.handle(h);
            out.reference_slots.emplace_back(start, refs.position());
            if (!ref.value)
                break;
            out.reference_values.push_back(ref.value);
        }
    } else if (type == "ACSH_HISTORY_CLASS") {
        SolidHistoryRoot root;
        root.handle = h;
        root.owner = out.owner;
        root.major = main.bit_long();
        root.minor = main.bit_long();
        out.graph_reference_begin = refs.position();
        root.evaluation_graph = refs.handle(h).value;
        out.graph_reference_end = refs.position();
        out.active_begin = main.position();
        root.history_node_id = main.bit_long();
        out.active_end = main.position();
        root.show_history = main.bits(1);
        root.record_history = main.bits(1);
        out.root = root;
    } else if (auto kind = history_kind(type)) {
        SolidHistoryOperation op(*kind);
        auto &b = op.base;
        b.handle = h;
        b.owner = out.owner;
        b.parent_id = main.bit_long();
        b.eval_major = main.bit_long();
        b.eval_minor = main.bit_long();
        b.value_code = main.bit_short();
        switch (b.value_code) {
        case 40:
            b.value = real();
            break;
        case 10:
        case 11: {
            double x = main.raw_double(), y = main.raw_double();
            require(std::isfinite(x) && std::isfinite(y), "Nonfinite history point");
            b.value = Vector2{x, y};
            break;
        }
        case 1:
            b.value = string();
            break;
        case 90:
            b.value = main.bit_long();
            break;
        case 91:
            b.value = refs.handle(h).value;
            break;
        case 70:
            b.value = main.bit_short();
            break;
        default:
            break;
        }
        b.eval_node_id = main.bit_long();
        b.major = main.bit_long();
        b.minor = main.bit_long();
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                b.transform[col * 4 + row] = real();
        (void)main.bit_short();
        auto color = std::uint32_t(main.bit_long());
        b.color = color == 0xc0000000    ? Color::by_layer()
                  : color == 0xc8000000  ? Color::none()
                  : (color & 0x01000000) ? Color::from_index(color & 255)
                                         : Color::from_true_color_value(color);
        auto flags = main.bits(8);
        if (flags & 1)
            out.color_name = string();
        if (flags & 2)
            out.color_book_name = string();
        b.step_id = main.bit_long();
        out.material_begin = refs.position();
        b.material = refs.handle(h).value;
        out.material_end = refs.position();
        out.operation = op;
        detail::read_dwg_history_parameters(main, refs, end, static_cast<DxfVersion>(v), out);
    } else {
        BlockEvaluationGraph g;
        g.first_node_id = main.bit_long();
        g.first_node_id_copy = main.bit_long();
        auto n = count();
        for (std::size_t i = 0; i < n; ++i) {
            BlockEvaluationNode node;
            node.id = main.bit_long();
            node.edge_flags = main.bit_long();
            node.next_id = main.bit_long();
            auto begin = refs.position();
            node.expression = refs.handle(h).value;
            out.reference_slots.emplace_back(begin, refs.position());
            for (auto &x : node.node_data)
                x = main.bit_long();
            g.nodes.push_back(node);
        }
        n = count();
        for (std::size_t i = 0; i < n; ++i) {
            BlockEvaluationEdge e;
            e.id = main.bit_long();
            e.next_id = main.bit_long();
            e.incoming_edge = main.bit_long();
            e.source_node = main.bit_long();
            e.destination_node = main.bit_long();
            for (auto &x : e.outgoing_edges)
                x = main.bit_long();
            g.edges.push_back(e);
        }
        out.graph = std::move(g);
    }
    require(main.position() == end, "Unrecognized DWG history main-stream tail");
    require(body.size() * 8 - refs.position() < 8, "Unrecognized DWG history handle-stream tail");
    return out;
}
