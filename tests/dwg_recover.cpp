// SPDX-License-Identifier: MPL-2.0
#include "../src/dwg_sections_internal.hpp"
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_sections.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace libopencad;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

DwgDocument strict(const std::string &bytes) {
    std::istringstream input(bytes);
    return DwgDocument::read(input);
}

DwgRecoverOutcome recover(const std::string &bytes) {
    std::istringstream input(bytes);
    return DwgDocument::recover(input);
}

DwgReadOutcome read_stats(const std::string &bytes, bool failsafe = false) {
    std::istringstream input(bytes);
    return DwgDocument::read_with_stats(input, {failsafe});
}

bool has_diagnostic(const DwgReadStatistics &stats, std::string_view code) {
    return std::any_of(stats.diagnostics.begin(), stats.diagnostics.end(), [&](const auto &entry) {
        return entry.code == code;
    });
}

void expect_read_error(const std::string &bytes, bool failsafe, std::string_view code) {
    try {
        (void)read_stats(bytes, failsafe);
    } catch (const DwgReadError &error) {
        check(!error.stats().stream_completed && has_diagnostic(error.stats(), code), "strict read diagnostic");
        return;
    }
    throw std::runtime_error("DWG statistics read unexpectedly succeeded");
}

std::string save(const DwgDocument &document) {
    std::ostringstream out;
    document.write(out);
    return out.str();
}

void emit(const std::string &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(out), "could not write recovery fixture");
}

bool repair_did(const RepairReport &repair, RepairActionKind kind) {
    return std::any_of(repair.actions.begin(), repair.actions.end(), [&](const auto &action) {
        return action.kind == kind;
    });
}

void expect_unusable(const std::string &bytes, const std::string &diagnostic, bool reached_records = false) {
    try {
        (void)recover(bytes);
    } catch (const DwgRecoveryError &error) {
        const auto &stats = error.stats();
        check(!stats.has_usable_drawing_data() && stats.record_stream_read == reached_records,
              "unusable stream statistics");
        check(diagnostic.empty() || std::any_of(stats.diagnostics.begin(), stats.diagnostics.end(), [&](const auto &entry) {
            return entry.code == diagnostic;
        }), "unusable stream diagnostic");
        return;
    }
    throw std::runtime_error("recovery accepted a drawing without usable records");
}

std::string set_section(const std::string &bytes, DxfVersion version, const std::string &name,
                        std::string contents) {
    return version == DxfVersion::AC1021 ? detail::replace_ac21_sections(bytes, {{name, std::move(contents)}})
                                       : detail::replace_ac18_sections(bytes, {{name, std::move(contents)}});
}

void update_record_crc(std::string &section, const DwgR2000Record &record) {
    const auto end = record.body_offset + record.body_size;
    std::uint16_t crc = 0xc0c1;
    for (auto i = record.offset; i < end; ++i) {
        crc ^= static_cast<unsigned char>(section[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    section[end] = static_cast<char>(crc & 0xff);
    section[end + 1] = static_cast<char>(crc >> 8);
}
} // namespace

int main(int argc, char **argv) try {
    check(argc == 3, "expected version and output prefix");
    const auto version = dxf_version_from_string(argv[1]);
    const std::string prefix = argv[2];
    auto bytes = create_dwg(version, {{DwgLineGeometry{{1, 2, 3}, {4, 2, 3}}},
                                      {DwgLineGeometry{{5, 6, 7}, {8, 6, 7}}},
                                      {DwgLineGeometry{{9, 10, 11}, {12, 10, 11}}}});
    const auto baseline = strict(bytes);
    const auto &objects = baseline.objects();
    check(objects.lines.size() == 3, "recovery fixture line count");
    const auto first = objects.lines.begin()->first;
    const auto last = objects.lines.rbegin()->first;
    const auto broken = std::next(objects.lines.begin())->first;

    const auto measured = read_stats(bytes);
    const auto &clean_stats = measured.stats;
    check(measured.writable && save(measured.document) == bytes, "measurement-only read preserves source bytes");
    check(clean_stats.source_version == argv[1] && clean_stats.record_stream_read && clean_stats.stream_completed &&
              !clean_stats.recovery_mode && clean_stats.has_usable_drawing_data() && !clean_stats.recovered() &&
              clean_stats.source_sections == (version == DxfVersion::AC1009 ? 1u : 2u) &&
              clean_stats.source_records == objects.records.size() &&
              clean_stats.decoded_source_records == clean_stats.source_records && !clean_stats.skipped_source_records,
          "clean DWG read statistics");
    check(clean_stats.output_entities == 3 &&
              clean_stats.output_table_records > 0 && clean_stats.output_records() >= clean_stats.output_entities &&
              (version == DxfVersion::AC1009 || clean_stats.output_objects > 0),
          "DWG output entity, object and table counts");
    auto tolerant = read_stats(bytes, true);
    check(tolerant.stats.recovery_mode == (version != DxfVersion::AC1009) && tolerant.writable &&
              save(tolerant.document) == bytes && tolerant.stats.output_records() == clean_stats.output_records(),
          "failsafe read of a clean drawing");
    std::ostringstream statistics;
    statistics << unsigned(clean_stats.maintenance_version) << ' ' << clean_stats.source_sections << ' '
               << clean_stats.source_records << ' ' << clean_stats.output_entities << ' '
               << clean_stats.output_objects << ' ' << clean_stats.output_table_records << '\n';
    emit(prefix + "-stats.txt", statistics.str());

    const auto valid = recover(bytes);
    check(valid.stats.source_version == argv[1] && valid.stats.has_usable_drawing_data() &&
              valid.stats.stream_completed && !valid.stats.skipped_source_records && valid.writable,
          "valid DWG recovery statistics");
    check(valid.stats.recovery_mode == (version != DxfVersion::AC1009), "legacy read uses the whole stream");
    check(valid.repair.after.scanned_entities == 3 && valid.document.line(first).start() == Point3{1, 2, 3},
          "valid DWG recovery result");
    auto resaved_valid = save(valid.document);
    const auto again = recover(resaved_valid);
    check(again.repair.actions.empty() && save(again.document) == resaved_valid, "repeat recovery is idempotent");
    emit(prefix + "-valid.dwg", bytes);

    expect_unusable({}, "file-header-read-failed");
    expect_unusable("AC1032", "file-header-read-failed");
    const auto &record = objects.records.at(broken);
    if (version == DxfVersion::AC1009) {
        bytes[record.offset + 1] ^= 1;
        expect_read_error(bytes, false, "legacy-read-failed");
        const auto failed = read_stats(bytes, true);
        check(!failed.writable && failed.stats.recovered() && failed.stats.skipped_source_records == 1 &&
                  !failed.stats.record_stream_read && !failed.stats.stream_completed &&
                  !failed.stats.has_usable_drawing_data(), "failsafe R12 reports failure of the whole record stream");
        auto blank = read_stats(create_dwg(version, {}));
        check(blank.writable && blank.stats.record_stream_read && blank.stats.stream_completed &&
                  !blank.stats.has_usable_drawing_data() && blank.stats.output_entities == 0,
              "empty valid R12 read completes without drawing records");
        expect_unusable(bytes, "legacy-read-failed");
        expect_unusable(create_dwg(version, {}), "", true);
        return 0;
    }

    const bool paged = version >= DxfVersion::AC1018;
    auto section = paged ? read_dwg_paged_section(bytes, "AcDb:AcDbObjects") : bytes;
    section[record.offset] = section[record.offset + 1] = 0; // Invalid record size, intact handle map.
    auto damaged = paged ? set_section(bytes, version, "AcDb:AcDbObjects", section) : section;
    bool strict_rejected = false;
    try {
        (void)strict(damaged);
    } catch (const std::invalid_argument &) {
        strict_rejected = true;
    }
    check(strict_rejected, "strict DWG read accepted the damaged frame");
    expect_read_error(damaged, false, "record-catalog-failed");
    const auto measured_partial = read_stats(damaged, true);
    const auto &read_stats_partial = measured_partial.stats;
    check(!measured_partial.writable && read_stats_partial.record_stream_read && read_stats_partial.stream_completed &&
              read_stats_partial.has_usable_drawing_data() && read_stats_partial.recovery_mode &&
              read_stats_partial.source_records == clean_stats.source_records &&
              read_stats_partial.output_entities + 1 == clean_stats.output_entities &&
              measured_partial.document.objects().lines.size() == 2,
          "failsafe read returns partial geometry and counts");
    try {
        (void)save(measured_partial.document);
        throw std::logic_error("failsafe statistics outcome wrote a damaged source");
    } catch (const std::runtime_error &) {
    }
    const auto partial = recover(damaged);
    const auto &stats = partial.stats;
    check(stats.source_sections == 2 && stats.source_records == objects.records.size() &&
              stats.decoded_source_records + 1 == stats.source_records && stats.skipped_source_records == 1 &&
              stats.has_usable_drawing_data() && stats.recovered() && stats.stream_completed,
          "partial recovery counters");
    check(stats.output_records() == read_stats_partial.output_records() &&
              stats.skipped_source_records == read_stats_partial.skipped_source_records &&
              stats.decoded_source_records == read_stats_partial.decoded_source_records,
          "recovery reuses read-stage statistics");
    check(std::any_of(stats.diagnostics.begin(), stats.diagnostics.end(), [&](const auto &entry) {
        return entry.code == "record-catalog-failed" && entry.record_handle == broken &&
               entry.source_offset == record.offset && entry.source_offset_basis ==
                   (paged ? DwgOffsetBasis::ObjectSectionByte : DwgOffsetBasis::FileByte);
    }), "record failure source location");
    check(partial.document.objects().lines.size() == 2 && !partial.document.objects().records.count(broken) &&
              partial.document.line(first).end() == baseline.line(first).end() &&
              partial.document.line(last).end() == baseline.line(last).end(),
          "surviving records and geometry");
    check(partial.writable && repair_did(partial.repair, RepairActionKind::RebuildDwgHandleMap) &&
              repair_did(partial.repair, RepairActionKind::RemoveInvalidBlockMembers),
          "block and handle map repairs");
    check(partial.repair.before.count(AuditIssueCode::DanglingReference) > 0 &&
              partial.repair.after.count(AuditIssueCode::DanglingReference) == 0,
          "audit before and after block repair");
    auto healed = save(partial.document);
    auto reopened = strict(healed);
    auto moved = reopened.line(last);
    moved.set_end({13, 10, 11});
    reopened.update(moved);
    check(strict(save(reopened)).line(last).end() == Point3{13, 10, 11}, "recovered survivor remains editable");
    emit(prefix + "-damaged.dwg", damaged);
    emit(prefix + "-recovered.dwg", healed);
    if (version == DxfVersion::AC1032) {
        auto body_damage = read_dwg_paged_section(bytes, "AcDb:AcDbObjects");
        const auto normal_flag = record.body_offset * 8 + record.geometry_end - 1;
        const auto mask = 0x80u >> (normal_flag % 8);
        check((static_cast<unsigned char>(body_damage[normal_flag / 8]) & mask) != 0,
              "expected compact LINE normal");
        body_damage[normal_flag / 8] ^= static_cast<char>(mask);
        update_record_crc(body_damage, record);
        auto decoded = recover(set_section(bytes, version, "AcDb:AcDbObjects", std::move(body_damage)));
        check(decoded.writable && decoded.stats.skipped_source_records == 1 &&
                  !decoded.document.objects().lines.count(broken) && decoded.document.objects().lines.size() == 2,
              "partially decoded LINE must not be published");
        check(std::any_of(decoded.stats.diagnostics.begin(), decoded.stats.diagnostics.end(), [&](const auto &entry) {
            return entry.code == "record-decode-failed" && entry.record_handle == broken && entry.record_type == "LINE";
        }), "typed decode diagnostic");

        auto handles = read_dwg_paged_section(bytes, "AcDb:Handles");
        handles.back() ^= 1;
        auto damaged_map = set_section(bytes, version, "AcDb:Handles", handles);
        expect_read_error(damaged_map, false, "checksum-mismatch");
        auto measured_checksum = read_stats(damaged_map, true);
        check(!measured_checksum.writable && !measured_checksum.stats.recovered() &&
                  measured_checksum.stats.has_usable_drawing_data() &&
                  has_diagnostic(measured_checksum.stats, "checksum-mismatch"), "handle-map checksum warning");
        auto checksum = recover(damaged_map);
        check(checksum.writable && !checksum.stats.skipped_source_records &&
                  repair_did(checksum.repair, RepairActionKind::RebuildDwgHandleMap) &&
                  strict(save(checksum.document)).objects().lines.size() == 3,
              "handle map checksum recovery");
        auto header_damage = bytes;
        header_damage.at(0x100 + 32 + 1) ^= 1; // Default AC18-family Header page.
        auto measured_header = read_stats(header_damage);
        check(!measured_header.writable && measured_header.stats.has_usable_drawing_data() &&
                  has_diagnostic(measured_header.stats, "header-extract-failed"), "optional header extraction failure");
        auto invalid_header = set_section(bytes, version, "AcDb:Header", "broken header");
        expect_read_error(invalid_header, false, "header-decode-failed");
        auto measured_bad_header = read_stats(invalid_header, true);
        check(!measured_bad_header.writable && measured_bad_header.stats.has_usable_drawing_data() &&
                  has_diagnostic(measured_bad_header.stats, "header-decode-failed"), "failsafe header decoding failure");
        expect_read_error({}, true, "file-header-read-failed");
        auto failed = read_stats("AC1032", true);
        check(!failed.writable && !failed.stats.has_usable_drawing_data() && !failed.stats.stream_completed &&
                  has_diagnostic(failed.stats, "file-header-read-failed"), "failsafe file directory failure");
        auto recovered_header = recover(header_damage);
        check(recovered_header.document.objects().lines.size() == 3 && recovered_header.stats.recovered() &&
                  !recovered_header.writable,
              "usable record stream survives an unreadable header section");
        std::ostringstream unsafe_output;
        bool refused = false;
        try {
            recovered_header.document.write(unsafe_output);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused && unsafe_output.str().empty(), "unsafe DWG output must fail before writing");
        for (const auto &[id, entry] : objects.records)
            section[entry.offset] = section[entry.offset + 1] = 0;
        auto all_damaged = set_section(bytes, version, "AcDb:AcDbObjects", std::move(section));
        auto unusable = read_stats(all_damaged, true);
        check(!unusable.writable && !unusable.stats.has_usable_drawing_data() && unusable.stats.stream_completed &&
                  unusable.stats.record_stream_read && unusable.stats.decoded_source_records == 0 &&
                  unusable.stats.skipped_source_records == clean_stats.source_records && unusable.stats.output_records() == 0,
              "failsafe reports a completed scan with no surviving records");
        expect_unusable(all_damaged, "record-catalog-failed", true);
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
