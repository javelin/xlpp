#pragma once

// Internal minimal XML scanning + zip access shared by the sidecar and the
// shared-formula loader. Targeted at Excel's machine-generated parts; not a
// general XML parser.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace xlpp {
namespace detail {

std::string xml_unescape(const std::string &in);

// One parsed start tag: attributes plus positions for iterating content.
struct XmlTag {
    std::size_t start = std::string::npos; // position of '<'
    std::size_t content_begin = 0;         // just past '>'
    bool self_closing = false;
    std::vector<std::pair<std::string, std::string>> attrs;

    const std::string *attr(const std::string &key) const;
};

// Finds the next <name ...> start tag at/after `from` (exact element name).
// Returns tag with npos start if none. Throws std::runtime_error on
// malformed input.
XmlTag find_xml_tag(const std::string &xml, const std::string &name, std::size_t from);

// Text content of a non-self-closing tag up to </name>, unescaped.
std::string xml_element_text(const std::string &xml, const XmlTag &tag,
                             const std::string &name);

// Reads one entry from an xlsx zip; throws std::runtime_error when missing.
std::string read_zip_entry(const std::string &xlsx_path, const char *entry);

} // namespace detail
} // namespace xlpp
