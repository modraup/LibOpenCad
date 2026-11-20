// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
static std::string hex(std::uint64_t v) {
    std::ostringstream o;
    o << std::uppercase << std::hex << v;
    return o.str();
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o, false);
    return o.str();
}
static DxfDocument read(const std::string &s, bool binary) {
    std::istringstream in(s);
    auto d = DxfDocument::read(in);
    std::ostringstream out;
    d.write(out, binary);
    std::istringstream encoded(out.str());
    return DxfDocument::read(encoded);
}
static std::string change(std::string s, const std::string &from, const std::string &to) {
    auto pos = s.find(from);
    check(pos != std::string::npos, "Fixture mutation not found");
    s.replace(pos, from.size(), to);
    return s;
}
static void save(const DxfDocument &d, const std::string &path) {
    std::ofstream o(path, std::ios::binary);
    d.write(o);
    o.close();
    check(bool(o), "Failed saving audit drawing");
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    const bool binary = std::string(argv[1]) == "binary";
    std::string prefix = argv[2];
    auto d = DxfDocument::create(binary);
    auto line = d.add_line({1, 2, 3}, {4, 5, 6});
    d.add_circle({2, 3, 4}, 2);
    d.add_ellipse({3, 4, 5}, {4, 0, 0}, .5);
    auto original = bytes(d);
    auto clean = d.audit();
    for (auto &i : clean.issues)
        std::cerr << i.message << '\n';
    check(clean.is_clean(), "New document is not clean");
    check(bytes(d) == original, "Read-only audit changed bytes");
    auto r = d.repair();
    check(r.actions.empty() && r.after.is_clean() && bytes(d) == original, "Clean repair was not a no-op");
    save(d, prefix + ".clean.dxf");
    auto invalid = d;
    invalid.for_each<DxfCircle>([](auto &c) { c.set_radius(-2); });
    invalid.for_each<DxfEllipse>([](auto &e) { e.set_minor_axis_ratio(-.5); });
    invalid.for_each<DxfLine>([](auto &l) { l.set_layer("不存在"); });
    auto before = bytes(invalid);
    auto report = invalid.audit();
    check(report.count(AuditIssueCode::InvalidGeometry) == 2, "Missing geometry diagnostics");
    check(report.count(AuditIssueCode::DanglingReference) == 1, "Missing layer diagnostic");
    auto repaired = invalid.repair();
    check(repaired.unresolved.size() == 3 && bytes(invalid) == before,
          "Ambiguous geometry/reference repaired silently");
    save(invalid, prefix + ".invalid.dxf");
    auto null = read(change(original, "  5\r\n" + hex(line) + "\r\n", "  5\r\n0\r\n"), binary);
    check(null.audit().count(AuditIssueCode::NullHandle) == 1, "Null entity not diagnosed");
    auto fixed = null.repair();
    check(fixed.after.is_clean(), "Null identity repair not clean");
    check(null.duplicate_handles().empty(), "Repair allocated duplicate");
    save(null, prefix + ".null.dxf");
    check(null.repair().actions.empty(), "Repair not idempotent");
    auto seed = original;
    auto p = seed.find("$HANDSEED\r\n  5\r\n");
    check(p != std::string::npos, "Seed not found");
    p += std::string("$HANDSEED\r\n  5\r\n").size();
    seed.replace(p, seed.find("\r\n", p) - p, "1");
    auto stale = read(seed, binary);
    check(stale.audit().count(AuditIssueCode::InvalidHandleSeed) == 1, "Stale seed not diagnosed");
    check(stale.repair().after.is_clean(), "Seed repair failed");
    save(stale, prefix + ".seed.dxf");
    auto duplicate = d;
    auto other = duplicate.add_line({7, 8, 9}, {10, 11, 12});
    auto dup = read(change(bytes(duplicate), "  5\r\n" + hex(other) + "\r\n", "  5\r\n" + hex(line) + "\r\n"),
                    binary);
    before = bytes(dup);
    check(dup.audit().count(AuditIssueCode::DuplicateHandle) == 1, "Duplicate not diagnosed");
    check(dup.repair().after.count(AuditIssueCode::DuplicateHandle) == 1 && bytes(dup) == before,
          "Duplicate identity was guessed");
    auto owner = read(change(original,
                             "  5\r\n" + hex(line) + "\r\n330\r\n" +
                                 d.get_symbol(SymbolTable::BlockRecord, "*Model_Space")->handle + "\r\n",
                             "  5\r\n" + hex(line) + "\r\n330\r\n0\r\n"),
                      binary);
    check(owner.audit().count(AuditIssueCode::OwnerMismatch) == 1, "Missing owner not diagnosed");
    check(owner.repair().after.is_clean(), "Owner repair failed");
    save(owner, prefix + ".owner.dxf");
    auto reactor =
        read(change(original, "  5\r\n" + hex(line) + "\r\n",
                    "  5\r\n" + hex(line) + "\r\n102\r\n{ACAD_REACTORS\r\n330\r\nDEADBEEF\r\n102\r\n}\r\n"),
             binary);
    check(reactor.audit().warning_count() == 1 && reactor.audit().error_count() == 0,
          "Reactor severity differs from Rust");
    before = bytes(reactor);
    check(reactor.repair().unresolved.size() == 1 && bytes(reactor) == before,
          "Dangling reactor removed silently");
    save(reactor, prefix + ".reactor.dxf");
    auto invalid_id = read(change(original, "  5\r\n" + hex(line) + "\r\n", "  5\r\nNOTHEX\r\n"), binary);
    check(invalid_id.audit().count(AuditIssueCode::InvalidHandle) == 1, "Invalid identity not diagnosed");
    before = bytes(invalid_id);
    check(invalid_id.repair().after.count(AuditIssueCode::InvalidHandle) == 1 && bytes(invalid_id) == before,
          "Invalid identity guessed");
    auto cyclic = d;
    auto parent = cyclic.add_extension_dictionary(hex(line));
    auto child = cyclic.add_dictionary(hex(parent), "Child");
    auto cycle_bytes = bytes(cyclic);
    // The dictionary writer does not require identity/owner group ordering.
    auto record = cycle_bytes.find("  5\r\n" + hex(parent) + "\r\n");
    check(record != std::string::npos, "Dictionary identity missing");
    auto begin = cycle_bytes.rfind("  0\r\n", record), end = cycle_bytes.find("  0\r\n", record);
    auto segment = cycle_bytes.substr(begin, end - begin);
    segment = change(segment, "330\r\n" + hex(line) + "\r\n", "330\r\n" + hex(child) + "\r\n");
    cycle_bytes.replace(begin, end - begin, segment);
    cyclic = read(cycle_bytes, binary);
    before = bytes(cyclic);
    check(cyclic.audit().count(AuditIssueCode::OwnerCycle) == 2, "Owner cycle not diagnosed");
    check(cyclic.repair().after.count(AuditIssueCode::OwnerCycle) == 2 && bytes(cyclic) == before,
          "Owner cycle repaired by guessing");
    save(cyclic, prefix + ".cycle.dxf");
    auto exhausted =
        read(change(original, "  5\r\n" + hex(line) + "\r\n", "  5\r\nFFFFFFFFFFFFFFFF\r\n"), binary);
    before = bytes(exhausted);
    bool refused = false;
    try {
        exhausted.repair();
    } catch (const std::overflow_error &) {
        refused = true;
    }
    check(refused && bytes(exhausted) == before, "Exhausted repair not atomic");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
