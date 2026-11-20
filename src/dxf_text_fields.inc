// SPDX-License-Identifier: MPL-2.0
// rust CadDocument set_text_field / remove_text_field / update_fields / stamp_plot_fields.
namespace {
enum class TextHostKind { MText, Text, Attdef, Attrib };

bool text_is_object_type(std::string_view type) {
    return type == "FIELD" || type == "FIELDLIST" || type == "DICTIONARY" || type == "XRECORD";
}

std::optional<std::uint64_t> text_owner_of(const DxfDocument &doc, std::uint64_t h) {
    const auto *rec = doc.get_record(hex_handle(h));
    if (!rec)
        return std::nullopt;
    auto owner = rec->owner_handle();
    if (owner.empty())
        return std::nullopt;
    return handle_value(owner);
}

std::optional<std::uint64_t> text_host_of_container(const DxfDocument &doc, std::uint64_t container) {
    auto h = container;
    for (int i = 0; i < 12; ++i) {
        auto o = text_owner_of(doc, h);
        if (!o)
            return std::nullopt;
        auto next = text_owner_of(doc, *o);
        if (!next)
            return o;
        const auto *rec = doc.get_record(hex_handle(*o));
        if (!rec || !text_is_object_type(rec->type()))
            return o;
        h = *o;
    }
    return std::nullopt;
}

std::optional<TextHostKind> text_host_kind(const DxfDocument &doc, std::uint64_t host) {
    const auto *rec = doc.get_record(hex_handle(host));
    if (rec) {
        if (rec->type() == "MTEXT")
            return TextHostKind::MText;
        if (rec->type() == "TEXT")
            return TextHostKind::Text;
        if (rec->type() == "ATTDEF")
            return TextHostKind::Attdef;
        if (rec->type() == "ATTRIB")
            return TextHostKind::Attrib;
    }
    bool attrib = false;
    doc.for_each<DxfInsert>([&](const auto &ins) {
        for (std::size_t i = 0; i < ins.attribute_count(); ++i)
            if (handle_value(ins.attribute(i).handle()) == host)
                attrib = true;
    });
    if (attrib)
        return TextHostKind::Attrib;
    return std::nullopt;
}

double text_field_checksum(std::string_view text) {
    double sum = 0;
    std::size_t i = 0;
    for (std::size_t p = 0; p < text.size();) {
        auto c = static_cast<unsigned char>(text[p]);
        std::uint32_t cp = 0;
        std::size_t n = 1;
        if (c < 0x80)
            cp = c;
        else if ((c & 0xE0) == 0xC0 && p + 1 < text.size()) {
            cp = (static_cast<std::uint32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(text[p + 1]) & 0x3F);
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && p + 2 < text.size()) {
            cp = (static_cast<std::uint32_t>(c & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[p + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[p + 2]) & 0x3F);
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && p + 3 < text.size()) {
            cp = (static_cast<std::uint32_t>(c & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[p + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[p + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[p + 3]) & 0x3F);
            n = 4;
        } else
            cp = c;
        p += n;
        if (cp >= 0x10000) {
            auto u = cp - 0x10000;
            sum += static_cast<double>(++i) * static_cast<double>(0xD800 + (u >> 10));
            sum += static_cast<double>(++i) * static_cast<double>(0xDC00 + (u & 0x3FF));
        } else
            sum += static_cast<double>(++i) * static_cast<double>(cp);
    }
    return sum;
}

std::string text_mtext_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::optional<std::string> text_fill_template(std::string_view template_code,
                                              const std::vector<FieldDefinition> &children, bool mtext) {
    std::vector<std::string> shown;
    shown.reserve(children.size());
    for (const auto &child : children) {
        auto value = table_value_display(child.value);
        shown.push_back(value.empty() ? child.value_string : value);
    }
    std::string out;
    std::string rest(template_code);
    for (;;) {
        auto p = rest.find("%<\\_FldIdx ");
        if (p == std::string::npos) {
            out += rest;
            return out;
        }
        out += rest.substr(0, p);
        auto after = rest.substr(p + 11);
        auto end = after.find(">%");
        if (end == std::string::npos)
            return std::nullopt;
        auto token = after.substr(0, end);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        std::size_t idx = 0;
        try {
            idx = static_cast<std::size_t>(std::stoul(token));
        } catch (...) {
            return std::nullopt;
        }
        if (idx >= shown.size())
            return std::nullopt;
        out += mtext ? text_mtext_escape(shown[idx]) : shown[idx];
        rest = after.substr(end + 2);
    }
}

bool text_set_host_text(DxfDocument &doc, std::uint64_t host, const std::string &text) {
    bool ok = false;
    doc.for_each<DxfMText>([&](auto &m) {
        if (handle_value(m.handle()) == host) {
            m.set_text(text);
            ok = true;
        }
    });
    if (ok)
        return true;
    doc.for_each<DxfText>([&](auto &t) {
        if (handle_value(t.handle()) == host && t.type() == "TEXT") {
            t.set_text(text);
            ok = true;
        }
    });
    if (ok)
        return true;
    doc.for_each<DxfAttribute>([&](auto &a) {
        if (handle_value(a.handle()) == host) {
            a.set_text(text);
            ok = true;
        }
    });
    if (ok)
        return true;
    doc.for_each<DxfInsert>([&](auto &ins) {
        for (std::size_t i = 0; i < ins.attribute_count(); ++i) {
            auto a = ins.attribute(i);
            if (handle_value(a.handle()) != host)
                continue;
            a.set_text(text);
            ins.set_attribute(i, a);
            ok = true;
        }
    });
    return ok;
}

bool text_reaches(const DxfDocument &doc, std::uint64_t start, std::uint64_t target) {
    auto cur = start;
    for (int i = 0; i < 16; ++i) {
        if (cur == target)
            return true;
        auto next = text_owner_of(doc, cur);
        if (!next || *next == cur)
            return false;
        cur = *next;
    }
    return false;
}

class PlottingContext final : public FieldContext {
    const FieldContext &inner_;

public:
    explicit PlottingContext(const FieldContext &inner) : inner_(inner) {}
    double now_julian() const override { return inner_.now_julian(); }
    std::optional<std::pair<double, double>> file_times() const override { return inner_.file_times(); }
    bool plotting() const override { return true; }
    std::optional<std::string> login() const override { return inner_.login(); }
    std::optional<std::string> getenv(std::string_view name) const override { return inner_.getenv(name); }
    std::optional<std::string> getvar(std::string_view name) const override { return inner_.getvar(name); }
    std::optional<std::uint64_t> file_size() const override { return inner_.file_size(); }
    DateLocale date_locale() const override { return inner_.date_locale(); }
    std::optional<std::string>
    sheet_sets(const std::function<std::optional<std::string>(const SheetSetDatabase &)> &fn) const override {
        return inner_.sheet_sets(fn);
    }
};

template <class Pick>
std::pair<std::vector<std::uint64_t>, std::size_t>
text_restamp_fields(DxfDocument &doc, const FieldContext &eval_ctx, const FieldContext &ctx, Pick pick,
                    const std::vector<std::uint64_t> *only) {
    (void)ctx;
    struct Kid {
        std::uint64_t handle = 0;
        FieldDefinition def;
    };
    struct Container {
        std::uint64_t handle = 0;
        std::uint64_t host = 0;
        FieldDefinition def;
        std::vector<Kid> kids;
    };
    std::vector<Container> containers;
    doc.for_each<DxfField>([&](const auto &f) {
        auto def = f.definition();
        if (def.evaluator_id != "_text")
            return;
        auto handle = handle_value(f.handle());
        auto host = text_host_of_container(doc, handle);
        if (!host)
            return;
        if (only && std::find(only->begin(), only->end(), *host) == only->end())
            return;
        Container c;
        c.handle = handle;
        c.host = *host;
        c.def = std::move(def);
        containers.push_back(std::move(c));
    });
    doc.for_each<DxfField>([&](const auto &f) {
        auto owner = handle_value(f.owner_handle());
        for (auto &c : containers) {
            if (c.handle != owner)
                continue;
            Kid k;
            k.handle = handle_value(f.handle());
            k.def = f.definition();
            c.kids.push_back(std::move(k));
        }
    });
    std::size_t found = 0;
    std::vector<std::pair<std::uint64_t, FieldDefinition>> value_updates;
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>> host_updates;
    for (auto &c : containers) {
        std::sort(c.kids.begin(), c.kids.end(), [](const Kid &a, const Kid &b) { return a.handle < b.handle; });
        found += c.kids.size();
        std::vector<std::string> shown;
        bool changed = false;
        shown.reserve(c.kids.size());
        for (const auto &kid : c.kids) {
            auto cached = table_value_display(kid.def.value);
            if (cached.empty())
                cached = kid.def.value_string;
            std::optional<std::string> fresh;
            if (pick(kid.def))
                fresh = resolve_field_handle(doc, kid.handle, c.host, eval_ctx);
            if (fresh && *fresh != cached && !(kid.def.evaluator_id.rfind("AcSm", 0) == 0 && *fresh == "####")) {
                changed = true;
                auto next = kid.def;
                next.value.type = 4;
                next.value.text = *fresh;
                next.value.formatted = *fresh;
                next.value_string = *fresh;
                next.value_string_length = static_cast<std::int32_t>(fresh->size());
                next.evaluation_status = 2;
                value_updates.push_back({kid.handle, std::move(next)});
                shown.push_back(*fresh);
            } else
                shown.push_back(cached);
        }
        if (!changed)
            continue;
        bool mtext = false;
        if (const auto *rec = doc.get_record(hex_handle(c.host)))
            mtext = rec->type() == "MTEXT";
        std::string out;
        std::string rest = c.def.code;
        bool ok = true;
        for (;;) {
            auto p = rest.find("%<\\_FldIdx ");
            if (p == std::string::npos) {
                out += rest;
                break;
            }
            out += rest.substr(0, p);
            auto after = rest.substr(p + 11);
            auto end = after.find(">%");
            if (end == std::string::npos) {
                ok = false;
                break;
            }
            auto token = after.substr(0, end);
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
                token.erase(token.begin());
            std::size_t idx = 0;
            try {
                idx = static_cast<std::size_t>(std::stoul(token));
            } catch (...) {
                ok = false;
                break;
            }
            if (idx >= shown.size()) {
                ok = false;
                break;
            }
            out += mtext ? text_mtext_escape(shown[idx]) : shown[idx];
            rest = after.substr(end + 2);
        }
        if (ok)
            host_updates.push_back({c.host, c.handle, std::move(out)});
    }
    if (value_updates.empty() && host_updates.empty())
        return {{}, found};
    DxfDocument staged = doc;
    for (const auto &item : value_updates)
        staged.set_field_definition(hex_handle(item.first), item.second);
    std::vector<std::uint64_t> changed;
    for (const auto &item : host_updates) {
        auto host = std::get<0>(item);
        auto container = std::get<1>(item);
        const auto &text = std::get<2>(item);
        staged.for_each<DxfField>([&](auto &f) {
            if (handle_value(f.handle()) != container)
                return;
            auto def = f.definition();
            for (auto &cv : def.child_values)
                if (cv.key == "ACFD_FIELDTEXT_CHECKSUM")
                    cv.value.number = text_field_checksum(text);
            f.set_definition(def);
        });
        std::string old;
        if (const auto *rec = staged.get_record(hex_handle(host))) {
            if (rec->type() == "MTEXT")
                staged.for_each<DxfMText>([&](const auto &m) {
                    if (handle_value(m.handle()) == host)
                        old = m.text();
                });
            else if (rec->type() == "TEXT")
                staged.for_each<DxfText>([&](const auto &t) {
                    if (handle_value(t.handle()) == host)
                        old = t.text();
                });
            else
                staged.for_each<DxfAttribute>([&](const auto &a) {
                    if (handle_value(a.handle()) == host)
                        old = a.text();
                });
        }
        text_set_host_text(staged, host, text);
        if (old != text)
            changed.push_back(host);
    }
    doc = std::move(staged);
    return {std::move(changed), found};
}
} // namespace

void DxfDocument::erase_dictionary_entry(DxfRecord &dictionary, std::string_view key) {
    bool body = false;
    unsigned depth = 0;
    for (std::size_t i = 0; i < dictionary.pairs_.size(); ++i) {
        const auto &p = dictionary.pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (p.value == "}" && depth)
                --depth;
            continue;
        }
        if (depth)
            continue;
        if (p.code == 1001 || p.code == 101)
            break;
        if (p.code == 100)
            body = p.value == "AcDbDictionary";
        if (body && p.code == 3 &&
            detail::symbol_name_key(dictionary.decode_string(p.value)) == detail::symbol_name_key(key)) {
            if (i + 1 >= dictionary.pairs_.size() ||
                (dictionary.pairs_[i + 1].code != 350 && dictionary.pairs_[i + 1].code != 360))
                return;
            dictionary.pairs_.erase(dictionary.pairs_.begin() + static_cast<std::ptrdiff_t>(i),
                                    dictionary.pairs_.begin() + static_cast<std::ptrdiff_t>(i + 2));
            return;
        }
    }
}

void DxfDocument::erase_xdictionary_group(DxfRecord &host) {
    for (std::size_t i = 0; i < host.pairs_.size(); ++i) {
        if (host.pairs_[i].code != 102 || host.pairs_[i].value != "{ACAD_XDICTIONARY")
            continue;
        auto end = i + 1;
        while (end < host.pairs_.size() && !(host.pairs_[end].code == 102 && host.pairs_[end].value == "}"))
            ++end;
        if (end < host.pairs_.size())
            ++end;
        host.pairs_.erase(host.pairs_.begin() + static_cast<std::ptrdiff_t>(i),
                          host.pairs_.begin() + static_cast<std::ptrdiff_t>(end));
        return;
    }
}

bool DxfDocument::dictionary_has_entries(const DxfRecord &dictionary) const {
    bool body = false;
    unsigned depth = 0;
    for (std::size_t i = 0; i < dictionary.pairs_.size(); ++i) {
        const auto &p = dictionary.pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (p.value == "}" && depth)
                --depth;
            continue;
        }
        if (depth)
            continue;
        if (p.code == 100)
            body = p.value == "AcDbDictionary";
        if (body && p.code == 3)
            return true;
    }
    return false;
}

void DxfDocument::erase_object_handles(const std::unordered_set<std::uint64_t> &handles) {
    if (handles.empty())
        return;
    for_each<DxfFieldList>([&](auto &list) {
        auto v = list.definition();
        v.fields.erase(std::remove_if(v.fields.begin(), v.fields.end(),
                                      [&](std::uint64_t h) { return handles.count(h); }),
                       v.fields.end());
        list.set_definition(v);
    });
    std::vector<Record> kept;
    kept.reserve(records_.size());
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto &r = record_at({i, std::nullopt, false});
        if (handles.count(handle_value(r.handle())))
            continue;
        kept.push_back(std::move(records_[i]));
    }
    records_ = std::move(kept);
    rebuild_indexes();
}

std::optional<std::uint64_t> DxfDocument::set_text_field(std::uint64_t host, std::string_view template_code,
                                                         const std::vector<FieldDefinition> &children) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Text fields require R2007+");
    auto kind = text_host_kind(*this, host);
    if (!kind)
        return std::nullopt;
    auto display = text_fill_template(template_code, children, *kind == TextHostKind::MText);
    if (!display)
        return std::nullopt;
    DxfDocument staged = *this;
    staged.remove_text_field(host);
    table_ensure_field_classes(staged);
    auto existing = staged.extension_dictionary_handle(hex_handle(host));
    auto xdict = existing.empty() ? staged.add_extension_dictionary(hex_handle(host)) : handle_value(existing);
    auto field_dict = staged.add_dictionary(hex_handle(xdict), "ACAD_FIELD");
    FieldDefinition container;
    container.evaluator_id = "_text";
    container.code = std::string(template_code);
    container.evaluation_option = 63;
    container.state = *kind == TextHostKind::MText ? 13 : 9;
    container.evaluation_status = 2;
    container.value.flags = 3;
    if (*kind == TextHostKind::Attdef) {
        FieldChildValue flag;
        flag.key = "ACFD_FIELDTEXT_ATTDEF";
        flag.value.type = 1;
        flag.value.number = 1;
        flag.value.flags = 2;
        container.child_values.push_back(std::move(flag));
    }
    FieldChildValue checksum;
    checksum.key = "ACFD_FIELDTEXT_CHECKSUM";
    checksum.value.type = 2;
    checksum.value.number = text_field_checksum(*display);
    checksum.value.flags = 2;
    container.child_values.push_back(std::move(checksum));
    auto id = staged.add_field(hex_handle(field_dict), "TEXT", container);
    for (const auto &child : children)
        add_owned_field_tree(staged, id, child);
    auto list = table_ensure_field_list(staged);
    std::vector<std::uint64_t> registered{id};
    collect_owned_field_handles(staged, id, registered);
    table_register_fields(staged, list, registered);
    if (!text_set_host_text(staged, host, *display))
        return std::nullopt;
    *this = std::move(staged);
    return id;
}

bool DxfDocument::remove_text_field(std::uint64_t host) {
    auto xdict = extension_dictionary_handle(hex_handle(host));
    if (xdict.empty())
        return false;
    std::string field_dict;
    try {
        field_dict = dictionary_entry(xdict, "ACAD_FIELD");
    } catch (...) {
        return false;
    }
    if (field_dict.empty())
        return false;
    auto target = handle_value(field_dict);
    std::unordered_set<std::uint64_t> doomed;
    for_each<DxfField>([&](const auto &f) {
        auto h = handle_value(f.handle());
        if (text_reaches(*this, h, target))
            doomed.insert(h);
    });
    doomed.insert(target);
    DxfDocument staged = *this;
    auto xdict_id = handle_value(xdict);
    auto found = staged.handle_index_.find(xdict_id);
    if (found != staged.handle_index_.end() && !found->second.empty())
        staged.erase_dictionary_entry(const_cast<DxfRecord &>(staged.record_at(found->second.back())), "ACAD_FIELD");
    staged.erase_object_handles(doomed);
    bool empty = false;
    if (auto *dict = staged.get_record(hex_handle(xdict_id)))
        empty = !staged.dictionary_has_entries(*dict);
    if (empty) {
        auto host_found = staged.handle_index_.find(host);
        if (host_found != staged.handle_index_.end() && !host_found->second.empty())
            staged.erase_xdictionary_group(const_cast<DxfRecord &>(staged.record_at(host_found->second.back())));
        staged.for_each<DxfInsert>([&](auto &ins) {
            for (std::size_t i = 0; i < ins.attribute_count(); ++i) {
                auto a = ins.attribute(i);
                if (handle_value(a.handle()) != host)
                    continue;
                staged.erase_xdictionary_group(a);
                ins.set_attribute(i, a);
            }
        });
        staged.erase_object_handles({xdict_id});
    }
    *this = std::move(staged);
    return true;
}

std::pair<std::vector<std::uint64_t>, std::size_t>
DxfDocument::update_fields(const FieldContext &ctx, std::int32_t event, const std::vector<std::uint64_t> *hosts) {
    return text_restamp_fields(*this, ctx, ctx, [event](const FieldDefinition &f) { return (f.evaluation_option & event) != 0; },
                               hosts);
}

std::vector<std::uint64_t> DxfDocument::stamp_plot_fields(const FieldContext &ctx) {
    PlottingContext plot(ctx);
    return text_restamp_fields(*this, plot, ctx, [](const FieldDefinition &f) { return (f.evaluation_option & 4) != 0; },
                               nullptr)
        .first;
}

std::vector<std::uint64_t> DxfDocument::refresh_sheet_set_fields(const FieldContext &ctx) {
    return text_restamp_fields(*this, ctx, ctx,
                               [](const FieldDefinition &f) { return f.evaluator_id.rfind("AcSm", 0) == 0; }, nullptr)
        .first;
}

std::vector<std::uint64_t> DxfDocument::refresh_table_formulas(const FieldContext &ctx, std::uint64_t table) {
    auto def = table_definition_of(*this, table);
    if (!def)
        return {};
    auto name = def->block_name;
    if (name.empty())
        return {};
    std::vector<std::uint64_t> hosts;
    for (const auto &h : entity_handles_in_block(name))
        hosts.push_back(handle_value(h));
    return text_restamp_fields(*this, ctx, ctx, [](const FieldDefinition &f) { return f.evaluator_id == "AcExpr"; },
                               &hosts)
        .first;
}

std::string DxfDocument::format_with_picture(double value, std::string_view picture) const {
    return libopencad::format_with_picture(*this, value, picture);
}

std::string DxfDocument::format_point_with_picture(Point3 point, std::string_view picture) const {
    return libopencad::format_point_with_picture(*this, point, picture);
}

void DxfDocument::remove_fields(const std::vector<std::uint64_t> &doomed) {
    erase_object_handles(std::unordered_set<std::uint64_t>(doomed.begin(), doomed.end()));
}

std::vector<std::uint64_t> DxfDocument::attach_attribute_fields(std::uint64_t insert, const FieldContext &ctx) {
    return attach_attribute_fields_mapped(insert, ctx, [](std::string_view s) { return std::string(s); });
}

std::vector<std::uint64_t> DxfDocument::attach_attribute_fields_mapped(
    std::uint64_t insert, const FieldContext &ctx, const std::function<std::string(std::string_view)> &map) {
    std::optional<InsertDefinition> def;
    struct Attrib {
        std::uint64_t handle = 0;
        std::string tag;
    };
    std::vector<Attrib> attribs;
    for_each<DxfInsert>([&](const auto &ins) {
        if (handle_value(ins.handle()) != insert)
            return;
        def = ins.definition();
        for (std::size_t i = 0; i < ins.attribute_count(); ++i) {
            auto a = ins.attribute(i);
            Attrib item;
            item.handle = handle_value(a.handle());
            item.tag = a.attribute_definition().tag;
            attribs.push_back(std::move(item));
        }
    });
    if (!def)
        return {};
    struct Def {
        std::uint64_t handle = 0;
        std::string tag;
    };
    std::vector<Def> defs;
    for (const auto &hex : entity_handles_in_block(def->block_name)) {
        auto h = handle_value(hex);
        for_each<DxfAttribute>([&](const auto &a) {
            if (handle_value(a.handle()) != h || a.type() != "ATTDEF")
                return;
            Def item;
            item.handle = h;
            item.tag = a.attribute_definition().tag;
            defs.push_back(std::move(item));
        });
    }
    std::vector<std::uint64_t> attached;
    for (const auto &attrib : attribs) {
        std::optional<std::uint64_t> attdef;
        for (const auto &item : defs)
            if (ascii_equal(item.tag, attrib.tag)) {
                attdef = item.handle;
                break;
            }
        if (!attdef)
            continue;
        std::optional<std::uint64_t> container;
        FieldDefinition container_def;
        for_each<DxfField>([&](const auto &f) {
            if (f.definition().evaluator_id != "_text")
                return;
            auto h = handle_value(f.handle());
            auto host = text_host_of_container(*this, h);
            if (host && *host == *attdef) {
                container = h;
                container_def = f.definition();
            }
        });
        if (!container)
            continue;
        std::vector<std::uint64_t> kids;
        for_each<DxfField>([&](const auto &f) {
            if (handle_value(f.owner_handle()) != *container)
                return;
            kids.push_back(handle_value(f.handle()));
        });
        std::sort(kids.begin(), kids.end());
        std::vector<FieldDefinition> copied;
        for (auto kid : kids) {
            auto inlined = inline_code(*this, kid);
            if (!inlined)
                continue;
            auto code = inlined->code;
            auto objects = inlined->objects;
            const std::string needle = "?BlockRefId";
            if (code.find(needle) != std::string::npos) {
                auto repl = "%<\\_ObjIdx " + std::to_string(objects.size()) + ">%";
                std::size_t p = 0;
                while ((p = code.find(needle, p)) != std::string::npos) {
                    code.replace(p, needle.size(), repl);
                    p += repl.size();
                }
                objects.push_back(insert);
            }
            code = map(code);
            auto shown = evaluate_code(*this, code, objects, insert, ctx);
            FieldDefinition child;
            child.evaluator_id = code_evaluator(code);
            child.code = code;
            child.references = std::move(objects);
            child.format = code_format(code);
            child.evaluation_option =
                child.evaluator_id == "AcVar" && code_word(child.code, 1) == "Date" ? 32 : 63;
            child.value.flags = 4;
            if (shown) {
                child.value.type = 4;
                child.value.text = *shown;
                child.value.formatted = *shown;
                child.value_string = *shown;
            }
            copied.push_back(std::move(child));
        }
        if (set_text_field(attrib.handle, container_def.code, copied))
            attached.push_back(attrib.handle);
    }
    return attached;
}
