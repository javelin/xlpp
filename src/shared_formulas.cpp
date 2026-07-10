#include "shared_formulas.hpp"

#include "xml_scan.hpp"

#include <xlpp/ast.hpp>
#include <xlpp/parser.hpp>
#include <xlpp/workbook_model.hpp>

#include <stdexcept>

namespace xlpp {
namespace detail {
namespace {

bool parse_ref_attr(const std::string &text, std::uint32_t &column, std::uint32_t &row) {
    CellAddress address;
    if (!parse_a1_reference(text, address)) {
        return false;
    }
    column = address.column;
    row = address.row;
    return true;
}

// Shifts every relative reference in the AST by (delta_row, delta_column).
void shift_relative(Ast &ast, std::int64_t delta_row, std::int64_t delta_column) {
    for (std::size_t i = 0; i < ast.size(); ++i) {
        Node &node = ast.mutable_at(static_cast<NodeId>(i));
        if (node.kind != NodeKind::cell && node.kind != NodeKind::range) {
            continue;
        }
        auto shift = [&](CellAddress &address) {
            if (!address.absolute_row) {
                const std::int64_t row = static_cast<std::int64_t>(address.row) + delta_row;
                address.row = row < 1 ? 1u : static_cast<std::uint32_t>(row);
            }
            if (!address.absolute_column) {
                const std::int64_t column =
                    static_cast<std::int64_t>(address.column) + delta_column;
                address.column = column < 1 ? 1u : static_cast<std::uint32_t>(column);
            }
        };
        shift(node.address_a);
        if (node.kind == NodeKind::range) {
            shift(node.address_b);
        }
    }
}

struct SharedMaster {
    std::string text;
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

// Resolves each sheet's XML part via the workbook relationships.
std::vector<std::string> sheet_part_names(const std::string &xlsx_path,
                                          std::size_t sheet_count) {
    const std::string workbook = read_zip_entry(xlsx_path, "xl/workbook.xml");
    const std::string rels = read_zip_entry(xlsx_path, "xl/_rels/workbook.xml.rels");

    std::unordered_map<std::string, std::string> targets; // rId -> part
    for (std::size_t pos = 0;;) {
        const XmlTag rel = find_xml_tag(rels, "Relationship", pos);
        if (rel.start == std::string::npos) {
            break;
        }
        const std::string *id = rel.attr("Id");
        const std::string *target = rel.attr("Target");
        if (id != nullptr && target != nullptr) {
            std::string part = *target;
            if (!part.empty() && part[0] == '/') {
                part = part.substr(1); // absolute part name
            } else {
                part = "xl/" + part;
            }
            targets.emplace(*id, std::move(part));
        }
        pos = rel.content_begin;
    }

    std::vector<std::string> parts;
    const XmlTag sheets = find_xml_tag(workbook, "sheets", 0);
    const std::size_t sheets_end = workbook.find("</sheets>", sheets.start);
    for (std::size_t pos = sheets.content_begin;;) {
        const XmlTag sheet = find_xml_tag(workbook, "sheet", pos);
        if (sheet.start == std::string::npos || sheet.start > sheets_end) {
            break;
        }
        const std::string *rid = sheet.attr("r:id");
        if (rid == nullptr) {
            throw std::runtime_error("shared_formulas: <sheet> without r:id in " + xlsx_path);
        }
        const auto it = targets.find(*rid);
        if (it == targets.end()) {
            throw std::runtime_error("shared_formulas: unresolved sheet relationship " +
                                     *rid + " in " + xlsx_path);
        }
        parts.push_back(it->second);
        pos = sheet.content_begin;
    }
    if (parts.size() != sheet_count) {
        throw std::runtime_error("shared_formulas: sheet count mismatch in " + xlsx_path);
    }
    return parts;
}

} // namespace

SheetFormulaScan scan_sheet_formulas(const std::string &xlsx_path,
                                     const std::vector<std::string> &sheet_names) {
    SheetFormulaScan scan;
    SharedFormulaPatches &patches = scan.shared;
    const std::vector<std::string> parts = sheet_part_names(xlsx_path, sheet_names.size());

    for (std::size_t sheet = 0; sheet < parts.size(); ++sheet) {
        const std::string xml = read_zip_entry(xlsx_path, parts[sheet].c_str());

        std::unordered_map<std::string, SharedMaster> masters;    // si -> master
        std::vector<std::pair<std::string, std::uint64_t>> members; // si, cell key

        std::uint32_t current_row = 0;
        std::uint32_t current_column = 0;
        std::size_t pos = 0;
        while (true) {
            const std::size_t next_c = xml.find("<c ", pos);
            // Single forward scan for "<f" followed by ' ' or '>' — separate
            // find("<f>") calls re-scan the whole tail on every iteration
            // when plain <f> never occurs (quadratic on multi-MB sheets).
            std::size_t next_f = pos;
            while (true) {
                next_f = xml.find("<f", next_f);
                if (next_f == std::string::npos) {
                    break;
                }
                const char after = next_f + 2 < xml.size() ? xml[next_f + 2] : '\0';
                if (after == ' ' || after == '>') {
                    break;
                }
                next_f += 2;
            }
            if (next_c == std::string::npos && next_f == std::string::npos) {
                break;
            }
            if (next_c < next_f) {
                const XmlTag cell = find_xml_tag(xml, "c", next_c);
                const std::string *ref = cell.attr("r");
                if (ref == nullptr ||
                    !parse_ref_attr(*ref, current_column, current_row)) {
                    throw std::runtime_error("shared_formulas: <c> without valid r in " +
                                             parts[sheet]);
                }
                pos = cell.content_begin;
                continue;
            }
            const XmlTag formula = find_xml_tag(xml, "f", next_f);
            pos = formula.content_begin;
            const std::string *type = formula.attr("t");
            if (type != nullptr && *type == "array") {
                const std::string *ref = formula.attr("ref");
                if (ref == nullptr) {
                    throw std::runtime_error("shared_formulas: array <f> without ref in " +
                                             parts[sheet]);
                }
                const std::string text =
                    formula.self_closing ? std::string()
                                         : xml_element_text(xml, formula, "f");
                if (!formula.self_closing) {
                    pos = xml.find("</f>", formula.content_begin) + 4;
                }
                if (text.empty()) {
                    continue; // member of a multi-anchor group; anchor carries text
                }
                ArrayBlockInfo block;
                block.sheet = static_cast<std::uint32_t>(sheet);
                const std::size_t colon = ref->find(':');
                std::uint32_t c1 = 0, r1 = 0, c2 = 0, r2 = 0;
                if (!parse_ref_attr(colon == std::string::npos ? *ref : ref->substr(0, colon),
                                    c1, r1) ||
                    !parse_ref_attr(colon == std::string::npos ? *ref
                                                               : ref->substr(colon + 1),
                                    c2, r2)) {
                    throw std::runtime_error("shared_formulas: bad array ref '" + *ref +
                                             "' in " + parts[sheet]);
                }
                block.col_first = std::min(c1, c2);
                block.col_last = std::max(c1, c2);
                block.row_first = std::min(r1, r2);
                block.row_last = std::max(r1, r2);
                block.formula = text;
                scan.arrays.push_back(std::move(block));
                continue;
            }
            if (type == nullptr || *type != "shared") {
                continue;
            }
            const std::string *si = formula.attr("si");
            if (si == nullptr) {
                throw std::runtime_error("shared_formulas: shared <f> without si in " +
                                         parts[sheet]);
            }
            const std::string text =
                formula.self_closing ? std::string() : xml_element_text(xml, formula, "f");
            if (!formula.self_closing) {
                pos = xml.find("</f>", formula.content_begin) + 4;
            }
            if (!text.empty()) {
                masters[*si] = SharedMaster{text, current_row, current_column};
            } else {
                members.emplace_back(*si, make_cell_key(static_cast<std::uint32_t>(sheet),
                                                        current_column, current_row));
            }
        }

        for (const auto &member : members) {
            const auto master = masters.find(member.first);
            if (master == masters.end()) {
                throw std::runtime_error("shared_formulas: member without master (si=" +
                                         member.first + ") in " + parts[sheet]);
            }
            const std::uint32_t row = key_row(member.second);
            const std::uint32_t column = key_column(member.second);
            Ast ast = parse_formula(master->second.text);
            shift_relative(ast,
                           static_cast<std::int64_t>(row) - master->second.row,
                           static_cast<std::int64_t>(column) - master->second.column);
            patches.emplace(member.second, to_formula(ast));
        }
    }
    return scan;
}

} // namespace detail
} // namespace xlpp
