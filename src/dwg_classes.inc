// SPDX-License-Identifier: MPL-2.0
// acadrust classes_reader.rs; class positions (including duplicate names) are authoritative.
std::vector<DwgClassRecord> read_class_records(std::string_view section, unsigned v, unsigned maintenance,
                                               const char *page) {
    require(section.size() >= 38, "Truncated DWG CLASSES");
    constexpr unsigned char sentinel[] = {0x8d, 0xa1, 0xc4, 0xb8, 0xc4, 0xa9, 0xf8, 0xc5,
                                          0xc0, 0xdc, 0xf4, 0x5f, 0xe7, 0xcf, 0xb6, 0x8a};
    require(section.substr(0, 16) == std::string_view(reinterpret_cast<const char *>(sentinel), 16),
            "Invalid DWG class sentinel");
    DwgBitReader size(section);
    size.seek(128);
    auto n = size.little_endian(4);
    auto begin = 20u + (((v >= 1024 && maintenance > 3) || v >= 1032) ? 4u : 0u);
    require(begin <= section.size() && n <= section.size() - begin && section.size() - begin - n >= 18,
            "Truncated DWG class payload");
    auto data = section.substr(begin, n);
    DwgBitReader crc(section);
    crc.seek((begin + n) * 8);
    require(crc.little_endian(2) == crc16(section.substr(16, begin + n - 16)), "DWG class CRC mismatch");
    DwgBitReader main(data), strings(data);
    auto end = data.size() * 8;
    bool text = false;
    if (v >= 1021) {
        auto bits = main.little_endian(4);
        require(bits > 32 && bits <= end, "Invalid DWG class stream size");
        end = bits - 1;
        strings.seek(end);
        text = strings.bits(1);
        if (text) {
            auto trailer = [&]() {
                require(end >= 16, "Invalid class text trailer");
                end -= 16;
                strings.seek(end);
                return strings.little_endian(2);
            };
            auto len = trailer();
            if (len & 32768)
                len = (len & 32767) + (trailer() << 15);
            require(len <= end, "Invalid class string size");
            strings = DwgBitReader(data, end);
            end -= len;
            strings.seek(end);
        }
    }
    auto position = main.position();
    main = DwgBitReader(data, end);
    main.seek(position);
    int maximum = 32767;
    if (v >= 1018) {
        maximum = main.bit_short();
        main.bits(8);
        main.bits(8);
        main.bits(1);
    }
    std::vector<DwgClassRecord> records;
    while (main.position() < end) {
        if (v < 1021 && end - main.position() < 8) {
            while (main.position() < end)
                require(main.bits(1) == 0, "Nonzero class padding");
            break;
        }
        auto number = main.bit_short();
        require(number >= 500 && number <= maximum, "Invalid DWG class number");
        DwgClassRecord record;
        record.number = number;
        record.proxy_flags = main.bit_short();
        auto name = [&]() {
            return v >= 1021 ? (text ? symbol_text(strings, true, page) : std::string())
                             : symbol_text(main, false, page);
        };
        record.application = name();
        record.cpp_name = name();
        record.dxf_name = name();
        record.zombie = main.bits(1);
        record.item_class_id = main.bit_short();
        if (v >= 1018)
            for (int i = 0; i < 5; ++i)
                record.fields[i] = main.bit_long();
        require(main.position() <= end, "DWG class main stream overflow");
        records.push_back(std::move(record));
    }
    return records;
}
bool is_surface_class(std::string_view s) {
    return s == "SURFACE" || s == "PLANESURFACE" || s == "EXTRUDEDSURFACE" || s == "LOFTEDSURFACE" ||
           s == "REVOLVEDSURFACE" || s == "SWEPTSURFACE" || s == "NURBSURFACE";
}
