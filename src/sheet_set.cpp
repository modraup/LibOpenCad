// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/sheet_set.rs cipher, XML and SheetSetDatabase.
#include <libopencad/sheet_set.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace libopencad {
namespace {

constexpr std::uint8_t encode_byte(std::uint8_t p) {
    const auto q = static_cast<std::uint8_t>(p + 1);
    const auto low = static_cast<std::uint8_t>((q & 0x0F) ^ 0x0D);
    const auto h = static_cast<std::uint8_t>(q >> 4);
    const auto hc = static_cast<std::uint8_t>((h & 1) ? ((h + 10) & 0x0F) : ((h + 8) & 0x0F));
    return static_cast<std::uint8_t>((hc << 4) | low);
}

constexpr std::array<std::uint8_t, 256> make_encode() {
    std::array<std::uint8_t, 256> t{};
    for (int i = 0; i < 256; ++i)
        t[static_cast<std::size_t>(i)] = encode_byte(static_cast<std::uint8_t>(i));
    return t;
}

constexpr auto kEncode = make_encode();

constexpr std::array<std::uint8_t, 256> make_decode() {
    std::array<std::uint8_t, 256> t{};
    for (int i = 0; i < 256; ++i)
        t[kEncode[static_cast<std::size_t>(i)]] = static_cast<std::uint8_t>(i);
    return t;
}

constexpr auto kDecode = make_decode();

bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

void escape_into(std::string_view s, bool attr, std::string &out) {
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            if (attr) {
                out += "&quot;";
                break;
            }
            out.push_back(c);
            break;
        default:
            out.push_back(c);
            break;
        }
    }
}

std::string unescape(std::string_view s) {
    if (s.find('&') == std::string_view::npos)
        return std::string(s);
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        auto p = s.find('&', i);
        if (p == std::string_view::npos) {
            out.append(s.substr(i));
            break;
        }
        out.append(s.substr(i, p - i));
        auto end = s.find(';', p);
        if (end == std::string_view::npos) {
            out.append(s.substr(p));
            break;
        }
        auto entity = s.substr(p + 1, end - p - 1);
        auto push = [&](char32_t cp) {
            if (cp <= 0x7F)
                out.push_back(static_cast<char>(cp));
            else if (cp <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        };
        if (entity == "amp")
            out.push_back('&');
        else if (entity == "lt")
            out.push_back('<');
        else if (entity == "gt")
            out.push_back('>');
        else if (entity == "quot")
            out.push_back('"');
        else if (entity == "apos")
            out.push_back('\'');
        else if (entity.size() >= 3 && (entity[0] == '#' && (entity[1] == 'x' || entity[1] == 'X'))) {
            try {
                push(static_cast<char32_t>(std::stoul(std::string(entity.substr(2)), nullptr, 16)));
            } catch (...) {
                out.append(s.substr(p, end - p + 1));
            }
        } else if (!entity.empty() && entity[0] == '#') {
            try {
                push(static_cast<char32_t>(std::stoul(std::string(entity.substr(1)))));
            } catch (...) {
                out.append(s.substr(p, end - p + 1));
            }
        } else {
            out.append(s.substr(p, end - p + 1));
        }
        i = end + 1;
    }
    return out;
}

struct Parser {
    std::string_view s;
    std::size_t i = 0;

    void skip_ws() {
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (!std::isspace(c))
                break;
            ++i;
        }
    }

    void skip_misc() {
        for (;;) {
            skip_ws();
            auto rest = s.substr(i);
            std::string_view close;
            if (rest.size() >= 2 && rest.substr(0, 2) == "<?")
                close = "?>";
            else if (rest.size() >= 4 && rest.substr(0, 4) == "<!--")
                close = "-->";
            else if (!rest.empty() && rest.substr(0, 2) == "<!")
                close = ">";
            else
                return;
            auto p = rest.find(close);
            if (p == std::string_view::npos) {
                i = s.size();
                return;
            }
            i += p + close.size();
        }
    }

    SheetSetElement element() {
        skip_misc();
        if (i >= s.size() || s[i] != '<')
            throw std::runtime_error("element expected at byte " + std::to_string(i));
        ++i;
        auto name_end = i;
        while (name_end < s.size()) {
            char c = s[name_end];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/')
                break;
            ++name_end;
        }
        if (name_end == s.size())
            throw std::runtime_error("unterminated tag");
        SheetSetElement el = SheetSetElement::make(std::string(s.substr(i, name_end - i)));
        i = name_end;
        for (;;) {
            skip_ws();
            auto rest = s.substr(i);
            if (rest.size() >= 2 && rest.substr(0, 2) == "/>") {
                i += 2;
                return el;
            }
            if (!rest.empty() && rest[0] == '>') {
                ++i;
                break;
            }
            auto eq = rest.find('=');
            if (eq == std::string_view::npos)
                throw std::runtime_error("attribute without value");
            auto key = std::string(trim_view(rest.substr(0, eq)));
            auto after = ltrim_view(rest.substr(eq + 1));
            if (after.empty() || (after[0] != '"' && after[0] != '\''))
                throw std::runtime_error("quoted attribute value expected");
            char quote = after[0];
            auto value_end = after.find(quote, 1);
            if (value_end == std::string_view::npos)
                throw std::runtime_error("unterminated attribute");
            el.attrs.emplace_back(std::move(key), unescape(after.substr(1, value_end - 1)));
            i = s.size() - after.size() + value_end + 1;
        }
        for (;;) {
            auto rest = s.substr(i);
            auto lt = rest.find('<');
            if (lt == std::string_view::npos)
                throw std::runtime_error("unterminated element");
            el.text += unescape(rest.substr(0, lt));
            i += lt;
            rest = s.substr(i);
            if (rest.size() >= 2 && rest.substr(0, 2) == "</") {
                auto gt = rest.find('>');
                if (gt == std::string_view::npos)
                    throw std::runtime_error("unterminated end tag");
                i += gt + 1;
                if (!el.children.empty() && trim_view(el.text).empty())
                    el.text.clear();
                return el;
            }
            if (rest.size() >= 4 && rest.substr(0, 4) == "<!--") {
                skip_misc();
                continue;
            }
            el.children.push_back(element());
        }
    }

    static std::string_view trim_view(std::string_view s) {
        auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string_view::npos)
            return {};
        auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    static std::string_view ltrim_view(std::string_view s) {
        auto a = s.find_first_not_of(" \t\r\n");
        return a == std::string_view::npos ? std::string_view{} : s.substr(a);
    }
};

void write_element(const SheetSetElement &el, std::string &out) {
    out.push_back('<');
    out += el.name;
    for (const auto &a : el.attrs) {
        out.push_back(' ');
        out += a.first;
        out += "=\"";
        escape_into(a.second, true, out);
        out.push_back('"');
    }
    if (el.text.empty() && el.children.empty()) {
        out += "/>";
        return;
    }
    out.push_back('>');
    escape_into(el.text, false, out);
    for (const auto &c : el.children)
        write_element(c, out);
    out += "</";
    out += el.name;
    out.push_back('>');
}

void renew_ids(SheetSetElement &el) {
    for (auto &a : el.attrs) {
        if (a.first == "ID")
            a.second = sheet_set_new_id();
    }
    for (auto &c : el.children)
        renew_ids(c);
}

std::optional<std::string> folder_of(const std::optional<std::string> &path) {
    if (!path)
        return std::nullopt;
    auto slash = path->find_last_of("/\\");
    if (slash == std::string::npos)
        return std::nullopt;
    return path->substr(0, slash);
}

std::string normalize_path(std::string p) {
    std::vector<std::string> parts;
    std::string cur;
    auto push = [&] {
        if (cur.empty() || cur == ".") {
            cur.clear();
            return;
        }
        if (cur == "..") {
            if (!parts.empty())
                parts.pop_back();
            cur.clear();
            return;
        }
        parts.push_back(cur);
        cur.clear();
    };
    for (char c : p) {
        if (c == '/' || c == '\\')
            push();
        else
            cur.push_back(c);
    }
    push();
    std::string out;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back(sep);
        out += parts[i];
    }
    if (out.empty() && !p.empty() && (p[0] == '/' || p[0] == '\\'))
        out = std::string(1, sep);
    return out;
}

void set_file_props(SheetSetElement &r, std::string_view file, const std::optional<std::string> &folder) {
    for (auto p : {"Environ_FileName", "FileName", "Relative_FileName", "SpecialFolder_FileName"})
        r.remove_named(p);
    auto native = native_sheet_path(file);
    r.set_prop("FileName", native);
    if (folder) {
        if (auto rel = relative_sheet_path(*folder, native))
            r.set_prop("Relative_FileName", *rel);
    }
}

bool exists_path(const std::string &p) {
    std::ifstream in(p, std::ios::binary);
    return static_cast<bool>(in);
}

std::string trim_ws(std::string_view s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

} // namespace

std::vector<std::uint8_t> sheet_set_encode(const std::uint8_t *plain, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i)
        out[i] = kEncode[plain[i]];
    return out;
}

std::vector<std::uint8_t> sheet_set_decode(const std::uint8_t *data, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i)
        out[i] = kDecode[data[i]];
    return out;
}

SheetSetElement SheetSetElement::make(std::string name) {
    SheetSetElement el;
    el.name = std::move(name);
    return el;
}

std::optional<std::string_view> SheetSetElement::attr(std::string_view key) const {
    for (const auto &a : attrs)
        if (a.first == key)
            return a.second;
    return std::nullopt;
}

std::string_view SheetSetElement::id() const {
    auto v = attr("ID");
    return v ? *v : std::string_view{};
}

std::optional<std::string_view> SheetSetElement::propname() const { return attr("propname"); }

const SheetSetElement *SheetSetElement::named(std::string_view name) const {
    for (const auto &c : children)
        if (c.propname() == name)
            return &c;
    return nullptr;
}

SheetSetElement *SheetSetElement::named(std::string_view name) {
    for (auto &c : children)
        if (c.propname() == name)
            return &c;
    return nullptr;
}

std::optional<std::string_view> SheetSetElement::prop(std::string_view name) const {
    auto *c = named(name);
    if (!c || c->name != "AcSmProp")
        return std::nullopt;
    return c->text;
}

void SheetSetElement::set_prop(std::string_view name, std::string_view value) { set_prop_vt(name, 8, value); }

void SheetSetElement::set_prop_vt(std::string_view name, std::int32_t vt, std::string_view value) {
    if (auto *c = named(name); c && c->name == "AcSmProp") {
        c->text = std::string(value);
        return;
    }
    put_named(sheet_set_prop(name, vt, value));
}

void SheetSetElement::put_named(SheetSetElement child) {
    auto name = std::string(child.propname().value_or(""));
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i].propname() == std::optional<std::string_view>{name}) {
            children[i] = std::move(child);
            return;
        }
    }
    auto key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::size_t at = children.size();
    for (std::size_t i = 0; i < children.size(); ++i) {
        auto n = children[i].propname();
        if (!n) {
            at = i;
            break;
        }
        std::string other(*n);
        std::transform(other.begin(), other.end(), other.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (other > key) {
            at = i;
            break;
        }
    }
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(at), std::move(child));
}

void SheetSetElement::remove_named(std::string_view name) {
    children.erase(std::remove_if(children.begin(), children.end(),
                                  [&](const SheetSetElement &c) { return c.propname() == name; }),
                   children.end());
}

const SheetSetElement *SheetSetElement::find(std::string_view id) const {
    if (this->id() == id)
        return this;
    for (const auto &c : children)
        if (auto *found = c.find(id))
            return found;
    return nullptr;
}

SheetSetElement *SheetSetElement::find(std::string_view id) {
    if (this->id() == id)
        return this;
    for (auto &c : children)
        if (auto *found = c.find(id))
            return found;
    return nullptr;
}

const SheetSetElement *SheetSetElement::parent_of(std::string_view id) const {
    for (const auto &c : children)
        if (c.id() == id)
            return this;
    for (const auto &c : children)
        if (auto *found = c.parent_of(id))
            return found;
    return nullptr;
}

std::string sheet_set_new_id() {
    static std::atomic<std::uint64_t> counter{0};
    const auto n = counter.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    std::mt19937_64 rng(n ^ now ^ 0x5eedfeedull);
    auto a = rng();
    auto b = rng();
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "g%08X-%04X-%04X-%04X-%012llX",
                  static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xFFFF),
                  static_cast<unsigned>(a & 0xFFFF), static_cast<unsigned>(b >> 48),
                  static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return buf;
}

SheetSetElement sheet_set_prop(std::string_view name, std::int32_t vt, std::string_view value) {
    SheetSetElement el = SheetSetElement::make("AcSmProp");
    el.attrs.emplace_back("propname", std::string(name));
    el.attrs.emplace_back("vt", std::to_string(vt));
    el.text = std::string(value);
    return el;
}

SheetSetElement sheet_set_object(std::string_view tag, std::string_view clsid,
                                 std::optional<std::string_view> propname) {
    auto el = SheetSetElement::make(std::string(tag));
    el.attrs.emplace_back("clsid", std::string(clsid));
    el.attrs.emplace_back("ID", sheet_set_new_id());
    if (propname) {
        el.attrs.emplace_back("propname", std::string(*propname));
        el.attrs.emplace_back("vt", "13");
    }
    return el;
}

std::optional<ComponentKind> component_kind_of(const SheetSetElement &el) {
    if (el.name == "AcSmSheetSet")
        return ComponentKind::SheetSet;
    if (el.name == "AcSmSubset")
        return ComponentKind::Subset;
    if (el.name == "AcSmSheet")
        return ComponentKind::Sheet;
    return std::nullopt;
}

SheetSetElement parse_sheet_set_xml(std::string_view text) {
    if (!text.empty() && static_cast<unsigned char>(text[0]) == 0xEF && text.size() >= 3 &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    Parser p{text, 0};
    return p.element();
}

std::string write_sheet_set_xml(const SheetSetElement &root) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
    write_element(root, out);
    out += "\r\n";
    return out;
}

SheetSetDatabase SheetSetDatabase::make(std::string_view name, std::string_view description) {
    SheetSetDatabase db;
    db.root = sheet_set_object("AcSmDatabase", kClsidDatabase, std::nullopt);
    db.root.children.push_back(sheet_set_prop("DbFingerPrint", 8, sheet_set_new_id()));
    db.root.children.push_back(sheet_set_prop("DbVersion", 8, "1.1"));
    db.root.children.push_back(sheet_set_prop("FileRevision", 3, "0"));
    auto set = sheet_set_object("AcSmSheetSet", kClsidSheetSet, "SheetSet");
    const std::pair<const char *, const char *> named[] = {
        {"AcSmCalloutBlocks", kClsidCalloutBlocks},
        {"AcSmCustomPropertyBag", kClsidCustomPropertyBag},
        {"AcSmProjectPointLocations", kClsidProjectPointLocations},
        {"AcSmResources", kClsidResources},
        {"AcSmSheetSelSets", kClsidSheetSelSets},
        {"AcSmViewCategories", kClsidViewCategories},
    };
    const char *propnames[] = {"CalloutBlocks", "CustomPropertyBag", "ProjectPointLocations",
                               "Resources", "SheetSelSets", "ViewCategories"};
    for (std::size_t i = 0; i < 6; ++i)
        set.put_named(sheet_set_object(named[i].first, named[i].second, propnames[i]));
    auto publish = sheet_set_object("AcSmPublishOptions", kClsidPublishOptions, "PublishOptions");
    publish.set_prop_vt("DwfType", 2, "-1");
    publish.set_prop_vt("PromptForName", 2, "-1");
    set.put_named(std::move(publish));
    set.set_prop("Desc", description);
    set.set_prop("Name", name);
    db.root.children.push_back(std::move(set));
    return db;
}

SheetSetDatabase SheetSetDatabase::from_xml(std::string_view text) {
    auto root = parse_sheet_set_xml(text);
    if (root.name != "AcSmDatabase")
        throw std::runtime_error("not a sheet set database (" + root.name + ")");
    SheetSetDatabase db;
    db.root = std::move(root);
    return db;
}

SheetSetDatabase SheetSetDatabase::from_bytes(const std::uint8_t *data, std::size_t size) {
    auto plain = sheet_set_decode(data, size);
    return from_xml(std::string(plain.begin(), plain.end()));
}

SheetSetDatabase SheetSetDatabase::read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read sheet set: " + path);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto db = from_bytes(data.data(), data.size());
    db.path = path;
    return db;
}

std::string SheetSetDatabase::to_xml() const { return write_sheet_set_xml(root); }

std::vector<std::uint8_t> SheetSetDatabase::to_bytes() const {
    auto xml = to_xml();
    return sheet_set_encode(reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size());
}

void SheetSetDatabase::write(const std::string &path) {
    if (auto *p = sheet_set().named("PublishOptions")) {
        if (!p->named("DefaultOutputdir")) {
            p->put_named(sheet_set_object("AcSmSimpleFileReferece", kClsidSimpleFileReference,
                                          "DefaultOutputdir"));
        }
    }
    const auto revision = file_revision() + 1;
    root.set_prop("DbFingerPrint", sheet_set_new_id());
    root.set_prop_vt("FileRevision", 3, std::to_string(revision));
    auto bytes = to_bytes();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("cannot write sheet set: " + path);
    this->path = path;
}

std::int32_t SheetSetDatabase::file_revision() const {
    if (auto v = root.prop("FileRevision")) {
        try {
            return std::stoi(std::string(trim_ws(*v)));
        } catch (...) {
        }
    }
    return 0;
}

const SheetSetElement &SheetSetDatabase::sheet_set() const {
    for (const auto &c : root.children)
        if (c.name == "AcSmSheetSet")
            return c;
    return root;
}

SheetSetElement &SheetSetDatabase::sheet_set() {
    for (auto &c : root.children)
        if (c.name == "AcSmSheetSet")
            return c;
    return root;
}

std::string_view SheetSetDatabase::name() const {
    auto v = sheet_set().prop("Name");
    return v ? *v : std::string_view{};
}

const SheetSetElement *SheetSetDatabase::find(std::string_view id) const { return root.find(id); }
SheetSetElement *SheetSetDatabase::find(std::string_view id) { return root.find(id); }
const SheetSetElement *SheetSetDatabase::parent_of(std::string_view id) const { return root.parent_of(id); }

std::optional<std::string> SheetSetDatabase::add_subset(std::string_view parent, std::string_view name,
                                                        std::string_view description) {
    auto *p = find(parent);
    if (!p)
        return std::nullopt;
    bool prompt = false;
    if (auto v = p->prop("PromptForDwt")) {
        auto t = trim_ws(*v);
        prompt = !(t.empty() || t == "0");
    }
    std::vector<SheetSetElement> inherited;
    for (auto n : {"DefDwtLayout", "NewSheetLocation"}) {
        if (auto *e = p->named(n)) {
            auto copy = *e;
            renew_ids(copy);
            inherited.push_back(std::move(copy));
        }
    }
    auto sub = sheet_set_object("AcSmSubset", kClsidSubset, std::nullopt);
    for (auto &e : inherited)
        sub.put_named(std::move(e));
    sub.set_prop("Desc", description);
    sub.set_prop("Name", name);
    if (prompt)
        sub.set_prop_vt("PromptForDwt", 2, "-1");
    auto id = std::string(sub.id());
    find(parent)->children.push_back(std::move(sub));
    return id;
}

std::optional<std::string> SheetSetDatabase::add_sheet(std::string_view parent, std::string_view number,
                                                       std::string_view title, std::string_view description) {
    auto sheet = sheet_set_object("AcSmSheet", kClsidSheet, std::nullopt);
    if (!description.empty())
        sheet.set_prop("Desc", description);
    if (!number.empty())
        sheet.set_prop("Number", number);
    sheet.put_named(sheet_set_object("AcSmSheetViews", kClsidSheetViews, "SheetViews"));
    sheet.set_prop("Title", title);
    for (const auto &[name, value, flags] : custom_properties(sheet_set())) {
        if (flags & kCustomSheetProp)
            set_custom_property(sheet, name, value, flags);
    }
    auto id = std::string(sheet.id());
    auto *p = find(parent);
    if (!p)
        return std::nullopt;
    p->children.push_back(std::move(sheet));
    return id;
}

bool SheetSetDatabase::remove(std::string_view id) {
    auto go = [&](auto &&self, SheetSetElement &el) -> bool {
        for (std::size_t i = 0; i < el.children.size(); ++i) {
            if (el.children[i].id() == id) {
                el.children.erase(el.children.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        for (auto &c : el.children)
            if (self(self, c))
                return true;
        return false;
    };
    return go(go, root);
}

bool SheetSetDatabase::set_file_reference(std::string_view component, std::string_view propname,
                                          std::string_view file) {
    auto folder = folder_of(path);
    auto *el = find(component);
    if (!el)
        return false;
    if (file.empty()) {
        el->remove_named(propname);
        return true;
    }
    auto r = sheet_set_object("AcSmFileReference", kClsidFileReference, propname);
    set_file_props(r, file, folder);
    el->put_named(std::move(r));
    return true;
}

std::optional<std::string> SheetSetDatabase::file_reference(std::string_view component,
                                                            std::string_view propname) const {
    auto *el = find(component);
    if (!el)
        return std::nullopt;
    auto *r = el->named(propname);
    if (!r)
        return std::nullopt;
    return resolve_file(*r);
}

bool SheetSetDatabase::set_layout_reference(std::string_view component, std::string_view propname,
                                            const LayoutReference &reference) {
    auto folder = folder_of(path);
    auto *el = find(component);
    if (!el)
        return false;
    SheetSetElement r;
    if (auto *old = el->named(propname); old && old->name == "AcSmAcDbLayoutReference") {
        r = *old;
        r.children.clear();
    } else {
        r = sheet_set_object("AcSmAcDbLayoutReference", kClsidLayoutReference, propname);
    }
    if (!reference.handle.empty())
        r.set_prop("AcDbHandle", reference.handle);
    set_file_props(r, reference.file_name, folder);
    r.set_prop("Name", reference.name);
    el->put_named(std::move(r));
    return true;
}

std::optional<LayoutReference> SheetSetDatabase::layout_reference(std::string_view component,
                                                                  std::string_view propname) const {
    auto *el = find(component);
    if (!el)
        return std::nullopt;
    auto *r = el->named(propname);
    if (!r)
        return std::nullopt;
    LayoutReference out;
    out.file_name = resolve_file(*r);
    if (auto n = r->prop("Name"))
        out.name = std::string(*n);
    if (auto h = r->prop("AcDbHandle"))
        out.handle = std::string(*h);
    return out;
}

std::string SheetSetDatabase::resolve_file(const SheetSetElement &reference) const {
    auto absolute = std::string(reference.prop("FileName").value_or(""));
    auto folder = folder_of(path);
    if (auto rel = reference.prop("Relative_FileName"); rel && folder) {
        std::string joined = *folder;
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        if (!joined.empty() && joined.back() != '/' && joined.back() != '\\')
            joined.push_back(sep);
        std::string rel_path(*rel);
        for (char &c : rel_path)
            if (c == '\\')
                c = sep;
        joined += rel_path;
        auto normalized = normalize_path(joined);
        if (exists_path(normalized) || absolute.empty())
            return normalized;
    }
    return absolute;
}

std::vector<const SheetSetElement *> SheetSetDatabase::sheets() const {
    std::vector<const SheetSetElement *> out;
    auto go = [&](auto &&self, const SheetSetElement &el) -> void {
        for (const auto &c : el.children) {
            auto kind = component_kind_of(c);
            if (kind == ComponentKind::Sheet)
                out.push_back(&c);
            else if (kind == ComponentKind::Subset)
                self(self, c);
        }
    };
    go(go, sheet_set());
    return out;
}

const SheetSetElement *SheetSetDatabase::sheet_for(std::string_view drawing,
                                                   std::optional<std::string_view> layout) const {
    auto want = sheet_path_key(drawing);
    std::vector<const SheetSetElement *> of_drawing;
    for (auto *s : sheets()) {
        auto *r = s->named("Layout");
        if (!r)
            continue;
        if (sheet_path_key(resolve_file(*r)) == want ||
            (r->prop("FileName") && sheet_path_key(*r->prop("FileName")) == want))
            of_drawing.push_back(s);
    }
    if (layout) {
        for (auto *s : of_drawing) {
            auto *r = s->named("Layout");
            auto n = r ? r->prop("Name") : std::nullopt;
            if (n && ascii_ieq(*n, *layout))
                return s;
        }
    }
    return of_drawing.empty() ? nullptr : of_drawing.front();
}

std::optional<std::string> SheetSetDatabase::sheet_value(const SheetSetElement &sheet,
                                                         std::string_view component,
                                                         std::string_view property) const {
    const auto &set = sheet_set();
    auto custom = [](const SheetSetElement &el, std::string_view name) -> std::optional<std::string> {
        for (const auto &[n, v, f] : custom_properties(el)) {
            (void)f;
            if (n == name)
                return v;
        }
        return std::nullopt;
    };
    if (component == "Sheet") {
        if (property == "Number" || property == "Title" || property == "RevisionNumber" ||
            property == "RevisionDate" || property == "IssuePurpose" || property == "Category")
            return std::string(sheet.prop(property).value_or(""));
        if (property == "Description")
            return std::string(sheet.prop("Desc").value_or(""));
        if (property == "NumberAndTitle")
            return number_and_title(sheet);
        if (auto v = custom(sheet, property))
            return v;
        for (const auto &[n, v, f] : custom_properties(set)) {
            if (n == property && (f & kCustomSheetProp) != 0)
                return v;
        }
        return std::nullopt;
    }
    if (component == "SheetSet")
        return set_value(property);
    if (component == "Subset") {
        auto *parent = parent_of(sheet.id());
        if (!parent || component_kind_of(*parent) != ComponentKind::Subset)
            return std::nullopt;
        if (property == "Description")
            return std::string(parent->prop("Desc").value_or(""));
        return std::string(parent->prop(property).value_or(""));
    }
    return std::nullopt;
}

std::optional<std::string> SheetSetDatabase::set_value(std::string_view property) const {
    const auto &set = sheet_set();
    if (property == "Name" || property == "ProjectName" || property == "ProjectNumber" ||
        property == "ProjectPhase" || property == "ProjectMilestone")
        return std::string(set.prop(property).value_or(""));
    if (property == "Description")
        return std::string(set.prop("Desc").value_or(""));
    for (const auto &[n, v, f] : custom_properties(set)) {
        (void)f;
        if (n == property)
            return v;
    }
    return std::nullopt;
}

std::optional<std::string> SheetSetDatabase::navigation_value(std::string_view set_id,
                                                              std::optional<std::string_view> component,
                                                              std::string_view property) const {
    if (!ascii_ieq(sheet_set().id(), set_id))
        return std::nullopt;
    if (!component)
        return set_value(property);
    auto *el = find(*component);
    if (!el)
        return std::nullopt;
    auto kind = component_kind_of(*el);
    if (kind == ComponentKind::Sheet)
        return sheet_value(*el, "Sheet", property);
    if (kind == ComponentKind::SheetSet)
        return set_value(property);
    if (el->name == "AcSmSheetView") {
        if (property == "Number" || property == "Title")
            return std::string(el->prop(property).value_or(""));
        if (property == "NumberAndTitle")
            return number_and_title(*el);
        return std::nullopt;
    }
    if (property == "Name")
        return std::string(el->prop("Name").value_or(""));
    if (property == "Description")
        return std::string(el->prop("Desc").value_or(""));
    return std::nullopt;
}

std::vector<const SheetSetElement *> SheetSetDatabase::view_categories() const {
    auto *v = sheet_set().named("ViewCategories");
    if (!v)
        return {};
    std::vector<const SheetSetElement *> out;
    for (const auto &c : v->children)
        if (c.name == "AcSmViewCategory" && c.prop("Name"))
            out.push_back(&c);
    return out;
}

std::vector<const SheetSetElement *> SheetSetDatabase::callout_blocks() const {
    auto *v = sheet_set().named("CalloutBlocks");
    if (!v)
        return {};
    std::vector<const SheetSetElement *> out;
    for (const auto &c : v->children)
        out.push_back(&c);
    return out;
}

std::optional<std::string> SheetSetDatabase::add_callout_block(std::string_view file, std::string_view name,
                                                               std::string_view handle) {
    auto folder = folder_of(path);
    auto r = sheet_set_object("AcSmAcDbBlockRecordReference", kClsidBlockRecordReference, std::nullopt);
    r.set_prop("AcDbHandle", handle);
    set_file_props(r, file, folder);
    r.set_prop("Name", name);
    auto id = std::string(r.id());
    auto &set = sheet_set();
    if (!set.named("CalloutBlocks"))
        set.put_named(sheet_set_object("AcSmCalloutBlocks", kClsidCalloutBlocks, "CalloutBlocks"));
    auto *bag = set.named("CalloutBlocks");
    if (!bag)
        return std::nullopt;
    bag->children.push_back(std::move(r));
    return id;
}

std::vector<std::string> SheetSetDatabase::category_blocks(std::string_view id) const {
    auto *c = find(id);
    if (!c)
        return {};
    auto *r = c->named("CalloutBlocks");
    if (!r)
        return {};
    std::vector<std::string> out;
    for (const auto &o : r->children)
        if (auto v = o.prop("ReferencedObject"))
            out.emplace_back(*v);
    return out;
}

std::optional<std::string> SheetSetDatabase::set_view_category(std::optional<std::string_view> id,
                                                               std::string_view name,
                                                               const std::vector<std::string> &blocks) {
    auto refs = sheet_set_object("AcSmCalloutBlockReferences", kClsidCalloutBlockReferences, "CalloutBlocks");
    for (const auto &b : blocks) {
        auto o = sheet_set_object("AcSmObjectReference", kClsidObjectReference, std::nullopt);
        o.set_prop_vt("ReferencedObject", -1, b);
        refs.children.push_back(std::move(o));
    }
    if (id) {
        auto *c = find(*id);
        if (!c)
            return std::nullopt;
        c->put_named(std::move(refs));
        c->set_prop("Name", name);
        return std::string(*id);
    }
    auto c = sheet_set_object("AcSmViewCategory", kClsidViewCategory, std::nullopt);
    c.put_named(std::move(refs));
    c.set_prop("Name", name);
    auto neu = std::string(c.id());
    auto &set = sheet_set();
    if (!set.named("ViewCategories"))
        set.put_named(sheet_set_object("AcSmViewCategories", kClsidViewCategories, "ViewCategories"));
    auto *bag = set.named("ViewCategories");
    if (!bag)
        return std::nullopt;
    bag->children.push_back(std::move(c));
    return neu;
}

std::vector<const SheetSetElement *> SheetSetDatabase::sheet_views(const SheetSetElement &sheet) const {
    auto *v = sheet.named("SheetViews");
    if (!v)
        return {};
    std::vector<const SheetSetElement *> out;
    for (const auto &c : v->children)
        if (c.name == "AcSmSheetView")
            out.push_back(&c);
    return out;
}

std::optional<std::string_view> SheetSetDatabase::view_category_of(const SheetSetElement &view) {
    auto *c = view.named("Category");
    if (!c)
        return std::nullopt;
    return c->prop("ReferencedObject");
}

std::optional<std::string> SheetSetDatabase::add_sheet_view(std::string_view sheet,
                                                            std::optional<std::string_view> category,
                                                            const LayoutReference &view,
                                                            std::string_view title) {
    auto folder = folder_of(path);
    auto v = sheet_set_object("AcSmSheetView", kClsidSheetView, std::nullopt);
    if (category) {
        auto r = sheet_set_object("AcSmObjectReference", kClsidObjectReference, "Category");
        r.set_prop_vt("ReferencedObject", -1, *category);
        v.put_named(std::move(r));
    }
    auto r = sheet_set_object("AcSmAcDbViewReference", kClsidViewReference, "NamedView");
    r.set_prop("AcDbHandle", view.handle);
    set_file_props(r, view.file_name, folder);
    r.set_prop("Name", view.name);
    v.put_named(std::move(r));
    v.set_prop("Title", title);
    auto id = std::string(v.id());
    auto *sh = find(sheet);
    if (!sh)
        return std::nullopt;
    if (!sh->named("SheetViews"))
        sh->put_named(sheet_set_object("AcSmSheetViews", kClsidSheetViews, "SheetViews"));
    auto *bag = sh->named("SheetViews");
    if (!bag)
        return std::nullopt;
    bag->children.push_back(std::move(v));
    return id;
}

std::optional<std::string> SheetSetDatabase::add_resource(std::string_view folder) {
    auto base = folder_of(path);
    auto r = sheet_set_object("AcSmFileReference", kClsidFileReference, std::nullopt);
    set_file_props(r, folder, base);
    auto id = std::string(r.id());
    auto &set = sheet_set();
    if (!set.named("Resources"))
        set.put_named(sheet_set_object("AcSmResources", kClsidResources, "Resources"));
    auto *bag = set.named("Resources");
    if (!bag)
        return std::nullopt;
    bag->children.push_back(std::move(r));
    return id;
}

std::string number_and_title(const SheetSetElement &sheet) {
    auto number = std::string(sheet.prop("Number").value_or(""));
    auto title = std::string(sheet.prop("Title").value_or(""));
    if (number.empty())
        return title;
    return number + " - " + title;
}

std::vector<std::tuple<std::string, std::string, std::int32_t>> custom_properties(const SheetSetElement &el) {
    auto *bag = el.named("CustomPropertyBag");
    if (!bag)
        return {};
    std::vector<std::tuple<std::string, std::string, std::int32_t>> out;
    for (const auto &c : bag->children) {
        if (c.name != "AcSmCustomPropertyValue")
            continue;
        std::int32_t flags = 0;
        if (auto f = c.prop("Flags")) {
            try {
                flags = std::stoi(trim_ws(*f));
            } catch (...) {
            }
        }
        out.emplace_back(std::string(c.propname().value_or("")), std::string(c.prop("Value").value_or("")),
                         flags);
    }
    return out;
}

void set_custom_property(SheetSetElement &el, std::string_view name, std::string_view value, std::int32_t flags) {
    if (!el.named("CustomPropertyBag"))
        el.put_named(sheet_set_object("AcSmCustomPropertyBag", kClsidCustomPropertyBag, "CustomPropertyBag"));
    auto *bag = el.named("CustomPropertyBag");
    if (auto *v = bag->named(name); v && v->name == "AcSmCustomPropertyValue") {
        v->set_prop_vt("Flags", 3, std::to_string(flags));
        v->set_prop("Value", value);
        return;
    }
    auto v = sheet_set_object("AcSmCustomPropertyValue", kClsidCustomPropertyValue, name);
    v.set_prop_vt("Flags", 3, std::to_string(flags));
    v.set_prop("Value", value);
    bag->children.push_back(std::move(v));
}

void remove_custom_property(SheetSetElement &el, std::string_view name) {
    if (auto *bag = el.named("CustomPropertyBag")) {
        bag->children.erase(std::remove_if(bag->children.begin(), bag->children.end(),
                                           [&](const SheetSetElement &c) { return c.propname() == name; }),
                            bag->children.end());
    }
}

std::optional<std::string> relative_sheet_path(std::string_view folder, std::string_view file) {
    auto parts = [](std::string_view p) {
        std::vector<std::string> out;
        std::string cur;
        auto push = [&] {
            if (cur.empty() || cur == ".") {
                cur.clear();
                return;
            }
            out.push_back(cur);
            cur.clear();
        };
        for (char c : p) {
            if (c == '/' || c == '\\')
                push();
            else
                cur.push_back(c);
        }
        push();
        return out;
    };
    auto a = parts(folder);
    auto b = parts(file);
    if (a.empty() || b.empty() || !ascii_ieq(a[0], b[0]))
        return std::nullopt;
    std::size_t common = 0;
    while (common < a.size() && common < b.size() && ascii_ieq(a[common], b[common]))
        ++common;
    std::vector<std::string> out;
    if (common == a.size())
        out.emplace_back(".");
    for (std::size_t i = 0; i < a.size() - common; ++i)
        out.emplace_back("..");
    out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(common), b.end());
    std::string joined;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (i)
            joined += "\\";
        joined += out[i];
    }
    return joined;
}

std::string native_sheet_path(std::string_view p) {
#ifdef _WIN32
    std::string out(p);
    for (char &c : out)
        if (c == '/')
            c = '\\';
    return out;
#else
    return std::string(p);
#endif
}

std::string sheet_path_key(std::string_view p) {
    std::string out(p);
    for (char &c : out)
        if (c == '/')
            c = '\\';
    while (!out.empty() && out.back() == '\\')
        out.pop_back();
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string placeholder_type_name(std::string_view component, std::string_view property) {
    if (component == "Sheet") {
        if (property == "NumberAndTitle" || property == "Title" || property == "Number" ||
            property == "Description")
            return "Sheet" + std::string(property);
        if (property == "RevisionNumber" || property == "RevisionDate" || property == "IssuePurpose" ||
            property == "Category")
            return std::string(property);
        return "Custom";
    }
    if (component == "View") {
        if (property == "ViewportScale")
            return "ViewportScale";
        return "View" + std::string(property);
    }
    return std::string(property);
}

std::optional<std::tuple<std::string, std::string, std::string>> parse_sheet_field_code(std::string_view code) {
    auto body = std::string(code);
    auto t = body.find_first_not_of(" \t");
    if (t != std::string::npos)
        body.erase(0, t);
    if (!body.empty() && body[0] == '\\')
        body.erase(body.begin());
    auto ws = body.find_first_of(" \t");
    if (ws == std::string::npos)
        return std::nullopt;
    body = body.substr(ws);
    t = body.find_first_not_of(" \t");
    if (t != std::string::npos)
        body.erase(0, t);
    else
        body.clear();
    auto href = body.find(" \\href ");
    if (href != std::string::npos) {
        body.resize(href);
        while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back())))
            body.pop_back();
    }
    std::string target, fmt;
    auto fp = body.find("\\f ");
    if (fp != std::string::npos) {
        auto f = body.substr(fp + 3);
        t = f.find_first_not_of(" \t");
        if (t != std::string::npos)
            f.erase(0, t);
        if (!f.empty() && f.front() == '"')
            f.erase(f.begin());
        if (!f.empty() && f.back() == '"')
            f.pop_back();
        for (std::size_t i = 0; i + 1 < f.size();) {
            if (f[i] == '\\' && f[i + 1] == '"')
                f.erase(i, 1);
            else
                ++i;
        }
        target = body.substr(0, fp);
        while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
            target.pop_back();
        fmt = f;
    } else {
        target = body;
        fmt.clear();
    }
    std::string component, property;
    if (target.rfind("Database(", 0) == 0) {
        auto p = target.rfind(").");
        if (p == std::string::npos)
            return std::nullopt;
        component = target.substr(0, p + 1);
        property = target.substr(p + 2);
    } else {
        auto d = target.find('.');
        if (d == std::string::npos)
            return std::nullopt;
        component = target.substr(0, d);
        property = target.substr(d + 1);
    }
    return std::make_tuple(component, property, fmt);
}

std::optional<std::tuple<std::string, std::string, std::optional<std::string>>>
parse_sheet_navigation(std::string_view component) {
    auto arg = [](std::string_view s, std::string_view call) -> std::optional<std::pair<std::string, std::size_t>> {
        auto start = s.find(call);
        if (start == std::string_view::npos)
            return std::nullopt;
        start += call.size();
        auto endrel = s.substr(start).find("\")");
        if (endrel == std::string_view::npos)
            return std::nullopt;
        return std::make_pair(std::string(s.substr(start, endrel)), start + endrel + 2);
    };
    auto file = arg(component, "Database(\"");
    if (!file)
        return std::nullopt;
    auto set = arg(component, ".SheetSet(\"");
    if (!set)
        return std::nullopt;
    std::optional<std::string> comp;
    auto rest = component.substr(set->second);
    if (auto c = arg(rest, ".Component(\""))
        comp = c->first;
    return std::make_tuple(file->first, set->first, comp);
}

std::vector<std::pair<std::string, std::string>> field_child_values(std::string_view code) {
    auto parsed = parse_sheet_field_code(code);
    if (!parsed)
        return {};
    auto [component, property, fmt] = *parsed;
    (void)fmt;
    if (auto nav = parse_sheet_navigation(component)) {
        auto [file, set, comp] = *nav;
        std::vector<std::pair<std::string, std::string>> v;
        if (comp) {
            v.emplace_back("SheetSetCompId", *comp);
            v.emplace_back("SheetSetCompName", "Component");
        }
        v.emplace_back("SheetSetFile", file);
        v.emplace_back("SheetSetId", set);
        v.emplace_back("SheetSetPropertyName", property);
        return v;
    }
    return {{"SheetSetCompName", component}, {"SheetSetPropertyName", property}};
}

} // namespace libopencad
