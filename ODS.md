//File:    ODS.md
//Date:    2026-10-29
//Purpose: How jText is laid out in an OpenDocument spreadsheet (.ods)
//
# jText — OpenDocument Spreadsheet (.ods)

**Version:** 0.1 (draft), for jText format 0.8
**Tools:** `jtext_to_ods`, `jtext_from_ods` (built where libzip and libxml2 are installed)

---

## 1. Purpose

A jText file as a spreadsheet LibreOffice Calc (and other OpenDocument programs) opens,
edits and saves, and back. Everything a jText section holds survives the round trip:
every value, every field declaration (including what a database cannot keep, such as
`Not Empty` and `NullDate(...)`), and every comment. Round trips are checked by comparing
the canonical XML export (`XML_EXPORT.md`) of the original and of the copy.

Like everything in jText, it never uses CSV: the file is an OpenDocument zip archive of
XML, read and written through libzip and libxml2.

## 2. The file

An OpenDocument spreadsheet, version 1.3: a zip archive whose first entry is `mimetype`
(stored, uncompressed: `application/vnd.oasis.opendocument.spreadsheet`), then
`META-INF/manifest.xml`, `styles.xml` and `content.xml`, which holds the sheets.

## 3. Sheets

Each jText section is two sheets, in section order:

| Sheet | Holds |
|---|---|
| `<section name>` | the data: what a person reads and edits |
| `<section name> (jText)` | what a grid cannot show: the field declarations and the comments |

A section name holding a character a sheet name cannot (`[ ] * ? : / \`) is refused.

### 3.1 The data sheet

- **Row 1:** the field names, one per column, in field position order.
- **Then one row per record,** one cell per field: column N is field N.
- A value is a cell:

| Field type | Cell |
|---|---|
| `Integer`, `Number` | a number cell (`office:value-type="float"`), its `office:value` the value's own text, when that text is a plain decimal number (`-12`, `3.50`, `1e3`); otherwise a text cell |
| `Date` | a date cell (`office:value-type="date"`), its `office:date-value` the value, when it is a valid date; otherwise a text cell |
| every other type | a text cell |

- `{NULL}` is the text `{NULL}`; an empty value is an empty cell (SPEC 2.6.1).
- Text is kept exactly: runs of spaces as `<text:s text:c="n"/>`, tabs as `<text:tab/>`,
  and each line of a multiline value as its own paragraph (`<text:p>`).
- A value holding a character XML cannot hold (`XML_EXPORT.md` 3.2) cannot be stored in a
  spreadsheet; the export refuses it, naming the record and field.

### 3.2 The companion sheet `<section name> (jText)`

Column A says what each row is:

| A | B | C | D | E | F | G ... |
|---|---|---|---|---|---|---|
| `jText` | the section name | the jText format version | | | | |
| `field` | position | the declaration line's comment | the declaration's levels, one per cell: `name`, `Integer`, `40`, `Not Null`, `NullDate(1900-01-01)` ... | | | |
| `comment` | block (`fields` / `data`) | kind (`line` / `comment-line`) | record id | position | seq | the text |

Row 1 is the `jText` row. The `comment` rows are the comments named as SPEC 4.3 names them
(a line by its record and the field it starts at; a comment line by the line it stands
before, or at the end of its block): the same rows a database keeps in `<table>_comments`.
All cells here are text cells.

## 4. Reading

- A data sheet is any sheet whose name does not end in ` (jText)`. Its companion, when
  present, gives the declarations and comments; without one, row 1 names the fields:
  column 1 becomes an `Integer` field (the record id, SPEC 2.4.3) and every other column a
  `String` field, and there are no comments. So a spreadsheet made by hand imports too.
- A number cell is read from its `office:value`, a date cell from its `office:date-value`,
  a text cell from its paragraphs (spaces, tabs and line breaks restored). Repeated cells
  and rows (`table:number-columns-repeated`, `table:number-rows-repeated`) are expanded;
  rows with no value at all are skipped.
- A row is one record. Its line breaks are where its comments need them (SPEC 3.2.1).
- A `Date` or `DateTime` field read empty holds the field's null date (SPEC 2.6.1).

## 5. What a spreadsheet program may change

Calc keeps text, dates and the companion sheet as they are. It rewrites a number cell's
`office:value` in its own form when it saves: `3.50` becomes `3.5`, `1e3` becomes `1000`.
A `Number` value whose exact text matters belongs in a text cell; the round-trip test
uses such values only through the direct path, not through Calc.
