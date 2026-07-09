#include "xml_scan.hpp"

#include <miniz.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace xlpp {
namespace detail {

std::string xml_unescape(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out += in[i];
            continue;
        }
        auto end = in.find(';', i);
        if (end == std::string::npos) {
            throw std::runtime_error("xml_unescape: unterminated entity");
        }
        auto entity = in.substr(i + 1, end - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (!entity.empty() && entity[0] == '#') {
            auto code = entity[1] == 'x' || entity[1] == 'X'
                ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                : std::strtoul(entity.c_str() + 1, nullptr, 10);
            if (code == 0 || code > 0x10FFFF) {
                throw std::runtime_error("xml_unescape: bad char reference &" + entity + ";");
            }
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        } else {
            throw std::runtime_error("xml_unescape: unknown entity &" + entity + ";");
        }
        i = end;
    }
    return out;
}

const std::string *XmlTag::attr(const std::string &key) const {
    for (const auto &kv : attrs) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

XmlTag find_xml_tag(const std::string &xml, const std::string &name, std::size_t from) {
    XmlTag tag;
    const std::string open = "<" + name;
    auto pos = from;
    while (true) {
        pos = xml.find(open, pos);
        if (pos == std::string::npos) {
            return tag;
        }
        char next = pos + open.size() < xml.size() ? xml[pos + open.size()] : '\0';
        if (next == ' ' || next == '>' || next == '/' || next == '\t' || next == '\n') {
            break;
        }
        pos += open.size();
    }
    tag.start = pos;
    auto i = pos + open.size();
    while (i < xml.size()) {
        while (i < xml.size() && (xml[i] == ' ' || xml[i] == '\t' || xml[i] == '\n' ||
                                  xml[i] == '\r')) {
            ++i;
        }
        if (i >= xml.size()) {
            throw std::runtime_error("find_xml_tag: unterminated <" + name + ">");
        }
        if (xml[i] == '>') {
            tag.content_begin = i + 1;
            return tag;
        }
        if (xml[i] == '/') {
            tag.self_closing = true;
            tag.content_begin = xml.find('>', i);
            if (tag.content_begin == std::string::npos) {
                throw std::runtime_error("find_xml_tag: unterminated <" + name + "/>");
            }
            ++tag.content_begin;
            return tag;
        }
        auto eq = xml.find('=', i);
        if (eq == std::string::npos) {
            throw std::runtime_error("find_xml_tag: malformed attribute in <" + name + ">");
        }
        auto key = xml.substr(i, eq - i);
        auto quote = xml[eq + 1];
        if (quote != '"' && quote != '\'') {
            throw std::runtime_error("find_xml_tag: unquoted attribute in <" + name + ">");
        }
        auto val_end = xml.find(quote, eq + 2);
        if (val_end == std::string::npos) {
            throw std::runtime_error("find_xml_tag: unterminated attribute in <" + name + ">");
        }
        tag.attrs.emplace_back(key, xml_unescape(xml.substr(eq + 2, val_end - eq - 2)));
        i = val_end + 1;
    }
    throw std::runtime_error("find_xml_tag: unterminated <" + name + ">");
}

std::string xml_element_text(const std::string &xml, const XmlTag &tag,
                             const std::string &name) {
    if (tag.self_closing) {
        return {};
    }
    const std::string close = "</" + name + ">";
    auto end = xml.find(close, tag.content_begin);
    if (end == std::string::npos) {
        throw std::runtime_error("xml_element_text: missing " + close);
    }
    return xml_unescape(xml.substr(tag.content_begin, end - tag.content_begin));
}

std::string read_zip_entry(const std::string &xlsx_path, const char *entry) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, xlsx_path.c_str(), 0)) {
        throw std::runtime_error("xml_scan: cannot open zip: " + xlsx_path);
    }
    std::size_t size = 0;
    void *data = mz_zip_reader_extract_file_to_heap(&zip, entry, &size, 0);
    if (data == nullptr) {
        mz_zip_reader_end(&zip);
        throw std::runtime_error("xml_scan: missing " + std::string(entry) + " in " +
                                 xlsx_path);
    }
    std::string content(static_cast<const char *>(data), size);
    mz_free(data);
    mz_zip_reader_end(&zip);
    return content;
}

} // namespace detail
} // namespace xlpp
