//File:    SPEC.md
//Date:    2026-06-05
//Purpose: Specification for jText File Format
//
# jText — Format Specification

**Version:** 0.7 (draft)
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

Every jText file — regardless of profile — begins with the **`//` filesystem wrapper** (Section 2.1). After that, the file follows either the **light profile** (§2.0.1, default for human-facing data) or the **full profile** (§2.0.2, for DB tooling and high-throughput logs). Both profiles share the same per-line grammar (Section 3).

### 2.0 File Profiles

| Profile | Recognition | Typical use |
|---------|-------------|-------------|
| **Light** | `//File:` … `//` then `# JText File - created` | Manifests, summaries, hand-edited catalogs (`ts_store` `run_manifest.jtext`) |
| **Full** | `=== jText File ===` (often after the same `//` wrapper) | Event logs, SQL template emission, `jtext_process` round-trips |

Parsers accept **both** profiles. Writers should emit one profile consistently per file.

#### 2.0.1 Light Profile (canonical for human-facing files)

The light profile uses a deliberate **`//` vs `#` contrast**:

- `//` — filesystem wrapper (human + `head`/`grep` provenance)
- `#` — jText internal metadata (parser-facing)
- `-- SectionName --` — section banners
- `# Fields: path.jtFlds` — shared field-list include (schema declared once, not in row comments)
- Compact data rows (`N. |# f1|f2|…`) or expanded per-field lines

```
//File:    run_manifest.jtext
//Date:    2026-06-08
//Purpose: ts_store test matrix run manifest
//Related: type=ts_store table=ts_run_manifest
//
# JText File - created 2026-06-08T00:00:00Z
# Purpose - ts_store test matrix run manifest
# jText Version: 0.8
# Case: Sensitive
# Table Name: ts_run_manifest

-- RunMeta --

# Fields: tests/jtext_includes/run_manifest_runmeta_fields.jtFlds

 1. |# 1|OS_001|ssd|Smoke|2026-06-08T00:00:00Z|gcc|113|113|0

-- Scenarios --

# Fields: tests/jtext_includes/run_manifest_scenarios_fields.jtFlds

 1. |# 1|TS_STORE_TEST_001_TS|gcc|binary|off|100|0.005|PASS|binary_logs/...

 1. |# 2|TS_STORE_TEST_002_TS|gcc|binary|off|100|0.005|PASS|binary_logs/...
```

Light-profile rules:

- The `//` block is **required** on all `.jtext`, `.jtFlds`, `.jschma`, and `.sql` companions.
- The `#` metadata block follows immediately after the closing `//` blank line.
- Section banners use `-- name --` (not `=== Section: ===`).
- Field schema is declared via `# Fields: <path>.jtFlds` (inline field definitions are the full profile's `=== Fields ===` block, §2.0.2); **not** repeated in row comments.
- Line-number padding (§3.7) applies when a section has more than 9 numbered lines.
- `-- EOF --` is optional.

#### 2.0.2 Full Profile (DB / high-throughput tooling)

The full profile adds `===` structural markers, optional emit templates, and explicit `=== Fields ===` / `=== Data ===` blocks. Use when `jtext_process`, SQL companions, or streaming validators need the heavier structure.

```
=== jText File ===
<file header lines>

=== Section: <section name> ===
=== Template: <template name> ===
<<< <sentinel>
<template text with {N} placeholders>
<sentinel>
=== Fields ===
<field declaration lines or === <#include#> Fields: path.jtFlds ===>
=== Data ===
<data lines>
=== End Data ===
=== End Section ===

=== End File ===
```

All `===` structural markers are **case-sensitive** and must match exactly. Whitespace around the markers is ignored. End markers are recommended but not required.

For full-profile files, `=== jText File ===` may serve as a magic line for content sniffing. Light-profile files are recognized by the `//File:` wrapper and `# JText File - created` line instead.

### 2.1 Filesystem Wrapper (`//` headers)

**Required for all profiles and all companion files.** All `.jtext`, `.jtFlds`, `.jtinsrt`, `.jschma`, and generated `.sql` sidecars should begin with these lines (before any `#` metadata or `===` content). The first three lines + blank `//` are **required**; `//Related:` is **optional but strongly encouraged** when origin is known:

```
//File:    <full-or-relative-path>
//Date:    YYYY-MM-DD
//Purpose: jText Data File | jText Field List File | SQL Schema File | SQL Data File | ...
//Related: type=PostgreSQL table=workshop_tools   (optional)
//
```

The compact `//Related: type=... db=... table=...` form (user-specified convention) is a single line, easy to grep (`^//Related`), extensible with more keys, and replaces the older ad-hoc "Related Database --" / "Related Table --" style with something clean and consistent.

This makes every file self-describing at a glance (e.g. `head *.jtext` or `head *.jschma`), works cleanly in git diffs, and provides a consistent example across projects (ts_store, jacQLite, jText itself). The leading `//` lines are comments skipped by parsers. Machine-readable metadata lives in the `#` block (light profile, §2.0.1) or the `=== jText File ===` block (full profile, §2.0.2).

### 2.2 Character Encoding

jText files are **UTF-8 encoded**.

- Writers must not emit a byte-order mark (BOM)
- Readers must silently skip a leading UTF-8 BOM (`EF BB BF`) if present, for compatibility with files round-tripped through editors that add one
- No other encoding (UTF-16, UTF-32, legacy 8-bit code pages) is supported

### 2.3 File Header (full profile and `#` block in light profile)

The file header captures metadata that applies to the whole file. In the **light profile**, these fields appear as `#` lines immediately after the `//` wrapper (see §2.0.1): `# JText File - created <date>`, `# Purpose - <purpose>`, `# jText Version: <jtext_version>`, `# Case: <case_mode>`, `# SQL Dialect: <sql_dialect>`, `# Table Name: <table_name>`, `# AutoID: <auto_id>`. In the **full profile**, they appear as numbered lines inside `=== jText File ===`. Every header line follows the standard line grammar (Section 3).

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
//File:    robot_arm_run_47.jText
//Date:    2026-05-11
//Purpose: jText Data File
//Related: type=PostgreSQL table=arm_events
//
=== jText File ===
 1. ?# robot_arm_run_47.jText            # filename
 2. ?# 2026-05-11                        # date
 3. ?# Robot arm calibration log         # purpose
 4. ?# 0.1                               # jtext_version
 5. ?# Insensitive                       # case_mode
 6. ?# PostgreSQL                        # sql_dialect
 7. ?# arm_events                        # table_name
 8. ?# true                              # auto_id
```

The formatter `?#` is one valid choice. Authors pick any formatter that doesn't conflict with their data (Section 3.1). A legacy file may still say `#?#`.

Note the line-number padding: numbers are right-aligned in a 2-column space (Section 3.7).

### 2.3 Section

Each section is a self-contained record set with its own field schema, its own optional templates, and its own data rows. A section can be cut from one file and pasted into another (as a single-section file) and remain valid, provided the file header is also present.

A section consists of (in order):

1. A **section banner** — `=== Section: <name> ===`
2. **Zero or more templates** — `=== Template: <name> ===` followed by a multiline block (Section 2.5)
3. A **fields block** — `=== Fields ===` followed by field declaration lines *(required if the section has a data block)*
4. A **data block** — `=== Data ===` followed by data lines, terminated by `=== End Data ===`
5. A **section terminator** — `=== End Section ===` (optional)

**Structural rule:** the fields block is required whenever the data block is present. A section that opens a data block without first declaring its fields is a **structural validation error** — line numbers within the data block reference field positions declared in the fields block, so a data block without a fields block has no meaningful interpretation. A section with neither a fields block nor a data block (containing only templates, or completely empty) is structurally permissible but semantically unusual; the validator may surface it as a warning.

**A section is one table.** The section name is the table's name. Header field 7, `table_name`, is only a default when a template does not name a table. The fields block is the only definition of the columns. `Create Table`, `SQL Insert`, and `SQL Insert - no id` are renderings of that block, not a second schema. Writers emit, in order: the section banner, the templates, the fields block, the data block, then the end markers.

Section names within a file **should be unique**. Duplicate section names produce a **warning** during validation but do not block parsing, saving, or use. That warning names the section and has no record id and no field number. How duplicates are interpreted (last-wins, first-wins, merge, error) is a consumer-side decision.

### 2.4 Fields Block

Each line in the fields block declares one field. The line number is the field's position (1–99); the data part contains the field's name, type, optional length, and optional constraints, all hierarchically separated.

Field declaration syntax:

```
<name> / <segment> [ / <segment> [ / <segment> [ / <segment> ] ] ]
```

The name comes first. The segments after it may appear **in any order**, each kind at most once:

- `<type>` — a type spelling from Section 2.4.1 (e.g. `String`, `Text`, `Integer`, `Bool`)
- `<auto>` — an auto designation (Section 2.4.3), e.g. `AutoID` or `Identity(1,1)`; it implies an integer type
- `<length>` — optional positive integer; validators may use this to detect over-length values (informational; consumers decide whether to enforce)
- `<constraint>` — optional, `Not Null` or `Nullable` (default)

A type or an auto designation is required. Whitespace around the separators is ignored, so declarations can be column-aligned (Section 2.4.4).

**Field 1 is always the record id** (Section 2.4.3): every section's first field is a mandatory integer that identifies the record.

**New schema markers are two characters.** Each field line in a new fields block carries a `<splice><comment>` marker (Section 3.1). A three-character marker is not valid on a field line of a table being declared. An older file is converted on ingress, so the schema the author then sees is already two characters.

If a field name needs to contain the splicing character (e.g., a name with a literal `/`), the author chooses a different formatter for that line. For example, `|#` makes `|` the splicing character, so `/` in the name is just data.

Example:

```
=== Fields ===
 1. /# event_id/Number/Not Null              # primary key
 2. /# timestamp/Date/Not Null               # event capture time
 3. /# thread_id/Number/Not Null             # ts_store thread origin
 4. /# level/String/20/Not Null              # debug/info/warn/error
 5. /# category/String/40/Nullable           # optional category tag
 6. /# payload/String/Nullable               # free-text description (unbounded)
```

The inline comment may include the author's intended target type or any other free-text annotation; jText does not parse or interpret the comment.

#### 2.4.1 Type Vocabulary

| Type      | Accepted spellings (case-insensitive) | Used For | Emission Behavior in Templates |
|-----------|---------------------------------------|----------|--------------------------------|
| `String`  | `String`, `Text`, `Char`, `NChar`, `VarChar`, `NVarChar`, `VarChar2`, `NText`, `CLOB` | Text values | Wrap in single quotes; double any embedded single quotes |
| `Integer` | `Integer`, `Int`, `BigInt`, `SmallInt`, `TinyInt`, `Int2`, `Int4`, `Int8`, `Long` | Whole numbers only (optional sign, digits, within 64 bits) | Emit value as-is, no quotes |
| `Number`  | `Number`, `Numeric`, `Decimal`, `Real`, `Float`, `Double`, `Double Precision` | Numeric values, also any verbatim emission (functions, identifiers, NULL literals) | Emit value as-is, no quotes |
| `Date`    | `Date` | Dates (strict `YYYY-MM-DD`) | Wrap in single quotes, emit value as-is |
| `DateTime` | `DateTime`, `DateTime2`, `SmallDateTime`, `Timestamp`, `TimestampTZ`, `DateTimeOffset` | Date and time of day, strict ISO 8601: `YYYY-MM-DD`, then `T` or one space, then `HH:MM:SS`, optional fractional seconds (1–9 digits), optional zone `Z` or `+HH:MM`/`-HH:MM`. The date must be a real calendar date; hours 00–23, minutes and seconds 00–59. | Wrap in single quotes, emit value as-is |
| `GST`     | `GST` | Galactic Standard Time, written `<year>:<ticks>`: `earth_approximate_year` (signed 64-bit) and `earth_approximate_year_ticks` (10 ps ticks since the start of that year). A year is 3,155,695,200,000,000,000 ticks; any 19-digit tick value is a valid GST (19 digits hold about 68 million seconds past a year). Validators **warn, never error**, at two levels: **Warning 1** when the ticks go beyond 1 year (3,155,695,200,000,000,000 and up: leap seconds), and **Warning 2** when they go beyond 1 year and 3 leap seconds (3,155,695,500,000,000,000 and up). Neither warning blocks an operation. The ticks are **always zero-padded to the full 19 digits**. Example: `2026:0000000000000000001` is 10 ps into year 2026. | Wrap in single quotes, emit value as-is |
| `Bool`    | `Bool`, `Boolean`, `Bit`, `YesNo` | True/false values, written `Yes`/`No`, `True`/`False`, `Y`/`N`, `T`/`F` or `1`/`0` (case-insensitive) | Emit `TRUE` or `FALSE` |

Type names differ from one database to the next, so each internal type accepts a **family** of spellings: an author writes whichever name their target database uses, and validators treat all spellings in a family the same. Length bounds are written as a separate segment (`/40`), not inside the type name.

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

#### 2.4.3 Record Id (Field 1)

Every section's **field 1 is its record id**. The rule is about position and type, never the name:

- **Position:** field 1, declared first in the fields block. In the data it is an ordinary field value: in a per-field record it is the value on the `1.` line; in a compact row it is the first value after the formatter (`1. |# <id>|<f2>|...`). It is **never** the number to the left of the `.`, which is always a field position (Section 2.6).
- **Name:** free. `id`, `ID`, `recID`, `FK`, `event_id`, `customer_id` and so on are all fine; validators never check the name.
- **Type:** always an integer. It is declared as `Integer` (any spelling in that family), as `Number` (whose values must then be integers: optional sign, digits only, no decimal point or exponent, within 64 bits), or with an **auto designation**, which means "an integer the producer assigns". The auto designation is its own segment and may sit anywhere after the name, alone or together with an integer type (`row_id/Identity(1,1)/Integer/Not Null`). It is matched case-insensitively:

  | Auto designation | Meaning |
  |------------------|---------|
  | `AutoNumber`, `AutoID`, `AutoIncrement`, `Auto_Increment`, `Identity`, `Serial`, `SmallSerial`, `BigSerial` | integer ids from 1, step 1 |
  | `Identity(seed)`, `Identity(seed,step)` | integer ids from `seed`, step `step` (non-zero; may be negative) |

- **Never null:** field 1 is required whether or not it says `Not Null`. Declaring it `Nullable`, or with a non-integer type (`String`, `Date`, `Bool`), is a semantic error.
- **Always present in the file:** a record without a field-1 value is a semantic error. When the incoming data has no id, the **producer must make one** while writing: the first generated id in a section is the seed (default 1), and each later one is the previous id plus the step, continuing past the largest id already written in that section (the smallest, for a negative step). Ids restart with each section, since each section is its own table.

Examples:

```
=== Fields ===
 1. /# ID/Integer/Not Null
 1. /# recID/AutoID
 1. /# customer_id/Identity(1000,1)
 1. /# event_id/Number/Not Null/AutoNumber
```

**Uniqueness is recommended, not enforced.** Field 1 may be a foreign key (for example, a child table whose rows each carry the parent's id), so validators do not reject repeated ids. Producers **should** nevertheless give every row its own unique id whenever they can, so that field 1 can serve as the row's primary key and each row maps to exactly one database row. A common pattern keeps the unique row key in field 1 and any repeating identifiers in later fields:

```
=== Fields ===
 1. /# row_id/Identity(1,1)          # unique row key, never reused
 2. /# version/Integer/Not Null      # list or data version
 3. /# option_id/Integer/Not Null    # stable id; repeats across versions
 4. /# code/Text/5/Not Null
```

#### 2.4.4 Standard Drop-Down List

A **drop-down list** (the option lists behind combo boxes in a GUI or TUI) is a common enough table to have one standard layout. Every drop-down list section has at least these seven fields, as fields 1–7 in this order:

```
=== Fields ===
 1. /# row_id      /Identity(1,1) /Integer / Not Null    # unique row key, never reused
 2. /# version     /Integer / Not Null                   # list version; many rows share one
 3. /# option_id   /Integer / Not Null                   # stable option id; repeats across versions
 4. /# Code        /Text    / Not Null                   # short description
 5. /# Description /Text    / Not Null                   # longer description
 6. /# IsVisible   /Bool    / Not Null                   # hide an option without deleting it
 7. /# ListOrder   /Integer / Not Null                   # display order in the GUI/TUI
```

- `row_id` is the record id (Section 2.4.3) and is unique: one row forever, never reused. The other identifiers (`version`, `option_id`) repeat by design.
- **Additional fields are allowed.** A list may carry extra fields of its own as fields 8 and up, after the seven standard ones. The seven are the minimum every drop-down list table has. Common additions are lifecycle fields, for example:

  ```
   8. /# AddedDate   /DateTime / Not Null             # when the option was added
   9. /# RemovedDate /DateTime / Nullable             # when it was retired; empty while in use
  10. /# IsDeleted   /Bool    / Not Null              # deleted (kept for history, never shown)
  ```

  Other additions (a colour, an icon, a link to another table) follow the same rule.
- **Unique within a version:** `option_id`, `Code` and `Description` must each be unique within one `version` of a list. No two rows may share (`version`, `option_id`), (`version`, `Code`) or (`version`, `Description`).
- Field names and type spellings may follow the target database (e.g. `NVarChar` for `Text`, `Bit` for `Bool`), as long as the positions and types are kept.
- **Sentinel option:** every list, in every version, includes the "nothing chosen" option: `option_id` 0, `Code` `-`, `Description` `No Value Selected`, `IsVisible` Yes, `ListOrder` 1. Every other option has a non-zero `option_id` and a `Code` other than `-`.
- A GUI or TUI shows one version of a list: the rows with that `version` and `IsVisible` = Yes, ordered by `ListOrder`.

Example data, one compact row per option:

```
=== Data ===
 1. |# 1|1|0|-|No Value Selected|Yes|1

 1. |# 2|1|1|NAT|Natural Body|Yes|2
```

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
 1. /# customer_id/Number/Not Null
 2. /# name/String/40/Not Null
 3. /# email/String/255/Nullable
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

Each line in the data block represents one field's value for the current record. The line number references the field at the corresponding position in the fields block (e.g., line `3.` provides a value for the field declared at position 3). The line number is **always a field position (1–99), never a record identifier**; a record is identified by its field 1 value (Section 2.4.3).

A complete record uses field numbers 1 through N (where N is the field count); a partial record may skip field numbers. Records are separated by a **blank line** within the data block.

A "blank line" for separation purposes is a line containing zero characters or only whitespace characters. Lines containing comments without data still count as content, not as separators.

#### 2.6.1 Empty vs. Missing

jText distinguishes two distinct states for a field's value, and the distinction is preserved faithfully (per Principle 2):

- **Missing** — the field's line does not appear in the record. The record proceeds from a lower-numbered field directly to a higher-numbered field with the missing number(s) skipped.
- **Empty** — the field's line appears, but the data portion contains no characters (e.g., `1. -#` or `1. -# # comment`).

These states are **semantically distinct** for `String` fields and **semantically equivalent** for `Number` and `Date` fields:

| Field state | String (Nullable) | Number (Nullable) | Date (Nullable) |
|-------------|-------------------|-------------------|-----------------|
| Value present | Quoted value | Raw value | Quoted ISO value |
| Empty (line, no data) | Emit `''` (empty string) | Emit `NULL` | Emit `NULL` |
| Missing (no line) | Emit `NULL` | Emit `NULL` | Emit `NULL` |

For `Not Null` fields, both empty and missing are **semantic validation errors** regardless of type.

The rationale: an empty string (`''`) is a meaningful value for textual data — it represents "deliberately blank, recorded as such." For numeric and date data, there is no analogous "empty number" or "empty date"; absence of a value is always NULL.

##### 2.6.3 ts_store Compact Data Rows (compatibility variant)

For high-volume logs from ts_store, a compact single-line-per-record form is supported for the Data block content (after any Templates/Fields/ includes):

```
1. |# <id>|<f2>|...|<fN>
```

- The formatter `|#` declares the compact style: splice `|`, comment `#`. The legacy spelling `#|#` is the same pair and is accepted on read.
- `|` is the record splicing character.
- The number before `.` is a **field position**, as on every data line: the position of the first value on the line. A full record therefore starts at `1.`, and its first value is field 1, the record id (Section 2.4.3). A row starting at `k.` carries fields k, k+1, ...; values past the last declared field are an error.
- The line parser splits the tail on `|` into the hierarchy (values).
- Each compact row is one record; rows are separated by a blank line like any other record.

Null vs empty:
- Empty string field: `||` (adjacent separators).
- Null field: `|\x1F|`  (U+001F INFORMATION SEPARATOR ONE, Unit Separator, as the field content).
  This allows `||` and `||` (with embedded control) to look similar on the surface while distinguishing null from empty string for roundtrips and fidelity.

This form is produced by ts_store for efficiency; the jText tools accept it when a Fields include or block has been seen (the field count is known for validation).

#### 2.6.2 Data Block Example

```
=== Data ===
 1. ?# 1
 2. ?# 2026-05-11T14:32:01.123
 3. ?# 7
 4. ?# info
 5. ?# robot_arm
 6. ?# Move arm from A to B

 1. ?# 2
 2. ?# 2026-05-11T14:32:01.456
 3. ?# 3
 4. ?# warn
 6. ?# Sensor reading out of range
=== End Data ===
```

The second record above omits field 5 (category). Since `category` is declared `Nullable`, this is valid; the template emits `NULL` for that position.

---

## 3. Line Grammar

Every non-structural line in a jText file follows this exact shape:

```
<padding><number>.<whitespace><formatter><whitespace><data>[<space><comment-char><space><comment>]
```

| Token              | Description                                                                  |
|--------------------|------------------------------------------------------------------------------|
| `<padding>`        | Zero or one space character (Section 3.7)                                    |
| `<number>`         | An integer in the range 1–99, written as one or two ASCII digits             |
| `.`                | A literal period immediately after the number                                |
| `<whitespace>`     | One or more spaces or tabs                                                   |
| `<formatter>`      | Exactly 2 characters: `<splice><comment>` (Section 3.1)                      |
| `<data>`           | The field's value, possibly empty (Section 3.3). A `'` is stored as `'`     |
| `<comment-char>`   | The second marker character, with a space on each side (Section 3.2)        |
| `<comment>`        | Free text to end of line. The text may be empty                              |

**Each line carries its own formatter.** Different lines in the same block (Fields, Data, or Template) may use different formatters. The formatter is a property of the line, not of the section or the block.

### 3.1 The Formatter

The formatter is exactly two characters in the shape `<splice><comment>`.

One character less than the old three-character marker, on every field line. A 10 million row table saves 10 million bytes when each record is one compact line, and that many bytes again for every extra field line. The saving is why new text is two characters, and why ingress drops the first character of an old three-character marker instead of keeping it.

**Rules:**

1. The first character is the record splicing character. On a compact line it splits fields. In a hierarchy it splits levels. It must differ from the comment character. A formatter whose two characters are the same, such as `##`, is a **structural validation error**.
2. The second character is the comment character. A comment starts where that character has a space on each side (Section 3.2).
3. Each character must be **printable ASCII**, **non-alphanumeric**, and **non-whitespace**. Non-ASCII or alphanumeric characters in either slot is a **structural validation error**.
4. There is no stand-in character. The file stores the data as entered. A `'` in a value remains a `'`.
5. The marker `[]` is raw data. It names no splicing character and no comment character. Comments are not allowed on that line. The rest of the line, after the space that follows `[]`, is the field value unchanged. A `#`, a `|`, a quote, and trailing spaces stay in the value. The word `NULL` on a raw line is the four characters N, U, L, L, not an empty database value.

Authors choose punctuation that is not doing other work in that line. Common conventions:

| Formatter | Typical use                                                                 |
|-----------|-----------------------------------------------------------------------------|
| `?#`      | Flat text. The comment character is `#`. A line with no `?` in the data is one value |
| `/#`      | Hierarchical data, split on `/`                                             |
| `|#`      | Compact record, fields split on `\|`                                        |
| `/?`      | Hierarchical data split on `/` when the text contains `#`                   |
| `*#`      | Hierarchical data split on `*`                                              |
| `[]`      | Raw data. No splice and no comment. The rest of the line is the value      |

**Ingress of an old three-character marker.** A reader accepts a marker of three characters, `{1}{2}{3}`. On ingress it keeps only `{2}{3}`. `{1}` is dropped. The line is displayed with `{2}{3}`, and every later write emits `{2}{3}`. `#|#` arrives as `|#`. `^|#` arrives as `|#`. `#/#` arrives as `/#`. The data is not changed. A longer run is accepted the same way: the reader keeps the right two characters and drops what is in front. `#X##` ends in two `#` characters, so it is an error.

**New table schema.** A fields block that declares a table uses a two-character marker on every field line. A three-character marker is not a legal declaration of a new field. After ingress, an old schema is already the two-character form, and that is the form the author edits.

The formatter declares, for this line only:

- **The first character** splits fields or hierarchy levels when it appears in the data
- **The second character** opens a comment when a space sits on each side of it

### 3.2 Comment Marker

A comment begins when the comment character appears with a **space** (ASCII 0x20) immediately on each side. The comment text follows the right-hand space and may be empty. End of line is not a space. A tab is not a space.

The parser scans the data from left to right. The first comment character with a space on each side opens the comment. Everything after the right-hand space, with leading whitespace stripped, is the comment. Trailing whitespace before that comment character is stripped from the data.

The comment character **without** a space on each side is data. It does not open a comment, and it is not removed. Each data field that contains one or more such characters produces a **warning**. The warning names the record id and the field number (Section 6.2). `C#` warns when `#` is the comment character. The value stays `C#`.

The formatter's own characters are not data. The `#` in `|#` does not warn and does not open a comment, because it is not surrounded by spaces.

### 3.3 Data and Hierarchies

The data is everything between the formatter and the comment marker (or the end of the line, if no comment is present).

If the data **contains the splicing character**, it is parsed as a **hierarchy**: the data is split on that character, producing a list of segments.

If the data **does not contain the splicing character**, it is treated as a **flat single value**. (A splicing character that does not appear in the data is legal — see Section 3.5.) A compact record is this same split: one line, fields separated by the splicing character (Section 2.6.3).

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
- Whitespace around the comment character: a space on each side. End of line does not open a comment. A tab does not open a comment

### 3.5 Declared-But-Unused Splice

A line whose data contains no instance of the splicing character is **valid**. The data is one value.

This allows one formatter across a section when most rows are split and some are a single value.

### 3.6 Examples

Flat data, quote stored as a quote:

```
 1. ?# Stanley's hammer                   # the quote stays in the data
```

Empty value (deliberately blank):

```
 1. ?#                                    # explicitly empty data
```

A comment character in the data, without a space on each side. The value stays `C# compiler`. Validation warns, and the warning names this record's field-1 id and field 1:

```
 1. ?# C# compiler                        # language name
```

Compact record. `|` splits fields. The `#` in `|#` is not a comment:

```
 1. |# 1|Stanley Hammer|good              # the claw hammer
```

Hierarchical data using `/` as the splicing character:

```
 1. /# Personal Tools/Drills/Battery      # 3-level hierarchy
```

Hierarchical data using `*` , because the data contains `/`:

```
 1. *# 4/3 is four thirds*3/3 is three thirds*2/3 is two thirds  # fractions preserved
```

Cosmetic leading and trailing splicing characters:

```
 1. /# /This is the top/SubSection1/SubSection2/    # decorative slashes ignored
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
 1. ?# customers.jText                    # filename
 2. ?# 2026-05-11                         # date
 3. ?# Customer master records            # purpose
 9. /# orders.jText/items.jText           # related_files

=== Section: Customers ===
=== Fields ===
 1. /# customer_id/Number/Not Null         # primary key
 2. /# name/String/40/Not Null
 3. /# email/String/255/Nullable
=== Data ===
... records ...
=== End Data ===
=== End Section ===
=== End File ===
```

**`orders.jText`** (many orders per customer):
```
=== jText File ===
 1. ?# orders.jText                       # filename
 2. ?# 2026-05-11                         # date
 3. ?# Customer orders                    # purpose
 9. /# customers.jText/items.jText        # related_files

=== Section: Orders ===
=== Fields ===
 1. /# order_id/Number/Not Null            # primary key
 2. /# customer_id/Number/Not Null         # references customers.jText
 3. /# total/Number/Not Null
=== Data ===
... records ...
=== End Data ===
=== End Section ===
=== End File ===
```

**`items.jText`** (many items per order):
```
=== jText File ===
 1. ?# items.jText                        # filename
 2. ?# 2026-05-11                         # date
 3. ?# Line items within orders           # purpose
 9. /# customers.jText/orders.jText       # related_files

=== Section: OrderItems ===
=== Fields ===
 1. /# item_id/Number/Not Null             # primary key
 2. /# order_id/Number/Not Null            # references orders.jText
 3. /# product/String/Not Null
 4. /# quantity/Number/Not Null
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

Values are stored as entered. A `'` in a field stays a `'`. A live insert or update binds that value with a prepared parameter. When a consumer writes SQL text, as the SQL Insert template does, each `'` in the raw value is doubled once, so `'` becomes `''` inside the SQL quotes. The jText file is not rewritten. Stored values are not passed through a loop that collapses `''` to `'`. A host other than that SQL text, such as VBA, receives the raw value and does its own quoting.

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
- Data block in a section without a preceding fields block (Section 2.3)
- Template placeholders referencing field numbers outside the section's field range (Section 2.5)

**Unknown structural markers** (e.g., a future `=== Manifest: foo ===` marker that this spec version does not define) produce a **warning** rather than an error. The parser skips the unrecognized section and continues. This preserves forward compatibility.

### 6.2 Semantic Validation

Semantic rules govern data quality. A file that violates semantic rules **can still be parsed and edited**, but consumers that require trustworthy data must refuse to produce output until violations are resolved.

Semantic errors include:

- Field value does not match its declared type (e.g., non-numeric value in a `Number` field)
- Field value exceeds its declared length bound
- `Not Null` field is missing or empty in some record
- Field 1 (the record id) is declared with a non-integer type or as `Nullable`, is missing or empty in some record, or holds a value that is not an integer (Section 2.4.3)

Semantic **warnings** (non-blocking) include:

- A comment character in a data field without a space on each side (Section 3.2). The character stays in the data.
- Duplicate section names within a file (Section 2.3)
- Unknown file-header field names (Section 2.2)
- Other non-fatal anomalies that consumers may choose to surface or ignore

**Every warning names the record and the field that triggered it.**

- **Record id** is the field 1 value of that record. It is not the number to the left of the period. When that record has no field 1 value, the warning says the record id is missing.
- **Field number** is the `N` of the line, reported as the field number.

A warning that is not on a data line still follows the same report. A duplicate section name has no record id and no field number; it names the section. An unknown header field has no record id and names that header field's number. A semantic error on a data line names the same record id and field number.

One field that contains several bare comment characters produces one warning for that field. Two fields produce two warnings, each with the record id and its own field number.

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
 1. ?# contacts.jCheck                       # filename
 2. ?# 2026-05-11T14:32:01                   # date
 3. ?# Validation metadata for contacts.jText # purpose

=== Section: Validation ===
=== Fields ===
 1. /# key/String/Not Null
 2. /# value/String/Not Null
=== Data ===
 1. ?# target_file
 2. ?# contacts.jText

 1. ?# target_hash_algorithm
 2. ?# xxh3-128

 1. ?# target_hash
 2. ?# a3f1b0c84e9d7a2c5f8e0b1d6a7c4e9f

 1. ?# validated_at
 2. ?# 2026-05-11T14:32:01.000Z

 1. ?# validator_version
 2. ?# jtext-validator-0.4

 1. ?# result
 2. ?# passed
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
 1. ?# tools_inventory.jText                       # filename
 2. ?# 2026-05-11                                  # date
 3. ?# Personal workshop tool catalog              # purpose
 4. ?# 0.1                                         # jtext_version
 5. ?# Insensitive                                 # case_mode
 6. ?# SQLite                                      # sql_dialect
 7. ?# tools                                       # table_name

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
 1. /# id/Number/Not Null                  # primary key, integer
 2. /# name/String/40/Not Null             # tool name
 3. /# category/String/100/Not Null        # hierarchical category path
 4. /# acquired/Date/Nullable              # acquisition date if known
 5. /# notes/String/Nullable               # free-text notes
=== Data ===
 1. ?# 1
 2. ?# Stanley Hammer
 3. /# Hand Tools/Hammers/Claw
 4. ?# 2019-04-12
 5. ?# 16oz fiberglass handle

 1. ?# 2
 2. ?# DeWalt Drill DCD777
 3. /# Power Tools/Drills/Cordless
 4. ?# 2022-11-30
 5. ?# 20V MAX, brushless

 1. ?# 3
 2. ?# Klein Screwdriver Set
 3. /# Hand Tools/Screwdrivers/Sets
 5. ?# 11-piece, magnetic tips             # field 4 (acquired) omitted: Nullable, emits NULL
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
| **Splicing character** | The first character of a two-character formatter. Splits fields on a compact line and levels in a hierarchy. |
| **Comment character** | The second character of a two-character formatter. Opens a comment when a space sits on each side of it. Without those spaces, inside the data, it stays data and produces a warning. |
| **Formatter** | The 2-character declaration at the start of a line: `<splice><comment>`. A 3-character marker is accepted on ingress and kept as its last two characters. |
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

**v0.7** (current)
- Section 3.1: the marker is two characters, `<splice><comment>`. A three-character marker `{1}{2}{3}` is accepted on ingress and kept as `{2}{3}`. Display and output use those two characters. A new fields block allows only a two-character marker. There is no stand-in. A `'` is stored as `'`.
- Section 3.2: a comment is the comment character with a space on each side. The same character in the data without a space on each side stays data and is a warning.
- Section 6.2: a warning names the record id (the field 1 value) and the field number of the line. A missing record id is reported as missing.
- Section 2.4.3 (new): field 1 is the mandatory record id. Any name, always an integer (`Number` or an auto designation such as `AutoID`, `Identity(seed,step)`, `Serial`), never null, and always present in the file: a producer generates one (seed, then + step, per section) when the incoming data has none.
- Section 2.4 / 2.4.1: field declaration segments after the name may come in any order (type, auto designation, length, constraint; each at most once). Types are families of spellings, since names vary by database (`Text`/`NChar`/`VarChar` are String; `Int`/`BigInt` are Integer; ...). New types `Integer` (whole numbers), `DateTime` (strict ISO 8601 date and time, optional fraction and zone), `GST` (Galactic Standard Time, `<year>:<ticks>`) and `Bool` (`Yes`/`No`, `True`/`False`, `Y`/`N`, `T`/`F`, `1`/`0`; emitted as `TRUE`/`FALSE`).
- Section 2.4.4 (new): the Standard Drop-Down List layout (7 fields), its sentinel option, and its uniqueness rules (`option_id`, `Code` and `Description` each unique within a `version`).
- Section 2.6 / 2.6.3: the number before `.` is always a field position, never a record id. Compact rows are written `1. |# <id>|<f2>|...`. An old `1. #|#` line is kept as `|#` on ingress. The light-profile example in 2.0.1 matches.
- Section 6.2: added the record-id errors.

**v0.6**
- Section 2.3: tightened wording to make the fields block requirement explicit. A data block without a preceding fields block is now formally a structural validation error (it was previously implied but not stated; implementations were ambiguous about whether to reject it or defer to the semantic validator).
- Section 6.1: added the new structural error to the list.
- No other changes from v0.5.

**v0.5**
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

*End of jText Specification v0.7 (draft).*

