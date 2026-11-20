// SPDX-License-Identifier: MPL-2.0
namespace {
TableValue field_value(MTFields raw) {
    MLFields f(std::move(raw));
    auto v = table_value(f);
    if (v.flags & 1) {
        v.number = 0;
        v.text.clear();
        v.binary.clear();
        v.point = {};
        v.handle = 0;
    }
    if (v.type == 16)
        v.point.z = 0;
    return v;
}
void field_value(MLFields &f, const TableValue &v) {
    MLFields value;
    table_value(value, v);
    if (v.type == 16 || v.type == 32)
        value.values.erase(std::remove_if(value.values.begin(), value.values.end(),
                                          [](const auto &p) { return p.code == 92; }),
                           value.values.end());
    mt_append(f, value.values);
    f.put_s(304, "ACVALUE_END");
}
void validate_field_references(const DxfDocument &d, const FieldDefinition &v) {
    for (auto h : v.references)
        if (h && d.records_with_handle(hex_handle(h)).size() != 1)
            throw std::invalid_argument("Missing FIELD object reference");
    auto value = [&](const TableValue &x) {
        if (!(x.flags & 1) && x.type == 64 && x.handle &&
            d.records_with_handle(hex_handle(x.handle)).size() != 1)
            throw std::invalid_argument("Missing FIELD value handle");
    };
    value(v.value);
    for (const auto &x : v.child_values)
        value(x.value);
}
} // namespace
FieldDefinition DxfField::definition() const {
    MTReader r(subclass_fields("AcDbField"));
    FieldDefinition v;
    auto f = r.scan([&](auto &r) {
        auto code = r.input.values[r.at].code;
        if (code == 6 || code == 7) {
            auto key = r.input.values[r.at++].value;
            MTFields body;
            if (!r.done() && r.input.values[r.at].code == 90) {
                // Pre-R2007 keyed values have no end marker. The type and its
                // data precede the next key or the field's display string.
                const auto begin = r.at++;
                while (!r.done() && !r.input.hidden[r.at]) {
                    const int c = r.input.values[r.at].code;
                    if (!(c == 91 || c == 140 || c == 1 || c == 92 || c == 310 || c == 11 || c == 21 ||
                          c == 31 || c == 330))
                        break;
                    ++r.at;
                }
                body.assign(r.input.values.begin() + begin, r.input.values.begin() + r.at);
            } else
                body = r.body("ACVALUE_END", 304);
            auto value = field_value(std::move(body));
            if (code == 6)
                v.child_values.push_back({std::move(key), std::move(value)});
            else
                v.value = std::move(value);
            return true;
        }
        if (code == 93 && r.at + 1 < r.input.values.size() && !r.input.hidden[r.at + 1] &&
            r.input.values[r.at + 1].code == 90) {
            v.value = field_value(r.body("ACVALUE_END", 304));
            return true;
        }
        return false;
    });
    v.evaluator_id = f.s(1, {});
    for (std::size_t n = 0; n < f.values.size(); ++n)
        if (!f.hidden[n] && (f.values[n].code == 2 || f.values[n].code == 3)) {
            v.code += f.values[n].value;
            f.used[n] = true;
        }
    v.format = f.s(4, {});
    f.take(90);
    f.take(97);
    f.take(93);
    for (auto n : f.take(360))
        v.children.push_back(handle_value(f.values[n].value));
    for (auto n : f.take(331))
        v.references.push_back(handle_value(f.values[n].value));
    v.evaluation_option = f.i(91, 0);
    v.filing_option = f.i(92, 0);
    v.state = f.i(94, 0);
    v.evaluation_status = f.i(95, 0);
    v.error_code = f.i(96, 0);
    v.error_message = f.s(300, {});
    v.value_string = f.s(301, {});
    v.value_string_length = f.i(98, 0);
    v.extra_fields = f.extras();
    return v;
}
void DxfField::set_definition(const FieldDefinition &v) {
    MLFields f;
    f.put_s(1, v.evaluator_id);
    f.put_s(2, v.code);
    f.put_i(90, mt_count(v.children.size()));
    for (auto h : v.children)
        f.put_s(360, hex_handle(h));
    f.put_i(97, mt_count(v.references.size()));
    for (auto h : v.references)
        f.put_s(331, hex_handle(h));
    f.put_i(91, v.evaluation_option);
    f.put_i(92, v.filing_option);
    f.put_i(94, v.state);
    f.put_i(95, v.evaluation_status);
    f.put_i(96, v.error_code);
    f.put_s(300, v.error_message);
    f.put_i(93, mt_count(v.child_values.size()));
    for (const auto &child : v.child_values) {
        f.put_s(6, child.key);
        field_value(f, child.value);
    }
    f.put_s(7, "ACFD_FIELD_VALUE");
    field_value(f, v.value);
    f.put_s(301, v.value_string);
    f.put_i(98, v.value_string_length);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbField", f.values);
}
FieldListDefinition DxfFieldList::definition() const {
    MLFields f(subclass_fields("AcDbIdSet"));
    FieldListDefinition v;
    f.take(90);
    v.unknown = f.b(290, false);
    for (auto n : f.take(330))
        v.fields.push_back(handle_value(f.values[n].value));
    v.extra_fields = f.extras();
    return v;
}
void DxfFieldList::set_definition(const FieldListDefinition &v) {
    MLFields f;
    f.put_i(90, mt_count(v.fields.size()));
    if (v.unknown)
        f.put_b(290, true);
    for (auto h : v.fields)
        f.put_s(330, hex_handle(h));
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbIdSet", f.values);
}
std::uint64_t DxfDocument::add_field(std::string_view dictionary, std::string name,
                                     const FieldDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("FIELD construction requires R2007+");
    if (!v.children.empty())
        throw std::invalid_argument("Create owned FIELD children with add_field_child");
    validate_field_references(*this, v);
    DxfDocument staged = *this;
    DxfField object;
    static_cast<DxfRecord &>(object) = staged.make_record("FIELD");
    object.pairs_.push_back({100, "AcDbField"});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_field_child(std::string_view parent, const FieldDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("FIELD construction requires R2007+");
    mleader_check_handle(*this, handle_value(parent), "FIELD");
    if (records_with_handle(parent).size() != 1 || !v.children.empty())
        throw std::invalid_argument("Invalid FIELD parent or adopted children");
    validate_field_references(*this, v);
    DxfDocument staged = *this;
    DxfField object;
    static_cast<DxfRecord &>(object) = staged.make_record("FIELD");
    object.set_field("", 330, std::string(parent));
    object.pairs_.push_back({100, "AcDbField"});
    object.set_definition(v);
    auto id = staged.insert_object(object);
    staged.for_each<DxfField>([&](auto &f) {
        if (handle_value(f.handle()) == handle_value(parent)) {
            auto p = f.definition();
            p.children.push_back(id);
            f.set_definition(p);
        }
    });
    *this = std::move(staged);
    return id;
}
void DxfDocument::set_field_definition(std::string_view handle, const FieldDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("FIELD edits require R2007+");
    mleader_check_handle(*this, handle_value(handle), "FIELD");
    if (records_with_handle(handle).size() != 1)
        throw std::invalid_argument("Expected unique FIELD");
    validate_field_references(*this, v);
    std::set<std::uint64_t> children;
    for (auto h : v.children) {
        mleader_check_handle(*this, h, "FIELD");
        if (!h || !children.insert(h).second ||
            handle_value(get_record(hex_handle(h))->owner_handle()) != handle_value(handle))
            throw std::invalid_argument("Invalid FIELD child ownership");
    }
    for_each<DxfField>([&](const auto &f) {
        if (handle_value(f.owner_handle()) == handle_value(handle) &&
            !children.count(handle_value(f.handle())))
            throw std::invalid_argument("FIELD setter cannot orphan children");
    });
    extension_tree({get_record(handle)});
    DxfDocument staged = *this;
    staged.for_each<DxfField>([&](auto &f) {
        if (handle_value(f.handle()) == handle_value(handle))
            f.set_definition(v);
    });
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_field_list(std::string_view dictionary, std::string name,
                                          const FieldListDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("FIELDLIST construction requires R2007+");
    for (auto h : v.fields)
        mleader_check_handle(*this, h, "FIELD");
    DxfDocument staged = *this;
    DxfFieldList object;
    static_cast<DxfRecord &>(object) = staged.make_record("FIELDLIST");
    object.pairs_.insert(object.pairs_.end(), {{100, "AcDbIdSet"}, {100, "AcDbFieldList"}});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
