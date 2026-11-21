// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
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

std::string input_bytes(const char *path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "could not open DWG fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}

const DwgReadDiagnostic *authentication_error(const DwgReadStatistics &stats) {
    auto found = std::find_if(stats.diagnostics.begin(), stats.diagnostics.end(), [](const auto &entry) {
        return entry.code == "decryption-failed" && entry.section == "AcDb:Security" &&
               entry.stage == DwgReadStage::FileHeader;
    });
    return found == stats.diagnostics.end() ? nullptr : &*found;
}

void verify_rejection(const std::string &bytes, const std::optional<std::string> &password,
                      std::string_view expected_message) {
    std::istringstream strict(bytes);
    try {
        (void)DwgDocument::read_with_stats(strict, {false, password});
        throw std::logic_error("strict reader accepted an unverified password");
    } catch (const DwgReadError &error) {
        const auto *diagnostic = authentication_error(error.stats());
        check(diagnostic && diagnostic->message == expected_message && error.what() == expected_message &&
                  !error.stats().record_stream_read && error.stats().source_records == 0,
              "strict password error diagnostic");
    }

    std::istringstream tolerant(bytes);
    auto measured = DwgDocument::read_with_stats(tolerant, {true, password});
    const auto *diagnostic = authentication_error(measured.stats);
    check(diagnostic && diagnostic->message == expected_message && measured.stats.recovered() &&
              !measured.writable && !measured.stats.has_usable_drawing_data() &&
              measured.document.objects().records.empty(),
          "failsafe must report the authentication failure and withhold protected records");
}
} // namespace

int main(int argc, char **argv) try {
    check(argc == 4, "expected DWG version and fixture paths");
    const std::string version = argv[1];
    const bool unicode = version == "AC1021";
    const std::string password = unicode ? u8"Straßeı😀CaseMixCaseMixCaseMixCaseMixCaseMixCaseMix" : "budweiser";
    const auto bytes = input_bytes(argv[2]);
    std::istringstream input(bytes);
    const auto verified = DwgDocument::read_with_stats(input, {false, password});
    const auto &records = verified.document.objects();
    check(verified.writable && verified.stats.source_version == version &&
              verified.stats.has_usable_drawing_data() && verified.stats.stream_completed &&
              !verified.stats.recovered() && verified.stats.skipped_source_records == 0,
          "valid password must decode the complete drawing");

    if (version == "AC1032") {
        auto plain = input_bytes(argv[3]);
        std::istringstream baseline_input(plain);
        auto baseline = DwgDocument::read(baseline_input);
        check(records.modelers.size() == 9 && records.modelers.size() == baseline.objects().modelers.size(),
              "protected modeler record count");
        bool saw_sab = false;
        for (const auto &[handle, modeler] : records.modelers) {
            const auto &sab = modeler.data.geometry.sab;
            check(sab == baseline.objects().modelers.at(handle).data.geometry.sab, "protected AcDs SAB payload");
            saw_sab = saw_sab || !sab.empty();
        }
        check(saw_sab, "fixture must exercise encrypted AcDs");

        std::istringstream unprotected(plain);
        auto ordinary = DwgDocument::read_with_stats(unprotected, {false, "irrelevant-password"});
        check(ordinary.writable && ordinary.stats.has_usable_drawing_data(), "password on an unprotected DWG");
    } else {
        check(records.lines.size() == 3 &&
                  records.lines.begin()->second.start == Point3{1, 2, 3} &&
                  records.lines.rbegin()->second.end == Point3{12, 10, 11}, "protected LINE geometry");
    }

    std::ostringstream copy;
    verified.document.write(copy);
    check(copy.str() == bytes, "authenticated read preserves the original encrypted file bytes");

    const auto upper = unicode ? u8"STRASSEI😀CASEMIXCASEMIXCASEMIXCASEMIXCASEMIXCASEMIX" : "BUDWEISER";
    std::istringstream uppercase_input(bytes);
    auto upper_document = DwgDocument::read(uppercase_input, {false, upper});
    check(upper_document.objects().records.size() == records.records.size(), "case-insensitive password derivation");
    try {
        upper_document.set_preview(std::nullopt);
        throw std::logic_error("native rewriting accepted a protected DWG");
    } catch (const std::invalid_argument &) {
    }

    std::istringstream recovered_input(bytes);
    auto recovered = DwgDocument::recover(recovered_input, {false, password});
    check(recovered.stats.has_usable_drawing_data() && recovered.stats.skipped_source_records == 0 &&
              recovered.document.objects().records.size() == records.records.size(), "authenticated recovery");

    constexpr std::string_view missing = "Drawing is password protected; please provide a password";
    constexpr std::string_view wrong = "Invalid password for encrypted DWG drawing";
    verify_rejection(bytes, std::nullopt, missing);
    verify_rejection(bytes, "incorrect", wrong);
    verify_rejection(bytes, std::string{}, wrong);
    std::istringstream default_input(bytes);
    try {
        (void)DwgDocument::read(default_input);
        throw std::logic_error("default reader accepted a protected DWG without credentials");
    } catch (const std::invalid_argument &error) {
        check(error.what() == missing, "default reader identifies the missing password");
    }
    std::istringstream bad_recovery(bytes);
    try {
        (void)DwgDocument::recover(bad_recovery, {true, "incorrect"});
        throw std::logic_error("recovery accepted a protected record stream without verified credentials");
    } catch (const DwgRecoveryError &error) {
        check(authentication_error(error.stats()), "recovery retains authentication failure diagnostics");
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
