// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <libopencad/dxf_read.hpp>
#include <limits>
#include <map>

namespace {
void implicit_version_matrix(const std::string &prefix, bool binary) {
    for (bool header : {false,true}) for (const std::string encoding : {"auto","ANSI_936","UTF-8","invalid"}) {
        const auto base = prefix + (binary ? "-binary-" : "-ascii-") +
                          (header ? "no-version-" : "no-header-") + encoding;
        const std::string payload = encoding == "ANSI_936" ? "\xD6\xD0\\U+6587" :
                                    encoding == "invalid" ? "a\xe1\x80" : "中\\U+6587";
        const std::string expected = 
                                     encoding == "invalid" ? (binary ? "a�" : "aá\xc2\x80") : "中文";
        Records data;
        if (header) data = {{{0,"SECTION"},{2,"HEADER"},{9,"$DWGCODEPAGE"},{3,"ANSI_1252"}},{{0,"ENDSEC"}}};
        data.insert(data.end(), {{{0,"SECTION"},{2,"ENTITIES"}},
                                 {{0,"TEXT"},{5,"A1"},{8,"0"},{1,payload},{40,"2"}},
                                 {{0,"ENDSEC"}},{{0,"EOF"}}});
        if (binary) for(auto &r:data) for(auto &[code,value]:r) value=detail::binary_value_from_text(code,value);
        const auto bytes = encode(data,binary);
        std::ofstream(base+"-source.dxf",std::ios::binary) << bytes;
        for (bool failsafe : {false,true}) {
            DxfReadStatistics stats;
            DxfReadOptions options;
            options.failsafe = failsafe;
            if (encoding == "ANSI_936" || encoding == "UTF-8") options.default_encoding = encoding;
            std::istringstream input(bytes);
            auto result = DxfDocument::read(input, options, stats);
            check(stats.diagnostics.size()==1 && stats.diagnostics.front().code==(header?"version-absent":"header-absent"),
                  "implicit version diagnosis");
            check(result.version()==DxfVersion::AC1032 && stats.retained_records==1 && result.get_record("A1"),
                  "implicit version and handles");
            result.for_each<DxfText>([&](const auto &e){check(e.text()==expected,"implicit encoding");});
            write(result,base+"-recovered.dxf");
        }
    }
}
void declared_page_matrix(const std::string &prefix,bool binary){
    for(const auto &decl:std::vector<std::string>{"","UNKNOWN","ANSI_1252"}){
        Records data{{{0,"SECTION"},{2,"HEADER"},{9,"$ACADVER"},{1,"AC1015"},{9,"$DWGCODEPAGE"},{3,decl}},
                     {{0,"ENDSEC"}},{{0,"SECTION"},{2,"ENTITIES"}},{{0,"TEXT"},{5,"A1"},{1,"\x80\\U+4E2D"}},{{0,"ENDSEC"}},{{0,"EOF"}}};
        if(binary) for(auto &r:data) for(auto &[code,value]:r) value=detail::binary_value_from_text(code,value);
        auto raw=encode(data,binary);
        auto base=prefix+(binary?"-binary-page-":"-ascii-page-")+(decl.empty()?"empty":decl);
        std::ofstream(base+"-source.dxf",std::ios::binary)<<raw;
        for(bool failsafe:{false,true}){
            DxfReadOptions options;
            options.failsafe=failsafe;
            options.default_encoding="ANSI_936";
            DxfReadStatistics stats;
            std::istringstream input(raw);
            auto doc=DxfDocument::read(input,options,stats);
            check(stats.retained_records==1 && stats.diagnostics.size()==(decl=="ANSI_1252"?0:1),"declared-page precedence");
            doc.for_each<DxfText>([](auto &e){check(e.text()=="€中","declared page decoding");});
            write(doc,base+"-recovered.dxf");
        }
    }
}
void recovery_matrix(const std::string &prefix, bool binary) {
    // Encode fragments independently. The prefix length gives the expected
    // position in the original file, including the binary signature or ASCII CRLFs.
    auto pairs = [binary](Record fields) {
        std::ostringstream stream;
        for (const auto &[code, value] : fields) {
            if (binary)
                detail::write_binary_pair(stream, false, code, detail::binary_value_from_text(code, value));
            else
                stream << code << "\r\n" << value << "\r\n";
        }
        return stream.str();
    };
    const std::string signature = binary ? std::string(detail::binary_signature) : "";
    const auto header = signature + pairs({{0, "SECTION"}, {2, "HEADER"}, {9, "$HANDSEED"}, {5, "100"}});
    const auto version = pairs({{9, "$ACADVER"}, {1, "AC1032"}});
    const auto units = pairs({{9, "$INSUNITS"}, {70, "4"}});
    const auto entities = pairs({{0, "SECTION"}, {2, "ENTITIES"}});
    const auto endsec = pairs({{0, "ENDSEC"}});
    const auto eof = pairs({{0, "EOF"}});
    const auto body = header + version + units + endsec + entities;
    const auto line =
        pairs({{0, "LINE"}, {5, "A1"}, {10, "1"}, {20, "2"}, {30, "3"}, {11, "4"}, {21, "5"}, {31, "6"}});
    const auto circle = pairs({{0, "CIRCLE"}, {5, "B2"}, {10, "2"}, {20, "3"}, {30, "4"}, {40, "5"}});
    const auto poly = pairs({{0, "POLYLINE"}, {5, "C1"}, {66, "1"}, {70, "8"}});
    const auto vertex = pairs({{0, "VERTEX"}, {5, "C2"}, {70, "32"}, {10, "1"}, {20, "2"}, {30, "3"}});
    const auto insert = pairs({{0, "INSERT"}, {5, "D1"}, {2, "EXAMPLE"}, {66, "1"}});
    const auto attrib = pairs({{0, "ATTRIB"}, {5, "D2"}, {1, "text"}, {2, "KEY"}});
    const auto end = endsec + eof;

    auto verify = [&](const char *name, const std::string &before, const std::string &after, const char *code,
                      const char *section, const char *handle, std::set<std::string> survivors,
                      size_t skipped, bool completed) {
        const auto bytes = before + after;
        const auto base = prefix + (binary ? "-binary-" : "-ascii-") + name;
        std::ofstream(base + "-source.dxf", std::ios::binary).write(bytes.data(), bytes.size());
        const auto line_number = 1 + std::count(before.begin(), before.end(), '\n');
        auto location = [&](const DxfReadDiagnostic &d) {
            return d.code == code && d.section == section && d.record_handle == handle &&
                   d.source_offset == before.size() &&
                   (binary ? !d.source_line : d.source_line == static_cast<size_t>(line_number));
        };
        DxfReadStatistics stats;
        std::istringstream strict(bytes);
        try {
            DxfDocument::read(strict, DxfReadOptions{}, stats);
            throw std::runtime_error(base + ": strict read accepted the damaged file");
        } catch (const DxfReadError &e) {
            if (!location(e.diagnostic()))
                throw std::runtime_error(base + ": unexpected strict diagnostic " + e.diagnostic().code +
                                         " at " + std::to_string(e.diagnostic().source_offset));
        }
        std::istringstream input(bytes);
        auto doc = DxfDocument::read(input, DxfReadOptions{true}, stats);
        check(!stats.diagnostics.empty() && location(stats.diagnostics.front()),
              "recovery diagnostic location");
        check(stats.binary == binary && stats.stream_completed == completed &&
                  stats.skipped_records == skipped && stats.retained_records == survivors.size() &&
                  stats.source_records == skipped + survivors.size(),
              "recovery record accounting");
        for (const char *h : {"A1", "B2", "C1", "C2", "D1", "D2", "EE"})
            check((doc.get_record(h) != nullptr) == (survivors.count(h) != 0), "recovery handle inventory");
        check(doc.insertion_units() == 4, "retain intact HEADER variables");
        if (std::string_view(name) == "unknown-version")
            check(doc.version() == DxfVersion::Unknown, "preserve unrecognized version");
        else
            check(doc.version() == DxfVersion::AC1032, "retain version");
        write(doc, base + "-recovered.dxf");
        std::istringstream saved(save(doc));
        DxfDocument::read(saved, DxfReadOptions{std::string_view(name) == "unknown-version"}, stats);
        check(stats.stream_completed && stats.skipped_records == 0, "recovered file has complete framing");
    };
    const std::set<std::string> both{"A1", "B2"};
    const auto tail = endsec + entities + line + circle + end;
    verify("header-type", header + version + pairs({{9, "$INSUNITS"}}), pairs({{1, "bad"}}) + units + tail,
           "invalid-header-value", "HEADER", "", both, 0, true);
    verify("header-empty", header + version, pairs({{9, "$INSUNITS"}}) + units + tail, "header-value-missing",
           "HEADER", "", both, 0, true);
    verify("unknown-version", header + pairs({{9, "$ACADVER"}}), pairs({{1, "AC9999"}}) + units + tail,
           "unknown-header-version", "HEADER", "", both, 0, true);
    std::ostringstream invalid_real;
    if (binary)
        detail::write_binary_pair(invalid_real, false, 10,
                                  detail::binary_double(std::numeric_limits<double>::quiet_NaN()));
    else
        invalid_real << "10\r\ninvalid\r\n";
    verify("header-number", header + version + pairs({{9, "$EXTMIN"}}),
           invalid_real.str() + pairs({{20, "2"}, {30, "3"}}) + units + tail, "invalid-group-value", "HEADER",
           "", both, 0, true);
    verify("unnamed-section", header + version + units + endsec,
           pairs({{0, "SECTION"}, {9, "$COMMENT"}, {1, "missing section name"}}) + endsec + entities + line +
               circle + end,
           "section-name-missing", "", "", both, 0, true);
    verify("section-next", header + version + units, entities + line + circle + end, "unclosed-section",
           "HEADER", "", both, 0, true);
    verify("section-eof", body + line + circle, eof, "unclosed-section", "ENTITIES", "", both, 0, true);
    verify("physical-end", body + line, "", "stream-ended-early", "ENTITIES", "", {"A1"}, 0, false);
    const auto circle_start = pairs({{0, "CIRCLE"}, {5, "B2"}});
    auto partial_radius = pairs({{40, "5"}});
    partial_radius.resize(binary ? 5 : partial_radius.find('\n') + 1);
    verify("partial-value", body + line + circle_start, partial_radius, "stream-read-failed", "ENTITIES",
           "B2", {"A1"}, 1, false);
    // A fully decoded group 0 has already ended the preceding record even
    // when its new type string is missing.
    verify("partial-marker", body + line, binary ? std::string("\0\0EN", 4) : "0\r\n", "stream-read-failed",
           "ENTITIES", "", {"A1"}, 0, false);
    verify("poly-no-end", body + line, poly + vertex + circle + end, "sequence-end-missing", "ENTITIES", "C1",
           both, 2, true);
    verify("insert-no-end", body + line, insert + attrib + circle + end, "sequence-end-missing", "ENTITIES",
           "D1", both, 2, true);
    verify("wrong-child", body + line + poly + vertex,
           attrib + pairs({{0, "SEQEND"}, {5, "EE"}}) + circle + end, "sequence-child-mismatch", "ENTITIES",
           "D2", both, 4, true);
    verify("poly-cut", body + line, poly + vertex, "sequence-end-missing", "ENTITIES", "C1", {"A1"}, 2,
           false);
}

void legacy_text_recovery(const std::string &prefix, bool binary) {
    const std::string left = "\xc7\xb0\xc1\xda\xd6\xd0\xce\xc4";  // 前邻中文
    const std::string right = "\xba\xf3\xc1\xda\xd6\xd0\xce\xc4"; // 后邻中文
    const std::string first = "\xbc\xd7", last = "\xd2\xd2";      // 甲, 乙
    struct Sample {
        const char *name;
        std::string damage, suffix, expected;
        const char *diagnostic;
    };
    const Sample samples[] = {
        {"gbk-lead", "\x81", " " + last, "甲� 乙", "invalid-code-page-bytes"},
        {"gbk-tail", "\x81", "", "甲�", "invalid-code-page-bytes"},
        {"gbk-pair", "\x81\xff", last, "甲�乙", "invalid-code-page-bytes"},
        {"gbk-ff", "\xff", last, "甲�乙", "invalid-code-page-bytes"},
        {"cif-short", "\\U+12", "", "甲\\U+12", "invalid-unicode-escape"},
        {"cif-hex", "\\U+12G4", last, "甲\\U+12G4乙", "invalid-unicode-escape"},
        {"cif-high", "\\U+D83D", "\\U+4e2d" + last, "甲中乙", "invalid-unicode-escape"},
        {"cif-low", "\\U+DE00", last, "甲乙", "invalid-unicode-escape"},
        {"valid", "\x81\x5cU+12G4\\U+D83D\\U+DE00\x80\x94\x39\xfc\x36", last, "甲乗U+12G4😀€😀乙", nullptr},
    };
    auto text = [](const char *handle, const std::string &value) -> Record {
        return {{0, "TEXT"}, {5, handle}, {8, "0"}, {10, "1"}, {20, "2"}, {30, "0"}, {40, "2"}, {1, value}};
    };
    for (const auto &sample : samples) {
        const auto middle = first + sample.damage + sample.suffix;
        Records drawing = {{{0, "SECTION"},
                            {2, "HEADER"},
                            {9, "$ACADVER"},
                            {1, "AC1015"},
                            {9, "$DWGCODEPAGE"},
                            {3, "ANSI_936"}},
                           {{0, "ENDSEC"}},
                           {{0, "SECTION"}, {2, "ENTITIES"}},
                           text("A1", left),
                           text("B2", middle),
                           text("C3", right),
                           {{0, "ENDSEC"}},
                           {{0, "EOF"}}};
        if (binary)
            for (auto &record : drawing)
                for (auto &[code, value] : record)
                    value = detail::binary_value_from_text(code, value);
        const auto bytes = encode(drawing, binary);
        const auto value = bytes.find(middle);
        check(value != bytes.npos && bytes.find(middle, value + 1) == bytes.npos,
              "unique middle text position");
        const auto expected_offset = value + first.size();
        const auto code_line =
            static_cast<std::size_t>(std::count(bytes.begin(), bytes.begin() + value, '\n'));
        const auto base = prefix + "-legacy-" + (binary ? "binary-" : "ascii-") + sample.name;
        std::ofstream(base + "-source.dxf", std::ios::binary).write(bytes.data(), bytes.size());
        auto check_position = [&](const DxfReadDiagnostic &d) {
            check(d.code == sample.diagnostic && d.section == "ENTITIES" && d.record_type == "TEXT" &&
                      d.record_handle == "B2" && d.source_offset == expected_offset &&
                      (binary ? !d.source_line : d.source_line == code_line),
                  "legacy text diagnostic position");
        };
        DxfReadStatistics stats;
        std::istringstream strict(bytes);
        if (sample.diagnostic) {
            try {
                DxfDocument::read(strict, DxfReadOptions{}, stats);
                throw std::runtime_error(base + ": strict reader accepted damaged text");
            } catch (const DxfReadError &e) {
                check_position(e.diagnostic());
            }
        } else {
            DxfDocument::read(strict, DxfReadOptions{}, stats);
            check(stats.diagnostics.empty(), "valid legacy transport");
        }
        std::istringstream input(bytes);
        const auto doc = DxfDocument::read(input, DxfReadOptions{true}, stats);
        check(stats.stream_completed && stats.retained_records == 3 && stats.skipped_records == 0 &&
                  stats.diagnostics.size() == (sample.diagnostic ? 1u : 0u),
              "legacy text recovery counters");
        if (sample.diagnostic)
            check_position(stats.diagnostics.front());
        auto verify_text = [&](const DxfDocument &d) {
            std::map<std::string, std::string> labels;
            d.for_each<DxfText>([&](const auto &t) { labels.emplace(t.handle(), t.text()); });
            check(labels == std::map<std::string, std::string>{{"A1", "前邻中文"},
                                                               {"B2", sample.expected},
                                                               {"C3", "后邻中文"}},
                  "retained text by handle");
        };
        verify_text(doc);
        write(doc, base + "-recovered.dxf");
        const auto output = records(save(doc), binary);
        check(output[3] == drawing[3] && output[5] == drawing[5], "unaltered neighboring text records");
        std::istringstream saved(save(doc));
        verify_text(DxfDocument::read(saved, DxfReadOptions{}, stats));
        check(stats.diagnostics.empty(), "strict reread of recovered text");

        if (std::string_view(sample.name) == "cif-hex") {
            // A missing declaration must select the configured page for both
            // recovery and subsequent saves. Modern UTF-8 treats CIF as text.
            auto no_page = drawing;
            no_page[0].resize(4);
            std::istringstream fallback_input(encode(no_page, binary));
            DxfReadOptions fallback{true};
            fallback.default_encoding = "ANSI_936";
            auto recovered = DxfDocument::read(fallback_input, fallback, stats);
            verify_text(recovered);
            check(stats.diagnostics.size() == 2 && stats.diagnostics[0].code == "fallback-encoding-applied" &&
                      stats.diagnostics[1].code == "invalid-unicode-escape",
                  "fallback page recovery");
            std::istringstream reopened(save(recovered));
            verify_text(DxfDocument::read(reopened, DxfReadOptions{}, stats));
            check(stats.diagnostics.empty(), "persist fallback page");

            auto modern = drawing;
            modern[0][3].second = "AC1021";
            modern[3].back().second = "前邻中文";
            modern[4].back().second = "甲\\U+12G4乙";
            modern[5].back().second = "后邻中文";
            std::istringstream utf8(encode(modern, binary));
            verify_text(DxfDocument::read(utf8, DxfReadOptions{}, stats));
            check(stats.diagnostics.empty(), "UTF-8 literal escape");
        }
    }
}
} // namespace
int main(int argc, char **argv) try {
    check(argc == 2, "output prefix");
    const std::string prefix = argv[1];
    declared_page_matrix(prefix,false);
    declared_page_matrix(prefix,true);
    implicit_version_matrix(prefix,false);
    implicit_version_matrix(prefix,true);
    recovery_matrix(prefix, false);
    recovery_matrix(prefix, true);
    legacy_text_recovery(prefix, false);
    legacy_text_recovery(prefix, true);
    const std::string head =
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1032\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    const std::string line = "0\nLINE\n5\nA\n8\n0\n10\n1\n20\n2\n30\n3\n11\n4\n21\n5\n31\n6\n";
    const std::string circle = "0\nCIRCLE\n5\nB\n8\n0\n10\n2\n20\n3\n30\n4\n40\n5\n";
    const std::string end = "0\nENDSEC\n0\nEOF\n";
    DxfReadStatistics stats;
    auto parse = [&](const std::string &bytes, bool failsafe) {
        std::istringstream in(bytes);
        return DxfDocument::read(in, DxfReadOptions{failsafe}, stats);
    };
    auto healthy = parse(head + line + circle + end, false);
    check(stats.source_records == 2 && stats.retained_records == 2 && stats.diagnostics.empty() &&
              stats.stream_completed,
          "healthy counters");
    auto malformed = line;
    malformed.replace(malformed.find("10\n1\n"), 5, "10\noops\n");
    auto input = head + malformed + circle + end;
    bool rejected = false;
    try {
        parse(input, false);
    } catch (const DxfReadError &e) {
        rejected = e.diagnostic().code == "invalid-group-value" && e.diagnostic().record_type == "LINE" &&
                   e.diagnostic().record_handle == "A";
        check(e.diagnostic().source_offset == head.size() + malformed.find("10\noops"),
              "exact byte position");
        check(e.diagnostic().source_line.has_value(), "ASCII line position");
    }
    check(rejected, "strict group error");
    auto recovered = parse(input, true);
    check(!recovered.get_record("A") && recovered.get_record("B"), "skip invalid entity, retain neighbor");
    check(stats.source_records == 2 && stats.skipped_records == 1 && stats.retained_records == 1 &&
              stats.stream_completed,
          "recovery counters");
    write(recovered, prefix + "-recovered.dxf");
    {
        std::ofstream file(prefix + "-bad.dxf");
        file << input;
    }
    auto truncated = head + line;
    rejected = false;
    try {
        parse(truncated, false);
    } catch (const DxfReadError &) {
        rejected = true;
    }
    check(rejected, "strict missing EOF");
    auto partial = parse(truncated, true);
    check(partial.get_record("A") && !stats.stream_completed, "retain completed record on missing EOF");
    write(partial, prefix + "-partial.dxf");
    {
        std::ofstream file(prefix + "-truncated.dxf");
        file << truncated;
    }
    auto opaque = parse(head + "0\nPRIVATE_THING\n5\nC\n1\nopaque\n" + end, false);
    check(opaque.get_record("C") && stats.opaque_records == 1 && stats.skipped_records == 0,
          "opaque preserved not skipped");
    check(stats.diagnostics[0].code == "opaque-record-preserved", "unsupported decoder diagnostic");
    // Broken numeric group in a compound record drops its entire sequence.
    auto compound = head +
                    "0\nPOLYLINE\n5\n10\n66\n1\n70\n8\n0\nVERTEX\n5\n11\n10\nbad\n20\n2\n0\nSEQEND\n5\n12\n" +
                    circle + end;
    auto fixed = parse(compound, true);
    check(!fixed.get_record("10") && !fixed.get_record("11") && !fixed.get_record("12") &&
              fixed.get_record("B"),
          "compound atomic skip");
    check(stats.skipped_records == 3 && stats.retained_records == 1, "compound counters");
    // True binary truncation: a partial circle value must not be reinterpreted as a new record.
    std::ostringstream out;
    healthy.write(out, true);
    auto binary = out.str();
    const std::string marker("\x28\x00", 2);
    auto cut = binary.find(marker, binary.find("CIRCLE"));
    check(cut != std::string::npos, "binary circle radius marker");
    auto stopped = parse(binary.substr(0, cut + 5), true);
    check(stopped.get_record("A") && !stopped.get_record("B") && !stats.stream_completed,
          "binary framing stops at last safe record");
    check(stats.binary && !stats.diagnostics[0].source_line && stats.diagnostics[0].source_offset == cut,
          "binary byte offset");
    rejected = false;
    try {
        std::istringstream in(head);
        DxfDocument::read(in, DxfReadOptions{true, 8}, stats);
    } catch (const DxfReadError &e) {
        rejected = e.diagnostic().code == "input-limit";
    }
    check(rejected, "input budget even in failsafe");
    // Malformed group-code pairs have known ASCII line framing; skip the whole record.
    auto code_bad = line;
    code_bad.replace(code_bad.find("10\n"), 2, "xxx");
    auto code_fixed = parse(head + code_bad + circle + end, true);
    check(!code_fixed.get_record("A") && code_fixed.get_record("B"), "ASCII pair resynchronization");
    auto bom = parse(std::string("\xEF\xBB\xBF") + head + line + end, false);
    check(bom.get_record("A") && stats.diagnostics.empty(), "UTF-8 BOM framing");
    auto missing_sequence =
        head + "0\nPOLYLINE\n5\n10\n70\n8\n0\nVERTEX\n5\n11\n10\n1\n20\n2\n" + circle + end;
    rejected = false;
    try {
        parse(missing_sequence, false);
    } catch (const DxfReadError &e) {
        rejected = e.diagnostic().code == "sequence-end-missing";
    }
    check(rejected, "missing SEQEND is a structured diagnostic");
    auto sequence_recovery = parse(missing_sequence, true);
    check(!sequence_recovery.get_record("10") && sequence_recovery.get_record("B"),
          "incomplete sequence not partially retained");
    auto mismatch =
        parse(head + "0\nPOLYLINE\n5\n10\n70\n8\n0\nATTRIB\n5\n11\n1\nwrong child\n0\nSEQEND\n5\n12\n" +
                  circle + end,
              true);
    check(!mismatch.get_record("10") && mismatch.get_record("B") && stats.skipped_records == 3,
          "wrong child cannot escape compound removal");
    auto empty_type = parse(head + "0\n\n5\nD\n" + circle + end, true);
    check(!empty_type.get_record("D") && empty_type.get_record("B"), "empty record type rejected");
    std::string many = head;
    for (int i = 0; i < 1100; ++i)
        many += "0\nLINE\n10\nbad\n";
    parse(many + end, true);
    check(stats.skipped_records == 1100 && stats.diagnostics.size() == 1000 &&
              stats.diagnostics.back().code == "diagnostic-limit-reached",
          "bounded diagnostics preserve counters");
    auto legacy_head=head;legacy_head.replace(legacy_head.find("AC1032"),6,"AC1015");
    const std::string gbk="\xD6\xD0\xCE\xC4";
    const auto legacy_bytes=legacy_head+"0\nTEXT\n5\nC\n8\n0\n10\n0\n20\n0\n30\n0\n40\n2\n1\n"+gbk+"\n"+end;
    DxfReadOptions encoding;encoding.default_encoding="ANSI_936";
    for(bool binary:{false,true}) {
        auto bytes=encode(records(legacy_bytes,false),false);
        if(binary){auto pairs=records(legacy_bytes,false);for(auto& r:pairs)for(auto& p:r)p.second=detail::binary_value_from_text(p.first,p.second);bytes=encode(pairs,true);}
        std::istringstream stream(bytes);auto decoded=DxfDocument::read(stream,encoding,stats);
        decoded.for_each<DxfText>([](const auto& t){check(t.text()=="中文","configured GBK fallback");});
        check(stats.diagnostics.size()==1 && stats.diagnostics[0].code=="fallback-encoding-applied","fallback reported");
        write(decoded,prefix+(binary?"-fallback-binary.dxf":"-fallback-ascii.dxf"));
    }
    std::istringstream modern(head+line+end);DxfDocument::read(modern,encoding,stats);check(stats.diagnostics.empty(),"modern UTF-8 ignores fallback");
    auto declared=legacy_bytes;declared.insert(declared.find("0\nENDSEC"),"9\n$DWGCODEPAGE\n3\nUTF-8\n");declared.replace(declared.find(gbk),gbk.size(),"中文");
    std::istringstream explicit_page(declared);auto explicit_doc=DxfDocument::read(explicit_page,encoding,stats);
    explicit_doc.for_each<DxfText>([](const auto& t){check(t.text()=="中文","declared encoding takes precedence");});check(stats.diagnostics.empty(),"no fallback diagnostic for explicit page");
    encoding.default_encoding="invalid-code-page";bool bad_encoding=false;
    try{std::istringstream stream(legacy_bytes);DxfDocument::read(stream,encoding,stats);}catch(const DxfReadError& e){bad_encoding=e.diagnostic().code=="unknown-default-encoding";}
    check(bad_encoding,"unknown fallback rejected");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
