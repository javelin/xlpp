#include <xlpp/value.hpp>

#include <cmath>
#include <cstdio>

namespace xlpp {

const char *error_text(ErrorCode code) {
    switch (code) {
    case ErrorCode::null_error: return "#NULL!";
    case ErrorCode::div0: return "#DIV/0!";
    case ErrorCode::value_error: return "#VALUE!";
    case ErrorCode::ref_error: return "#REF!";
    case ErrorCode::name_error: return "#NAME?";
    case ErrorCode::num_error: return "#NUM!";
    case ErrorCode::na: return "#N/A";
    case ErrorCode::unsupported: return "#UNSUPPORTED";
    case ErrorCode::cyclic: return "#CYCLIC";
    }
    return "#VALUE!";
}

bool parse_error_text(const std::string &text, ErrorCode &code) {
    if (text == "#NULL!") { code = ErrorCode::null_error; return true; }
    if (text == "#DIV/0!") { code = ErrorCode::div0; return true; }
    if (text == "#VALUE!") { code = ErrorCode::value_error; return true; }
    if (text == "#REF!") { code = ErrorCode::ref_error; return true; }
    if (text == "#NAME?") { code = ErrorCode::name_error; return true; }
    if (text == "#NUM!") { code = ErrorCode::num_error; return true; }
    if (text == "#N/A") { code = ErrorCode::na; return true; }
    return false;
}

std::string number_to_text(double value) {
    if (std::isnan(value)) {
        return "#NUM!";
    }
    // Integral values within exact double range print without a decimal point.
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return buffer;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    // Excel renders exponents upper-case (1E-05).
    for (char *c = buffer; *c; ++c) {
        if (*c == 'e') {
            *c = 'E';
        }
    }
    return buffer;
}

} // namespace xlpp
