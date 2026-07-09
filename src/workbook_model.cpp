#include <xlpp/workbook_model.hpp>

#include <xlpp/parser.hpp>

#include "shared_formulas.hpp"

#include <xlnt/xlnt.hpp>

#include <cstdio>
#include <stdexcept>

namespace xlpp {
namespace {

Value cached_value_of(const xlnt::cell &cell) {
    switch (cell.data_type()) {
    case xlnt::cell_type::empty:
        return Value::make_blank();
    case xlnt::cell_type::boolean:
        return Value::make_boolean(cell.value<bool>());
    case xlnt::cell_type::number:
    case xlnt::cell_type::date:
        return Value::make_number(cell.value<double>());
    case xlnt::cell_type::error: {
        ErrorCode code;
        if (parse_error_text(cell.to_string(), code)) {
            return Value::make_error(code);
        }
        return Value::make_error(ErrorCode::value_error);
    }
    case xlnt::cell_type::shared_string:
    case xlnt::cell_type::inline_string:
    case xlnt::cell_type::formula_string:
        return Value::make_text(cell.to_string());
    }
    return Value::make_blank();
}

} // namespace

std::string upper_ascii(const std::string &text) {
    std::string upper = text;
    for (auto &c : upper) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return upper;
}

WorkbookModel WorkbookModel::load(const std::string &xlsx_path) {
    WorkbookModel model;

    const WorkbookSidecar sidecar = load_workbook_sidecar(xlsx_path);
    model.calc = sidecar.calc;
    model.sheet_names = sidecar.sheet_names;

    // xlnt gives shared-formula members the master's text without
    // re-anchoring relative refs; these patches carry the translated text.
    const detail::SharedFormulaPatches shared_patches =
        detail::load_shared_formula_patches(xlsx_path, model.sheet_names);

    xlnt::workbook workbook;
    workbook.load(xlsx_path);

    std::uint32_t sheet_index = 0;
    for (auto sheet : workbook) {
        for (auto row : sheet.rows(false)) {
            for (auto cell : row) {
                const bool has_formula = cell.has_formula();
                const Value cached = cached_value_of(cell);
                if (!has_formula && cached.kind == ValueKind::blank) {
                    continue;
                }
                const CellKey key = make_cell_key(sheet_index, cell.column_index(),
                                                  cell.row());
                CellRecord record;
                record.cached = cached;
                if (has_formula) {
                    const auto patch = shared_patches.find(key);
                    try {
                        Ast ast = parse_formula(patch != shared_patches.end()
                                                    ? patch->second
                                                    : cell.formula());
                        record.formula = static_cast<std::int32_t>(model.formulas.size());
                        model.formulas.push_back(std::move(ast));
                        model.formula_cells.push_back(key);
                    } catch (const std::exception &) {
                        ++model.formula_parse_failures; // downgraded to input (NFR4)
                    }
                }
                model.cells.emplace(key, std::move(record));
            }
        }
        ++sheet_index;
    }

    for (const DefinedName &name : sidecar.defined_names) {
        NameDefinition definition;
        definition.sheet_scope = name.local_sheet_id
            ? static_cast<std::int32_t>(*name.local_sheet_id)
            : -1;
        try {
            definition.ast = parse_formula(name.value);
            definition.parsed = true;
        } catch (const std::exception &) {
            definition.parsed = false; // e.g. multi-area unions -> #NAME? on use
        }
        model.names[upper_ascii(name.name)].push_back(std::move(definition));
    }

    return model;
}

std::int32_t WorkbookModel::sheet_index(const std::string &name) const {
    const std::string upper = upper_ascii(name);
    for (std::size_t i = 0; i < sheet_names.size(); ++i) {
        if (upper_ascii(sheet_names[i]) == upper) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

const NameDefinition *WorkbookModel::find_name(const std::string &name,
                                               std::int32_t sheet) const {
    const auto it = names.find(upper_ascii(name));
    if (it == names.end()) {
        return nullptr;
    }
    const NameDefinition *global = nullptr;
    for (const NameDefinition &definition : it->second) {
        if (definition.sheet_scope == sheet) {
            return &definition;
        }
        if (definition.sheet_scope < 0) {
            global = &definition;
        }
    }
    return global;
}

} // namespace xlpp
