// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <tuple>

using namespace libopencad;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string bytes_of(const DwgDocument &document) {
    std::ostringstream output;
    document.write(output);
    return output.str();
}

DwgReadOutcome read_stream(const std::filesystem::path &path, const DwgReadOptions &options) {
    std::ifstream input(path, std::ios::binary);
    return DwgDocument::read_with_stats(input, options);
}

void compare_stats(const DwgReadStatistics &mapped, const DwgReadStatistics &streamed) {
    const auto counts = [](const DwgReadStatistics &s) {
        return std::tie(s.source_version, s.maintenance_version, s.recovery_mode, s.record_stream_read,
                        s.stream_completed, s.source_sections, s.source_records, s.decoded_source_records,
                        s.skipped_source_records, s.recovered_errors, s.output_entities, s.output_objects,
                        s.output_table_records);
    };
    check(counts(mapped) == counts(streamed), "mmap and stream statistics differ");
    check(mapped.diagnostics.size() == streamed.diagnostics.size(), "mmap diagnostic count");
    const auto diagnostic = [](const DwgReadDiagnostic &d) {
        return std::tie(d.code, d.message, d.section, d.stage, d.source_offset, d.source_offset_basis,
                        d.record_handle, d.record_type);
    };
    for (std::size_t i = 0; i < mapped.diagnostics.size(); ++i)
        check(diagnostic(mapped.diagnostics[i]) == diagnostic(streamed.diagnostics[i]), "mmap diagnostic differs");
}

template <class Read> DwgReadError read_error(Read read) {
    try {
        (void)read();
    } catch (const DwgReadError &error) {
        return error;
    }
    throw std::logic_error("DWG read unexpectedly succeeded");
}

void compare_failures(const std::filesystem::path &path, const DwgReadOptions &options, std::string_view code) {
    const auto streamed = read_error([&] { return read_stream(path, options); });
    const auto mapped = read_error([&] { return DwgDocument::from_mmap(path, options); });
    compare_stats(mapped.stats(), streamed.stats());
    check(mapped.what() == std::string_view(streamed.what()) && !mapped.stats().diagnostics.empty() &&
              mapped.stats().diagnostics.front().code == code,
          "mmap failure must report the same diagnostic as the stream reader");
}

void verify_protected_failure(const std::filesystem::path &path, const std::optional<std::string> &password) {
    compare_failures(path, {false, password}, "decryption-failed");
    const DwgReadOptions options{true, password};
    const auto mapped = DwgDocument::from_mmap(path, options);
    const auto streamed = read_stream(path, options);
    compare_stats(mapped.stats, streamed.stats);
    check(!mapped.writable && !streamed.writable && mapped.document.objects().records.empty(),
          "failsafe mapped read disclosed or allowed writes to protected records");
    try {
        (void)bytes_of(mapped.document);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::logic_error("failsafe mapped document was writable");
}

void verify_edit(const std::filesystem::path &path, bool linear, const DwgDocument &mapped,
                 const std::string &original) {
    auto edit = mapped;
    if (linear) {
        const Preview preview{PreviewFormat::Png, {0x89, 'P', 'N', 'G', 1, 2, 3, 4, 5}};
        edit.set_preview(preview);
        check(edit.preview() == preview, "mapped linear preview was not replaced");
        std::istringstream saved(bytes_of(edit));
        check(DwgDocument::read(saved).preview() == preview, "edited linear preview did not persist");
    } else {
        const auto layer = edit.add_layer("MMAP-EDIT");
        std::istringstream saved(bytes_of(edit));
        const auto reread = DwgDocument::read(saved);
        check(reread.objects().symbols.at(layer).name == "MMAP-EDIT", "mapped drawing edit did not persist");
    }
    check(bytes_of(mapped) == original && bytes_of(DwgDocument::from_mmap(path).document) == original,
          "editing a mapped document modified the shared source");
}
} // namespace

int main(int argc, char **argv) try {
    check(argc == 4, "expected mmap fixture case, path, and temp file");
    const std::string kind = argv[1];
    const std::filesystem::path path = argv[2];
    const bool protected_file = kind != "linear" && kind != "modeler";
    DwgReadOptions options;
    if (protected_file)
        options.password = kind == "AC1021" ? u8"Straßeı😀CaseMixCaseMixCaseMixCaseMixCaseMixCaseMix" : "budweiser";

    std::ifstream source(path, std::ios::binary);
    check(bool(source), "cannot open original DWG fixture");
    const std::string original{std::istreambuf_iterator<char>(source), {}};
    source.close();
    const auto streamed = read_stream(path, options);
    auto mapped_document = [&] {
        const auto mapped = DwgDocument::from_mmap(path, options);
        compare_stats(mapped.stats, streamed.stats);
        check(mapped.writable && mapped.stats.has_usable_drawing_data() && mapped.stats.stream_completed,
              "valid mmap read did not yield a complete writable drawing");
        return mapped.document; // Keep a copy after the originating outcome is destroyed.
    }();
    const auto moved = std::move(mapped_document);
    check(bytes_of(moved) == original, "mmap document did not preserve the source bytes");
    const auto &mapped = moved.objects();
    const auto &baseline = streamed.document.objects();
    check(mapped.records.size() == baseline.records.size() && mapped.lines.size() == baseline.lines.size() &&
              mapped.modelers.size() == baseline.modelers.size(), "mmap DWG record counts differ");
    for (const auto &[handle, line] : mapped.lines) {
        const auto &expected = baseline.lines.at(handle);
        check(line.start == expected.start && line.end == expected.end, "mmap LINE geometry differs");
    }
    for (const auto &[handle, modeler] : mapped.modelers)
        check(modeler.data.geometry.sab == baseline.modelers.at(handle).data.geometry.sab,
              "mmap modeler SAB data differs");

    if (protected_file) {
        verify_protected_failure(path, std::nullopt);
        verify_protected_failure(path, "incorrect");
        verify_protected_failure(path, std::string{});
    } else {
        verify_edit(path, kind == "linear", moved, original);
    }
    if (kind == "linear") {
        const std::filesystem::path empty = argv[3];
        check(static_cast<bool>(std::ofstream(empty, std::ios::binary)), "cannot create empty mmap fixture");
        compare_failures(empty, {}, "file-header-read-failed");
        std::filesystem::remove(empty);
        for (const auto &bad_path : {empty, path.parent_path()}) {
            const auto error = read_error([&] { return DwgDocument::from_mmap(bad_path); });
            check(error.stats().source_version.empty() && !error.stats().record_stream_read,
                  "mmap open failure parsed DWG data");
        }
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
