#include <xlpp/engine.hpp>

#include <xlpp/parser.hpp>

#include "evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_set>
#include <vector>

namespace xlpp {
namespace {

constexpr CellKey orphan_sentinel = make_cell_key(0xFFFFu, 0, 0);

// Formula cells bucketed per (sheet, column), rows sorted — lets range edges
// enumerate only existing formula cells instead of every rect cell (large
// calc sheets reference multi-thousand-row ranges).
class FormulaCellIndex {
public:
    explicit FormulaCellIndex(const WorkbookModel &model) {
        for (const auto &entry : model.cells) {
            if (entry.second.formula >= 0) {
                buckets_[entry.first >> 28].emplace_back(
                    key_row(entry.first), static_cast<std::uint32_t>(entry.second.formula));
            }
        }
        for (auto &bucket : buckets_) {
            std::sort(bucket.second.begin(), bucket.second.end());
        }
    }

    template <typename Fn>
    void for_each_in(std::uint32_t sheet, std::uint32_t col_first, std::uint32_t col_last,
                     std::uint32_t row_first, std::uint32_t row_last, Fn &&fn) const {
        for (std::uint32_t col = col_first; col <= col_last; ++col) {
            const auto it = buckets_.find(make_cell_key(sheet, col, 0) >> 28);
            if (it == buckets_.end()) {
                continue;
            }
            auto lo = std::lower_bound(it->second.begin(), it->second.end(),
                                       std::make_pair(row_first, 0u));
            for (; lo != it->second.end() && lo->first <= row_last; ++lo) {
                fn(lo->second);
            }
        }
    }

private:
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>>
        buckets_;
};

// Static dependency extraction — used only to compute a good evaluation ORDER
// (Kahn). Correctness does not depend on it: evaluation is demand-driven, and
// a read of an uncomputed cell raises detail::PendingCell which the engine
// resolves on an explicit work stack. Static edges over-approximate (a lookup
// table range "depends" on every cell in it), which is exactly why they are
// only a scheduling hint: Excel-legal table/computed-column overlaps (TNY-4)
// would otherwise look like cycles.
class EdgeCollector {
public:
    EdgeCollector(const WorkbookModel &model, const FormulaCellIndex &index,
                  std::int32_t anchor_sheet)
        : model_(model), index_(index), anchor_sheet_(anchor_sheet) {
    }

    std::vector<std::uint32_t> dependencies; // formula indices
    // (sheet, col_first, row_first, col_last, row_last) per static reference —
    // persisted for incremental dirty testing.
    std::vector<std::array<std::uint32_t, 5>> rects;
    bool volatile_call = false; // INDIRECT/OFFSET: reads escape the static graph

    void walk(const Ast &ast, NodeId id) {
        const Node &node = ast.at(id);
        switch (node.kind) {
        case NodeKind::cell:
        case NodeKind::range:
            add_reference(node);
            break;
        case NodeKind::name:
            expand_name(node);
            break;
        case NodeKind::call: {
            const std::string upper = upper_ascii(node.text);
            if (upper == "INDIRECT" || upper == "OFFSET") {
                volatile_call = true;
            }
            break;
        }
        default:
            break;
        }
        for (const NodeId child : node.children) {
            walk(ast, child);
        }
    }

private:
    void add_reference(const Node &node) {
        const std::int32_t sheet = node.has_sheet ? model_.sheet_index(node.sheet)
                                                  : anchor_sheet_;
        if (sheet < 0) {
            return;
        }
        const bool is_range = node.kind == NodeKind::range;
        const std::uint32_t col_first = std::min(node.address_a.column,
                                                 is_range ? node.address_b.column
                                                          : node.address_a.column);
        const std::uint32_t col_last = std::max(node.address_a.column,
                                                is_range ? node.address_b.column
                                                         : node.address_a.column);
        const std::uint32_t row_first = std::min(node.address_a.row,
                                                 is_range ? node.address_b.row
                                                          : node.address_a.row);
        const std::uint32_t row_last = std::max(node.address_a.row,
                                                is_range ? node.address_b.row
                                                         : node.address_a.row);
        rects.push_back({static_cast<std::uint32_t>(sheet), col_first, row_first,
                         col_last, row_last});
        index_.for_each_in(static_cast<std::uint32_t>(sheet), col_first, col_last,
                           row_first, row_last, [this](std::uint32_t formula_index) {
                               dependencies.push_back(formula_index);
                           });
    }

    void expand_name(const Node &node) {
        const std::string upper = upper_ascii(node.text);
        if (!visited_names_.insert(upper).second) {
            return;
        }
        const NameDefinition *definition = model_.find_name(node.text, anchor_sheet_);
        if (definition != nullptr && definition->parsed) {
            walk(definition->ast, definition->ast.root());
        }
        visited_names_.erase(upper);
    }

    const WorkbookModel &model_;
    const FormulaCellIndex &index_;
    std::int32_t anchor_sheet_;
    std::unordered_set<std::string> visited_names_;
};

} // namespace

Engine::Engine(WorkbookModel model)
    : model_(std::move(model)) {
}

bool Engine::resolve(const std::string &sheet, const std::string &ref, CellKey &key) const {
    const std::int32_t sheet_index = model_.sheet_index(sheet);
    CellAddress address;
    if (sheet_index < 0 || !parse_a1_reference(ref, address)) {
        return false;
    }
    key = make_cell_key(static_cast<std::uint32_t>(sheet_index), address.column, address.row);
    return true;
}

bool Engine::set_value(const std::string &sheet, const std::string &ref, const Value &value) {
    CellKey key = 0;
    if (!resolve(sheet, ref, key)) {
        return false;
    }
    CellRecord &record = model_.cells[key];
    if (record.formula >= 0) {
        // Detach: the cell becomes a plain input.
        for (auto &anchor : model_.formula_cells) {
            if (anchor == key) {
                anchor = orphan_sentinel;
            }
        }
        record.formula = -1;
    }
    record.cached = value;
    computed_.erase(key); // stale computed value, if any
    changed_inputs_.push_back(key);
    return true;
}

void Engine::build_graph() {
    const std::size_t n = model_.formulas.size();
    const FormulaCellIndex cell_index(model_);
    dependents_.assign(n, {});
    reference_rects_.assign(n, {});
    volatile_.assign(n, false);
    std::vector<std::uint32_t> indegree(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const CellKey anchor = model_.formula_cells[i];
        if (anchor == orphan_sentinel) {
            continue;
        }
        EdgeCollector collector(model_, cell_index,
                                static_cast<std::int32_t>(key_sheet(anchor)));
        collector.walk(model_.formulas[i], model_.formulas[i].root());
        volatile_[i] = collector.volatile_call;
        for (auto &rect : collector.rects) {
            reference_rects_[i].push_back(
                RefRect{static_cast<std::int32_t>(rect[0]), rect[1], rect[2], rect[3],
                        rect[4]});
        }
        for (const std::uint32_t dep : collector.dependencies) {
            if (dep != i) {
                dependents_[dep].push_back(static_cast<std::uint32_t>(i));
                ++indegree[i];
            }
        }
    }
    order_.clear();
    order_.reserve(n);
    std::deque<std::uint32_t> ready;
    for (std::size_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) {
            ready.push_back(static_cast<std::uint32_t>(i));
        }
    }
    std::vector<bool> scheduled(n, false);
    while (!ready.empty()) {
        const std::uint32_t index = ready.front();
        ready.pop_front();
        scheduled[index] = true;
        order_.push_back(index);
        for (const std::uint32_t dependent : dependents_[index]) {
            if (--indegree[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!scheduled[i]) {
            order_.push_back(static_cast<std::uint32_t>(i)); // static-cycle members
        }
    }
    graph_built_ = true;
}

namespace {

// Max |change| between two values for convergence testing; kind changes and
// error transitions count as "not converged".
double value_delta(const Value &a, const Value &b) {
    if (a.kind == ValueKind::number && b.kind == ValueKind::number) {
        return std::fabs(a.number - b.number);
    }
    if (a.kind != b.kind) {
        return std::numeric_limits<double>::infinity();
    }
    switch (a.kind) {
    case ValueKind::text: return a.text == b.text ? 0.0 : 1.0;
    case ValueKind::boolean: return a.boolean == b.boolean ? 0.0 : 1.0;
    case ValueKind::error: return a.error == b.error ? 0.0 : 1.0;
    default: return 0.0;
    }
}

} // namespace

void Engine::recalculate() {
    const std::size_t n = model_.formulas.size();
    if (!graph_built_) {
        build_graph();
    }

    // ---- incremental dirty closure (FR5) ------------------------------------
    // A recalc after set_value only re-evaluates: formulas whose static
    // references cover a changed input, volatile formulas (INDIRECT/OFFSET
    // reads escape the static graph), and their transitive dependents.
    // iterate=true books that hit cycles fall back to full recalc.
    const bool incremental =
        !computed_.empty() && !(cycles_seen_ && model_.calc.iterate);
    if (incremental) {
        std::vector<bool> dirty(n, false);
        std::deque<std::uint32_t> queue;
        auto mark = [&](std::uint32_t index) {
            if (!dirty[index]) {
                dirty[index] = true;
                queue.push_back(index);
            }
        };
        for (std::size_t i = 0; i < n; ++i) {
            if (volatile_[i]) {
                mark(static_cast<std::uint32_t>(i));
                continue;
            }
            for (const CellKey key : changed_inputs_) {
                const auto sheet = static_cast<std::int32_t>(key_sheet(key));
                const std::uint32_t column = key_column(key);
                const std::uint32_t row = key_row(key);
                bool hit = false;
                for (const RefRect &rect : reference_rects_[i]) {
                    if (rect.sheet == sheet && column >= rect.col_first &&
                        column <= rect.col_last && row >= rect.row_first &&
                        row <= rect.row_last) {
                        hit = true;
                        break;
                    }
                }
                if (hit) {
                    mark(static_cast<std::uint32_t>(i));
                    break;
                }
            }
        }
        while (!queue.empty()) {
            const std::uint32_t index = queue.front();
            queue.pop_front();
            for (const std::uint32_t dependent : dependents_[index]) {
                mark(dependent);
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (dirty[i]) {
                computed_.erase(model_.formula_cells[i]);
            }
        }
    } else {
        computed_.clear();
    }
    changed_inputs_.clear();
    stats_ = {};
    const std::vector<std::uint32_t> &order = order_;

    // ---- demand-driven evaluation -------------------------------------------
    // state: 0 = untouched, 1 = attempted (grey), 2 = computed. A PendingCell
    // for a grey dependency is a true dynamic cycle (the cell's value is being
    // read while it is itself being evaluated). With iterate=false that cell
    // becomes a cyclic diagnostic; with iterate=true (FR6) the cycle is fed
    // the previous iteration's value (first pass: the file's cached value,
    // matching Excel's use of last-known values) and the whole book is swept
    // again until max |delta| < iterateDelta or iterateCount passes.
    detail::Evaluator evaluator(model_, computed_);
    std::unordered_map<CellKey, Value> previous; // previous-pass values
    bool cycles_hit = false;

    auto run_pass = [&]() {
        cycles_hit = false;
        std::vector<std::uint8_t> state(n, 0);
        std::vector<std::uint32_t> work;
        // Cells given a previous-iteration value to break a cycle: the value
        // serves their READERS within this pass, but the cell itself must
        // still be recomputed when the work stack returns to it.
        std::unordered_set<CellKey> injected;
        for (const std::uint32_t start : order) {
            if (state[start] == 2) {
                continue;
            }
            work.clear();
            work.push_back(start);
            while (!work.empty()) {
                const std::uint32_t index = work.back();
                const CellKey anchor = model_.formula_cells[index];
                if (anchor == orphan_sentinel ||
                    (computed_.count(anchor) != 0 && injected.count(anchor) == 0)) {
                    state[index] = 2;
                    work.pop_back();
                    continue;
                }
                if (injected.count(anchor) != 0) {
                    computed_.erase(anchor); // reader consumed the provisional
                    injected.erase(anchor);
                }
                state[index] = 1;
                try {
                    const auto member = model_.array_members.find(anchor);
                    if (member != model_.array_members.end()) {
                        // CSE block: evaluate the matrix once, publish every
                        // member element (later members skip via computed_).
                        const ArrayBlock &block = model_.array_blocks[member->second.block];
                        const std::uint32_t rows = block.row_last - block.row_first + 1;
                        const std::uint32_t cols = block.col_last - block.col_first + 1;
                        const std::vector<Value> elements = evaluator.evaluate_array_block(
                            make_cell_key(block.sheet, block.col_first, block.row_first),
                            block.ast, rows, cols);
                        for (std::uint32_t row = 0; row < rows; ++row) {
                            for (std::uint32_t col = 0; col < cols; ++col) {
                                computed_[make_cell_key(block.sheet, block.col_first + col,
                                                        block.row_first + row)] =
                                    elements[row * cols + col];
                                ++stats_.recalculated;
                            }
                        }
                    } else {
                        computed_[anchor] =
                            evaluator.evaluate_cell(anchor, model_.formulas[index]);
                        ++stats_.recalculated;
                    }
                    state[index] = 2;
                    work.pop_back();
                } catch (const detail::PendingCell &pending) {
                    const auto cell = model_.cells.find(pending.key);
                    const std::uint32_t dep =
                        static_cast<std::uint32_t>(cell->second.formula);
                    if (state[dep] == 1) {
                        // Dynamic cycle: break it at the dependency.
                        cycles_hit = true;
                        if (model_.calc.iterate) {
                            const auto prev = previous.find(pending.key);
                            computed_[pending.key] = prev != previous.end()
                                ? prev->second
                                : cell->second.cached;
                            injected.insert(pending.key);
                        } else {
                            computed_[pending.key] = Value::make_error(ErrorCode::cyclic);
                        }
                    } else {
                        work.push_back(dep);
                    }
                }
            }
        }
    };

    run_pass();
    cycles_seen_ = cycles_hit;

    if (cycles_hit && model_.calc.iterate) {
        stats_.converged = false;
        while (stats_.iterations < model_.calc.iterate_count) {
            ++stats_.iterations;
            previous = std::move(computed_);
            computed_ = {};
            run_pass();
            double max_delta = 0.0;
            for (const auto &entry : computed_) {
                const auto prev = previous.find(entry.first);
                if (prev == previous.end()) {
                    max_delta = std::numeric_limits<double>::infinity();
                    break;
                }
                max_delta = std::max(max_delta, value_delta(entry.second, prev->second));
            }
            if (max_delta < model_.calc.iterate_delta) {
                stats_.converged = true;
                break;
            }
        }
    }

    for (const auto &entry : computed_) {
        if (entry.second.is_diagnostic()) {
            ++(entry.second.error == ErrorCode::cyclic ? stats_.cyclic
                                                       : stats_.unsupported);
        } else {
            ++stats_.evaluated;
        }
    }
}

Value Engine::value(CellKey key) const {
    const auto computed = computed_.find(key);
    if (computed != computed_.end()) {
        return computed->second;
    }
    const auto cell = model_.cells.find(key);
    if (cell != model_.cells.end()) {
        return cell->second.cached;
    }
    return Value::make_blank();
}

Value Engine::value(const std::string &sheet, const std::string &ref) const {
    CellKey key = 0;
    if (!resolve(sheet, ref, key)) {
        return Value::make_error(ErrorCode::ref_error);
    }
    return value(key);
}

} // namespace xlpp
