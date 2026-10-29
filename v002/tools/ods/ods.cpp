// File: tools/ods/ods.cpp
//
// OpenDocument spreadsheets (ODS.md): libzip for the archive, libxml2 for content.xml.

#include "ods/ods.hpp"
#include "parser/utf8.hpp"   // jtext::decode_utf8

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <memory>
#include <string_view>

namespace ods {

namespace {

constexpr std::string_view kMime = "application/vnd.oasis.opendocument.spreadsheet";
constexpr const char* kOffice = "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr const char* kTable = "urn:oasis:names:tc:opendocument:xmlns:table:1.0";
constexpr const char* kText = "urn:oasis:names:tc:opendocument:xmlns:text:1.0";

// ── Writing ───────────────────────────────────────────────────

auto esc(std::string_view s, bool attribute) -> std::string
{
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += attribute ? "&quot;" : "\""; break;
        default: out += c;
        }
    }
    return out;
}

// One line of a text cell as a paragraph's content: a run of spaces keeps its first
// space as itself and the rest as <text:s/>, except at the start of the paragraph, where
// all of it is <text:s/> (ODF collapses plain white space); a tab is <text:tab/>.
auto paragraph(std::string_view line) -> std::string
{
    std::string out;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == '\t') { out += "<text:tab/>"; ++i; continue; }
        if (line[i] != ' ') {
            const auto end = line.find_first_of(" \t", i);
            out += esc(line.substr(i, end - i), false);
            i = end == std::string_view::npos ? line.size() : end;
            continue;
        }
        std::size_t n = 0;
        while (i + n < line.size() && line[i + n] == ' ') ++n;
        std::size_t extra = n;
        if (i > 0) { out += ' '; --extra; }
        if (extra == 1) out += "<text:s/>";
        else if (extra > 1) out += std::format("<text:s text:c=\"{}\"/>", extra);
        i += n;
    }
    return out;
}

auto text_paragraphs(std::string_view text) -> std::string
{
    std::string out;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find('\n', start);
        const auto line = text.substr(start, end == std::string_view::npos ? end : end - start);
        out += line.empty() ? std::string{"<text:p/>"} : "<text:p>" + paragraph(line) + "</text:p>";
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

auto cell_xml(const Cell& c) -> std::string
{
    switch (c.kind) {
    case cell_kind::empty:
        return "<table:table-cell/>";
    case cell_kind::number:
        return std::format("<table:table-cell office:value-type=\"float\" office:value=\"{}\">{}</table:table-cell>",
                           esc(c.text, true), text_paragraphs(c.text));
    case cell_kind::date:
        return std::format("<table:table-cell office:value-type=\"date\" office:date-value=\"{}\">{}</table:table-cell>",
                           esc(c.text, true), text_paragraphs(c.text));
    case cell_kind::text:
        break;
    }
    return std::format("<table:table-cell office:value-type=\"string\">{}</table:table-cell>",
                       text_paragraphs(c.text));
}

auto content_xml(const std::vector<Sheet>& sheets) -> std::string
{
    std::string x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" office:version=\"1.3\">"
        "<office:body><office:spreadsheet>\n";
    for (const auto& s : sheets) {
        x += std::format("<table:table table:name=\"{}\">\n", esc(s.name, true));
        std::size_t width = 1;
        for (const auto& r : s.rows) width = std::max(width, r.size());
        x += std::format("<table:table-column table:number-columns-repeated=\"{}\"/>\n", width);
        for (const auto& r : s.rows) {
            x += "<table:table-row>";
            for (const auto& c : r) x += cell_xml(c);
            x += "</table:table-row>\n";
        }
        x += "</table:table>\n";
    }
    x += "</office:spreadsheet></office:body></office:document-content>\n";
    return x;
}

constexpr std::string_view kManifest =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">\n"
    " <manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" "
    "manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>\n"
    " <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
    " <manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n"
    "</manifest:manifest>\n";

constexpr std::string_view kStyles =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
    "office:version=\"1.3\"/>\n";

// ── Reading ───────────────────────────────────────────────────

auto is(const xmlNode* n, const char* ns, const char* name) -> bool
{
    return n->type == XML_ELEMENT_NODE && n->ns && xmlStrcmp(n->ns->href, BAD_CAST ns) == 0 &&
           xmlStrcmp(n->name, BAD_CAST name) == 0;
}

auto attr(const xmlNode* n, const char* ns, const char* name) -> std::string
{
    std::unique_ptr<xmlChar, decltype(xmlFree)> v(xmlGetNsProp(n, BAD_CAST name, BAD_CAST ns), xmlFree);
    return v ? std::string(reinterpret_cast<const char*>(v.get())) : std::string{};
}

auto count(const xmlNode* n, const char* ns, const char* name) -> std::size_t
{
    const auto v = attr(n, ns, name);
    std::size_t c = 1;
    if (!v.empty()) std::from_chars(v.data(), v.data() + v.size(), c);
    return std::max<std::size_t>(c, 1);
}

// A paragraph's text: <text:s/> is spaces, <text:tab/> a tab, <text:line-break/> a new
// line. Plain text is kept as it is, spaces included: LibreOffice writes a cell's spaces
// literally and reads them back so, rather than collapsing them as ODF's paragraph rule
// would.
void paragraph_text(const xmlNode* n, std::string& out)
{
    for (const xmlNode* c = n->children; c; c = c->next) {
        if (c->type == XML_TEXT_NODE || c->type == XML_CDATA_SECTION_NODE) {
            if (c->content) out += reinterpret_cast<const char*>(c->content);
        } else if (is(c, kText, "s")) {
            out.append(count(c, kText, "c"), ' ');
        } else if (is(c, kText, "tab")) {
            out += '\t';
        } else if (is(c, kText, "line-break")) {
            out += '\n';
        } else if (c->type == XML_ELEMENT_NODE && !is(c, kOffice, "annotation")) {
            paragraph_text(c, out);   // text:span, text:a, ...
        }
    }
}

auto cell_of(const xmlNode* n) -> Cell
{
    const auto type = attr(n, kOffice, "value-type");
    if (type == "float" || type == "percentage" || type == "currency") return {cell_kind::number, attr(n, kOffice, "value")};
    if (type == "date") return {cell_kind::date, attr(n, kOffice, "date-value")};
    std::string text;
    bool first = true;
    for (const xmlNode* c = n->children; c; c = c->next) {
        if (!is(c, kText, "p")) continue;
        if (!first) text += '\n';
        first = false;
        paragraph_text(c, text);
    }
    if (type.empty() && text.empty()) return {};
    return {cell_kind::text, std::move(text)};
}

void read_rows(const xmlNode* parent, Sheet& sheet)
{
    for (const xmlNode* r = parent->children; r; r = r->next) {
        if (is(r, kTable, "table-rows") || is(r, kTable, "table-header-rows") || is(r, kTable, "table-row-group")) {
            read_rows(r, sheet);
            continue;
        }
        if (!is(r, kTable, "table-row")) continue;
        std::vector<Cell> row;
        for (const xmlNode* c = r->children; c; c = c->next) {
            if (!is(c, kTable, "table-cell") && !is(c, kTable, "covered-table-cell")) continue;
            const auto cell = cell_of(c);
            const auto n = count(c, kTable, "number-columns-repeated");
            // A long run of empty cells is how a row's unused columns are written.
            if (cell.kind == cell_kind::empty && n > 1024) continue;
            row.insert(row.end(), n, cell);
        }
        while (!row.empty() && row.back().kind == cell_kind::empty) row.pop_back();
        if (row.empty()) continue;   // empty rows, however many are repeated
        const auto n = std::min<std::size_t>(count(r, kTable, "number-rows-repeated"), 1'000'000);
        sheet.rows.insert(sheet.rows.end(), n, row);
    }
}

}  // namespace

auto xml_can_hold(const std::string& s) -> bool
{
    for (std::size_t i = 0; i < s.size();) {
        const auto d = jtext::decode_utf8(std::string_view(s).substr(i));
        if (!d) return false;
        const char32_t c = d->cp;
        if ((c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) || c == 0xFFFE || c == 0xFFFF) return false;
        i += d->len;
    }
    return true;
}

auto write(const std::string& path, const std::vector<Sheet>& sheets) -> std::expected<void, std::string>
{
    int err = 0;
    zip_t* z = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) {
        zip_error_t e;
        zip_error_init_with_code(&e, err);
        std::string msg = zip_error_strerror(&e);
        zip_error_fini(&e);
        return std::unexpected(path + ": " + msg);
    }
    // The buffers live until zip_close() writes them.
    const std::string content = content_xml(sheets);
    const std::vector<std::pair<const char*, std::string_view>> entries{
        {"mimetype", kMime}, {"META-INF/manifest.xml", kManifest}, {"styles.xml", kStyles},
        {"content.xml", content}};
    for (const auto& [name, data] : entries) {
        zip_source_t* src = zip_source_buffer(z, data.data(), data.size(), 0);
        const zip_int64_t idx = src ? zip_file_add(z, name, src, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) : -1;
        if (idx < 0) {
            if (src) zip_source_free(src);
            std::string msg = zip_strerror(z);
            zip_discard(z);
            return std::unexpected(path + ": " + msg);
        }
        // OpenDocument: mimetype first, stored, so a reader can identify the file.
        zip_set_file_compression(z, static_cast<zip_uint64_t>(idx),
                                 std::string_view(name) == "mimetype" ? ZIP_CM_STORE : ZIP_CM_DEFLATE, 0);
    }
    if (zip_close(z) != 0) {
        std::string msg = zip_strerror(z);
        zip_discard(z);
        return std::unexpected(path + ": " + msg);
    }
    return {};
}

auto read(const std::string& path) -> std::expected<std::vector<Sheet>, std::string>
{
    int err = 0;
    std::unique_ptr<zip_t, decltype(&zip_discard)> z(zip_open(path.c_str(), ZIP_RDONLY, &err), zip_discard);
    if (!z) return std::unexpected(path + ": not a readable zip archive");
    zip_stat_t st;
    if (zip_stat(z.get(), "content.xml", 0, &st) != 0 || !(st.valid & ZIP_STAT_SIZE))
        return std::unexpected(path + ": no content.xml (not an OpenDocument file)");
    std::unique_ptr<zip_file_t, decltype(&zip_fclose)> f(zip_fopen(z.get(), "content.xml", 0), zip_fclose);
    if (!f) return std::unexpected(path + ": cannot open content.xml");
    std::string content(static_cast<std::size_t>(st.size), '\0');
    if (zip_fread(f.get(), content.data(), st.size) != static_cast<zip_int64_t>(st.size))
        return std::unexpected(path + ": cannot read content.xml");

    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
        xmlReadMemory(content.data(), static_cast<int>(content.size()), "content.xml", "UTF-8",
                      XML_PARSE_NONET | XML_PARSE_HUGE),
        xmlFreeDoc);
    if (!doc) return std::unexpected(path + ": content.xml is not well-formed XML");

    std::vector<Sheet> sheets;
    const auto walk = [&](auto&& self, const xmlNode* n) -> void {
        for (const xmlNode* c = n; c; c = c->next) {
            if (is(c, kTable, "table")) {
                Sheet s{attr(c, kTable, "name"), {}};
                read_rows(c, s);
                sheets.push_back(std::move(s));
            } else if (c->type == XML_ELEMENT_NODE) {
                self(self, c->children);
            }
        }
    };
    walk(walk, xmlDocGetRootElement(doc.get()));
    return sheets;
}

}  // namespace ods
