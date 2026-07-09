// parse_corpus — Phase 1 gate (IMPLEMENTATION_PLAN.md): parse every formula
// in the given workbooks; report a per-file summary TSV and any failures.
// For each formula: parse -> print canonical -> re-parse -> require
// structurally identical AST (round-trip check).
//
// Usage: parse_corpus file.xlsx [file.xlsx ...]

#include <xlpp/ast.hpp>
#include <xlpp/lexer.hpp>
#include <xlpp/parser.hpp>

#include <xlnt/xlnt.hpp>

#include <exception>
#include <iostream>
#include <string>

namespace {

struct FileStats {
    std::size_t formulas = 0;
    std::size_t parse_failures = 0;
    std::size_t round_trip_failures = 0;
};

constexpr std::size_t max_reported_failures = 20;

FileStats parse_workbook(const std::string &path, std::size_t &reported) {
    xlnt::workbook workbook;
    workbook.load(path);

    FileStats stats;
    for (auto sheet : workbook) {
        for (auto row : sheet.rows(false)) {
            for (auto cell : row) {
                if (!cell.has_formula()) {
                    continue;
                }
                ++stats.formulas;
                const std::string &formula = cell.formula();
                try {
                    const xlpp::Ast ast = xlpp::parse_formula(formula);
                    const std::string printed = xlpp::to_formula(ast);
                    const xlpp::Ast reparsed = xlpp::parse_formula(printed);
                    if (!xlpp::equal(ast, ast.root(), reparsed, reparsed.root())) {
                        ++stats.round_trip_failures;
                        if (reported < max_reported_failures) {
                            ++reported;
                            std::cerr << "ROUNDTRIP " << path << " " << sheet.title() << "!"
                                      << cell.reference().to_string() << "\n  in:  " << formula
                                      << "\n  out: " << printed << "\n";
                        }
                    }
                } catch (const std::exception &error) {
                    ++stats.parse_failures;
                    if (reported < max_reported_failures) {
                        ++reported;
                        std::cerr << "PARSE " << path << " " << sheet.title() << "!"
                                  << cell.reference().to_string() << ": " << error.what()
                                  << "\n  " << formula << "\n";
                    }
                }
            }
        }
    }
    return stats;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: parse_corpus file.xlsx [file.xlsx ...]\n";
        return 2;
    }

    std::cout << "file\tstatus\tformulas\tparse_failures\troundtrip_failures\n";

    std::size_t total_formulas = 0;
    std::size_t total_failures = 0;
    std::size_t reported = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];
        try {
            const FileStats stats = parse_workbook(path, reported);
            total_formulas += stats.formulas;
            total_failures += stats.parse_failures + stats.round_trip_failures;
            std::cout << path << "\tOK\t" << stats.formulas << '\t' << stats.parse_failures
                      << '\t' << stats.round_trip_failures << '\n';
        } catch (const std::exception &error) {
            ++total_failures;
            std::cout << path << "\tLOAD_FAIL\t0\t0\t0\t" << error.what() << '\n';
        }
    }

    std::cout << "TOTAL\t" << (total_failures == 0 ? "PASS" : "FAIL") << '\t' << total_formulas
              << "\tfailures=" << total_failures << '\n';
    return total_failures == 0 ? 0 : 1;
}
