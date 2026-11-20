// SPDX-License-Identifier: MPL-2.0
// DXF reference traversal follows acadrust common data, XRecordValueType and audit.rs.
std::vector<std::pair<std::size_t,bool>> DxfDocument::reference_positions(const DxfRecord& r) const {
    std::vector<std::pair<std::size_t,bool>> result;
    bool payload=false,xdata=false,opaque=false;int depth=0;bool common=false;
    for(std::size_t i=1;i<r.pairs_.size();++i) {
        const auto& p=r.pairs_[i];
        if(!depth && p.code==100 && r.type()=="XRECORD" && p.value=="AcDbXrecord") payload=true;
        if(payload) {if(pointer_code(p.code))result.push_back({i,true});continue;}
        if(!depth && p.code==1001){xdata=true;opaque=false;}
        if(!depth && p.code==101)opaque=true;
        if(p.code==102) {
            if(!p.value.empty() && p.value.front()=='{') {
                if(depth==0) {
                    common=p.value=="{ACAD_REACTORS" || p.value=="{ACAD_XDICTIONARY";
                    if(common) {
                        const bool dictionary=p.value=="{ACAD_XDICTIONARY";
                        auto end=i+1;while(end<r.pairs_.size() && r.pairs_[end].code==(dictionary?360:330))++end;
                        common=end<r.pairs_.size() && r.pairs_[end].code==102 && r.pairs_[end].value=="}" && (!dictionary || end==i+2);
                    }
                }
                ++depth;
            } else if(p.value=="}" && depth) {--depth;if(!depth)common=false;}
            continue;
        }
        if(pointer_code(p.code) || (p.code>=320 && p.code<=329))result.push_back({i,!opaque && (!xdata || p.code==1005) && (!depth || (depth==1 && common))});
    }return result;
}
std::vector<ReferenceIssue> DxfDocument::reference_issues() const {
    std::vector<ReferenceIssue> result;
    const SymbolTable tables[]={SymbolTable::Layer,SymbolTable::LineType,SymbolTable::TextStyle,SymbolTable::DimStyle,
        SymbolTable::AppId,SymbolTable::Ucs,SymbolTable::View,SymbolTable::VPort,SymbolTable::BlockRecord};
    auto inspect=[&](const DxfRecord& r,bool entity_owner,std::optional<std::uint64_t> parent=std::nullopt) {
        if(entity_owner && (r.owner_handle().empty() || parse_handle(r.owner_handle())==std::optional<std::uint64_t>{0}))
            result.push_back({r.handle(),330,r.owner_handle(),ReferenceStatus::MissingOwner,std::nullopt});
        for(auto [at,editable]:reference_positions(r)) {
            const auto& p=r.pairs_[at];const auto value=parse_handle(p.value);
            std::optional<ReferenceStatus> error;
            if(!value)error=ReferenceStatus::Invalid;
            else if(*value) {const auto found=handle_index_.find(*value);
                if(found==handle_index_.end())error=ReferenceStatus::Dangling;
                else if(found->second.size()!=1)error=ReferenceStatus::Ambiguous;}
            if(error)result.push_back({r.handle(),p.code,p.value,*error,std::nullopt});
        }
        const auto owner_value=parse_handle(r.owner_handle());
        if(owner_value && *owner_value && records_with_handle(r.owner_handle()).size()==1) {
            const auto* owner=get_record(r.owner_handle());bool invalid=false;
            if(parent)invalid=*owner_value!=*parent;
            else if(entity_owner || r.type()=="BLOCK" || r.type()=="ENDBLK")invalid=owner->type()!="BLOCK_RECORD";
            else for(auto table:tables)if(r.type()==symbol_type(table))
                invalid=owner->type()!="TABLE" || trim(owner->field("",2))!=symbol_type(table);
            if(invalid)result.push_back({r.handle(),330,r.owner_handle(),ReferenceStatus::InvalidOwner,std::nullopt});
        }
        if(!r.handle().empty() && ownership_chain(r.handle()).status==OwnershipStatus::Cycle)
            result.push_back({r.handle(),330,r.owner_handle(),ReferenceStatus::OwnerCycle,std::nullopt});
        for(auto table:tables)for(auto at:symbol_references(r,table)) {
            const auto& p=r.pairs_[at];const auto name=r.decode_string(p.value);if(name.empty())continue;
            const auto found=symbol_locations(table,name);
            if(found.empty() || found.size()>1)result.push_back({r.handle(),p.code,name,
                found.empty()?ReferenceStatus::Dangling:ReferenceStatus::Ambiguous,table});
        }
    };
    for(std::size_t i=0;i<records_.size();++i) {
        const auto& r=records_[i];const bool entity=std::find(entity_records_.begin(),entity_records_.end(),i)!=entity_records_.end();
        std::visit([&](const DxfRecord& value){inspect(value,entity);},r);
        if(const auto* c=sequence(r)){for(const auto& v:c->children_)inspect(v,true,handle_value(c->handle()));if(c->seqend_)inspect(*c->seqend_,true,handle_value(c->handle()));}
    }
    // Match upstream audit's owner back-links. The first OBJECTS dictionary
    // is the named-object root; additional null-owner objects are not roots.
    std::string section;
    const DxfRecord* root = nullptr;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& r = record_at({i, std::nullopt, false});
        if (r.type() == "SECTION") section = r.field("", 2);
        else if (r.type() == "ENDSEC") section.clear();
        else if (section == "OBJECTS" && r.type() == "DICTIONARY" && !root) root = &r;
        if (section != "OBJECTS" || r.handle().empty()) continue;
        const auto owner_id = parse_handle(r.owner_handle());
        if ((!owner_id || !*owner_id) && &r != root) {
            result.push_back({r.handle(),330,r.owner_handle(),ReferenceStatus::MissingOwner,std::nullopt});
            continue;
        }
        const auto* owner = get_record(r.owner_handle());
        if (!owner || !owner_id || !*owner_id) continue;
        bool checked = false, listed = false;
        const auto id = parse_handle(r.handle());
        if (owner->type() == "DICTIONARY" || owner->type() == "ACDBDICTIONARYWDFLT") {
            checked = true;
            const auto fields = owner->subclass_fields("AcDbDictionary");
            for (std::size_t n = 1; n < fields.size(); ++n)
                if (fields[n-1].code == 3 && (fields[n].code == 350 || fields[n].code == 360)
                    && parse_handle(fields[n].value) == id) listed = true;
        } else if (owner->type() == "FIELD") {
            checked = true;
            for (const auto& p : owner->subclass_fields("AcDbField"))
                if (p.code == 360 && parse_handle(p.value) == id) listed = true;
        }
        if (checked && !listed)
            result.push_back({r.handle(),330,r.owner_handle(),ReferenceStatus::UnlistedChild,std::nullopt});
    }
    std::unordered_set<std::uint64_t> attdef_owners;
    for_each<DxfAttribute>([&](const auto& e) {
        if (e.type() == "ATTDEF") attdef_owners.insert(handle_value(e.owner_handle()));
    });
    for_each<DxfBlock>([&](const auto& b) {
        if ((b.flags() & 2) && !attdef_owners.count(handle_value(b.owner_handle())))
            result.push_back({b.handle(),70,b.owner_handle(),ReferenceStatus::StaleAttributeFlag,SymbolTable::BlockRecord});
    });
    return result;
}
std::size_t DxfDocument::repair_attribute_definition_flags() {
    std::unordered_set<std::uint64_t> attdef_owners;
    for_each<DxfAttribute>([&](const auto& e) {
        if (e.type() == "ATTDEF") attdef_owners.insert(handle_value(e.owner_handle()));
    });
    // Stage by record position, not handle: malformed duplicate identities
    // must not cause a different block's flags to be cleared.
    DxfDocument staged = *this;
    std::size_t count = 0;
    staged.for_each<DxfBlock>([&](auto& b) {
        if ((b.flags() & 2) && !attdef_owners.count(handle_value(b.owner_handle()))) {
            b.set_flags(b.flags() & ~2);
            ++count;
        }
    });
    if (count) *this = std::move(staged);
    return count;
}
std::size_t DxfDocument::repair_references(const std::vector<HandleReplacement>& replacements) {
    auto key=[](std::string_view s){const auto n=parse_handle(s);return n?hex_handle(*n):std::string(trim(s));};
    std::unordered_map<std::string,std::string> remap;
    for(const auto& replacement:replacements) {
        const auto target=parse_handle(replacement.to);
        if(!target || !*target || records_with_handle(replacement.to).size()!=1)
            throw std::invalid_argument("Repair target must be a unique existing handle");
        if(key(replacement.from)=="0" || !remap.emplace(key(replacement.from),hex_handle(*target)).second)
            throw std::invalid_argument("Duplicate or null reference repair source");
    }
    DxfDocument staged=*this;std::size_t changed=0;
    auto rewrite=[&](DxfRecord& r){for(auto [at,editable]:staged.reference_positions(r)) {
        auto& p=r.pairs_[at];const auto found=remap.find(key(p.value));
        if(found==remap.end() || found->second==key(p.value))continue;
        if(!editable)throw std::invalid_argument("Cannot repair an opaque application reference");
        p.value=found->second;++changed;
    }};
    for(auto& r:staged.records_) {std::visit(rewrite,r);if(auto* c=sequence(r)){
        for(auto& v:c->children_)rewrite(v);if(c->seqend_)rewrite(*c->seqend_);}}
    staged.rebuild_indexes();
    // Do not introduce new ownership cycles while repairing an otherwise valid relationship.
    for(const auto& issue:staged.reference_issues()) if(issue.status==ReferenceStatus::OwnerCycle &&
        ownership_chain(issue.source_handle).status!=OwnershipStatus::Cycle)
        throw std::invalid_argument("Reference repair would introduce an ownership cycle");
    const auto previous=reference_issues();
    for(const auto& issue:staged.reference_issues())if(issue.status==ReferenceStatus::InvalidOwner &&
        std::none_of(previous.begin(),previous.end(),[&](const auto& old){return old.source_handle==issue.source_handle && old.status==ReferenceStatus::InvalidOwner;}))
        throw std::invalid_argument("Reference repair would introduce invalid owner type or child membership");
    *this=std::move(staged);return changed;
}

// Caller stages the document before inserting objects or changing dictionary links.
std::uint64_t DxfDocument::insert_object(DxfRecord object) {
    const auto id=allocate_handle();object.set_field("",5,hex_handle(id));
    std::string section;std::optional<std::size_t> position;std::size_t eof=records_.size();
    for(std::size_t i=0;i<records_.size();++i) {
        const auto& r=record_at({i,std::nullopt,false});
        if(r.type()=="SECTION")section=r.field("",2);
        if(r.type()=="ENDSEC") {if(section=="OBJECTS"){if(position)throw std::invalid_argument("Multiple OBJECTS sections");position=i;}section.clear();}
        if(r.type()=="EOF")eof=i;
    }
    Record entry=object;
    for(const auto& c:classes())if(c.name==object.type()&&!c.is_entity&&object.type()!="XRECORD"&&object.type()!="ACDBDICTIONARYWDFLT"&&object.type()!="ACDBPLACEHOLDER"&&object.type()!="PLOTSETTINGS"){DxfSemanticObject value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);break;}

    if(object.type()=="GROUP" || object.type()=="SCALE" || object.type()=="SORTENTSTABLE" || object.type()=="SPATIAL_FILTER" || object.type()=="DICTIONARYVAR" || object.type()=="DBCOLOR" || object.type()=="WIPEOUTVARIABLES"){DxfAuxiliaryObject value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type() == "BREAKDATA" || object.type() == "BREAKPOINTREF" || object.type() == "ACDSRECORD" || object.type() == "ACDSSCHEMA" || object.type() == "DUMMY" || object.type() == "IDBUFFER" || object.type() == "INDEX" || object.type() == "LAYER_INDEX" || object.type() == "LONG_TRANSACTION" || object.type() == "OBJECT_PTR" || object.type() == "PARTIAL_VIEWING_FILTER" || object.type() == "ACDBPOINTCLOUDDEF" || object.type() == "ACDBPOINTCLOUDDEF_EX" || object.type() == "ACDBPOINTCLOUDDEF_REACTOR" || object.type() == "ACDBPOINTCLOUDDEF_REACTOR_EX" || object.type() == "ACDBPOINTCLOUDCOLORMAP" || object.type() == "NAVISWORKSMODELDEF" || object.type() == "CONTEXTDATAMANAGER"){DxfDataObject value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}

    if(known_object_context(object.type())){DxfObjectContext value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}

    if(object.type()=="VISUALSTYLE" || object.type()=="GEODATA"){DxfDrawingObject value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(is_class_object_name(object.type())){DxfClassObject value;value.version_=version_;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="ACAD_PROXY_OBJECT"){DxfProxyObject value;value.version_=version_;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="FIELD"){DxfField value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="FIELDLIST"){DxfFieldList value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="MATERIAL"){DxfMaterial value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="IMAGEDEF"){DxfImageDefinition value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="IMAGEDEF_REACTOR"){DxfImageReactor value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="RASTERVARIABLES"){DxfRasterVariables value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="PDFDEFINITION" || object.type()=="DWFDEFINITION" || object.type()=="DGNDEFINITION"){DxfUnderlayDefinition value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="CELLSTYLEMAP"){DxfCellStyleMap value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="TABLEGEOMETRY"){DxfTableGeometry value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="TABLECONTENT" || object.type()=="TABLETEMPLATE"){DxfTableContent value;static_cast<DxfRecord&>(value)=object;entry=std::move(value);}
    if(object.type()=="TABLESTYLE"){DxfTableStyle style;static_cast<DxfRecord&>(style)=object;entry=std::move(style);}
    if(object.type()=="MLEADERSTYLE"){DxfMultiLeaderStyle style;static_cast<DxfRecord&>(style)=object;entry=std::move(style);}
    if(object.type()=="MLINESTYLE"){DxfMLineStyle style;static_cast<DxfRecord&>(style)=object;entry=std::move(style);}
    if(object.type()=="LAYOUT"){DxfLayout layout;static_cast<DxfRecord&>(layout)=std::move(object);entry=std::move(layout);}
    if(position)records_.insert(records_.begin()+*position,std::move(entry));
    else {
        auto start=make_record("SECTION");start.set_field("",2,"OBJECTS");
        std::vector<Record> added{std::move(start),std::move(entry),make_record("ENDSEC")};
        records_.insert(records_.begin()+eof,added.begin(),added.end());
    }
    rebuild_indexes();return id;
}
std::uint64_t DxfDocument::add_extension_dictionary(std::string_view owner_handle) {
    const auto found=handle_index_.find(handle_value(owner_handle));
    if(found==handle_index_.end() || found->second.size()!=1)throw std::invalid_argument("Extension owner must resolve uniquely");
    const auto location=found->second.front();const auto& owner=record_at(location);
    std::size_t insertion=owner.pairs_.size();unsigned depth=0;
    for(std::size_t i=1;i<owner.pairs_.size();++i) {
        const auto& p=owner.pairs_[i];
        if(!depth && (p.code==100 || p.code==1001 || p.code==101)){insertion=i;break;}
        if(p.code!=102)continue;
        if(!depth && p.value=="{ACAD_XDICTIONARY")throw std::invalid_argument("Extension dictionary already exists");
        if(!p.value.empty() && p.value.front()=='{')++depth;
        else if(p.value=="}") {
            if(!depth)throw std::invalid_argument("Unbalanced application group");
            --depth;
        }
    }
    if(depth)throw std::invalid_argument("Unterminated application group");
    DxfDocument staged=*this;
    auto dict=staged.make_record("DICTIONARY");dict.set_field("",330,owner.handle());
    dict.pairs_.push_back({100,"AcDbDictionary"});dict.set_short_field("AcDbDictionary",280,1);dict.set_short_field("AcDbDictionary",281,1);
    // The owner can be a VERTEX/SEQEND or an existing OBJECTS record. Re-resolve after insertion.
    const auto id=staged.insert_object(std::move(dict));
    const auto loc=staged.handle_index_.at(handle_value(owner_handle)).front();
    auto& target=const_cast<DxfRecord&>(staged.record_at(loc));
    target.pairs_.insert(target.pairs_.begin()+insertion,{{102,"{ACAD_XDICTIONARY"},{360,hex_handle(id)},{102,"}"}});
    staged.rebuild_indexes();*this=std::move(staged);return id;
}
std::uint64_t DxfDocument::add_dictionary_object(std::string_view parent,std::string name,DxfRecord object) {
    DxfDocument staged=*this;
    const auto id=staged.add_dictionary_object_impl(parent,std::move(name),std::move(object));
    *this=std::move(staged);return id;
}
std::uint64_t DxfDocument::add_dictionary_object_impl(std::string_view parent,std::string name,DxfRecord object) {
    const auto found=handle_index_.find(handle_value(parent));
    if(found==handle_index_.end() || found->second.size()!=1)throw std::invalid_argument("Parent dictionary must resolve uniquely");
    const auto& dict=record_at(found->second.front());
    if(dict.type()!="DICTIONARY" || name.empty())throw std::invalid_argument("Invalid dictionary parent or entry name");
    const auto key=detail::symbol_name_key(name);
    const auto insertion=dict.locate_if("AcDbDictionary",[&](std::size_t at,int code){
        if(code==3 && detail::symbol_name_key(dict.decode_string(dict.pairs_[at].value))==key)
            throw std::invalid_argument("Dictionary entry already exists");
        return false;
    }).index;
    if(insertion==std::string_view::npos)throw std::invalid_argument("Missing dictionary subclass");
    unsigned depth=0;
    for(std::size_t i=1;i<insertion;++i) {
        const auto& p=dict.pairs_[i];if(p.code!=102)continue;
        if(!p.value.empty() && p.value.front()=='{')++depth;
        else if(p.value=="}") {
            if(!depth)throw std::invalid_argument("Unbalanced dictionary application group");
            --depth;
        }
    }
    if(depth)throw std::invalid_argument("Unterminated dictionary application group");
    object.set_field("",330,dict.handle());
    const auto id=insert_object(std::move(object));
    const auto loc=handle_index_.at(handle_value(parent)).front();
    auto& target=const_cast<DxfRecord&>(record_at(loc));
    // Stay in the dictionary subclass, before XDATA or an embedded object tail.
    target.pairs_.insert(target.pairs_.begin()+insertion,{{3,target.encode_string(name)},{360,hex_handle(id)}});
    rebuild_indexes();return id;
}
std::uint64_t DxfDocument::add_dictionary(std::string_view parent,std::string name) {
    DxfDocument staged=*this;auto dict=staged.make_record("DICTIONARY");
    dict.pairs_.push_back({100,"AcDbDictionary"});dict.set_short_field("AcDbDictionary",280,1);dict.set_short_field("AcDbDictionary",281,1);
    const auto id=staged.add_dictionary_object(parent,std::move(name),std::move(dict));*this=std::move(staged);return id;
}
std::uint64_t DxfDocument::add_xrecord(std::string_view parent,std::string name,const std::vector<SymbolProperty>& values) {
    DxfDocument staged=*this;auto record=staged.make_record("XRECORD");
    record.pairs_.push_back({100,"AcDbXrecord"});record.set_short_field("AcDbXrecord",280,0);
    for(const auto& p:values) {
        if(p.code<=0 || p.code>1071)throw std::invalid_argument("Invalid XRECORD value code");
        auto bytes=detail::binary_value_from_text(p.code,p.value);
        if(pointer_code(p.code) && (!parse_handle(p.value) || (handle_value(p.value) && records_with_handle(p.value).size()!=1)))
            throw std::invalid_argument("XRECORD reference target must resolve uniquely");
        record.pairs_.push_back({p.code,detail::is_string_value(p.code)?record.encode_string(p.value):(binary_?std::move(bytes):p.value)});
    }
    const auto id=staged.add_dictionary_object(parent,std::move(name),std::move(record));*this=std::move(staged);return id;
}

std::size_t DxfDocument::repair_symbol_references(SymbolTable table,std::string_view old_name,std::string_view target_name) {
    const auto matches=symbol_locations(table,target_name);
    if(matches.size()!=1)throw std::invalid_argument("Symbol repair target must resolve uniquely");
    const auto canonical=record_at({matches.front(),std::nullopt,false}).text_field(symbol_subclass(table),2);
    const auto key=detail::symbol_name_key(old_name);if(key.empty())throw std::invalid_argument("Empty symbol reference source");
    DxfDocument staged=*this;std::size_t changed=0;
    auto rewrite=[&](DxfRecord& r){for(auto at:staged.symbol_references(r,table)){
        auto& p=r.pairs_[at];if(detail::symbol_name_key(r.decode_string(p.value))==key && r.decode_string(p.value)!=canonical){p.value=r.encode_string(canonical);++changed;}
    }};
    for(auto& r:staged.records_) {std::visit(rewrite,r);if(auto* c=sequence(r)){
        for(auto& v:c->children_)rewrite(v);if(c->seqend_)rewrite(*c->seqend_);}}
    staged.rebuild_indexes();*this=std::move(staged);return changed;
}

std::size_t DxfDocument::repair_entity_owners() {
    DxfDocument staged=*this;std::size_t changed=0;std::string section,block_name;
    auto needs_owner=[](const DxfRecord& r){
        std::size_t owners=0;r.locate_if("",[&](std::size_t,int code){if(code==330)++owners;return false;});
        if(owners>1)throw std::invalid_argument("Ambiguous common owner fields");
        return r.owner_handle().empty() || parse_handle(r.owner_handle())==std::optional<std::uint64_t>{0};
    };
    auto block_handle=[&](std::string_view name){
        const auto locations=staged.symbol_locations(SymbolTable::BlockRecord,name);
        if(locations.size()!=1)throw std::invalid_argument("Owner repair requires one matching BLOCK_RECORD");
        const auto& block=staged.record_at({locations.front(),std::nullopt,false});
        if(!handle_value(block.handle()) || staged.records_with_handle(block.handle()).size()!=1)
            throw std::invalid_argument("Owner repair requires a unique block identity");
        return block.handle();
    };
    for(std::size_t i=0;i<staged.records_.size();++i) {
        auto& variant=staged.records_[i];auto& r=std::visit([](auto& v)->DxfRecord&{return v;},variant);
        if(r.type()=="SECTION")section=r.field("",2);
        if(r.type()=="ENDSEC"){section.clear();block_name.clear();}
        if(section=="BLOCKS" && r.type()=="BLOCK")block_name=r.text_field("AcDbBlockBegin",2);
        const bool entity=std::find(staged.entity_records_.begin(),staged.entity_records_.end(),i)!=staged.entity_records_.end();
        if(entity && needs_owner(r)) {
            std::string target;
            if(section=="ENTITIES") {
                const auto paper=r.short_field("AcDbEntity",67,0);
                if(paper!=0 && paper!=1)throw std::invalid_argument("Invalid entity space flag for owner repair");
                target=block_handle(paper==1?"*Paper_Space":"*Model_Space");
            } else if(section=="BLOCKS" && !block_name.empty())target=block_handle(block_name);
            else throw std::invalid_argument("Cannot infer missing entity owner");
            r.set_field("",330,target);++changed;
        }
        if(auto* c=sequence(variant)) {
            auto assign=[&](DxfRecord& child){if(!needs_owner(child))return;
                if(!handle_value(r.handle()) || staged.records_with_handle(r.handle()).size()!=1)
                    throw std::invalid_argument("Cannot assign a child to an ambiguous parent identity");
                child.set_field("",330,r.handle());++changed;
            };
            for(auto& child:c->children_)assign(child);if(c->seqend_)assign(*c->seqend_);
        }
        if(r.type()=="ENDBLK")block_name.clear();
    }
    staged.rebuild_indexes();
    for(const auto& issue:staged.reference_issues())if(issue.status==ReferenceStatus::OwnerCycle && ownership_chain(issue.source_handle).status!=OwnershipStatus::Cycle)
        throw std::invalid_argument("Owner repair would introduce an ownership cycle");
    *this=std::move(staged);return changed;
}
