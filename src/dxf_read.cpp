// SPDX-License-Identifier: MPL-2.0
// acadrust io/dxf/reader.rs: failsafe recovery, source diagnostics and counters.
// This layer validates framing before the loss-preserving document parser.
#include "dxf_binary.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <charconv>
#include <istream>
#include <libopencad/dxf_document.hpp>
#include <sstream>
#include <unordered_set>
namespace libopencad {
namespace {
struct CheckedPair {
    int code;
    std::string value;
    std::size_t offset, line, value_offset;
};
struct CheckedRecord {
    std::vector<CheckedPair> pairs;
    bool invalid = false;
    std::string section;
    std::string type() const { return pairs.empty() ? "" : pairs.front().value; }
};
std::string strip(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == value.npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
bool structural(const std::string &type) {
    static const std::unordered_set<std::string> names{"SECTION", "ENDSEC", "TABLE", "ENDTAB",
                                                       "EOF",     "BLOCK",  "ENDBLK"};
    return names.count(type) != 0;
}
} // namespace
DxfDocument DxfDocument::read(std::istream &input, const DxfReadOptions &options, DxfReadStatistics &stats) {
    stats = {};
    if(options.default_encoding && !detail::known_code_page(*options.default_encoding))
        throw DxfReadError({"unknown-default-encoding","Unrecognized fallback code page",{},{},{},0,{}});
    std::string bytes;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof buffer);
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > options.max_bytes - bytes.size())
            throw DxfReadError(
                {"input-limit", "DXF diagnostic input exceeds max_bytes", {}, {}, {}, bytes.size(), {}});
        bytes.append(buffer, count);
    }
    if (input.bad())
        throw DxfReadError({"input-failed", "DXF input stream failed", {}, {}, {}, bytes.size(), {}});
    stats.binary = !bytes.empty() && bytes[0] == 'A';
    bool compact = false;
    std::string section;
    std::vector<CheckedRecord> records;
    // Match the upstream bounded diagnostic collection (999 details + limit notice).
    auto append_diagnostic = [&](const DxfReadDiagnostic &diagnostic) {
        if (stats.diagnostics.size() < 999)
            stats.diagnostics.push_back(diagnostic);
        else if (stats.diagnostics.size() == 999)
            stats.diagnostics.push_back({"diagnostic-limit-reached",
                                         "Additional reader diagnostics omitted",
                                         {},
                                         {},
                                         {},
                                         diagnostic.source_offset,
                                         diagnostic.source_line});
    };
    auto report = [&](std::string code, std::string message, std::size_t offset, std::size_t line,
                      const CheckedRecord *record = nullptr) {
        DxfReadDiagnostic diagnostic{std::move(code), std::move(message), section, {}, {}, offset, {}};
        if (!stats.binary)
            diagnostic.source_line = line;
        if (record) {
            diagnostic.section = record->section;
            diagnostic.record_type = record->type();
            if (record->type() != "SECTION") {
                for (const auto &p : record->pairs) {
                    if (p.code == 100)
                        break;
                    if (p.code == 5 || p.code == 105)
                        diagnostic.record_handle = strip(p.value);
                }
            }
        }
        append_diagnostic(diagnostic);
        if (!options.failsafe)
            throw DxfReadError(std::move(diagnostic));
    };
    if (stats.binary && bytes.compare(0, detail::binary_signature.size(), detail::binary_signature) != 0)
        throw DxfReadError({"binary-signature", "Invalid binary DXF signature", {}, {}, {}, 0, {}});
    std::istringstream stream(bytes);
    if (stats.binary)
        stream.ignore(detail::binary_signature.size());
    else if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
        stream.ignore(3);
    std::size_t line = 1;
    bool first = true;
    // HEADER values share a SECTION record. Group 9 gives a trustworthy
    // boundary for discarding one variable without losing the rest of the drawing.
    std::optional<std::size_t> header_variable;
    bool discard_variable = false;
    auto finish_variable = [&] {
        if (!header_variable)
            return;
        auto &record = records.back();
        const auto &name = record.pairs[*header_variable];
        if (!discard_variable && record.pairs.size() == *header_variable + 1) {
            report("header-value-missing", "Missing value for " + name.value, name.offset, name.line, &record);
            discard_variable = true;
        }
        if (discard_variable)
            record.pairs.resize(*header_variable);
        header_variable.reset();
        discard_variable = false;
    };
    while (stream.peek() != std::char_traits<char>::eof()) {
        const auto offset = static_cast<std::size_t>(stream.tellg());
        const auto pair_line = line;
        std::size_t value_offset = offset;
        int code = -1;
        std::string value;
        try {
            if (stats.binary) {
                const auto low = stream.get();
                if (first) {
                    compact = low == 0 && stream.peek() >= 0x20 && stream.peek() < 0x7f;
                    first = false;
                }
                code = detail::read_binary_code(stream, low, compact);
                value_offset = static_cast<std::size_t>(stream.tellg());
                value = detail::read_binary_value(stream, code);
            } else {
                std::string text;
                std::getline(stream, text);
                ++line;
                value_offset = offset + text.size() + 1;
                text = strip(text);
                if (!text.empty() && text[0] == '+')
                    text.erase(0, 1);
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), code);
                const bool valid_code = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
                if (!valid_code)
                    code = -1;
                if (!std::getline(stream, value))
                    throw std::runtime_error("Missing DXF group value");
                ++line;
                if (!value.empty() && value.back() == '\r')
                    value.pop_back();
                if (!valid_code)
                    throw std::runtime_error("Invalid DXF group code");
            }
            if (code < 0 || code > 1071)
                throw std::runtime_error("DXF group code out of range");
        } catch (const std::exception &error) {
            report("stream-read-failed", error.what(), offset, pair_line,
                   records.empty() || code == 0 ? nullptr : &records.back());
            if (header_variable && code != 0 && code != 9)
                discard_variable = true;
            else if (!header_variable && !records.empty() && code != 0)
                records.back().invalid = true;
            // Binary framing cannot be recovered by searching arbitrary bytes for 0.
            if (stats.binary || stream.eof())
                break;
            continue;
        }
        if (code == 0 || code == 9)
            finish_variable();
        if (discard_variable)
            continue;
        if (code == 0) {
            value = strip(value);
            records.push_back({});
            records.back().section = section;
        }
        if (records.empty()) {
            report("record-start-missing", "DXF data precedes the first record", offset, pair_line);
            continue;
        }
        auto &record = records.back();
        record.pairs.push_back({code, value, offset, pair_line, value_offset});
        if (code == 0 && value.empty()) {
            report("record-type-missing", "Empty DXF record type", offset, pair_line, &record);
            record.invalid = true;
        }
        if (record.type() == "SECTION" && record.pairs.size() == 2 && code == 2) {
            section = value;
            record.section = value;
        }
        if (code == 0 && value == "ENDSEC")
            section.clear();
        if (record.type() == "SECTION" && section == "HEADER" && code == 9)
            header_variable = record.pairs.size() - 1;
        if (code != 0 && !detail::is_string_value(code)) {
            try {
                const auto text = stats.binary ? detail::text_value_from_binary(code, value) : value;
                detail::binary_value_from_text(code, text);
                if (stats.binary && code >= 290 && code <= 299 && static_cast<unsigned char>(value[0]) > 1)
                    throw std::runtime_error("Invalid DXF boolean");
            } catch (const std::exception &error) {
                report("invalid-group-value", error.what(), offset, pair_line, &record);
                if (header_variable)
                    discard_variable = true;
                else
                    record.invalid = true;
                continue;
            }
        }
        if (header_variable && record.pairs.size() == *header_variable + 2) {
            const auto &name = record.pairs[*header_variable].value;
            if ((name == "$ACADVER" && code != 1) ||
                (name == "$INSUNITS" && !detail::as_i16(code, value, stats.binary))) {
                report("invalid-header-value", "Unexpected value type for " + name, offset, pair_line, &record);
                discard_variable = true;
            } else if (name == "$ACADVER" && dxf_version_from_string(value) == DxfVersion::Unknown) {
                // Preserve the original string and Unknown version during recovery.
                report("unknown-header-version", "Unrecognized $ACADVER", offset, pair_line, &record);
            }
        }
    }
    finish_variable();
    // A malformed or incomplete compound entity is recovered as a whole.
    std::optional<std::size_t> parent;
    auto incomplete = [&](std::size_t end) {
        const auto &record = records[*parent];
        report("sequence-end-missing", "Compound entity has no SEQEND", record.pairs[0].offset,
               record.pairs[0].line, &record);
        for (std::size_t j = *parent; j < end; ++j)
            records[j].invalid = true;
        parent.reset();
    };
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto type = records[i].type();
        bool child = type == "VERTEX" || type == "ATTRIB" || type == "SEQEND";
        if (parent && !child)
            incomplete(i);
        bool compound = type == "POLYLINE";
        if (type == "INSERT")
            for (const auto &p : records[i].pairs)
                if (p.code == 66)
                    compound = detail::as_i16(66, p.value, stats.binary) == 1;
        if (compound)
            parent = i;
        if (child && !parent) {
            const auto &record = records[i];
            report("orphan-sequence-record", "Child record without a compound parent", record.pairs[0].offset,
                   record.pairs[0].line, &record);
            records[i].invalid = true;
        }
        if (parent && child && type != "SEQEND" &&
            ((records[*parent].type() == "INSERT") != (type == "ATTRIB"))) {
            const auto &record = records[i];
            report("sequence-child-mismatch", "Child type does not match compound parent",
                   record.pairs[0].offset, record.pairs[0].line, &record);
            records[i].invalid = true;
        }
        if (parent && records[i].invalid) {
            for (std::size_t j = *parent; j <= i; ++j)
                records[j].invalid = true;
        }
        if (parent && records[*parent].invalid)
            records[i].invalid = true;
        if (type == "SEQEND")
            parent.reset();
    }
    if (parent)
        incomplete(records.size());
    std::ostringstream sanitized;
    if (stats.binary)
        sanitized.write(detail::binary_signature.data(), detail::binary_signature.size());
    auto emit = [&](int code, const std::string &value) {
        if (stats.binary)
            detail::write_binary_pair(sanitized, compact, code, value);
        else
            sanitized << code << '\n' << value << '\n';
    };
    section.clear();
    bool table = false, ended = false, skip_unnamed = false;
    std::vector<const CheckedRecord *> retained;
    bool saw_header = false;
    const bool header_present = std::any_of(records.begin(), records.end(), [](const CheckedRecord& r) {
        return !r.invalid && r.type() == "SECTION" && r.pairs.size() > 1 &&
               r.pairs[1].code == 2 && strip(r.pairs[1].value) == "HEADER";
    });
    // Rust constructs an AC1032 document when no version is supplied. Re-encode
    // configured legacy input as UTF-8 so the resulting file is self-contained.
    bool implicit_version = !header_present;
    const char* page = implicit_version && options.default_encoding
        ? detail::legacy_page("AC1009", *options.default_encoding) : nullptr;
    for (auto &record : records) {
        const auto type = record.type();
        const auto &position = record.pairs.front();
        const bool data = !structural(type);
        if (data)
            ++stats.source_records;
        if (skip_unnamed) {
            if (type == "ENDSEC") {
                skip_unnamed = false;
                continue;
            }
            if (type != "SECTION" && type != "EOF") {
                if (data)
                    ++stats.skipped_records;
                continue;
            }
            skip_unnamed = false;
        }
        if (record.invalid) {
            if (structural(type)) {
                // A damaged structural marker has no trustworthy recovery boundary.
                report("section-read-failed", "Cannot recover a damaged structural record", position.offset,
                       position.line, &record);
                break;
            }
            ++stats.skipped_records;
            continue;
        }
        if (ended) {
            report("records-after-eof", "Records after DXF EOF", position.offset, position.line, &record);
            if (data)
                ++stats.skipped_records;
            continue;
        }
        if (type == "SECTION") {
            const auto name = record.pairs.begin() + 1;
            if (!section.empty()) {
                report("unclosed-section", "New SECTION before ENDSEC in " + section, position.offset,
                       position.line);
                if (table) {
                    emit(0, "ENDTAB");
                    table = false;
                }
                emit(0, "ENDSEC");
                section.clear();
            }
            if (name == record.pairs.end() || name->code != 2 || strip(name->value).empty()) {
                report("section-name-missing", "Missing section name", position.offset, position.line, &record);
                skip_unnamed = true;
                continue;
            }
            section = name->value;
            ++stats.source_sections;
        } else if (type == "TABLE" && section == "TABLES") {
            const auto name = std::find_if(record.pairs.begin(), record.pairs.end(),
                                           [](const auto &p) { return p.code == 2; });
            if (table || name == record.pairs.end()) {
                report("table-read-failed", "Missing table name or nested TABLE", position.offset,
                       position.line, &record);
                break;
            }
            table = true;
        } else if (type == "ENDTAB") {
            if (!table) {
                report("unmatched-endtab", "Unmatched ENDTAB", position.offset, position.line, &record);
                continue;
            }
            table = false;
        } else if (type == "ENDSEC") {
            if (section.empty()) {
                report("unmatched-endsec", "Unmatched ENDSEC", position.offset, position.line, &record);
                continue;
            }
            if (table) {
                report("unclosed-table", "ENDSEC before ENDTAB", position.offset, position.line, &record);
                emit(0, "ENDTAB");
                table = false;
            }
            section.clear();
        } else if (type == "EOF") {
            if (!section.empty()) {
                report("unclosed-section", "EOF before ENDSEC", position.offset, position.line, &record);
                if (table) {
                    emit(0, "ENDTAB");
                    table = false;
                }
                emit(0, "ENDSEC");
                section.clear();
            }
            ended = true;
            stats.stream_completed = true;
        } else if (section.empty()) {
            report("record-outside-section", "Record outside a section", position.offset, position.line,
                   &record);
            if (data)
                ++stats.skipped_records;
            continue;
        }
        if (!header_present && !saw_header) {
            append_diagnostic({"header-absent", "No HEADER, using the upstream R2018 default", {}, type, {},
                               position.offset, stats.binary ? std::nullopt : std::optional<std::size_t>(position.line)});
            emit(0, "SECTION");
            emit(2, "HEADER");
            emit(9, "$ACADVER");
            emit(1, "AC1032");
            emit(0, "ENDSEC");
            saw_header = true;
        }
        bool apply_fallback = false;
        if (type == "SECTION" && section == "HEADER") {
            std::string version, declaration;
            bool declared = false;
            for (std::size_t i = 1; i < record.pairs.size(); ++i) {
                const auto &name = record.pairs[i - 1];
                const auto &field = record.pairs[i];
                if (name.code != 9)
                    continue;
                if (name.value == "$ACADVER" && field.code == 1)
                    version = strip(field.value);
                if (name.value == "$DWGCODEPAGE") {
                    declared = true;
                    if (field.code == 3)
                        declaration = strip(field.value);
                }
            }
            saw_header = true;
            implicit_version = std::none_of(record.pairs.begin(), record.pairs.end(), [](const CheckedPair& p) {
                return p.code == 9 && p.value == "$ACADVER";
            });
            if (implicit_version)
                append_diagnostic({"version-absent", "No $ACADVER, using the upstream R2018 default", "HEADER",
                                   type, {}, position.offset, stats.binary ? std::nullopt : std::optional<std::size_t>(position.line)});
            const auto v = dxf_version_from_string(version);
            apply_fallback =
                !declared && options.default_encoding && v != DxfVersion::Unknown && v < DxfVersion::AC1021;
            if(declared && v!=DxfVersion::Unknown && v<DxfVersion::AC1021 && !detail::known_code_page(declaration)){
                // The upstream reader maps unknown declarations, including an empty
                // name, to Windows-1252. Keep a usable declaration in the saved file.
                declaration="ANSI_1252";
                for(std::size_t i=1;i<record.pairs.size();++i)
                    if(record.pairs[i-1].code==9 && record.pairs[i-1].value=="$DWGCODEPAGE" && record.pairs[i].code==3)
                        record.pairs[i].value=declaration;
                append_diagnostic({"unknown-declared-code-page","Using Windows-1252 for an unknown legacy declaration",
                                   "HEADER",type,{},position.offset,stats.binary?std::nullopt:std::optional<std::size_t>(position.line)});
            }
            page = implicit_version
                ? (options.default_encoding ? detail::legacy_page("AC1009", *options.default_encoding) : nullptr)
                : detail::legacy_page(version, apply_fallback ? *options.default_encoding : declaration);
        }
        for (auto &pair : record.pairs) {
            if (page && detail::is_string_value(pair.code)) {
                detail::recover_legacy_text(pair.value, page, [&](auto error, std::size_t at) {
                    const bool byte_error = error == detail::LegacyTextError::InvalidBytes;
                    const char *message = byte_error ? "Invalid legacy code-page sequence"
                                          : error == detail::LegacyTextError::MalformedEscape
                                              ? "Malformed Unicode escape"
                                              : "Unpaired UTF-16 surrogate";
                    report(byte_error ? "invalid-code-page-bytes" : "invalid-unicode-escape",
                           std::string(message) + " in group " + std::to_string(pair.code),
                           pair.value_offset + at, pair.line, &record);
                });
                if (implicit_version) pair.value = detail::from_legacy(pair.value, page);
            } else if (implicit_version && detail::is_string_value(pair.code)) {
                pair.value = detail::decode_undeclared_text(pair.value, stats.binary);
            }
            emit(pair.code, pair.value);
        }
        if (type == "SECTION" && section == "HEADER" && implicit_version) {
            emit(9, "$ACADVER");
            emit(1, "AC1032");
        }
        if (apply_fallback) {
            emit(9, "$DWGCODEPAGE");
            emit(3, *options.default_encoding);
            append_diagnostic({"fallback-encoding-applied",
                               "Applied configured legacy code page",
                               "HEADER",
                               "SECTION",
                               {},
                               position.offset,
                               stats.binary ? std::nullopt : std::optional<std::size_t>(position.line)});
        }
        if (data) {
            ++stats.retained_records;
            retained.push_back(&record);
        }
    }
    if (!ended) {
        report("stream-ended-early", "Drawing stream ended before EOF", bytes.size(), line);
        if (table)
            emit(0, "ENDTAB");
        if (!section.empty())
            emit(0, "ENDSEC");
        emit(0, "EOF");
    }
    std::istringstream cleaned(sanitized.str());
    DxfDocument doc;
    try {
        doc = DxfDocument::read(cleaned);
    } catch (const std::exception &e) {
        throw DxfReadError({"document-read-failed", e.what(), section, {}, {}, bytes.size(), {}});
    }
    doc.for_each<DxfTable>([&](const DxfTable &table) {
        auto warn = [&](std::string message) {
            DxfReadDiagnostic warning{"invalid-table-adjunct", std::move(message), "OBJECTS",
                                      table.type(), table.handle(), 0, {}};
            for (const auto *r : retained) {
                if (r->type() != table.type()) continue;
                const auto id = std::find_if(r->pairs.begin(), r->pairs.end(),
                    [](const auto &p) { return p.code == 5; });
                if (id != r->pairs.end() && strip(id->value) == table.handle()) {
                    warning.source_offset = r->pairs.front().offset;
                    if (!stats.binary) warning.source_line = r->pairs.front().line;
                    break;
                }
            }
            append_diagnostic(warning);
        };
        try {
            const auto content = doc.table_content_handle(table.handle());
            if (!content.empty()) {
                const auto layout = table.definition();
                const auto location = doc.handle_index_.at(std::stoull(content, nullptr, 16)).front();
                const auto *object = std::get_if<DxfTableContent>(&doc.records_.at(location.record));
                if (!object) throw std::invalid_argument("Invalid TABLECONTENT target");
                const auto value = object->definition();
                if (value.columns.size() != layout.column_widths.size() ||
                    value.rows.size() != layout.cells.size() ||
                    !std::equal(value.rows.begin(), value.rows.end(), layout.cells.begin(),
                         [](const auto &row, const auto &cells) { return row.cells.size() == cells.size(); }))
                    warn("TABLECONTENT dimensions do not match ACAD_TABLE");
            }
        } catch (const std::invalid_argument &error) { warn(error.what()); }
        try {
            if (!doc.table_geometry_handle(table.handle()).empty()) {
                const auto layout = table.definition();
                const auto geometry = doc.table_geometry_definition(table.handle());
                if (geometry.rows < 0 || geometry.columns < 0 ||
                    std::size_t(geometry.rows) != layout.cells.size() ||
                    std::size_t(geometry.columns) != layout.column_widths.size() ||
                    geometry.cells.size() != layout.cells.size() * layout.column_widths.size())
                    warn("TABLEGEOMETRY dimensions do not match ACAD_TABLE");
            }
        } catch (const std::invalid_argument &error) { warn(error.what()); }
    });
    // Unknown records survive; they are not counted as successful semantic decodes.
    static const std::unordered_set<std::string> raw_known{"XRECORD",
                                                           "DICTIONARY",
                                                           "ACDBDICTIONARYWDFLT",
                                                           "ACDBPLACEHOLDER",
                                                           "PLOTSETTINGS",
                                                           "CLASS",
                                                           "STYLE",
                                                           "DIMSTYLE",
                                                           "VPORT",
                                                           "UCS",
                                                           "VIEW",
                                                           "APPID"};
    doc.for_each<DxfRecord>([&](const auto &r) {
        if (structural(r.type()) || raw_known.count(r.type()))
            return;
        ++stats.opaque_records;
        auto found = std::find_if(retained.begin(), retained.end(), [&](const auto *value) {
            if (value->type() != r.type())
                return false;
            for (const auto &p : value->pairs)
                if (p.code == 5 || p.code == 105)
                    return strip(p.value) == r.handle();
            return r.handle().empty();
        });
        DxfReadDiagnostic warning{"opaque-record-preserved",
                                  "Record preserved without a dedicated typed decoder",
                                  {},
                                  r.type(),
                                  r.handle(),
                                  0,
                                  {}};
        if (found != retained.end()) {
            warning.section = (*found)->section;
            warning.source_offset = (*found)->pairs[0].offset;
            if (!stats.binary)
                warning.source_line = (*found)->pairs[0].line;
            retained.erase(found);
        }
        append_diagnostic(warning);
    });
    return doc;
}

namespace {
DxfRecoveryError recovery_failed(const DxfReadStatistics &stats) {
    if (stats.diagnostics.empty())
        return DxfRecoveryError("the reader produced no usable drawing records");
    return DxfRecoveryError("the reader produced no usable drawing records (" +
                            std::to_string(stats.diagnostics.size()) + " diagnostics)");
}
} // namespace

RecoverOutcome DxfDocument::recover(std::istream &input) { return recover(input, {}); }

RecoverOutcome DxfDocument::recover(std::istream &input, DxfReadOptions options) {
    options.failsafe = true;
    RecoverOutcome outcome;
    try {
        outcome.document = DxfDocument::read(input, options, outcome.stats);
    } catch (const DxfReadError &) {
        if (!outcome.stats.has_usable_drawing_data())
            throw recovery_failed(outcome.stats);
        throw;
    }
    if (!outcome.stats.has_usable_drawing_data())
        throw recovery_failed(outcome.stats);
    outcome.repair = outcome.document.repair();
    return outcome;
}
} // namespace libopencad
