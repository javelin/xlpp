#include <xlpp/sidecar.hpp>

#include "xml_scan.hpp"

#include <cstdlib>
#include <stdexcept>

namespace xlpp {

WorkbookSidecar load_workbook_sidecar(const std::string &xlsx_path) {
    using detail::find_xml_tag;
    using detail::xml_element_text;

    const auto xml = detail::read_zip_entry(xlsx_path, "xl/workbook.xml");
    WorkbookSidecar sidecar;

    // <sheets><sheet name="..." .../>...</sheets> — order defines localSheetId.
    auto sheets = find_xml_tag(xml, "sheets", 0);
    if (sheets.start == std::string::npos) {
        throw std::runtime_error("sidecar: no <sheets> in " + xlsx_path);
    }
    auto sheets_end = xml.find("</sheets>", sheets.start);
    for (auto pos = sheets.content_begin;;) {
        auto sheet = find_xml_tag(xml, "sheet", pos);
        if (sheet.start == std::string::npos || sheet.start > sheets_end) {
            break;
        }
        auto name = sheet.attr("name");
        if (name == nullptr) {
            throw std::runtime_error("sidecar: <sheet> without name in " + xlsx_path);
        }
        sidecar.sheet_names.push_back(*name);
        pos = sheet.content_begin;
    }
    if (sidecar.sheet_names.empty()) {
        throw std::runtime_error("sidecar: zero sheets in " + xlsx_path);
    }

    // <definedNames> is optional.
    auto dns = find_xml_tag(xml, "definedNames", 0);
    if (dns.start != std::string::npos && !dns.self_closing) {
        auto dns_end = xml.find("</definedNames>", dns.start);
        for (auto pos = dns.content_begin;;) {
            auto dn = find_xml_tag(xml, "definedName", pos);
            if (dn.start == std::string::npos || dn.start > dns_end) {
                break;
            }
            DefinedName parsed;
            auto name = dn.attr("name");
            if (name == nullptr) {
                throw std::runtime_error("sidecar: <definedName> without name in " + xlsx_path);
            }
            parsed.name = *name;
            if (auto sheet_id = dn.attr("localSheetId")) {
                parsed.local_sheet_id = std::strtoul(sheet_id->c_str(), nullptr, 10);
                if (*parsed.local_sheet_id >= sidecar.sheet_names.size()) {
                    throw std::runtime_error("sidecar: localSheetId out of range for '" +
                                             parsed.name + "' in " + xlsx_path);
                }
            }
            if (auto hidden = dn.attr("hidden")) {
                parsed.hidden = *hidden == "1" || *hidden == "true";
            }
            parsed.value = xml_element_text(xml, dn, "definedName");
            sidecar.defined_names.push_back(std::move(parsed));
            pos = dn.content_begin;
        }
    }

    // <calcPr> is optional; attributes override Excel defaults.
    auto calc = find_xml_tag(xml, "calcPr", 0);
    if (calc.start != std::string::npos) {
        if (auto iterate = calc.attr("iterate")) {
            sidecar.calc.iterate = *iterate == "1" || *iterate == "true";
        }
        if (auto count = calc.attr("iterateCount")) {
            sidecar.calc.iterate_count = std::strtoul(count->c_str(), nullptr, 10);
        }
        if (auto delta = calc.attr("iterateDelta")) {
            sidecar.calc.iterate_delta = std::strtod(delta->c_str(), nullptr);
        }
    }

    return sidecar;
}

} // namespace xlpp
