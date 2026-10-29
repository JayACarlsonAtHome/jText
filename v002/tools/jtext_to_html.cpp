// File: tools/jtext_to_html.cpp
//
// jtext_to_html - a jText file as an HTML page (HTML_EXPORT.md): the canonical XML export
// (XML_EXPORT.md), element for element, laid out as plain tables a browser shows. A header
// table; per section a table of its includes and templates, one of its fields, and one of
// its records with a Comments column. jtext_from_html reads it back.
//
//   jtext_to_html [--data] <file> [<out.html>]

#include "export/xml_export.hpp"
#include "parser/section.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Node = xmlNode;

auto name_is(const Node* n, std::string_view name) -> bool
{
    return n && n->type == XML_ELEMENT_NODE && name == reinterpret_cast<const char*>(n->name);
}

auto attr(const Node* n, const char* name) -> std::optional<std::string>
{
    xmlChar* v = xmlGetProp(n, reinterpret_cast<const xmlChar*>(name));
    if (!v) return std::nullopt;
    std::string out(reinterpret_cast<const char*>(v));
    xmlFree(v);
    return out;
}

// Text as HTML holds it: & < > " escaped, CR as a reference (a reader would make it LF).
auto esc(std::string_view s) -> std::string
{
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\r': out += "&#13;"; break;
        default: out += c;
        }
    }
    return out;
}

auto content(const Node* n) -> std::string
{
    xmlChar* v = xmlNodeGetContent(n);
    std::string out = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return out;
}

// A cell for an element's text: base64 stays base64, marked (XML_EXPORT.md 3.2).
auto text_cell(const Node* n, std::string_view extra = {}) -> std::string
{
    const bool b64 = attr(n, "encoding").value_or("") == "base64";
    return std::format("<td{}{}>{}</td>", extra, b64 ? " data-encoding=\"base64\"" : "", esc(content(n)));
}

// A cell for an element's line comment (`comment` or `comment-base64`); empty when none.
auto comment_cell(const Node* n) -> std::string
{
    if (auto c = attr(n, "comment")) return "<td>" + esc(*c) + "</td>";
    if (auto c = attr(n, "comment-base64")) return "<td data-encoding=\"base64\">" + esc(*c) + "</td>";
    return "<td></td>";
}

// A comment line: a row of its own across the table.
auto comment_row(const Node* n, int columns) -> std::string
{
    return std::format("<tr class=\"comment-line\">{}</tr>\n",
                       text_cell(n, std::format(" colspan=\"{}\"", columns)));
}

void header(const Node* h, std::string& out)
{
    out += "<table class=\"jtext-header\">\n<thead><tr><th>Header</th><th>Value</th><th>Comment</th></tr></thead>\n<tbody>\n";
    for (const Node* e = h->children; e; e = e->next) {
        if (name_is(e, "comment")) out += comment_row(e, 3);
        else if (name_is(e, "related_files")) {
            for (const Node* f = e->children; f; f = f->next)
                if (name_is(f, "file"))
                    out += "<tr data-element=\"related_file\"><td>related file</td>" + text_cell(f) + "<td></td></tr>\n";
        } else if (e->type == XML_ELEMENT_NODE) {
            const std::string name = reinterpret_cast<const char*>(e->name);
            out += std::format("<tr data-element=\"{0}\"><td>{0}</td>{1}{2}</tr>\n", name, text_cell(e), comment_cell(e));
        }
    }
    out += "</tbody>\n</table>\n";
}

constexpr const char* kFieldAttrs[] = {"position", "name", "type", "auto", "length",
                                       "not-null", "not-empty", "null-date"};

void section(const Node* s, std::string& out)
{
    const auto name = attr(s, "name");
    out += name ? std::format("<section class=\"jtext-section\" data-name=\"{0}\">\n<h2>Section: {0}</h2>\n", esc(*name))
                : std::string("<section class=\"jtext-section\">\n<h2>Records</h2>\n");

    // Includes and templates.
    std::string parts;
    const Node* fields = nullptr;
    const Node* records = nullptr;
    for (const Node* c = s->children; c; c = c->next) {
        if (name_is(c, "include"))
            parts += std::format("<tr data-element=\"include\"><td>include</td><td>{}</td>{}<td></td></tr>\n",
                                 esc(attr(c, "kind").value_or("")), text_cell(c));
        else if (name_is(c, "template"))
            parts += std::format("<tr data-element=\"template\"><td>template</td><td>{}</td>{}{}</tr>\n",
                                 esc(attr(c, "name").value_or("")), text_cell(c), comment_cell(c));
        else if (name_is(c, "fields")) fields = c;
        else if (name_is(c, "records")) records = c;
    }
    if (!parts.empty())
        out += "<table class=\"jtext-parts\">\n<thead><tr><th>Part</th><th>Name</th><th>Value</th><th>Comment</th>"
               "</tr></thead>\n<tbody>\n" + parts + "</tbody>\n</table>\n";

    // Fields: one row per <field>, a column per attribute.
    std::vector<std::pair<std::string, std::string>> columns;   // position, name: the records' header
    out += "<table class=\"jtext-fields\">\n<thead><tr><th>Position</th><th>Name</th><th>Type</th><th>Auto</th>"
           "<th>Length</th><th>Not Null</th><th>Not Empty</th><th>Null Date</th><th>Comment</th></tr></thead>\n<tbody>\n";
    for (const Node* f = fields ? fields->children : nullptr; f; f = f->next) {
        if (name_is(f, "comment")) out += comment_row(f, 9);
        else if (name_is(f, "field")) {
            out += "<tr data-element=\"field\">";
            for (const char* a : kFieldAttrs) out += "<td>" + esc(attr(f, a).value_or("")) + "</td>";
            out += comment_cell(f) + "</tr>\n";
            columns.emplace_back(attr(f, "position").value_or(""), attr(f, "name").value_or(""));
        }
    }
    out += "</tbody>\n</table>\n";

    // Records: a column per field, then the record's comments, each naming its line.
    const int width = static_cast<int>(columns.size()) + 1;
    out += "<table class=\"jtext-records\">\n<thead><tr>";
    for (const auto& [p, n] : columns) out += std::format("<th data-position=\"{}\">{}</th>", esc(p), esc(n));
    out += "<th>Comments</th></tr></thead>\n<tbody>\n";
    for (const Node* r = records ? records->children : nullptr; r; r = r->next) {
        if (name_is(r, "comment")) {
            out += comment_row(r, width);
            continue;
        }
        if (!name_is(r, "record")) continue;
        std::string cells, notes;
        std::vector<const Node*> before;
        for (const Node* v = r->children; v; v = v->next) {
            if (name_is(v, "comment")) {
                before.push_back(v);
                continue;
            }
            if (!name_is(v, "value")) continue;
            const std::string p = attr(v, "position").value_or("");
            for (const Node* c : before) {
                const bool b64 = attr(c, "encoding").value_or("") == "base64";
                notes += std::format("<li data-kind=\"comment-line\" data-position=\"{}\"{}>{}</li>", p,
                                     b64 ? " data-encoding=\"base64\"" : "", esc(content(c)));
            }
            before.clear();
            if (auto c = attr(v, "comment"))
                notes += std::format("<li data-kind=\"line\" data-position=\"{}\">{}</li>", p, esc(*c));
            else if (auto c = attr(v, "comment-base64"))
                notes += std::format("<li data-kind=\"line\" data-position=\"{}\" data-encoding=\"base64\">{}</li>", p,
                                     esc(*c));
            cells += text_cell(v, std::format(" data-position=\"{}\"", p));
        }
        out += "<tr data-element=\"record\">" + cells + "<td class=\"comments\">" +
               (notes.empty() ? std::string{} : "<ul>" + notes + "</ul>") + "</td></tr>\n";
    }
    out += "</tbody>\n</table>\n</section>\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::vector<std::string_view> args(argv + 1, argv + argc);
    auto scope = jtext::xml_scope::full;
    if (!args.empty() && args.front() == "--data") {
        scope = jtext::xml_scope::data;
        args.erase(args.begin());
    }
    if (args.empty() || args.size() > 2) {
        std::cerr << "Usage: jtext_to_html [--data] <file> [<out.html>]\n";
        return 1;
    }
    std::ifstream in{std::string(args[0])};
    if (!in) {
        std::cerr << "ERROR: cannot read " << args[0] << "\n";
        return 2;
    }
    const auto pf = jtext::parse_file_structure(in);
    if (!pf) {
        std::cerr << "ERROR: " << args[0] << ": " << pf.error().to_string() << "\n";
        return 2;
    }
    const std::string xml = jtext::to_xml(*pf, scope);
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
        xmlReadMemory(xml.data(), static_cast<int>(xml.size()), nullptr, "UTF-8", XML_PARSE_NONET), xmlFreeDoc);
    const Node* root = doc ? xmlDocGetRootElement(doc.get()) : nullptr;
    if (!name_is(root, "jtext")) {
        std::cerr << "ERROR: the XML export did not read back\n";
        return 2;
    }

    std::string title = std::string(args[0]);
    for (const Node* c = root->children; c; c = c->next)
        if (name_is(c, "header"))
            for (const Node* e = c->children; e; e = e->next)
                if (name_is(e, "filename")) title = content(e);

    std::string out = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>" + esc(title) +
                      "</title>\n<style>\n"
                      "table { border-collapse: collapse; margin: 0 0 1.5em 0; }\n"
                      "th, td { border: 1px solid #999; padding: 2px 6px; vertical-align: top; text-align: left; "
                      "white-space: pre-wrap; }\n"
                      "th { background: #eee; }\n"
                      "tr.comment-line td { font-style: italic; color: #555; }\n"
                      "td.comments ul { margin: 0; padding-left: 1.2em; }\n"
                      "li[data-kind=\"line\"]::before { content: \"line \" attr(data-position) \": \"; color: #777; }\n"
                      "li[data-kind=\"comment-line\"]::before { content: \"// before line \" attr(data-position) \": \"; "
                      "color: #777; }\n"
                      "</style>\n</head>\n";
    out += std::format("<body data-format=\"{}\" data-export=\"{}\" data-scope=\"{}\">\n<h1>{}</h1>\n",
                       esc(attr(root, "format").value_or("")), esc(attr(root, "export").value_or("")),
                       esc(attr(root, "scope").value_or("")), esc(title));
    for (const Node* c = root->children; c; c = c->next) {
        if (name_is(c, "header")) header(c, out);
        else if (name_is(c, "section")) section(c, out);
    }
    out += "</body>\n</html>\n";

    if (args.size() == 1) {
        std::cout << out;
        return 0;
    }
    std::ofstream file{std::string(args[1]), std::ios::binary};
    if (!(file << out)) {
        std::cerr << "ERROR: cannot write " << args[1] << "\n";
        return 3;
    }
    return 0;
}
