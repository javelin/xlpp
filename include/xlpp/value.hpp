#pragma once

// Scalar value model (PRD FR8): tagged scalar with first-class Excel errors.
// `unsupported` and `cyclic` are internal diagnostics for constructs deferred
// to later phases; they propagate like errors but are reported separately.

#include <cstdint>
#include <string>

namespace xlpp {

enum class ValueKind : std::uint8_t { blank, number, text, boolean, error };

enum class ErrorCode : std::uint8_t {
    null_error,  // #NULL!
    div0,        // #DIV/0!
    value_error, // #VALUE!
    ref_error,   // #REF!
    name_error,  // #NAME?
    num_error,   // #NUM!
    na,          // #N/A
    unsupported, // internal: construct deferred to a later phase
    cyclic,      // internal: cell participates in a cycle (Phase 5)
};

struct Value {
    ValueKind kind = ValueKind::blank;
    double number = 0.0;
    bool boolean = false;
    ErrorCode error = ErrorCode::value_error;
    std::string text;

    static Value make_blank() {
        return {};
    }

    static Value make_number(double v) {
        Value value;
        value.kind = ValueKind::number;
        value.number = v;
        return value;
    }

    static Value make_text(std::string v) {
        Value value;
        value.kind = ValueKind::text;
        value.text = std::move(v);
        return value;
    }

    static Value make_boolean(bool v) {
        Value value;
        value.kind = ValueKind::boolean;
        value.boolean = v;
        return value;
    }

    static Value make_error(ErrorCode code) {
        Value value;
        value.kind = ValueKind::error;
        value.error = code;
        return value;
    }

    bool is_error() const {
        return kind == ValueKind::error;
    }

    // True for the internal diagnostics that must not count as mismatches.
    bool is_diagnostic() const {
        return kind == ValueKind::error &&
               (error == ErrorCode::unsupported || error == ErrorCode::cyclic);
    }
};

// "#REF!" etc.; diagnostics render as "#UNSUPPORTED" / "#CYCLIC".
const char *error_text(ErrorCode code);

// Parses "#REF!" etc.; returns false for non-error text.
bool parse_error_text(const std::string &text, ErrorCode &code);

// Excel general-format conversion used by & / CONCATENATE / TEXT-ish contexts.
std::string number_to_text(double value);

} // namespace xlpp
