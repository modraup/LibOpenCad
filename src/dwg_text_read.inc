// SPDX-License-Identifier: MPL-2.0
// Ported from acadrust object_reader/entities.rs::read_text_entity_data.
void read_text_record(std::string_view body, DwgR2000Record &r, std::uint64_t h, unsigned v, const char *page,
                      DwgR2000Objects &objects) {
    auto end = r.main_bits;
    DwgBitReader strings(body), handles(body);
    if (v >= 1021) {
        require(end > 17, "Missing DWG TEXT string stream");
        strings.seek(--end);
        require(strings.bits(1), "Missing DWG TEXT string stream");
        auto trailer = [&] {
            require(end >= 16, "Invalid DWG TEXT string trailer");
            end -= 16;
            strings.seek(end);
            return strings.little_endian(2);
        };
        auto size = trailer();
        if (size & 0x8000)
            size = (size & 32767) + (trailer() << 15);
        require(size <= end, "Invalid DWG TEXT string size");
        r.text_end = end;
        end -= size;
        r.text_start = end;
        strings = DwgBitReader(body, r.text_end);
        strings.seek(end);
    }
    DwgBitReader main(body, end);
    main.seek(r.handle_end_bit);
    handles.seek(r.main_bits);
    auto &t = objects.texts[h];
    t.properties = read_entity_properties(main, handles, h, end, v, r);
    r.geometry_start = main.position();
    bool early = v < 1015;
    if (!early)
        t.flags = main.bits(8);
    double z = early ? main.bit_double() : (t.flags & 1) ? 0 : main.raw_double();
    t.insertion = {main.raw_double(), main.raw_double(), z};
    t.has_alignment = early || !(t.flags & 2);
    t.alignment = {0, 0, z};
    if (t.has_alignment) {
        t.alignment.x = early ? main.raw_double() : main.default_double(t.insertion.x);
        t.alignment.y = early ? main.raw_double() : main.default_double(t.insertion.y);
    }
    r.position_end = main.position();
    t.normal = !early && main.bits(1) ? Vector3::UNIT_Z : read_vector(main);
    t.thickness = !early && main.bits(1) ? 0 : main.bit_double();
    t.oblique_angle = early ? main.bit_double() : (t.flags & 4) ? 0 : main.raw_double();
    t.rotation = early ? main.bit_double() : (t.flags & 8) ? 0 : main.raw_double();
    t.height = early ? main.bit_double() : main.raw_double();
    t.width_factor = early ? main.bit_double() : (t.flags & 16) ? 1 : main.raw_double();
    if (v < 1021)
        r.text_start = main.position();
    t.value = symbol_text(v < 1021 ? main : strings, v >= 1021, page);
    if (v < 1021)
        r.text_end = main.position();
    else
        require(strings.position() == r.text_end, "Unexpected DWG TEXT string fields");
    t.generation = early || !(t.flags & 32) ? main.bit_short() : 0;
    t.horizontal = early || !(t.flags & 64) ? main.bit_short() : 0;
    t.vertical = early || !(t.flags & 128) ? main.bit_short() : 0;
    r.text_style_bit = handles.position();
    t.style = handles.handle(h).value;
    r.geometry_end = main.position();
    require(main.position() == end && body.size() * 8 - handles.position() < 8,
            "Unexpected trailing DWG TEXT fields");
}
