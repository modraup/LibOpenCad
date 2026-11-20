// SPDX-License-Identifier: MPL-2.0
// acadrust 0c3ae80d src/io/dxf/multiline_attributes.rs: lines/split/add_records.
// Included in DxfVersionConverter; operations affect only its staged document.
static std::string attribute_hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << value;
    return out.str();
}
static std::string attribute_real(double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Nonfinite multiline attribute geometry");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}
static void attribute_unlink_dictionary(DxfRecord &r) {
    for (std::size_t i = 1; i < r.pairs_.size(); ++i) {
        if (r.pairs_[i].code == 100)
            break;
        if (r.pairs_[i].code == 102 && r.pairs_[i].value == "{ACAD_XDICTIONARY") {
            auto end = i + 1;
            unsigned depth = 1;
            for (; end < r.pairs_.size() && depth; ++end)
                if (r.pairs_[end].code == 102) {
                    if (!r.pairs_[end].value.empty() && r.pairs_[end].value.front() == '{')
                        ++depth;
                    else if (r.pairs_[end].value == "}")
                        --depth;
                }
            if (depth)
                throw std::invalid_argument("Unterminated extension dictionary group");
            r.pairs_.erase(r.pairs_.begin() + i, r.pairs_.begin() + end);
            return;
        }
    }
}
struct AttributeSplit {
    std::string first, owner, tag;
    bool definition = false;
    MTextDefinition mtext;
    std::vector<DxfAttribute> lines;
};
AttributeSplit attribute_lines(const DxfAttribute &source) {
    auto value = source.attribute_definition();
    bool body = false;
    for (const auto &p : source.pairs_) {
        if (p.code >= 1000)
            break;
        if (p.code == 101) {
            if (body)
                throw std::invalid_argument("Multiple embedded attribute objects");
            body = true;
            continue;
        }
        if (body && !code_in(p.code, {1,   3,  7,  10, 20, 30, 11, 21, 31, 210, 220,
                                      230, 40, 41, 42, 43, 44, 46, 50, 71, 72,  73}))
            throw std::invalid_argument("Unreviewed embedded attribute field");
    }
    AttributeSplit result;
    result.first = source.handle();
    result.owner = source.owner_handle();
    result.tag = value.tag;
    result.definition = source.type() == "ATTDEF";
    if (!doc_.get_record(result.first))
        throw std::invalid_argument("Multiline attribute requires a non-null identity");
    const auto *owner = doc_.get_record(result.owner);
    if (!owner || owner->type() != (result.definition ? "BLOCK_RECORD" : "INSERT"))
        throw std::invalid_argument("Multiline attribute has no valid block/INSERT owner");
    auto existing = doc_.extension_dictionary_handle(result.first);
    if (!existing.empty()) {
        const auto *dictionary = doc_.get_record(existing);
        if (!dictionary || dictionary->type() != "DICTIONARY" || dictionary->owner_handle() != result.first)
            throw std::invalid_argument("Multiline attribute extension dictionary has invalid ownership");
        if (!doc_.dictionary_entry(existing, "ACAD_MLATT").empty())
            throw std::invalid_argument("Existing ACAD_MLATT conflicts with embedded multiline data");
        const auto roundtrip = doc_.dictionary_entry(existing, "ACAD_XREC_ROUNDTRIP");
        if (!roundtrip.empty()) {
            const auto *r = doc_.get_record(roundtrip);
            if (!r || r->type() != "XRECORD" || r->owner_handle() != existing)
                throw std::invalid_argument("Invalid shared ACAD_XREC_ROUNDTRIP record");
        }
    }
    auto &m = result.mtext;
    if (value.embedded_mtext)
        m = *value.embedded_mtext;
    else {
        m.text = value.text.text;
        m.insertion = value.text.insertion;
        m.height = value.text.height;
        m.rotation = value.text.rotation;
    }
    if (m.normal != Point3{0, 0, 1} || m.extents_width != 0 || m.extents_height != 0)
        issue(Kind::Loss, "mlatt-unserialized-mtext-fields", &source, 101,
              "Upstream ACAD_MLATT omits embedded normal and measured extents");
    const auto text = m.text.empty() ? value.text.text : m.text;
    const auto pitch = m.height * m.line_spacing_factor * 5.0 / 3.0;
    const Point3 step{std::sin(value.text.rotation) * pitch, -std::cos(value.text.rotation) * pitch, 0};
    attribute_real(step.x);
    attribute_real(step.y);
    std::size_t begin = 0;
    do {
        if (result.lines.size() > 32767)
            throw std::invalid_argument("ACAD_MLATT line count exceeds int16 range");
        const auto end = text.find("\\P", begin);
        auto line = source;
        auto definition = value;
        const auto index = result.lines.size();
        if (index) {
            line.set_field("", 5, attribute_hex(doc_.allocate_handle()));
            attribute_unlink_dictionary(line);
            const Point3 offset{step.x * index, step.y * index, 0};
            definition.text.insertion = {value.text.insertion.x + offset.x, value.text.insertion.y + offset.y,
                                         value.text.insertion.z};
            if (value.text.alignment_point)
                definition.text.alignment_point =
                    Point3{value.text.alignment_point->x + offset.x, value.text.alignment_point->y + offset.y,
                           value.text.alignment_point->z};
            if (!result.definition)
                definition.lock_position = true;
        }
        std::ostringstream tag;
        tag << value.tag << '_' << std::setw(3) << std::setfill('0') << index + 1;
        definition.tag = tag.str();
        definition.text.text = text.substr(begin, end == std::string::npos ? end : end - begin);
        definition.multiline = false;
        definition.embedded_mtext.reset();
        line.set_attribute_definition(definition);
        filter(
            line, result.definition ? "AcDbAttributeDefinition" : "AcDbAttribute",
            [](int code) { return code_in(code, {71, 72, 11, 21, 31}); }, Kind::Information,
            "Multiline-only attribute fields moved to ACAD_MLATT");
        result.lines.push_back(std::move(line));
        if (end == std::string::npos)
            break;
        begin = end + 2;
    } while (true);
    return result;
}
void attribute_records(const AttributeSplit &split) {
    auto dictionary = doc_.extension_dictionary_handle(split.first);
    if (dictionary.empty())
        dictionary = attribute_hex(doc_.add_extension_dictionary(split.first));
    std::vector<SymbolProperty> values{
        {70, split.definition ? "4" : "2"}, {70, "0"}, {70, std::to_string(split.lines.size() - 1)}};
    for (std::size_t i = 1; i < split.lines.size(); ++i)
        values.push_back({340, split.lines[i].handle()});
    const auto &m = split.mtext;
    auto real = [&](int code, double v) { values.push_back({code, attribute_real(v)}); };
    auto point = [&](int code, Point3 p) {
        real(code, p.x);
        real(code + 10, p.y);
        real(code + 20, p.z);
    };
    values.insert(values.end(), {{40, "1"}, {2, split.tag}, {1, "Embedded Object"}});
    point(10, m.insertion);
    real(40, m.height);
    real(41, m.rectangle_width);
    real(46, m.rectangle_height.value_or(0));
    values.insert(
        values.end(),
        {{71, std::to_string(m.attachment)}, {72, std::to_string(m.drawing_direction)}, {1, m.text}});
    if (!m.style.empty())
        values.push_back({7, m.style});
    if (m.rotation != 0)
        point(11, {std::cos(m.rotation), std::sin(m.rotation), 0});
    values.push_back({73, std::to_string(m.line_spacing_style)});
    real(44, m.line_spacing_factor);
    auto add_record = [&](const std::string &name, const std::vector<SymbolProperty> &payload) {
        const auto h = attribute_hex(doc_.add_xrecord(dictionary, name, payload));
        auto &record = const_cast<DxfRecord &>(*doc_.get_record(h));
        auto at = std::find_if(record.pairs_.begin(), record.pairs_.end(),
                               [](const auto &p) { return p.code == 100; });
        record.pairs_.insert(at, {{102, "{ACAD_REACTORS"}, {330, dictionary}, {102, "}"}});
    };
    add_record("ACAD_MLATT", values);
    const auto roundtrip = doc_.dictionary_entry(dictionary, "ACAD_XREC_ROUNDTRIP");
    if (roundtrip.empty())
        add_record("ACAD_XREC_ROUNDTRIP", {{102, "ACAD_MATT_HEIGHT"}, {140, attribute_real(m.height)}});
    else {
        auto &r = const_cast<DxfRecord &>(*doc_.get_record(roundtrip));
        auto begin = std::find_if(r.pairs_.begin(), r.pairs_.end(),
                                  [](const auto &p) { return p.code == 100 && p.value == "AcDbXrecord"; });
        if (begin == r.pairs_.end())
            throw std::invalid_argument("Missing shared XRECORD subclass");
        auto marker = std::find_if(begin, r.pairs_.end(), [&](const auto &p) {
            return p.code == 102 && r.decode_string(p.value) == "ACAD_MATT_HEIGHT";
        });
        if (marker != r.pairs_.end()) {
            auto end = marker + 1;
            if (end != r.pairs_.end() && end->code == 140)
                ++end;
            r.pairs_.erase(marker, end);
        }
        r.pairs_.push_back({102, r.encode_string("ACAD_MATT_HEIGHT")});
        r.pairs_.push_back({140, r.encode_number(m.height)});
    }
    for (std::size_t i = 1; i < split.lines.size(); ++i)
        doc_.add_extension_dictionary(split.lines[i].handle());
    issue(Kind::Information, "multiline-attribute-split", &split.lines.front(), 101,
          "Split into ordered attributes with ACAD_MLATT and shared height round-trip data");
}
void split_multiline_attributes() {
    if (target() < V::AC1015 || target() >= V::AC1032)
        return;
    std::vector<DxfAttribute> sources;
    std::map<std::string, std::set<std::u32string>> tags;
    // As upstream, process INSERT attributes before standalone definitions.
    doc_.for_each<DxfInsert>([&](const auto &insert) {
        for (std::size_t i = 0; i < insert.attribute_count(); ++i) {
            auto a = insert.attribute(i);
            auto value = a.attribute_definition();
            tags[a.owner_handle()].insert(detail::symbol_name_key(value.tag));
            if (value.multiline || value.embedded_mtext)
                sources.push_back(std::move(a));
        }
    });
    doc_.for_each<DxfAttribute>([&](const auto &a) {
        const auto value = a.attribute_definition();
        tags[a.owner_handle()].insert(detail::symbol_name_key(value.tag));
        if (value.multiline || value.embedded_mtext)
            sources.push_back(a);
    });
    for (const auto &source : sources) {
        try {
            auto split = attribute_lines(source);
            for (const auto &line : split.lines)
                if (!tags[split.owner]
                         .insert(detail::symbol_name_key(line.attribute_definition().tag))
                         .second)
                    throw std::invalid_argument("Generated multiline attribute tag conflicts in its owner");
            const auto location = doc_.handle_index_.at(std::stoull(split.first, nullptr, 16)).front();
            if (location.vertex) {
                auto *insert = std::get_if<DxfInsert>(&doc_.records_[location.record]);
                if (!insert)
                    throw std::invalid_argument("ATTRIB is not an INSERT child");
                auto pos = insert->children_.begin() + *location.vertex;
                pos = insert->children_.erase(pos);
                std::vector<DxfRecord> lines(split.lines.begin(), split.lines.end());
                insert->children_.insert(pos, lines.begin(), lines.end());
            } else {
                std::vector<DxfDocument::Record> lines;
                for (auto &line : split.lines)
                    lines.emplace_back(line);
                auto pos = doc_.records_.begin() + location.record;
                pos = doc_.records_.erase(pos);
                doc_.records_.insert(pos, lines.begin(), lines.end());
            }
            doc_.rebuild_indexes();
            attribute_records(split);
            ++report_.changed_records;
        } catch (const std::exception &error) {
            issue(Kind::Error, "multiline-attribute-split-failed", &source, 101, error.what());
        }
    }
}
// Upgrade ACAD_MLATT back to an embedded R2018 attribute. Unlike Rust's
// permissive reader merge, conversion refuses ambiguous or foreign references.
std::vector<std::pair<std::string, std::string>> attribute_dictionary_entries(const DxfRecord &r) {
    std::vector<std::pair<std::string, std::string>> result;
    std::string key;
    bool body = false, tail = false;
    unsigned depth = 0;
    for (const auto &p : r.pairs_) {
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
            tail = true;
        if (p.code == 100)
            body = p.value == "AcDbDictionary";
        if (!body || tail)
            continue;
        if (p.code == 3)
            key = r.decode_string(p.value);
        if ((p.code == 350 || p.code == 360) && !key.empty()) {
            result.emplace_back(key, p.value);
            key.clear();
        }
    }
    return result;
}
void attribute_remove_dictionary_entry(DxfRecord &r, const std::string &key) {
    bool body = false;
    unsigned depth = 0;
    for (std::size_t i = 0; i < r.pairs_.size(); ++i) {
        const auto &p = r.pairs_[i];
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
        if (body && p.code == 3 && r.decode_string(p.value) == key) {
            if (i + 1 == r.pairs_.size() || !code_in(r.pairs_[i + 1].code, {350, 360}))
                throw std::invalid_argument("Malformed MLATT dictionary entry");
            r.pairs_.erase(r.pairs_.begin() + i, r.pairs_.begin() + i + 2);
            return;
        }
    }
}
void merge_multiline_attribute(const DxfAttribute &source) {
    const auto dictionary = doc_.extension_dictionary_handle(source.handle());
    if (dictionary.empty())
        return;
    const auto handle = doc_.dictionary_entry(dictionary, "ACAD_MLATT");
    if (handle.empty())
        return;
    const auto *dict = doc_.get_record(dictionary);
    const auto *record = doc_.get_record(handle);
    if (!dict || dict->owner_handle() != source.handle() || !record || record->type() != "XRECORD" ||
        record->owner_handle() != dictionary)
        throw std::invalid_argument("Invalid ACAD_MLATT ownership");
    const auto &pairs = record->pairs_;
    auto start = std::find_if(pairs.begin(), pairs.end(),
                              [](const auto &p) { return p.code == 100 && p.value == "AcDbXrecord"; });
    auto embedded = std::find_if(start, pairs.end(), [&](const auto &p) {
        return p.code == 1 && record->decode_string(p.value) == "Embedded Object";
    });
    if (embedded == pairs.end())
        throw std::invalid_argument("ACAD_MLATT has no embedded MTEXT body");
    std::vector<int> flags;
    std::vector<std::string> others;
    std::string tag;
    for (auto p = start; p != embedded; ++p) {
        if (!code_in(p->code, {100, 280, 70, 340, 40, 2}))
            throw std::invalid_argument("Unreviewed ACAD_MLATT header field");
        if (p->code == 70)
            flags.push_back(std::stoi(p->value));
        if (p->code == 340)
            others.push_back(p->value);
        if (p->code == 2)
            tag = record->decode_string(p->value);
    }
    if (flags.size() != 3 || flags[0] != (source.type() == "ATTDEF" ? 4 : 2) || flags[1] != 0 ||
        flags[2] < 0 || std::size_t(flags[2]) != others.size() || tag.empty())
        throw std::invalid_argument("Malformed ACAD_MLATT type, count or tag");
    DxfMText embedded_text;
    embedded_text.code_page_ = record->code_page_;
    embedded_text.version_ = V::AC1032;
    embedded_text.pairs_ = {{0, "MTEXT"}, {100, "AcDbMText"}};
    // Reject unknown payload fields instead of deleting data during cleanup.
    for (auto p = embedded + 1; p != pairs.end(); ++p) {
        if (!code_in(p->code,
                     {1, 3, 7, 10, 20, 30, 11, 21, 31, 210, 220, 230, 40, 41, 42, 43, 44, 46, 71, 72, 73}))
            throw std::invalid_argument("Unreviewed ACAD_MLATT embedded field");
        embedded_text.pairs_.push_back(*p);
    }
    auto m = embedded_text.definition();
    std::set<std::uint64_t> drop{std::stoull(handle, nullptr, 16)};
    auto first = source;
    auto value = first.attribute_definition();
    std::string fallback = value.text.text;
    const auto canonical = [](const std::string &h) { return std::stoull(h, nullptr, 16); };
    std::set<std::uint64_t> seen{canonical(source.handle())};
    for (std::size_t i = 0; i < others.size(); ++i) {
        const auto *other = doc_.get_record(others[i]);
        if (!seen.insert(canonical(others[i])).second || !other || other->type() != source.type() ||
            canonical(other->owner_handle()) != canonical(source.owner_handle()))
            throw std::invalid_argument("ACAD_MLATT references a duplicate, missing or foreign line");
        DxfAttribute line;
        static_cast<DxfRecord &>(line) = *other;
        line.version_ = target();
        auto definition = line.attribute_definition();
        std::ostringstream expected;
        expected << tag << '_' << std::setw(3) << std::setfill('0') << i + 2;
        if (definition.tag != expected.str() || definition.multiline)
            throw std::invalid_argument("ACAD_MLATT references an unrelated attribute tag");
        fallback += "\\P" + definition.text.text;
        auto child_dict = doc_.extension_dictionary_handle(other->handle());
        if (!child_dict.empty()) {
            const auto *d = doc_.get_record(child_dict);
            if (!d || d->type() != "DICTIONARY" ||
                canonical(d->owner_handle()) != canonical(other->handle()) ||
                !attribute_dictionary_entries(*d).empty() || !d->extended_data().empty() ||
                std::any_of(d->pairs_.begin(), d->pairs_.end(),
                            [](const auto &p) { return p.code == 102 || p.code == 101; }))
                throw std::invalid_argument(
                    "Additional MLATT line has application data that cannot be discarded");
            drop.insert(canonical(child_dict));
        }
        drop.insert(canonical(other->handle()));
    }
    if (m.text.empty())
        m.text = std::move(fallback);
    value.tag = tag;
    value.multiline = true;
    value.text.text = m.text;
    value.embedded_mtext = m;
    first.version_ = target();
    first.set_attribute_definition(value);
    auto &dictionary_record = const_cast<DxfRecord &>(*doc_.get_record(dictionary));
    attribute_remove_dictionary_entry(dictionary_record, "ACAD_MLATT");
    auto roundtrip = doc_.dictionary_entry(dictionary, "ACAD_XREC_ROUNDTRIP");
    if (!roundtrip.empty()) {
        auto *ptr = doc_.get_record(roundtrip);
        if (!ptr || ptr->type() != "XRECORD" || ptr->owner_handle() != dictionary)
            throw std::invalid_argument("Invalid MLATT height record");
        auto &r = const_cast<DxfRecord &>(*ptr);
        auto begin = std::find_if(r.pairs_.begin(), r.pairs_.end(),
                                  [](const auto &p) { return p.code == 100 && p.value == "AcDbXrecord"; });
        if (begin == r.pairs_.end())
            throw std::invalid_argument("Invalid MLATT height record body");
        auto marker = std::find_if(begin, r.pairs_.end(), [&](const auto &p) {
            return p.code == 102 && r.decode_string(p.value) == "ACAD_MATT_HEIGHT";
        });
        if (marker != r.pairs_.end()) {
            auto end = marker + 1;
            if (end != r.pairs_.end() && end->code == 140)
                ++end;
            r.pairs_.erase(marker, end);
        }
        begin = std::find_if(r.pairs_.begin(), r.pairs_.end(),
                             [](const auto &p) { return p.code == 100 && p.value == "AcDbXrecord"; });
        auto payload_begin = begin + 1;
        if (payload_begin != r.pairs_.end() && payload_begin->code == 280)
            ++payload_begin; // cloning flag only
        const bool payload = payload_begin != r.pairs_.end();
        if (!payload) {
            drop.insert(canonical(roundtrip));
            attribute_remove_dictionary_entry(dictionary_record, "ACAD_XREC_ROUNDTRIP");
        }
    }
    const auto private_dictionary_data = [](const DxfRecord &r) {
        return std::any_of(r.pairs_.begin(), r.pairs_.end(),
                           [](const auto &p) { return p.code == 102 || p.code == 101 || p.code >= 1000; });
    };
    if (attribute_dictionary_entries(dictionary_record).empty() &&
        !private_dictionary_data(dictionary_record)) {
        drop.insert(canonical(dictionary));
        attribute_unlink_dictionary(first);
    }
    const auto loc = doc_.handle_index_.at(canonical(source.handle())).front();
    if (loc.vertex)
        sequence(doc_.records_[loc.record])->children_[*loc.vertex] = static_cast<DxfRecord &>(first);
    else
        doc_.records_[loc.record] = first;
    // Fail rather than deleting references owned by neighboring application data.
    auto references = [&](const DxfRecord &r) {
        if (!r.handle().empty() && drop.count(canonical(r.handle())))
            return;
        for (const auto &p : r.pairs_)
            if ((p.code >= 330 && p.code <= 369) || p.code == 1005)
                if (drop.count(canonical(p.value)))
                    throw std::invalid_argument("External reference prevents ACAD_MLATT cleanup");
    };
    for (const auto &entry : doc_.records_) {
        std::visit([&](const DxfRecord &r) { references(r); }, entry);
        std::visit(
            [&](const auto &r) {
                if constexpr (std::is_base_of_v<DxfCompoundEntity, std::decay_t<decltype(r)>>) {
                    for (const auto &c : r.children_)
                        references(c);
                    if (r.seqend_)
                        references(*r.seqend_);
                }
            },
            entry);
    }
    for (auto &entry : doc_.records_)
        if (auto *group = sequence(entry))
            group->children_.erase(std::remove_if(group->children_.begin(), group->children_.end(),
                                                  [&](const auto &r) {
                                                      return !r.handle().empty() &&
                                                             drop.count(canonical(r.handle()));
                                                  }),
                                   group->children_.end());
    doc_.records_.erase(std::remove_if(doc_.records_.begin(), doc_.records_.end(),
                                       [&](const auto &entry) {
                                           return std::visit(
                                               [&](const DxfRecord &r) {
                                                   return !r.handle().empty() &&
                                                          drop.count(canonical(r.handle()));
                                               },
                                               entry);
                                       }),
                        doc_.records_.end());
    doc_.rebuild_indexes();
    ++report_.changed_records;
    issue(Kind::Information, "multiline-attribute-merged", &source, 101,
          "Restored embedded MTEXT and removed only owned split lines and MLATT metadata");
}
void merge_multiline_attributes() {
    if (target() < V::AC1032)
        return;
    std::vector<DxfAttribute> sources;
    doc_.for_each<DxfAttribute>([&](const auto &a) { sources.push_back(a); });
    doc_.for_each<DxfInsert>([&](const auto &e) {
        for (std::size_t i = 0; i < e.attribute_count(); ++i)
            sources.push_back(e.attribute(i));
    });
    for (const auto &source : sources) {
        if (!doc_.get_record(source.handle()))
            continue;
        try {
            merge_multiline_attribute(source);
        } catch (const std::exception &error) {
            issue(Kind::Error, "multiline-attribute-merge-failed", &source, 101, error.what());
        }
    }
}
