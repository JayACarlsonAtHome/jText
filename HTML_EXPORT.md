//File:    HTML_EXPORT.md
//Date:    2026-10-29
//Purpose: Specification for the HTML export and import of jText
//
# jText — HTML Export and Import

**Version:** 0.1 (draft), for jText format 0.8
**Status:** Draft. Implemented: `jtext_to_html` and `jtext_from_html`; the reference tools
test the round trip (jText → HTML → jText, compared through the XML export) on every
fixture, in both scopes.

---

## 1. Purpose

A jText file as a page any browser shows, and back. The page is the canonical XML export
(`XML_EXPORT.md`) element for element, laid out as plain HTML tables: it holds exactly what
the XML holds, the same scopes (`full`, `data`), the same values, declarations and comments,
and the same base64 rule. Like everything in jText, it never uses CSV or any other
delimiter-separated form.

## 2. The page

- `<body data-format=".." data-export=".." data-scope="full|data">`: the XML root's attributes.
- **Header** (`full` only): `<table class="jtext-header">`, one row per header element,
  `<tr data-element="NAME">` with the columns Header, Value, Comment. A related file is a
  `related_file` row.
- **Section:** `<section class="jtext-section" data-name="NAME">` (no `data-name` in `data`
  scope), holding:
  - `<table class="jtext-parts">`: its includes (`data-element="include"`: kind, path) and
    templates (`data-element="template"`: name, body, the opener's comment). Omitted when
    there are none.
  - `<table class="jtext-fields">`: one `<tr data-element="field">` per `<field>`, a column
    per attribute in the XML's order (Position, Name, Type, Auto, Length, Not Null, Not Empty,
    Null Date), then Comment. An attribute the XML omits is an empty cell.
  - `<table class="jtext-records">`: a column per field (`<th data-position="N">name</th>`),
    then **Comments**. Each `<tr data-element="record">` has a `<td data-position="N">` per
    value, and a `<td class="comments">` with a `<ul>` of the record's comments:
    `<li data-kind="line" data-position="N">` is the comment on the line that starts at
    field N; `<li data-kind="comment-line" data-position="N">` a comment line standing
    before it. A style sheet shows each with its place ("line 3:", "// before line 3:").
- **Comment lines** of a block (the header, the fields, the end of the records) are rows of
  their own, `<tr class="comment-line">`, one cell across the table, where they stood.

## 3. Text

Cells hold their text as written, escaped (`&`, `<`, `>`, `"`), a CR as `&#13;`, every
other character, LF and TAB included, as itself; the style sheet shows them with
`white-space: pre-wrap`. A value or comment the XML writes in base64 (XML_EXPORT.md 3.2)
is the same base64 in its cell, marked `data-encoding="base64"`.

## 4. Import

`jtext_from_html <in.html> <out.jtext>` reads the tables back into the canonical XML, then
into jText as `jtext_from_xml` does (XML_EXPORT.md 6). A page edited by hand imports as long
as its tables keep this shape.
