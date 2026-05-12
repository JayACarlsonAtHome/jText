# jText — Format Specification

**Version:** 0.5 (draft)
**Status:** Working draft — line grammar locked; file/section structure proposed
**License:** Apache-2.0 (matches the jText project)

---

## 1. Philosophy

jText is a **file format** for structured tabular records that prioritizes **author control over reserved characters**. The grammar is small, self-describing per line, and forgiving of incomplete data. The format never requires escaping: when the data contains a character that would otherwise be structural, the author picks a different structural character for that line.

This document specifies the format. Implementations of the format — parsers, validators, emitters, command-line tools, editor integrations — are not specified here. The format is the contract; implementations come and go.

The format is designed to be:

- **Human-readable** — top-to-bottom, in any text editor, with no rendering tool required
- **Human-writable** — by hand, with a stable visual rhythm that becomes muscle memory
- **Machine-parseable** — every line carries its own micro-grammar, so streaming parsers need almost no state
- **Machine-writable at speed** — high-throughput producers can emit jText via intermediate formats and asynchronous converters
- **Portable at every scope** — a line, a section, or a file can be moved or excerpted and still parse unambiguously
- **Spreadsheet-friendly** — the per-line positional structure round-trips cleanly through Excel/CSV-style tools
- **Relationally composable** — multiple jText files can form a relational dataset linked by key fields

jText is well-suited to records of roughly 1 to 30 fields, hand-curated or machine-emitted, where the data contains real-world content (paths, fractions, free text, hierarchies, punctuation) that other formats would require to be escaped or quoted. It is *not* designed to replace databases for high-volume querying, CSV for very wide tables, or JSON for nested object graphs.

### 1.1 Core Principles

Two principles govern every design decision in jText:

**Principle 1: A jText file may be saved in any state. A jText file may only be used when it is valid.**

The format separates *creation* from *consumption*. Producers — whether human editors or automated tools — never block on validation. Validation is a deliberate operation performed by consumers when they need to trust the data.

**Principle 2: A jText file is a faithful representation of the data.**

What was entered is what is stored. Parsers never fabricate values; tools never silently fill in missing data; readers never substitute defaults into the parsed representation. Substitutions, defaults, and sentinels manifest only in template-driven output, never in the file itself or the in-memory representation. This separation allows the same data to be emitted in different ways for different consumers without ever modifying the source file.

A narrowly-scoped exception to Principle 2 is permitted for **cosmetic regularization** (Section 3.7): tools may normalize whitespace and line-number padding when saving, since this does not change the file's meaning. No other writer-side modification is permitted.

### 1.2 Format Agnosticism

jText is **explicitly agnostic to its emission targets**. The format will never grow features specific to SQL dialects, JSON variants, XML schemas, or any other downstream format. Authors who need target-specific transformations handle them in their templates (which are literal text in the target language) or in pre/post-processing steps. This keeps jText itself small and stable.

---

## 2. File Structure

A jText file consists of:

1. A **file header** — metadata about the whole file
2. One or more **sections** — each section is a self-contained record set with its own schema and templates
3. An **end-of-file marker**

```
=== jText File ===
<file header lines>

=== Section: <section name> ===
=== Template: <template name> ===
<<< <sentinel>
<template text with {N} placeholders>
<sentinel>
=== Fields ===
<field declaration lines>
=== Data ===
<data lines>
=== End Data ===
=== End Section ===

=== Section: <another section name> ===
... (additional sections as needed)
=== End Section ===

=== End File ===
```

All structural markers (lines beginning with `===`) are **case-sensitive** and must match exactly. Whitespace around the markers is ignored.

End-of-section, end-of-data, and end-of-file markers are **recommended for clarity but not required**. When absent, the parser infers them from the start of the next structural element or from end-of-file.

The first non-blank line of a jText file is always `=== jText File ===`. This serves as the format's de facto magic number: tools that content-sniff files can recognize jText by this line regardless of file extension.

### 2.1 Character Encoding

jText files are **UTF-8 encoded**.

- Writers must not emit a byte-order mark (BOM)
- Readers must silently skip a leading UTF-8 BOM (`EF BB BF`) if present, for compatibility with files round-tripped through editors that add one
- No other encoding (UTF-16, UTF-32, legacy 8-bit code pages) is supported

### 2.2 File Header

The file header captures metadata that applies to the whole file. Every line in the file header follows the standard line grammar (Section 3).

The recognized file-header fields are:

| # | Field          | Required | Notes                                                       |
|---|----------------|----------|-------------------------------------------------------------|
| 1 | filename       | yes      | The canonical name of this file                             |
| 2 | date           | yes      | ISO 8601 recommended (`YYYY-MM-DD` or full datetime)        |
| 3 | purpose        | yes      | Free-text description of what this file is for              |
| 4 | jtext_version  | optional | jText spec version this file targets (default: `0.1`)       |
| 5 | case_mode      | optional | `Insensitive` (default) or `Sensitive` — affects name lookups |
| 6 | sql_dialect    | optional | e.g. `PostgreSQL`, `MySQL`, `SQLite` — informational only   |
| 7 | table_name     | optional | Default table name; consumed by templates if desired        |
| 8 | auto_id        | optional | `true` (default) or `false` — informational                 |
| 9 | related_files  | optional | List of related files in a multi-file dataset (see Section 4) |

Additional fields beyond this list are allowed and preserved by parsers; consumers ignore fields they don't recognize.

#### 2.2.1 File Header Example

```
=== jText File ===
 1. #?# robot_arm_run_47.jText            # filename
 2. #?# 2026-05-11                        # date
 3. #?# Robot arm calibration log         # purpose
 4. #?# 0.1                               # jtext_version
 5. #?# Insensitive                       # case_mode
 6. #?# PostgreSQL                        # sql_dialect
 7. #?# arm_events                        # table_name
 8. #?# true                              # auto_id
```

The formatter `#?#` is one valid choice. Authors pick any formatter that doesn't conflict with their data (Section 3.1).

Note the line-number padding: numbers are right-aligned in a 2-column space (Section 3.7).

### 2.3 Section

Each section is a self-contained record set with its own field schema, its own optional templates, and its own data rows. A section can be cut from one file and pasted into another (as a single-section file) and remain valid, provided the file header is also present.

A section consists of (in order):

1. A **section banner** — `=== Section: <name> ===`
2. **Zero or more templates** — `=== Template: <name> ===` followed by a multiline block (Section 2.5)
3. A **fields block** — `=== Fields ===` followed by field declaration lines
4. A **data block** — `=== Data ===` followed by data lines, terminated by `=== End Data ===`
5. A **section terminator** — `=== End Section ===` (optional)

Section names within a file **should be unique**. Duplicate section names produce a **warning** during validation but do not block parsing, saving, or use. How duplicates are interpreted (last-wins, first-wins, merge, error) is a consumer-side decision.

### 2.4 Fields Block

Each line in the fields block declares one field. The line number is the field's position (1–99); the data part contains the field's name, type, optional length, and optional constraints, all hierarchically separated.

Field declaration syntax:

```
<name> / <type> [ / <length> [ / <constraints> ] ]
```

Where:

- `<name>` — human-readable field name (any text not containing the hierarchy separator)
- `<type>` — one of the types listed in Section 2.4.1
- `<length>` — optional positive integer; validators may use this to detect over-length values (informational; consumers decide whether to enforce)
- `<constraints>` — optional, currently `Not Null` or `Nullable` (default)

If a field name needs to contain the hierarchy separator character (e.g., a name with a literal `/`), the author chooses a different formatter for that line. For example, `#|#` makes `|` the separator, so `/` in the name is just data.

Example:

```
=== Fields ===
 1. #/# event_id/Number/Not Null              # primary key
 2. #/# timestamp/Date/Not Null               # event capture time
 3. #/# thread_id/Number/Not Null             # ts_store thread origin
 4. #/# level/String/20/Not Null              # debug/info/warn/error
 5. #/# category/String/40/Nullable           # optional category tag
 6. #/# payload/String/Nullable               # free-text description (unbounded)
```

The inline comment may include the author's intended target type or any other free-text annotation; jText does not parse or interpret the comment.

#### 2.4.1 Type Vocabulary

| Type     | Used For                              | Emission Behavior in Templates                                  |
|----------|---------------------------------------|------------------------------------------------------------------|
| `String` | Text values                           | Wrap in single quotes; double any embedded single quotes        |
| `Number` | Numeric values, also any verbatim emission (functions, identifiers, NULL literals) | Emit value as-is, no quotes |
| `Date`   | Dates and datetimes (ISO 8601 format) | Wrap in single quotes, emit value as-is                          |

This vocabulary is intentionally small. Authors who need richer typing handle it through:

- Inline comments on the field declaration (for documentation)
- The template text itself (for emission-time formatting)
- External validators (for stricter type checking)

#### 2.4.2 Constraints

| Constraint | Meaning                                                           |
|------------|-------------------------------------------------------------------|
| `Not Null` | Every record must have a non-empty value for this field          |
| `Nullable` | Missing or empty values are permitted (default if omitted)       |

Future versions may add `Unique`, `Primary Key`, `Foreign Key`, and `Default` constraints. When added, they will follow the principles in Section 5.5 (Future-Feature Principles).

### 2.5 Templates

A section may declare zero or more **emit templates**. Each template is a named multiline block containing literal text with `{N}` placeholders that reference field numbers.

Templates come in two flavors, distinguished by content:

- **Per-record templates** contain at least one `{N}` placeholder. When applied, the template is repeated once per record in the data block, with each `{N}` substituted by the corresponding field's value (formatted per its type's emission behavior).
- **Section-level templates** contain no `{N}` placeholders. When applied, they emit once per section, typically used for setup statements like `CREATE TABLE`.

A common pattern combines both:

```
=== Section: Customers ===

=== Template: Create Table ===
<<< !!!CT!!!
CREATE TABLE customers (
    customer_id INTEGER PRIMARY KEY,
    name VARCHAR(40) NOT NULL,
    email VARCHAR(255)
);
!!!CT!!!

=== Template: SQL Insert ===
<<< !!!SI!!!
INSERT INTO customers (customer_id, name, email)
VALUES ({1}, {2}, {3});
!!!SI!!!

=== Fields ===
 1. #/# customer_id/Number/Not Null
 2. #/# name/String/40/Not Null
 3. #/# email/String/255/Nullable
=== Data ===
... records ...
=== End Data ===
=== End Section ===
```

The combination above lets a single emit operation produce both the schema-creation SQL and the data-insertion SQL — making migration from jText to a real database a one-command operation.

Notes:

- Templates use the multiline block syntax of Section 5
- The template name appears after `=== Template:` on the banner line
- `{N}` is replaced by the value of field N, formatted per the field's declared type
- All other text in the template is emitted literally, including newlines

**Templates support `{N}` substitution only.** No modifiers, filters, or transformations. This is a deliberate design choice (Section 1.2): jText is target-agnostic, so it does not implement target-specific formatting features. Authors who need richer formatting handle it either in their template's target language (e.g., SQL functions like `UPPER()`, `COALESCE()`), through pre-processing of the data, or with a custom emitter.

Template error cases:

- A `{N}` placeholder referencing a field number outside the section's field range (e.g., `{6}` in a 5-field section) is a **structural validation error**
- A `{0}` placeholder is reserved for future use; tools should treat it as a structural error in this spec version
- Malformed placeholder syntax (e.g., `{name}`, `{1.foo}`, `{`-without-closing-`}`) is emitted as literal text (the parser does not interpret it as a placeholder)

### 2.6 Data Block

Each line in the data block represents one field's value for the current record. The line number references the field at the corresponding position in the fields block (e.g., line `3.` provides a value for the field declared at position 3).

A complete record uses field numbers 1 through N (where N is the field count); a partial record may skip field numbers. Records are separated by a **blank line** within the data block.

A "blank line" for separation purposes is a line containing zero characters or only whitespace characters. Lines containing comments without data still count as content, not as separators.

#### 2.6.1 Empty vs. Missing

jText distinguishes two distinct states for a field's value, and the distinction is preserved faithfully (per Principle 2):

- **Missing** — the field's line does not appear in the record. The record proceeds from a lower-numbered field directly to a higher-numbered field with the missing number(s) skipped.
- **Empty** — the field's line appears, but the data portion contains no characters (e.g., `1. #-#` or `1. #-# # comment`).

These states are **semantically distinct** for `String` fields and **semantically equivalent** for `Number` and `Date` fields:

| Field state | String (Nullable) | Number (Nullable) | Date (Nullable) |
|-------------|-------------------|-------------------|-----------------|
| Value present | Quoted value | Raw value | Quoted ISO value |
| Empty (line, no data) | Emit `''` (empty string) | Emit `NULL` | Emit `NULL` |
| Missing (no line) | Emit `NULL` | Emit `NULL` | Emit `NULL` |

For `Not Null` fields, both empty and missing are **semantic validation errors** regardless of type.

The rationale: an empty string (`''`) is a meaningful value for textual data — it represents "deliberately blank, recorded as such." For numeric and date data, there is no analogous "empty number" or "empty date"; absence of a value is always NULL.

#### 2.6.2 Data Block Example

```
=== Data ===
 1. #?# 1
 2. #?# 2026-05-11T14:32:01.123
 3. #?# 7
 4. #?# info
 5. #?# robot_arm
 6. #?# Move arm from A to B

 1. #?# 2
 2. #?# 2026-05-11T14:32:01.456
 3. #?# 3
 4. #?# warn
 6. #?# Sensor reading out of range
=== End Data ===
```

The second record above omits field 5 (category). Since `category` is declared `Nullable`, this is valid; the template emits `NULL` for that position.

---

## 3. Line Grammar

Every non-structural line in a jText file follows this exact shape:

```
<padding><number>.<whitespace><formatter><whitespace><data>[<whitespace><comment-marker><whitespace><comment>]
```

| Token              | Description                                                                  |
|--------------------|------------------------------------------------------------------------------|
| `<padding>`        | Zero or one space character (Section 3.7)                                    |
| `<number>`         | An integer in the range 1–99, written as one or two ASCII digits             |
| `.`                | A literal period immediately after the number                                |
| `<whitespace>`     | One or more spaces or tabs                                                   |
| `<formatter>`      | Exactly 3 characters: `<bookend><separator><bookend>` (Section 3.1)          |
| `<data>`           | The field's value, possibly empty (Section 3.3)                              |
| `<comment-marker>` | The bookend character, surrounded by whitespace (Section 3.4)                |
| `<comment>`        | Free text to end of line                                                     |

**Each line carries its own formatter.** Different lines in the same block (Fields, Data, or Template) may use different formatters. The formatter is a property of the line, not of the section or the block.

### 3.1 The Formatter

The formatter is exactly three characters in the shape `<bookend><separator><bookend>`.

**Rules:**

1. The two bookend characters **must be identical**. A formatter like `#?$` is a **structural validation error**.
2. The bookend character and the separator character **must be different**. A formatter like `###` is a **structural validation error**.
3. Both characters must be **printable ASCII**, **non-alphanumeric**, and **non-whitespace**. Non-ASCII or alphanumeric characters in either slot is a **structural validation error**.

Authors are recommended to choose conservative, visually distinct punctuation. Common conventions:

| Formatter | Typical use                                                      |
|-----------|------------------------------------------------------------------|
| `#?#`     | Flat text, no internal hierarchy, comment marker is `#`         |
| `#/#`     | Hierarchical data with `/` as the level separator                |
| `#-#`     | Flat text when data may contain `/` (paths, fractions)           |
| `?/?`     | Hierarchical data when content contains `#`                      |
| `#|#`     | Hierarchical data when content contains both `#` and `/`         |
| `#*#`     | Hierarchical data when content contains both `#` and `/`         |

The author's job per line: scan the data, pick two characters that don't appear in it, and use them as bookend and separator.

The formatter declares, for this line only:

- **The bookend character** is the comment marker when it appears later with whitespace on both sides
- **The separator character** is the hierarchy delimiter when it appears in the data

### 3.2 Comment Marker

A comment begins when the bookend character appears in the line with **whitespace immediately on both sides** (or end-of-line on the right side).

A bookend character that does not have whitespace on both sides is **just data**, not a comment marker.

The parser scans the line from left-to-right looking for the bookend character with whitespace on both sides; the first such occurrence (after the formatter itself) opens the comment, and everything to the end of the line is the comment.

Trailing whitespace before the comment marker is stripped from the data; leading whitespace after the comment marker is stripped from the comment.

### 3.3 Data and Hierarchies

The data is everything between the formatter and the comment marker (or the end of the line, if no comment is present).

If the data **contains the separator character**, it is parsed as a **hierarchy**: the data is split on the separator, producing a list of named segments.

If the data **does not contain the separator character**, it is treated as a **flat single value**. (A hierarchical formatter declared on a line with no separator in the data is legal — see Section 3.5.)

**Hierarchy parsing rules:**

- **Leading separator** → cosmetic, ignored. `/A/B/C` → `["A", "B", "C"]`
- **Trailing separator** → cosmetic, ignored. `A/B/C/` → `["A", "B", "C"]`
- **Whitespace around segments** → trimmed. `A / B / C` → `["A", "B", "C"]`
- **Empty segment anywhere in the hierarchy** → **structural validation error**. There is no meaningful interpretation of a nameless level in a hierarchy. This rule covers internal empty segments (`A//B`), all-separator data (`///`), and whitespace-only segments (`A/ /B`) uniformly.

### 3.4 Whitespace Handling

- Whitespace between the period and the formatter: one or more (parsers accept; writers emit one or a configurable amount for alignment)
- Whitespace between the formatter and the data: one or more (same rule)
- Whitespace within flat data: preserved verbatim
- Whitespace within hierarchical segments: trimmed per segment
- Whitespace around the comment marker: required on both sides (or end-of-line on the right)

### 3.5 Declared-But-Unused Hierarchy

A line whose formatter declares a hierarchy separator (bookend ≠ separator) but whose data contains no instance of that separator is **valid**. The data is treated as a flat single-value hierarchy with one element.

This allows authors to maintain a consistent formatter across all lines in a section (where most rows are hierarchical but some are single-level) without per-row syntax changes.

### 3.6 Examples

Flat data, no hierarchy:

```
 1. #?# This is just plain text          # a simple flat value
```

Empty value (deliberately blank):

```
 1. #?#                                   # explicitly empty data
```

Data containing a `/` that is *not* hierarchical (path stored as flat value):

```
 1. #?# /etc/passwd                       # filesystem path, atomic value
```

Hierarchical data using `/` as the separator:

```
 1. #/# Personal Tools/Drills/Battery     # 3-level hierarchy
```

Hierarchical data using `*` as the separator, because the data contains `/`:

```
 1. #*# 4/3 is four thirds*3/3 is three thirds*2/3 is two thirds  # fractions preserved
```

A line whose data is hierarchical with cosmetic leading/trailing separators:

```
 1. #/# /This is the top/SubSection1/SubSection2/    # decorative slashes ignored
```

→ parses to `["This is the top", "SubSection1", "SubSection2"]`

### 3.7 Line-Number Padding

Field numbers are constrained to the range **1 to 99** (a section may have up to 99 fields). On disk, the canonical form right-aligns line numbers in a 2-column space:

- Single-digit numbers are preceded by one space: ` 1.`, ` 2.`, ..., ` 9.`
- Double-digit numbers occupy both columns: `10.`, `11.`, ..., `99.`

This keeps the periods in the same column across all lines in a section, providing a stable visual anchor for the reader.

Hand-edited files may have inconsistent padding (single-digit numbers with no leading space). Parsers accept this. Any tool that saves a jText file should **normalize** line numbers to the canonical 2-column form on output. This is the one writer-side cosmetic modification permitted under Principle 2.

A line number outside the range 1–99 (e.g., `0.` or `100.`) is a **structural validation error**.

---

## 4. Multi-File Datasets

A jText file may participate in a **relational dataset** with other jText files. This allows authors to express one-to-one, one-to-many, and many-to-many relationships in plain text without a database, using key fields to link records across files.

Each individual file remains independently valid jText. The relational structure is *additional information layered on top*, declared in the file header and observed by convention in field naming.

### 4.1 Example

A three-file dataset representing customers, their orders, and items within each order:

**`customers.jText`** (one record per customer):
```
=== jText File ===
 1. #?# customers.jText                    # filename
 2. #?# 2026-05-11                         # date
 3. #?# Customer master records            # purpose
 9. #/# orders.jText/items.jText           # related_files

=== Section: Customers ===
=== Fields ===
 1. #/# customer_id/Number/Not Null         # primary key
 2. #/# name/String/40/Not Null
 3. #/# email/String/255/Nullable
=== Data ===
... records ...
=== End Data ===
=== End Section ===
=== End File ===
```

**`orders.jText`** (many orders per customer):
```
=== jText File ===
 1. #?# orders.jText                       # filename
 2. #?# 2026-05-11                         # date
 3. #?# Customer orders                    # purpose
 9. #/# customers.jText/items.jText        # related_files

=== Section: Orders ===
=== Fields ===
 1. #/# order_id/Number/Not Null            # primary key
 2. #/# customer_id/Number/Not Null         # references customers.jText
 3. #/# total/Number/Not Null
=== Data ===
... records ...
=== End Data ===
=== End Section ===
=== End File ===
```

**`items.jText`** (many items per order):
```
=== jText File ===
 1. #?# items.jText                        # filename
 2. #?# 2026-05-11                         # date
 3. #?# Line items within orders           # purpose
 9. #/# customers.jText/orders.jText       # related_files

=== Section: OrderItems ===
=== Fields ===
 1. #/# item_id/Number/Not Null             # primary key
 2. #/# order_id/Number/Not Null            # references orders.jText
 3. #/# product/String/Not Null
 4. #/# quantity/Number/Not Null
=== Data ===
... records ...
=== End Data ===
=== End Section ===
=== End File ===
```

### 4.2 Conventions

- **Bidirectional declaration**: each file in a related set names all the others in its `related_files` field. This lets a consumer holding any one file discover the others.
- **Key references by name and type**: a foreign-key relationship is identified by a field whose name and type match a primary-key field in another file. For example, `customer_id` in `orders.jText` matches `customer_id` in `customers.jText`.
- **No referential-integrity enforcement**: the format does not require or check that every foreign-key value has a matching primary-key value in the referenced file. Consumers may add such checks if desired.
- **Independent validity**: each file in a related set must be valid jText on its own. Removing a file from the set does not invalidate the others; it just leaves dangling references.

### 4.3 Migration to and from Databases

The combination of `related_files`, typed fields, and per-section templates enables painless migration between jText and relational databases:

```
# Create schemas and populate a fresh database from jText files
jtext emit customers.jText --templates "Create Table" "SQL Insert" | psql mydb
jtext emit orders.jText    --templates "Create Table" "SQL Insert" | psql mydb
jtext emit items.jText     --templates "Create Table" "SQL Insert" | psql mydb
```

The same templates that produced INSERT statements can be inverted for the round-trip: an `export` tool reading from the database can produce jText files using the schema and types declared in the original. This makes jText a viable **on-ramp and off-ramp** to a database, rather than a destination format that locks data in.

(The specific command shown is illustrative; the spec does not prescribe tool names or command-line syntax.)

---

## 5. Multiline Data

When a single field's value spans multiple lines (a stack trace, a JSON payload, a code snippet, a template body), the standard one-line grammar cannot represent it. Multiline data uses a **sentinel-bracketed block**.

### 5.1 Multiline Syntax

A multiline field consists of:

1. An **opener line**: the field number, period, whitespace, and the literal token `<<<` followed by a **sentinel string** chosen by the author
2. Any number of **content lines** — verbatim, no interpretation
3. A **closer line**: the same sentinel string

```
 1. <<< !!!END!!!
This is the first line of multi-line data.
It can contain anything: /, #, $, /etc/passwd, even === Section: foo ===.
The only forbidden content is a line whose entire content is the sentinel.
!!!END!!!
```

Templates (Section 2.5) use the same multiline block syntax; the field number is omitted because the template is attached to its section banner rather than a numbered field.

### 5.2 Sentinel Rules

- The sentinel is chosen by the author, per-block
- It must be **at least 3 characters**, **printable ASCII**, and **not contain whitespace**
- The closer line must match the opener's sentinel **exactly** (byte-for-byte)
- The sentinel may not appear as a complete line within the content

If the author's content might contain a chosen sentinel, they pick a different sentinel for that block (`@@@DONE@@@`, `~~~STOP~~~`, etc.). The same "author picks characters that aren't in the data" principle as the line-level grammar.

### 5.3 Comments on Multiline Fields

Comments are not supported within a multiline block. If a comment is desired, place it on the opener line *after* the sentinel:

```
 1. <<< !!!END!!!     # captured stack trace from worker thread 3
... content ...
!!!END!!!
```

---

## 6. Validation

A jText file is **valid** when both of the following hold:

1. **Structural validity** — the grammar of Sections 2, 3, and 5 is satisfied
2. **Semantic validity** — the data in each section satisfies the field types and constraints declared in that section's fields block

### 6.1 Structural Validation

Structural rules govern parseability. A file that violates structural rules cannot be parsed; consumers must report a parse error and refuse to operate on such input.

Structural errors include:

- Malformed formatters (Section 3.1)
- Empty segments in hierarchies (Section 3.3)
- Line numbers outside the range 1–99 (Section 3.7)
- Unmatched multiline sentinels (Section 5.2)
- Mismatched or impossible structural markers (Section 2)
- Template placeholders referencing field numbers outside the section's field range (Section 2.5)

**Unknown structural markers** (e.g., a future `=== Manifest: foo ===` marker that this spec version does not define) produce a **warning** rather than an error. The parser skips the unrecognized section and continues. This preserves forward compatibility.

### 6.2 Semantic Validation

Semantic rules govern data quality. A file that violates semantic rules **can still be parsed and edited**, but consumers that require trustworthy data must refuse to produce output until violations are resolved.

Semantic errors include:

- Field value does not match its declared type (e.g., non-numeric value in a `Number` field)
- Field value exceeds its declared length bound
- `Not Null` field is missing or empty in some record

Semantic **warnings** (non-blocking) include:

- Duplicate section names within a file (Section 2.3)
- Unknown file-header field names (Section 2.2)
- Other non-fatal anomalies that consumers may choose to surface or ignore

A validator should report **all** errors and warnings found, not stop at the first one. This allows authors to fix everything in one editing pass.

### 6.3 Operations on jText Files

Three conceptual operations are recognized by the format. Specific tools and libraries implement these under their own names; the format does not prescribe function or command names.

| Operation | Validates | Writes | Use Case |
|-----------|-----------|--------|----------|
| **Save** | No | Yes | Editor save, work-in-progress, high-throughput producers |
| **Validate** | Yes | No | Check status, return error list, no file changes |
| **Write** | Yes | Yes if no errors | Strict save: file is written only when validation finds no errors |

Behavior on warnings (warnings present, no errors) is left to the implementing tool. Interactive tools may prompt the user; automated tools may proceed or abort per configured policy. The format requires only that errors prevent the write.

### 6.4 Producer Architecture

jText does not require its producers to write the format directly. High-throughput systems typically use a two-stage architecture:

1. A **fast intermediate format** (often fixed-width binary or similar) optimized for write speed
2. A **converter** that reads the intermediate format and emits jText for human review or downstream consumption

This decouples the format's design from the producer's performance requirements. jText itself can be slightly verbose (clear markers, formatter prefixes, readable structure) because converters run asynchronously, not in the hot path. The hot path uses whatever the producer needs; the consumable artifact is jText.

The intermediate format is not specified by jText. Producers define their own.

### 6.5 Future-Feature Principles

When future versions add features such as `Default`, `Unique`, `Primary Key`, `Foreign Key`, or per-field NULL sentinels, those features will obey the following principles:

1. **Faithful representation** — features must not cause the parsed in-memory data to differ from what is recorded in the file. Defaults and sentinels manifest only at emission time, never during parse or write.
2. **Emission-only substitutions** — `Default` values, `null=` sentinels, and similar substitutions apply when a template's placeholder is resolved, not when data is read from or written to the file.
3. **Backward compatibility** — new constraints and field-declaration features must parse cleanly on older readers (which may emit warnings about unknown constraint names but must not fail).

---

## 7. Validation Caching

Validation of large files is expensive. To avoid re-validating unchanged files, jText supports a sidecar mechanism that caches validation results alongside source files.

### 7.1 Naming and Location

For a source file `<basename>.jText`, the sidecar is `<basename>.jCheck` in the same directory.

```
data/
├── contacts.jText
├── contacts.jCheck      ← validation sidecar for contacts.jText
├── tools.jText
└── tools.jCheck
```

The sidecar is optional. A jText file without a sidecar is simply "not yet validated"; consumers that require validated data must run validation (which produces a sidecar) before proceeding.

### 7.2 Sidecar Format

The `.jCheck` file is itself a jText file. It contains a single section named `Validation` with metadata fields recording what was validated, when, and the result.

```
=== jText File ===
 1. #?# contacts.jCheck                       # filename
 2. #?# 2026-05-11T14:32:01                   # date
 3. #?# Validation metadata for contacts.jText # purpose

=== Section: Validation ===
=== Fields ===
 1. #/# key/String/Not Null
 2. #/# value/String/Not Null
=== Data ===
 1. #?# target_file
 2. #?# contacts.jText

 1. #?# target_hash_algorithm
 2. #?# xxh3-128

 1. #?# target_hash
 2. #?# a3f1b0c84e9d7a2c5f8e0b1d6a7c4e9f

 1. #?# validated_at
 2. #?# 2026-05-11T14:32:01.000Z

 1. #?# validator_version
 2. #?# jtext-validator-0.4

 1. #?# result
 2. #?# passed
=== End Data ===
=== End Section ===

=== End File ===
```

If validation failed, `result` is `failed` and an additional section named `Errors` enumerates each violation with section name, record number, field number, and a human-readable description.

`.jCheck` files are not themselves cached; tools parse them fresh on each access. This prevents infinite-regress (no `.jCheck.jCheck` files).

### 7.3 Cache Invalidation

A consumer that finds a `.jCheck` sidecar **should** verify all of the following before trusting it:

1. The sidecar's `target_file` matches the current source file's name
2. The sidecar's `target_hash` matches a fresh hash of the current source file's contents
3. The sidecar's `validator_version` is acceptable to the consumer

If any check fails, the sidecar is stale and the consumer should re-validate (producing a fresh sidecar).

### 7.4 Hash Algorithm

The recommended algorithm is **xxh3-128** — fast, well-tested, and sufficient for change detection. Consumers with tamper-resistance requirements may use BLAKE3 or SHA-256; in that case, the `target_hash_algorithm` field indicates which was used.

What is hashed: the raw bytes of the source `.jText` file as they appear on disk.

### 7.5 Read-Only Sources

When a source file's directory is not writable, tools may either:

- Re-validate on every consumption (no cache)
- Write the sidecar to a per-user cache location (implementation-defined)

The spec does not mandate either behavior.

### 7.6 Optional Centralized Cache Directory

As an optimization, tools **may** support a `.jCheck/` subdirectory containing all sidecars for that directory's jText files. When such a directory exists, sidecars in it take precedence over sidecars next to source files. This is optional; tools without this support are still spec-compliant.

---

## 8. Worked Example

```
=== jText File ===
 1. #?# tools_inventory.jText                       # filename
 2. #?# 2026-05-11                                  # date
 3. #?# Personal workshop tool catalog              # purpose
 4. #?# 0.1                                         # jtext_version
 5. #?# Insensitive                                 # case_mode
 6. #?# SQLite                                      # sql_dialect
 7. #?# tools                                       # table_name

=== Section: Hand Tools ===
=== Template: Create Table ===
<<< !!!CT!!!
CREATE TABLE tools (
    id        INTEGER PRIMARY KEY,
    name      VARCHAR(40) NOT NULL,
    category  VARCHAR(100) NOT NULL,
    acquired  DATE,
    notes     TEXT
);
!!!CT!!!

=== Template: SQL Insert ===
<<< !!!SI!!!
INSERT INTO tools (id, name, category, acquired, notes)
VALUES ({1}, {2}, {3}, {4}, {5});
!!!SI!!!

=== Template: CSV ===
<<< !!!CSV!!!
{1},{2},{3},{4},{5}
!!!CSV!!!

=== Fields ===
 1. #/# id/Number/Not Null                  # primary key, integer
 2. #/# name/String/40/Not Null             # tool name
 3. #/# category/String/100/Not Null        # hierarchical category path
 4. #/# acquired/Date/Nullable              # acquisition date if known
 5. #/# notes/String/Nullable               # free-text notes
=== Data ===
 1. #?# 1
 2. #?# Stanley Hammer
 3. #/# Hand Tools/Hammers/Claw
 4. #?# 2019-04-12
 5. #?# 16oz fiberglass handle

 1. #?# 2
 2. #?# DeWalt Drill DCD777
 3. #/# Power Tools/Drills/Cordless
 4. #?# 2022-11-30
 5. #?# 20V MAX, brushless

 1. #?# 3
 2. #?# Klein Screwdriver Set
 3. #/# Hand Tools/Screwdrivers/Sets
 5. #?# 11-piece, magnetic tips             # field 4 (acquired) omitted: Nullable, emits NULL
=== End Data ===
=== End Section ===

=== End File ===
```

When emitted with both the `Create Table` and `SQL Insert` templates, this section produces:

```sql
CREATE TABLE tools (
    id        INTEGER PRIMARY KEY,
    name      VARCHAR(40) NOT NULL,
    category  VARCHAR(100) NOT NULL,
    acquired  DATE,
    notes     TEXT
);
INSERT INTO tools (id, name, category, acquired, notes)
VALUES (1, 'Stanley Hammer', 'Hand Tools/Hammers/Claw', '2019-04-12', '16oz fiberglass handle');
INSERT INTO tools (id, name, category, acquired, notes)
VALUES (2, 'DeWalt Drill DCD777', 'Power Tools/Drills/Cordless', '2022-11-30', '20V MAX, brushless');
INSERT INTO tools (id, name, category, acquired, notes)
VALUES (3, 'Klein Screwdriver Set', 'Hand Tools/Screwdrivers/Sets', NULL, '11-piece, magnetic tips');
```

Note how:

- The schema-creation SQL is generated from the same file as the data
- `Number` type emits `1`, `2`, `3` without quotes
- `String` type wraps values in single quotes
- `Date` type wraps the date string in single quotes
- The omitted `Nullable` `acquired` field in record 3 emits `NULL`

---

## 9. Open Questions

One open question remains for resolution in a future spec version:

1. **Companion data files for non-tabular content** — when narrative text and bulk structured data (e.g., 3D points, large numeric arrays, images, audio) belong together, what is the file-pairing convention? Section 4's multi-file relational mechanism handles tabular companions; non-tabular companions (large opaque payloads keyed by event ID) may warrant a different convention. The exact syntax for declaring such companions in the file header is deferred.

All other open questions from earlier drafts have been resolved.

---

## 10. Glossary

| Term | Definition |
|------|------------|
| **Bookend** | The repeated outer character in a formatter; serves as the comment marker for the line |
| **Separator** | The middle character in a formatter; serves as the hierarchy delimiter within the data |
| **Formatter** | The 3-character declaration at the start of a line: `<bookend><separator><bookend>` |
| **Field** | A named, typed slot declared in a section's fields block |
| **Record** | A set of field values within a data block, separated by blank lines |
| **Section** | A self-contained record set with its own schema, optional templates, and data |
| **Hierarchy** | A multi-level value, expressed by separator characters within the data |
| **Multiline block** | A field value or template body that spans multiple lines, bracketed by author-chosen sentinels |
| **Template** | A named multiline block in a section header containing literal text with `{N}` placeholders, used to emit each record (or each section) in some target format |
| **Sidecar** | A `.jCheck` file co-located with a source `.jText` file that caches validation results |
| **Structural validity** | Conformance to the grammar rules; enforced at parse time |
| **Semantic validity** | Conformance to the data rules declared by field types and constraints; checked on demand |
| **Missing field** | A field whose data line does not appear in a record |
| **Empty field** | A field whose data line appears but contains no data characters |
| **Multi-file dataset** | A set of related jText files linked by key references and declared in the `related_files` file-header field |

---

## 11. Changelog

**v0.5** (current)
- Section 3.3: unified "empty hierarchy segment" rule — the previous treatment that distinguished "internal empty segment" from "data that is only separators" has been collapsed into a single rule. An empty segment anywhere in a hierarchy is a structural error, because there is no meaningful interpretation of a nameless level. Implementation simplified accordingly.
- Section 6.1: updated error list to reference the unified rule.
- No other changes from v0.4.

**v0.4**
- Reframed jText as a file format rather than a library or tool
- Operations (Save/Validate/Write) described conceptually, not as method names
- Added Section 3.7: line-number padding (1–99 range, right-aligned to 2 columns)
- Added Section 4: Multi-File Datasets, promoting relational structure to first-class status
- Section 2.5: clarified per-record vs section-level templates (`Create Table` pattern)
- Section 2.5: added template error cases
- Section 2.6: defined "blank line" for record-separation purposes
- Section 3: explicit statement that formatters are per-line
- Section 3.1: added common formatters table
- Section 6.2: validator should report all errors, not stop at first
- Field range updated to 1–30 fields as the design sweet spot

**v0.3**
- Added Principle 2: faithful representation
- Added Section 1.2: format agnosticism
- Added Section 2.1: UTF-8 encoding rules
- Added `jtext_version` field to file header (default 0.1)
- Added Section 2.6.1: empty vs. missing semantics
- Section uniqueness produces warning, not error
- Unknown structural markers produce warning, not error
- Documented intermediate-format producer architecture
- Future-feature principles (emission-only, faithful representation, backward compat)
- Template extensions explicitly rejected

**v0.2**
- First full draft with three-type vocabulary, templates, and validation caching

**v0.1**
- Initial sketch

---

*End of jText Specification v0.5 (draft).*
