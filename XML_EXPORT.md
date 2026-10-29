//File:    XML_EXPORT.md
//Date:    2026-10-29
//Purpose: Specification for the canonical XML export of jText
//
# jText — Canonical XML Export

**Version:** 0.1 (draft), for jText format 0.8
**Status:** Draft. Implemented: `jtext::to_xml` (`src/export/xml_export.hpp`), the
`jtext_to_xml` tool, and the import, `jtext_from_xml` (Section 6); the database and
spreadsheet round-trip tests compare through it.

---

## 1. Purpose

The XML export is the **canonical form of a jText file's data**: one XML text for one set
of data, written exactly one way. Two sources that hold the same data export to
**byte-identical** XML, so equality is a plain file comparison.

Every round trip (jText → database → jText, jText → spreadsheet → jText, and later ones)
ends in this export: the original and each copy are exported, and the exports compared.
A difference in the bytes is a difference in the data.

It exports the jText **data model** (SPEC.md), not the text form. Formatter characters,
line-number padding, blank lines, and whether a record was written as a multifield row or
one line per field do not appear: they are presentation, and they may change on every save
without changing the data. **Comments are exported** (Section 4.6): they are the author's
notes on the data, and a round trip that loses them has lost something.

Like everything in jText, it never uses CSV or any other delimiter-separated form.

## 2. Scopes

| Scope | Exports | Used for |
|---|---|---|
| `full` | header, every section with its name, includes, templates, fields, records, comments | archiving, interchange, comparing two jText files |
| `data` | per section: fields and records only, with their comments | round trips through a store that keeps no header, template or section name (a database table) |

A `data` export of a database read-back compares equal to a `data` export of the original
when the data survived. The root element names the scope.

## 3. Encoding and layout

Fixed, so that equal data gives equal bytes:

- **UTF-8**, no byte-order mark. **LF** line endings; the file ends with one LF.
- The first line is exactly `<?xml version="1.0" encoding="UTF-8"?>`.
- **Indentation:** two spaces per level. One element per line, except a value's text, which
  is on the line of its element.
- **Attributes** in the order this document lists them, in double quotes, one space before
  each. An attribute whose value is the default is **omitted**.
- **Empty elements** are written `<x/>`, never `<x></x>`.
- No comments, processing instructions, CDATA sections, DOCTYPE or namespaces.

### 3.1 Escaping

Text (element content):

| Character | Written |
|---|---|
| `&` | `&amp;` |
| `<` | `&lt;` |
| `>` | `&gt;` |
| CR (U+000D) | `&#13;` (an XML reader would turn a raw CR into LF) |

Every other character, LF and TAB included, is written as itself.

Attribute values additionally write `"` as `&quot;`, TAB as `&#9;` and LF as `&#10;` (an
XML reader would turn them into spaces).

### 3.2 Characters XML cannot hold

XML 1.0 cannot hold U+0000–U+0008, U+000B, U+000C, U+000E–U+001F, U+FFFE and U+FFFF, not
even as character references. A value containing any of them is written whole in
**base64** (RFC 4648, standard alphabet, padded, no line breaks) of its UTF-8 bytes, with
`encoding="base64"` on its element. A value without them is never base64. So each value has
exactly one form.

## 4. Elements

### 4.1 Root

```xml
<jtext format="0.8" export="0.1" scope="full" xml:space="preserve">
```

| Attribute | Value |
|---|---|
| `format` | the jText format version the exporter implements (`jtext::kFormatVersion`) |
| `export` | the version of this specification |
| `scope` | `full` or `data` |
| `xml:space` | always `preserve`: whitespace in values is data |

The file's declared `jtext_version` is header data (4.2), not `format`.

### 4.2 Header (`full` only)

```xml
  <header>
    <filename>tools_inventory.jText</filename>
    <date>2026-05-11</date>
    <purpose>Personal workshop tool catalog</purpose>
    <jtext_version>0.8</jtext_version>
    <case_mode>Insensitive</case_mode>
    <sql_dialect>SQLite</sql_dialect>
    <table_name>tools</table_name>
    <auto_id>true</auto_id>
    <related_files>
      <file>orders.jText</file>
    </related_files>
  </header>
```

The recognized header fields (SPEC 2.3), in that order, each only when the file declares
it, with its line's comment (4.6), from either profile (full-profile header lines or the light profile's `#` lines).
`case_mode` is written `Insensitive` or `Sensitive`, `auto_id` `true` or `false`, whatever
spelling the file used. `<header/>` when nothing is declared.

### 4.3 Section

```xml
  <section name="Hand Tools">
    <include kind="Schema">schema.jschma</include>
    <template name="SQL Insert">INSERT INTO tools VALUES ({1}, {2});</template>
    <fields>...</fields>
    <records>...</records>
  </section>
```

Sections in file order. In `data` scope the `name` attribute, includes and templates are
omitted. A template's body is its value's lines joined with LF (SPEC 5); the comment on its
`[*` opener is the template's `comment` (4.6).

### 4.4 Fields

```xml
    <fields>
      <field position="1" name="id" type="Integer" not-null="true" not-empty="true"/>
      <field position="2" name="name" type="String" length="40" not-empty="true"/>
      <field position="4" name="acquired" type="Date" null-date="1900-01-01"/>
    </fields>
```

One `<field>` per declared position, in position order.

| Attribute | Value | Default (omitted) |
|---|---|---|
| `position` | 1–99 | — |
| `name` | the field's name | — |
| `type` | the internal type, never the spelling: `String`, `Integer`, `Number`, `Date`, `DateTime`, `GST`, `Bool` | — |
| `auto` | the auto designation, normalized: `Identity(seed,step)` (`AutoID`, `Serial` ... are `Identity(1,1)`) | none |
| `length` | the declared length | none |
| `not-null` | `true` | `false` |
| `not-empty` | `true` | `false` |
| `null-date` | `Date`/`DateTime` only: the field's null date (`jtext::null_date`), always written for those types | — |
| `comment` | the comment on the field's declaration line (4.6) | none |

Field 1 is always written `not-null="true" not-empty="true"` (SPEC 2.4.3).

A section whose fields come from an include the exporter cannot read exports the include
(`full`) and no `<field>` elements; its records are then written by position as found.

### 4.5 Records

```xml
    <records>
      <record>
        <value position="1">3</value>
        <value position="2">Klein Screwdriver Set</value>
        <value position="3">Hand Tools/Screwdrivers/Sets</value>
        <value position="4">1900-01-01</value>
        <value position="5">{NULL}</value>
        <value position="6"/>
      </record>
    </records>
```

- Records in file order. A reader of an unordered store (a database table) orders by field 1.
- **Every** declared position, in order, in every record: a missing field and an empty
  field are both the empty string (SPEC 2.6.1), and both are written `<value position="N"/>`.
- `{NULL}` is the text `{NULL}`; `NULL` and `\N` are their text. Nothing is null.
- A multiline value's lines are joined with LF, written as LF.
- A data line's comment is the `comment` of the value it starts (4.6).
- `<records/>` when the section has none.

### 4.6 Comments

Comments are data, and a comment belongs to the line it is on (SPEC 3.2.1). The export
keeps records and values, not the layout of lines, so it names a line by its record and the
field the line starts at.

**A comment on a line** is the `comment` attribute of the element for the field where the
line starts: a header element, a `<field>`, a `<template>` (its `[*` opener's comment), or
the `<value>` of a data line's first field. On a data value it reads "the line that starts
here has this comment"; a compact row's one comment is therefore on its first value.

**A comment line** is a `<comment>` element of its own, standing directly before the
element of the line it stands before: before a header element, a `<field>`, or, inside a
`<record>`, before the `<value>` where that line starts. Comment lines at the end of a block
are the last elements of `<header>`, `<fields>` or `<records>`. Several in a row keep their
order.

```xml
    <records>
      <record>
        <comment>hand tools first</comment>
        <value position="1" comment="claw hammer">1</value>
        <value position="2">Stanley Hammer</value>
      </record>
      <comment>more to add</comment>
    </records>
```

The text is the comment as the jText reader returns it: without its comment character (or
leading `#` / `//`) and the spaces around it. An empty comment on a line is no comment, and
no attribute; an empty comment line is `<comment/>`. A comment holding a character XML
cannot hold (3.2) is written in base64: on a line as a `comment-base64` attribute in place
of `comment`, a comment line as `<comment encoding="base64">`.

## 5. Comparing

Export both sides in the same scope and compare the bytes. For a database round trip:

```
original.jtext ──export data──▶ a.xml
original.jtext ──to SQL──▶ database ──read back──▶ copy.jtext ──export data──▶ b.xml
a.xml == b.xml
```

A difference is a lost or changed value or comment, a changed type, length or constraint,
or a lost or added record. The exporter never normalizes values (no trimming beyond what the jText
reader already did, no case folding, no date reformatting), so it hides no difference.

## 6. Import

`jtext_from_xml <in.xml> <out.jtext>` reads either scope back into jText, so XML is a
complete interchange format: exporting the imported file gives the same XML, byte for byte
(the reference tools test that on every fixture, in both scopes).

- `full`: the header as declared (no date, Case or auto id is added that the export does
  not hold; the version line is the writer's), every section with its name, includes,
  templates (with their opener's comment), fields and records.
- `data`: fields and records; the sections are named `records`, `records_2`, ...
- A `<field>` becomes its declaration: the name, its `auto` designation or its type, the
  length, `Not Null` / `Nullable`, `Not Empty`, and `NullDate(...)` when it is not the
  default.
- Every comment goes back on its line, and every comment line where it stood (4.6).
- The header's `related_files` has no place in the jText writer yet; it is reported and
  left out.
