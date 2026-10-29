// File: tools/jtext_from_html.cpp
//
// jtext_from_html - an HTML page written by jtext_to_html (HTML_EXPORT.md) back into
// jText. Its tables are read back into the canonical XML export they were laid out from
// (XML_EXPORT.md), element for element, and that goes into jText as jtext_from_xml does.
//
//   jtext_from_html <in.html> <out.jtext>

#include "common/xml_import.hpp"

#include <libxml/HTMLparser.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace xi = jtools::xml_import;

namespace {

using Node = xmlNode;
using Doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;

auto has_class(const Node* n, std::string_view cls) -> bool
{
    const std::string c = xi::attr_or(n, "class");
    return c == cls;
}

// The element children of `n` named `name`, in order (through <thead>/<tbody>).
auto children(const Node* n, std::string_view name) -> std::vector<const Node*>
{
    std::vector<const Node*> out;
    for (const Node* c = n ? n->children : nullptr; c; c = c->next) {
        if (xi::name_is(c, name)) out.push_back(c);
        else if (xi::name_is(c, "thead") || xi::name_is(c, "tbody")) {
            auto more = children(c, name);
            out.insert(out.end(), more.begin(), more.end());
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

auto b64(const Node* cell) -> bool { return xi::attr_or(cell, "data-encoding") == "base64"; }

// An XML element `name` under `parent`, holding a cell's text (base64 stays base64, marked).
auto add_text(Node* parent, const char* name, const Node* cell) -> Node*
{
    Node* e = xmlNewChild(parent, nullptr, reinterpret_cast<const xmlChar*>(name), nullptr);
    const std::string t = content(cell);
    xmlAddChild(e, xmlNewText(reinterpret_cast<const xmlChar*>(t.c_str())));
    if (b64(cell)) xmlNewProp(e, reinterpret_cast<const xmlChar*>("encoding"), reinterpret_cast<const xmlChar*>("base64"));
    return e;
}

void set(Node* e, const char* name, const std::string& value)
{
    xmlNewProp(e, reinterpret_cast<const xmlChar*>(name), reinterpret_cast<const xmlChar*>(value.c_str()));
}

// A comment cell as the element's line comment: `comment`, or `comment-base64`.
void set_comment(Node* e, const Node* cell)
{
    const std::string t = content(cell);
    if (!t.empty()) set(e, b64(cell) ? "comment-base64" : "comment", t);
}

auto find_table(const Node* n, std::string_view cls) -> const Node*
{
    for (const Node* c = n ? n->children : nullptr; c; c = c->next) {
        if (xi::name_is(c, "table") && has_class(c, cls)) return c;
        if (const Node* deeper = find_table(c, cls)) return deeper;
    }
    return nullptr;
}

void header(const Node* table, Node* root)
{
    Node* h = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("header"), nullptr);
    Node* related = nullptr;
    for (const Node* tr : children(table, "tr")) {
        const auto td = children(tr, "td");
        if (has_class(tr, "comment-line") && !td.empty()) {
            add_text(h, "comment", td[0]);
            continue;
        }
        const std::string element = xi::attr_or(tr, "data-element");
        if (element.empty() || td.size() < 3) continue;
        if (element == "related_file") {
            if (!related) related = xmlNewChild(h, nullptr, reinterpret_cast<const xmlChar*>("related_files"), nullptr);
            add_text(related, "file", td[1]);
            continue;
        }
        set_comment(add_text(h, element.c_str(), td[1]), td[2]);
    }
}

void section(const Node* sec, Node* root)
{
    Node* s = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("section"), nullptr);
    if (auto name = xi::attr(sec, "data-name")) set(s, "name", *name);

    if (const Node* parts = find_table(sec, "jtext-parts")) {
        for (const Node* tr : children(parts, "tr")) {
            const auto td = children(tr, "td");
            const std::string element = xi::attr_or(tr, "data-element");
            if (td.size() < 4) continue;
            if (element == "include") set(add_text(s, "include", td[2]), "kind", content(td[1]));
            else if (element == "template") {
                Node* t = add_text(s, "template", td[2]);
                set(t, "name", content(td[1]));
                set_comment(t, td[3]);
            }
        }
    }

    // Fields: a column per attribute, in the export's order.
    static constexpr const char* kAttrs[] = {"position", "name", "type", "auto", "length",
                                             "not-null", "not-empty", "null-date"};
    Node* fields = xmlNewChild(s, nullptr, reinterpret_cast<const xmlChar*>("fields"), nullptr);
    for (const Node* tr : children(find_table(sec, "jtext-fields"), "tr")) {
        const auto td = children(tr, "td");
        if (has_class(tr, "comment-line") && !td.empty()) {
            add_text(fields, "comment", td[0]);
            continue;
        }
        if (xi::attr_or(tr, "data-element") != "field" || td.size() < 9) continue;
        Node* f = xmlNewChild(fields, nullptr, reinterpret_cast<const xmlChar*>("field"), nullptr);
        for (std::size_t i = 0; i < std::size(kAttrs); ++i)
            if (const std::string v = content(td[i]); !v.empty()) set(f, kAttrs[i], v);
        set_comment(f, td[8]);
    }

    // Records: each value by its position; each comment before, or on, the line it names.
    Node* records = xmlNewChild(s, nullptr, reinterpret_cast<const xmlChar*>("records"), nullptr);
    for (const Node* tr : children(find_table(sec, "jtext-records"), "tr")) {
        const auto td = children(tr, "td");
        if (has_class(tr, "comment-line") && !td.empty()) {
            add_text(records, "comment", td[0]);
            continue;
        }
        if (xi::attr_or(tr, "data-element") != "record") continue;
        std::vector<const Node*> notes;
        for (const Node* cell : td)
            if (has_class(cell, "comments"))
                for (const Node* ul : children(cell, "ul"))
                    for (const Node* li : children(ul, "li")) notes.push_back(li);
        Node* r = xmlNewChild(records, nullptr, reinterpret_cast<const xmlChar*>("record"), nullptr);
        for (const Node* cell : td) {
            const auto p = xi::attr(cell, "data-position");
            if (!p) continue;
            for (const Node* li : notes)
                if (xi::attr_or(li, "data-kind") == "comment-line" && xi::attr_or(li, "data-position") == *p)
                    add_text(r, "comment", li);
            Node* v = add_text(r, "value", cell);
            set(v, "position", *p);
            for (const Node* li : notes)
                if (xi::attr_or(li, "data-kind") == "line" && xi::attr_or(li, "data-position") == *p) set_comment(v, li);
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: jtext_from_html <in.html> <out.jtext>\n";
        return 1;
    }
    Doc html(htmlReadFile(argv[1], "UTF-8", HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET), xmlFreeDoc);
    const Node* body = nullptr;
    for (const Node* n = html ? xmlDocGetRootElement(html.get()) : nullptr; n && !body; n = n->next)
        for (const Node* c = n->children; c; c = c->next)
            if (xi::name_is(c, "body")) body = c;
    if (!body || xi::attr_or(body, "data-scope").empty()) {
        std::cerr << "ERROR: " << argv[1] << " is not a page jtext_to_html wrote (no data-scope on <body>)\n";
        return 2;
    }

    // The canonical XML the page was laid out from.
    Doc xml(xmlNewDoc(reinterpret_cast<const xmlChar*>("1.0")), xmlFreeDoc);
    Node* root = xmlNewNode(nullptr, reinterpret_cast<const xmlChar*>("jtext"));
    xmlDocSetRootElement(xml.get(), root);
    for (const char* a : {"format", "export", "scope"}) set(root, a, xi::attr_or(body, (std::string("data-") + a).c_str()));
    if (const Node* h = find_table(body, "jtext-header")) header(h, root);
    for (const Node* c = body->children; c; c = c->next)
        if (xi::name_is(c, "section") && has_class(c, "jtext-section")) section(c, root);

    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "ERROR: cannot write " << argv[2] << "\n";
        return 3;
    }
    const std::string name = fs::path(argv[2]).filename().string();
    const std::string source = fs::path(argv[1]).filename().string();
    const std::size_t sections = xi::write_jtext(root, out, name, source, "type=html file=" + source);
    std::cout << "Wrote " << argv[2] << " (" << sections << " section(s))\n";
    if (xi::g_dropped)
        std::cerr << "Note: the header's related_files has no place in the jText writer and was left out\n";
    return 0;
}
