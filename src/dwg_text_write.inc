// SPDX-License-Identifier: MPL-2.0
// acadrust object_writer/entities.rs::write_text and r12/entities.rs.
std::string detail::edit_dwg_text(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                  std::string_view value, Point3 insertion) {
    utf8_scalars(value);
    require(value.find('\0') == std::string_view::npos, "Invalid DWG TEXT UTF-8");
    require(std::isfinite(insertion.x) && std::isfinite(insertion.y) && std::isfinite(insertion.z),
            "Non-finite DWG TEXT insertion point");
    const auto &t = objects.texts.at(h);
    const auto &r = objects.records.at(h);
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    auto page = drawing_code_page(file, v);
    bool changed = value != t.value;
    if (v == 1009) {
        auto body = file.substr(r.offset, r.body_size + 2);
        std::string live(body.substr(0, r.elevation_bit / 8));
        DwgBitWriter z, xy;
        if (insertion.z != 0)
            raw_double(z, insertion.z);
        live += z.bytes();
        live[1] = char((static_cast<unsigned char>(live[1]) & ~4) | (insertion.z != 0 ? 4 : 0));
        live.append(body.substr(r.thickness_bit / 8, (r.geometry_start - r.thickness_bit) / 8));
        raw_double(xy, insertion.x);
        raw_double(xy, insertion.y);
        live += xy.bytes();
        live.append(body.substr(r.position_end / 8, (r.text_start - r.position_end) / 8));
        if (changed) {
            auto encoded = to_legacy(value, page);
            require(encoded.size() <= 65535, "AC1009 TEXT too long");
            DwgBitWriter length;
            length.little_endian(2, encoded.size());
            live += length.bytes();
            live += encoded;
        } else
            live.append(body.substr(r.text_start / 8, (r.text_end - r.text_start) / 8));
        live.append(body.substr(r.text_end / 8));
        return replace_legacy_entity(file, r, std::move(live));
    }
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, strings, refs;
        copy_until(main, old, r.geometry_start);
        if (v < 1015)
            main.bit_double(insertion.z);
        else {
            main.bits(8, (t.flags & ~1u) | (insertion.z == 0 ? 1 : 0));
            if (insertion.z != 0)
                raw_double(main, insertion.z);
        }
        raw_double(main, insertion.x);
        raw_double(main, insertion.y);
        if (t.has_alignment) {
            if (v >= 1015)
                main.bits(2, 3);
            raw_double(main, t.alignment.x);
            if (v >= 1015)
                main.bits(2, 3);
            raw_double(main, t.alignment.y);
        }
        old.seek(r.position_end);
        copy_until(main, old, v < 1021 ? r.text_start : r.geometry_end);
        if (v < 1021) {
            if (changed) {
                symbol_string(main, strings, v, value, page);
                old.seek(r.text_end);
            } else
                copy_until(main, old, r.text_end);
            copy_until(main, old, r.main_bits);
        } else if (changed)
            symbol_string(main, strings, v, value, page);
        else {
            old.seek(r.text_start);
            copy_until(strings, old, r.text_end);
        }
        old.seek(r.main_bits);
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return finish_symbol(main, strings, refs, v, r.split_field_bit);
    });
}

std::string detail::format_dwg_text(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                    const DwgText &t) {
    require(std::isfinite(t.height) && std::isfinite(t.rotation) && std::isfinite(t.width_factor) &&
                std::isfinite(t.alignment.x) && std::isfinite(t.alignment.y) &&
                std::isfinite(t.oblique_angle) && std::isfinite(t.thickness) && std::isfinite(t.normal.x) &&
                std::isfinite(t.normal.y) && std::isfinite(t.normal.z),
            "Non-finite DWG TEXT formatting");
    require(t.horizontal >= 0 && t.horizontal <= 5 && t.vertical >= 0 && t.vertical <= 3,
            "Invalid DWG TEXT alignment");
    const auto &r = objects.records.at(h);
    const auto &before = objects.texts.at(h);
    const auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    const auto a = t.has_alignment ? t.alignment : Point3{};
    if (v == 1009) {
        require(t.style <= 255, "AC1009 TEXT style slot exceeds one byte");
        auto body = file.substr(r.offset, r.body_size);
        // Preserve unknown option bits and rebuild the documented TEXT fields.
        unsigned options = t.flags & ~511u;
        if (t.oblique_angle != 0)
            options |= 4;
        if (t.generation != 0)
            options |= 16;
        if (t.normal != Vector3::UNIT_Z)
            options |= 128;
        if (t.rotation != 0)
            options |= 1;
        if (t.width_factor != 1)
            options |= 2;
        if (t.style != 0)
            options |= 8;
        if (t.horizontal != 0)
            options |= 32;
        if (t.has_alignment)
            options |= 64;
        if (t.vertical != 0)
            options |= 256;
        const auto old_flags = static_cast<unsigned char>(body[1]);
        std::string live(body.substr(0, r.thickness_bit / 8));
        DwgBitWriter thickness;
        if (t.thickness != 0)
            raw_double(thickness, t.thickness);
        live += thickness.bytes();
        const auto after_thickness = r.thickness_bit / 8 + ((old_flags & 8) ? 8 : 0);
        live.append(body.substr(after_thickness, r.geometry_start / 8 - after_thickness));
        live[1] = char((old_flags & ~8u) | (t.thickness != 0 ? 8 : 0));
        set_integer(live, 6, options, 2);
        DwgBitWriter fields;
        raw_double(fields, t.insertion.x);
        raw_double(fields, t.insertion.y);
        raw_double(fields, t.height);
        live += fields.bytes();
        live.append(body.substr(r.text_start / 8, (r.text_end - r.text_start) / 8));
        DwgBitWriter tail;
        if (options & 1)
            raw_double(tail, t.rotation);
        if (options & 2)
            raw_double(tail, t.width_factor);
        if (options & 4)
            raw_double(tail, t.oblique_angle);
        if (options & 8)
            tail.bits(8, t.style);
        if (options & 16)
            tail.bits(8, t.generation);
        if (options & 32)
            tail.bits(8, t.horizontal);
        if (options & 64) {
            raw_double(tail, a.x);
            raw_double(tail, a.y);
        }
        if (options & 128) {
            raw_double(tail, t.normal.x);
            raw_double(tail, t.normal.y);
            raw_double(tail, t.normal.z);
        }
        if (options & 256)
            tail.bits(8, t.vertical);
        live += tail.bytes();
        live.resize(live.size() + 2);
        return replace_legacy_entity(file, r, std::move(live));
    }
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        const auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, strings, refs;
        copy_until(main, old, r.geometry_start);
        const bool early = v < 1015;
        unsigned flags = (t.insertion.z == 0 ? 1 : 0) | (a == Point3{} ? 2 : 0) |
                         (t.oblique_angle == 0 ? 4 : 0) | (t.rotation == 0 ? 8 : 0) |
                         (t.width_factor == 1 ? 16 : 0) | (t.generation == 0 ? 32 : 0) |
                         (t.horizontal == 0 ? 64 : 0) | (t.vertical == 0 ? 128 : 0);
        if (early)
            main.bit_double(t.insertion.z);
        else {
            main.bits(8, flags);
            if (!(flags & 1))
                raw_double(main, t.insertion.z);
        }
        raw_double(main, t.insertion.x);
        raw_double(main, t.insertion.y);
        if (early || !(flags & 2)) {
            if (!early)
                main.bits(2, 3);
            raw_double(main, a.x);
            if (!early)
                main.bits(2, 3);
            raw_double(main, a.y);
        }
        if (!early)
            main.bits(1, t.normal == Vector3::UNIT_Z);
        if (early || t.normal != Vector3::UNIT_Z)
            vector_bits(main, t.normal);
        if (!early)
            main.bits(1, t.thickness == 0);
        if (early || t.thickness != 0)
            main.bit_double(t.thickness);
        if (early) {
            main.bit_double(t.oblique_angle);
            main.bit_double(t.rotation);
            main.bit_double(t.height);
            main.bit_double(t.width_factor);
        } else {
            if (!(flags & 4))
                raw_double(main, t.oblique_angle);
            if (!(flags & 8))
                raw_double(main, t.rotation);
            raw_double(main, t.height);
            if (!(flags & 16))
                raw_double(main, t.width_factor);
        }
        // Retain the original string representation, including legacy escapes.
        old.seek(r.text_start);
        copy_until(v < 1021 ? main : strings, old, r.text_end);
        if (early || !(flags & 32))
            main.bit_short(t.generation);
        if (early || !(flags & 64))
            main.bit_short(t.horizontal);
        if (early || !(flags & 128))
            main.bit_short(t.vertical);
        old.seek(r.main_bits);
        copy_until(refs, old, r.text_style_bit);
        if (t.style == before.style)
            copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        else {
            old.handle(h);
            refs.absolute_handle(5, t.style);
            copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        }
        return finish_symbol(main, strings, refs, v, r.split_field_bit);
    });
}
