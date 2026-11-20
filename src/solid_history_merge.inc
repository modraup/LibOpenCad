// SPDX-License-Identifier: MPL-2.0
// acadrust document.rs merge_solid_history_boolean; all changes stage in a copy.
SolidHistoryGraph DxfDocument::merge_solid_history_boolean(std::uint64_t target, std::uint64_t tool,
                                                           std::uint8_t operation) {
    if (target == tool || version_ < DxfVersion::AC1021)
        throw std::invalid_argument("History merge requires distinct R2007+ modelers");
    validate_history_mutation(target);
    validate_history_mutation(tool);
    auto copy = *this;
    auto target_graph = copy.ensure_history_evaluation_graph(target);
    auto tool_graph = copy.ensure_history_evaluation_graph(tool);
    auto target_eval = *target_graph.evaluation_graph, tool_eval = *tool_graph.evaluation_graph;
    if (target_graph.root == tool_graph.root || target_eval == tool_eval)
        throw std::invalid_argument("Shared history graph cannot be merged");
    auto target_active = SolidHistoryReader(copy).active(target_graph);
    auto tool_active = SolidHistoryReader(copy).active(tool_graph);
    if (!target_active || !tool_active)
        throw std::invalid_argument("Ambiguous active history result");
    auto reject_private_scope = [&](std::uint64_t h, std::string_view scope) {
        const auto &r = SolidHistoryReader::mutable_record(copy, h);
        bool active = false;
        for (const auto &p : r.pairs_) {
            if (p.code == 1001 || p.code == 101)
                break;
            if (p.code == 100)
                active = p.value == scope;
            if (active && p.code == 102)
                throw std::invalid_argument("History merge cannot rewrite private subclass groups");
        }
    };
    reject_private_scope(target_eval, "AcDbEvalGraph");
    reject_private_scope(tool_eval, "AcDbEvalGraph");
    for (auto h : tool_graph.nodes)
        reject_private_scope(h, "AcDbEvalExpr");
    auto merged = *copy.evaluation_graph(target_eval);
    auto incoming = *copy.evaluation_graph(tool_eval);
    auto validate = [&](std::uint64_t entity, const SolidHistoryGraph &g, const BlockEvaluationGraph &eg,
                        std::uint64_t active) {
        copy.set_solid_history_evaluation_graph(entity, eg); // cycles, edge endpoints, incoming chains
        std::set<std::int32_t> ids;
        std::set<std::uint64_t> expressions;
        for (auto &n : eg.nodes)
            expressions.insert(n.expression);
        if (expressions != std::set<std::uint64_t>(g.nodes.begin(), g.nodes.end()))
            throw std::invalid_argument("Evaluation graph omits history operations");
        for (auto h : g.nodes) {
            auto n = *copy.solid_history_node(h);
            if (n.node_id() <= 0 || !ids.insert(n.node_id()).second)
                throw std::invalid_argument("Duplicate or invalid history operation id");
        }
        auto latest = std::find_if(eg.nodes.begin(), eg.nodes.end(),
                                   [&](const auto &n) { return n.expression == active; });
        if (latest == eg.nodes.end() || latest->node_data[2] >= 0 ||
            std::any_of(eg.edges.begin(), eg.edges.end(),
                        [&](const auto &e) { return e.source_node == latest->id; }))
            throw std::invalid_argument("History merge requires the latest results");
    };
    validate(target, target_graph, merged, target_active->handle);
    validate(tool, tool_graph, incoming, tool_active->handle);
    auto add = [](std::int32_t value, std::int32_t by) {
        auto n = std::int64_t(value) + by;
        if (n < INT32_MIN || n > INT32_MAX)
            throw std::overflow_error("History merge id exhausted");
        return static_cast<std::int32_t>(n);
    };
    auto shift = [&](std::int32_t n, std::int32_t by) { return n >= 0 ? add(n, by) : n; };
    auto put = [&](std::uint64_t h, std::string_view scope, int code, std::int32_t value) {
        auto &r = SolidHistoryReader::mutable_record(copy, h);
        auto text = std::to_string(value);
        r.set_field(scope, code, r.binary_ ? detail::binary_value_from_text(code, text) : text);
    };
    std::int32_t offset = 0, graph_offset = 0, edge_offset = 0;
    for (auto h : target_graph.nodes) {
        auto b = *copy.solid_history_node(h);
        offset = std::max({offset, b.step_id, b.eval_node_id});
    }
    for (auto &n : merged.nodes)
        graph_offset = std::max(graph_offset, add(n.id, 1));
    for (auto &e : merged.edges)
        edge_offset = std::max(edge_offset, add(e.id, 1));
    for (auto n : incoming.nodes) {
        n.id = add(n.id, graph_offset);
        n.next_id = add(n.next_id, graph_offset);
        for (auto &v : n.node_data)
            v = shift(v, edge_offset);
        merged.nodes.push_back(n);
    }
    for (auto e : incoming.edges) {
        e.id = add(e.id, edge_offset);
        e.source_node = add(e.source_node, graph_offset);
        e.destination_node = add(e.destination_node, graph_offset);
        for (auto &v : e.outgoing_edges)
            v = shift(v, edge_offset);
        // next_id is an upstream control field, not an edge pointer to offset.
        merged.edges.push_back(e);
    }
    std::int32_t highest = offset, tool_active_id = 0;
    for (auto h : tool_graph.nodes) {
        auto b = *copy.solid_history_node(h);
        auto &r = SolidHistoryReader::mutable_record(copy, h);
        r.set_field("", 330, hex(target_eval));
        if (b.eval_node_id > 0) {
            b.eval_node_id = add(b.eval_node_id, offset);
            // Code 90 can also encode the evaluation VALUE: change only the first ID field.
            auto fields = r.subclass_fields("AcDbEvalExpr");
            auto at = std::find_if(fields.begin(), fields.end(), [](const auto &f) { return f.code == 90; });
            if (at == fields.end())
                throw std::invalid_argument("Missing history evaluation id");
            at->value = std::to_string(b.eval_node_id);
            r.set_subclass_fields("AcDbEvalExpr", fields);
        }
        if (b.step_id > 0) {
            b.step_id = add(b.step_id, offset);
            put(h, "AcDbShHistoryNode", 92, b.step_id);
        }
        if (b.kind == SolidHistoryKind::Boolean) {
            put(h, "AcDbShBoolean", 92, shift(b.first_operand, offset));
            put(h, "AcDbShBoolean", 93, shift(b.second_operand, offset));
        }
        highest = std::max({highest, b.step_id, b.eval_node_id});
        if (h == tool_active->handle)
            tool_active_id = b.node_id();
    }
    std::int32_t id = 0, first_edge = 0;
    for (auto &n : merged.nodes)
        id = std::max(id, add(n.id, 1));
    for (auto &e : merged.edges)
        first_edge = std::max(first_edge, add(e.id, 1));
    auto second_edge = add(first_edge, 1), next = add(id, 1), boolean_id = add(highest, 1);
    for (auto pair :
         {std::pair{target_active->handle, first_edge}, std::pair{tool_active->handle, second_edge}}) {
        auto n = std::find_if(merged.nodes.begin(), merged.nodes.end(),
                              [&](const auto &v) { return v.expression == pair.first; });
        n->node_data[2] = n->node_data[3] = pair.second;
        BlockEvaluationEdge edge;
        edge.id = pair.second;
        edge.incoming_edge = 1;
        edge.source_node = n->id;
        edge.destination_node = id;
        edge.outgoing_edges.fill(-1);
        edge.outgoing_edges[pair.second == first_edge ? 1 : 0] =
            pair.second == first_edge ? second_edge : first_edge;
        merged.edges.push_back(edge);
    }
    copy.ensure_history_class("ACSH_BOOLEAN_CLASS", "AcDbShBoolean");
    auto record = copy.make_record("ACSH_BOOLEAN_CLASS");
    record.set_field("", 330, hex(target_eval));
    auto boolean = copy.insert_object(record);
    SolidHistoryOperation result(SolidHistoryKind::Boolean);
    result.base.step_id = result.base.eval_node_id = boolean_id;
    result.operation_major = 1;
    result.parameters = HistoryBoolean{operation, target_active->node_id(), tool_active_id};
    copy.write_history_operation(boolean, result);
    BlockEvaluationNode node;
    node.id = id;
    node.edge_flags = 32;
    node.next_id = next;
    node.expression = boolean;
    node.node_data = {first_edge, second_edge, -1, -1};
    merged.nodes.push_back(node);
    merged.first_node_id = merged.first_node_id_copy = next;
    SolidHistoryReader::replace_graph(copy, target_eval, merged);
    put(target_graph.root, "AcDbShHistory", 92, boolean_id);
    auto &entity = SolidHistoryReader::mutable_record(copy, tool);
    if (!SolidHistoryReader::fields(entity, "AcDb3dSolid").values.empty())
        entity.set_field("AcDb3dSolid", 350, "0");
    copy.write_history_metadata(tool, "CADCODEC_SURFACE_HISTORY_V1", nullptr);
    // Detach first. External non-reactor links or owned private children block deletion atomically.
    copy.rebuild_indexes();
    copy.erase_history_records({tool_graph.root, tool_eval});
    copy.set_solid_history_evaluation_graph(target, merged);
    auto graph = copy.solid_history_graph(target);
    auto tree = copy.solid_history_tree(target);
    if (!graph || !tree || tree->operands.size() != 2 || copy.solid_history_graph(tool))
        throw std::invalid_argument("Merged history did not resolve");
    *this = std::move(copy);
    return *graph;
}
