// xlpp_probe — Phase 0 feasibility prober (IMPLEMENTATION_PLAN.md, Phase 0).
//
// Loads each workbook through xlnt plus the xlpp sidecar, prints one summary
// TSV row per file, and (with --dump-dir) writes a normalized per-cell dump
//   sheet \t ref \t formula \t cached-value
// used to cross-check xlnt's parse fidelity against the openpyxl analysis.
//
// Usage: xlpp_probe [--dump-dir DIR] file.xlsx [file.xlsx ...]

#include <xlpp/sidecar.hpp>

#include <xlnt/xlnt.hpp>

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string cached_value_string(const xlnt::cell &cell) {
    switch (cell.data_type()) {
    case xlnt::cell_type::empty:
        return {};
    case xlnt::cell_type::boolean:
        return cell.value<bool>() ? "TRUE" : "FALSE";
    case xlnt::cell_type::number: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.17g", cell.value<double>());
        return buffer;
    }
    case xlnt::cell_type::error:
    case xlnt::cell_type::shared_string:
    case xlnt::cell_type::inline_string:
    case xlnt::cell_type::formula_string:
        return cell.to_string();
    case xlnt::cell_type::date:
        return cell.to_string();
    }
    return {};
}

std::string sanitize(const std::string &text) {
    std::string out = text;
    for (auto &ch : out) {
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return out;
}

std::string dump_name_for(const std::string &path) {
    auto slash = path.find_last_of('/');
    auto base = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = base.rfind(".xlsx");
    if (dot != std::string::npos) {
        base.resize(dot);
    }
    return base + ".tsv";
}

struct FileStats {
    std::size_t sheets = 0;
    std::size_t cells = 0;
    std::size_t formulas = 0;
};

FileStats probe_workbook(const std::string &path, const std::string &dump_dir) {
    xlnt::workbook workbook;
    workbook.load(path);

    std::ofstream dump;
    if (!dump_dir.empty()) {
        auto dump_path = dump_dir + "/" + dump_name_for(path);
        dump.open(dump_path);
        if (!dump) {
            throw std::runtime_error("cannot open dump file: " + dump_path);
        }
    }

    FileStats stats;
    for (auto sheet : workbook) {
        ++stats.sheets;
        for (auto row : sheet.rows(false)) {
            for (auto cell : row) {
                ++stats.cells;
                const bool has_formula = cell.has_formula();
                if (has_formula) {
                    ++stats.formulas;
                }
                if (dump.is_open() && (has_formula || cell.data_type() != xlnt::cell_type::empty)) {
                    dump << sheet.title() << '\t' << cell.reference().to_string() << '\t'
                         << (has_formula ? sanitize(cell.formula()) : std::string()) << '\t'
                         << sanitize(cached_value_string(cell)) << '\n';
                }
            }
        }
    }
    return stats;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> files;
    std::string dump_dir;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-dir" && i + 1 < argc) {
            dump_dir = argv[++i];
        } else {
            files.push_back(std::move(arg));
        }
    }
    if (files.empty()) {
        std::cerr << "usage: xlpp_probe [--dump-dir DIR] file.xlsx [file.xlsx ...]\n";
        return 2;
    }

    std::cout << "file\tstatus\tsheets\tcells\tformulas\tdefined_names\titerate\titer_count"
                 "\titer_delta\terror\n";

    int failures = 0;
    for (const auto &path : files) {
        try {
            auto sidecar = xlpp::load_workbook_sidecar(path);
            auto stats = probe_workbook(path, dump_dir);
            std::cout << path << "\tOK\t" << stats.sheets << '\t' << stats.cells << '\t'
                      << stats.formulas << '\t' << sidecar.defined_names.size() << '\t'
                      << (sidecar.calc.iterate ? 1 : 0) << '\t' << sidecar.calc.iterate_count
                      << '\t' << sidecar.calc.iterate_delta << "\t\n";
        } catch (const std::exception &error) {
            ++failures;
            std::cout << path << "\tFAIL\t0\t0\t0\t0\t0\t0\t0\t" << sanitize(error.what())
                      << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
