// SPDX-License-Identifier: MPL-2.0
// Source-count and diagnostic contract from acadrust DwgReader::read_with_stats.
namespace {
constexpr const char *header_section = "AcDb:Header";
constexpr const char *classes_section = "AcDb:Classes";
constexpr const char *handles_section = "AcDb:Handles";
constexpr const char *objects_section = "AcDb:AcDbObjects";

struct DwgSourceRead {
    detail::DwgReadData data;
    DwgR2000Index index;
    std::string object_section;
    std::optional<std::string> header;
    bool handle_checksum_damaged = false;
    bool password_protected = false;
};

void count_dwg_output(const DwgR2000Objects &objects, unsigned version, DwgReadStatistics &stats) {
    if (version == 1009) {
        stats.output_entities = objects.records.size();
        for (const auto &[handle, sequence] : objects.legacy_sequences)
            stats.output_entities -= sequence.size() - 1;
        for (const auto &[type, entries] : objects.legacy_symbol_names)
            stats.output_table_records += std::count_if(entries.begin(), entries.end(), [](const auto &name) {
                return !name.empty();
            });
        return;
    }

    std::set<std::int16_t> entity_classes;
    for (const auto &record : objects.classes)
        if (record.item_class_id == 498)
            entity_classes.insert(record.number);
    for (const auto &[handle, record] : objects.records) {
        const auto type = record.type;
        if (type == 0 || type == 2 || (type >= 4 && type <= 6) || (type >= 10 && type <= 14) ||
            type == 54 || type == 55 || type == 58 || type == 59)
            continue; // Structural records excluded by the pinned document's entity iterator.
        if (named_table(type) || type == 71)
            ++stats.output_table_records;
        else if ((type >= 48 && type <= 53) || type == 56 || (type >= 60 && type <= 70))
            continue; // Symbol-table control records stay in the byte-backed index.
        else if ((type >= 1 && type <= 41) || (type >= 43 && type <= 47) ||
                 type == 74 || type == 77 || type == 78 || type == 498 || entity_classes.count(type))
            ++stats.output_entities;
        else
            ++stats.output_objects;
    }
}

DwgSourceRead read_dwg_source(std::string_view source, DwgReadStatistics &stats,
                              std::optional<std::string_view> password) {
    const auto version = unsigned(dxf_version_from_string(source.substr(0, 6)));
    if (version)
        stats.source_version = std::string(dxf_version_string(static_cast<DxfVersion>(version)));
    const auto paged = version >= 1018;
    const auto failsafe = stats.recovery_mode;
    DwgSourceRead result;
    std::optional<detail::DwgPagedReader> reader;
    auto report = [&](const char *code, const char *section, DwgReadStage stage, const std::exception &error) {
        read_diagnostic(stats, {code, error.what(), section, stage, {}, {}, {}, {}});
    };
    try {
        require(source.size() >= 6, "Truncated DWG signature");
        require(version, "Unsupported DWG signature");
        if (paged)
            reader.emplace(source);
        else if (version != 1009)
            result.index = read_linear_directory(source);
    } catch (const std::invalid_argument &error) {
        read_diagnostic(stats, {"file-header-read-failed", error.what(), {}, DwgReadStage::FileHeader,
                                0, DwgOffsetBasis::FileByte, {}, {}});
        if (!failsafe || source.size() < 6)
            throw DwgReadError(error.what(), std::move(stats));
        result.data.writable = false;
        return result;
    }

    auto &objects = result.data.objects;
    if (version == 1009) {
        stats.source_sections = 1;
        try {
            objects = read_r12_geometry(source);
            count_dwg_output(objects, version, stats);
            stats.source_records = stats.decoded_source_records = stats.output_entities;
            stats.record_stream_read = stats.stream_completed = true;
            stats.recovery_mode = false; // The pinned legacy reader decodes the stream as a unit.
        } catch (const std::invalid_argument &error) {
            stats.skipped_source_records = 1;
            report("legacy-read-failed", "", DwgReadStage::RecordStream, error);
            if (!failsafe)
                throw DwgReadError(error.what(), std::move(stats));
            result.data.writable = false;
        }
        return result;
    }

    stats.maintenance_version = static_cast<std::uint8_t>(source[11]);
    if (reader) {
        try {
            reader->authenticate(password);
        } catch (const std::invalid_argument &error) {
            report("decryption-failed", "AcDb:Security", DwgReadStage::FileHeader, error);
            if (!failsafe)
                throw DwgReadError(error.what(), std::move(stats));
            result.data.writable = false;
        }
        result.password_protected = reader->password_protected();
    }
    const auto encoding = drawing_code_page(source, version);
    auto section = [&](std::string_view name, unsigned id) {
        if (reader)
            return reader->read(name);
        const auto locator = result.index.sections.find(id);
        require(locator != result.index.sections.end(), "DWG section not found");
        return std::string(source.substr(locator->second.offset, locator->second.size));
    };
    std::optional<std::string> classes;
    try {
        classes = section(classes_section, 1);
    } catch (const std::invalid_argument &error) {
        objects.classes_error = error.what();
        report("classes-extract-failed", classes_section, DwgReadStage::Classes, error);
    }
    if (classes) {
        try {
            objects.classes = read_class_records(*classes, version, stats.maintenance_version, encoding);
        } catch (const std::invalid_argument &error) {
            objects.classes_error = error.what();
            report("classes-decode-failed", classes_section, DwgReadStage::Classes, error);
        }
    }

    try {
        result.header = section(header_section, 0);
    } catch (const std::invalid_argument &error) {
        result.data.writable = false;
        report("header-extract-failed", header_section, DwgReadStage::Header, error);
    }
    if (result.header) {
        try {
            (void)dwg_header_payload(*result.header, version);
        } catch (const std::invalid_argument &error) {
            report("header-decode-failed", header_section, DwgReadStage::Header, error);
            if (!failsafe)
                throw DwgReadError(error.what(), std::move(stats));
            result.data.writable = false;
            result.header.reset();
        }
    }

    std::string handles;
    try {
        handles = section(handles_section, 2);
        stats.source_sections = 1;
    } catch (const std::invalid_argument &error) {
        report("handles-extract-failed", handles_section, DwgReadStage::Handles, error);
        result.data.writable = false;
        stats.stream_completed = true;
        return result;
    }
    try {
        result.index.object_offsets = read_offsets(handles, source.size(), &stats);
    } catch (const std::invalid_argument &error) {
        report("handles-decode-failed", handles_section, DwgReadStage::Handles, error);
        if (!failsafe)
            throw DwgReadError(error.what(), std::move(stats));
        result.data.writable = false;
        stats.stream_completed = true;
        return result;
    }
    result.handle_checksum_damaged = std::any_of(stats.diagnostics.begin(), stats.diagnostics.end(), [](const auto &d) {
        return d.stage == DwgReadStage::Handles && d.code == "checksum-mismatch";
    });
    stats.source_records = result.index.object_offsets.size();
    stats.stream_completed = true;
    if (!stats.source_records)
        return result;
    if (paged) {
        try {
            result.object_section = reader->read(objects_section);
        } catch (const std::invalid_argument &error) {
            report("objects-extract-failed", objects_section, DwgReadStage::Objects, error);
            result.data.writable = false;
            return result;
        }
    }
    ++stats.source_sections;
    stats.record_stream_read = true;
    stats.stream_completed = false;
    std::vector<std::string> names;
    for (const auto &record : objects.classes)
        names.push_back(record.dxf_name);
    auto decoded = decode_objects(paged ? std::string_view(result.object_section) : source,
                                  result.index, version, encoding, names, &stats);
    decoded.classes = std::move(objects.classes);
    decoded.classes_error = std::move(objects.classes_error);
    if (version >= 1027)
        detail::attach_dwg_modeler_data(*reader, decoded);
    stats.decoded_source_records = decoded.records.size();
    count_dwg_output(decoded, version, stats);
    stats.stream_completed = true;
    objects = std::move(decoded);
    return result;
}
} // namespace

detail::DwgReadData detail::read_dwg_with_stats(std::string_view bytes, DwgReadStatistics &stats,
                                               std::optional<std::string_view> password) {
    auto read = read_dwg_source(bytes, stats, password);
    read.data.writable = read.data.writable && !stats.skipped_source_records && !read.handle_checksum_damaged;
    return std::move(read.data);
}
