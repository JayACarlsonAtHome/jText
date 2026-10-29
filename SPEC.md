//File:    SPEC.md
//Date:    2026-06-05
//Purpose: Specification for jText File Format
//
# jText — Format Specification

**Version:** 0.8 (draft)
**Status:** Working draft. The reference library (canonical jText v001/v002) implements it
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

What was entered is what is stored. Parsers never fabricate values; tools never silently fill in missing data; readers never substitute defaults into the parsed representation. Data stays data: a file holds text, never a null (Section 2.6.1). Substitutions, defaults, and sentinels manifest only in template-driven output, never in the file itself or the in-memory representation. This separation allows the same data to be emitted in different ways for different consumers without ever modifying the source file.

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

Parsers accept **both** profiles. A file is one profile or the other, never a mix: a `===` marker in a light-profile file is a structural error, and a light file names its fields only through `# Fields:` includes (inline field definitions are the full profile's `=== Fields ===` block).

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

-- End RunMeta --

-- Scenarios --

# Fields: tests/jtext_includes/run_manifest_scenarios_fields.jtFlds

 1. |# 1|TS_STORE_TEST_001_TS|gcc|binary|off|100|0.005|PASS|binary_logs/...
 1. |# 2|TS_STORE_TEST_002_TS|gcc|binary|off|100|0.005|PASS|binary_logs/...

-- End Scenarios --
-- EOF --
```

Light-profile rules:

- Writers begin the file with the `//` block (Section 2.1); readers skip it.
- The `#` metadata block follows: the magic line `# JText File - created <date>`, then the header keys of Section 2.3.
- A section opens with `-- Name --` and **closes** with `-- End Name --`; the names must match. Sections do not nest.
- The file **closes** with `-- EOF --`. After it come only blank lines and comments.
- Inside a section: `# Fields: <path>.jtFlds` names the section's fields (not repeated in row comments); other lines starting with `#` or `//` are comments; blank lines end records (Section 2.6); everything else is a data line (Section 3), including multiline values (Section 5).
- Line numbers are two columns wide (Section 3.7).

#### 2.0.2 Full Profile (DB / high-throughput tooling)

The full profile adds `===` structural markers, optional emit templates, and explicit `=== Fields ===` / `=== Data ===` blocks. Use when `jtext_process`, SQL companions, or streaming validators need the heavier structure.

```
=== jText File ===
<file header lines>

=== Section: <section name> ===
=== <#include#> Schema: path.jschma ===
=== Template: <template name> ===
[*
** <template text with {N} placeholders>
*]
=== Fields ===
<field declaration lines, or === <#include#> Fields: path.jtFlds ===>
=== End Fields ===
=== Data ===
<data lines>
=== End Data ===
=== End Section ===

=== End File ===
```

All `===` structural markers are **case-sensitive** and must match exactly. Whitespace around the markers is ignored. **Every block closes** (Section 6.1): a block left open is a structural error, which is what a file cut off mid-write looks like, so a truncated file is never mistaken for a complete one.

| Opens | Closes with |
|---|---|
| `=== jText File ===` | `=== End File ===` |
| `=== Section: name ===` | `=== End Section ===` |
| `=== Fields ===` | `=== End Fields ===` |
| `=== Data ===` | `=== End Data ===` |
| `[*` (Section 5) | `*]` |

- **Includes** `=== <#include#> <Kind>: <path> ===` may stand in a section before its Fields block or inside it. The kind is free text (`Fields`, `Schema`); the path is relative to the file. A `Fields` include supplies the section's field declarations.
- Lines starting with `#` or `//` are comments anywhere in the file. After `=== End File ===` come only blank lines and comments.
- `=== jText File ===` serves as a magic line for content sniffing. Light-profile files are recognized by `# JText File - created` instead.
- Readers also accept a file that opens directly with `=== Section:` and has no header block (older ts_store logs). It has no `=== End File ===`. Writers always write the header.

A file cut off mid-write, or written before closers were required, is closed by adding its missing closers: the reference tool `jtext_close FILE...` puts each where its block ends (`=== End Fields ===` before `=== Data ===`, a section's closer before the next section, everything still open at the end of the file), lists every line it added, and changes nothing else. `--check` only reports.

### 2.1 Filesystem Wrapper (`//` headers)

**Written for all profiles and all companion files.** All `.jtext`, `.jtFlds` and `.jtinsrt` files begin with these lines (before any `#` metadata or `===` content). Writers always emit the first three lines and the closing `//`; `//Related:` is **optional but strongly encouraged** when origin is known. Readers skip the block and do not require it:

```
//File:    <the file's name>
//Date:    YYYY-MM-DD
//Purpose: jText Data File | jText Field List File | ...
//Related: type=PostgreSQL table=workshop_tools   (optional)
//
```

Files of SQL (`.jschma` schemas and generated `.sql` sidecars) carry the same block as SQL comments, so `psql`, `sqlite3` and `mysql` run them as written. MySQL and MariaDB need the space after `--`:

```
-- File:    <the file's name>
-- Date:    YYYY-MM-DD
-- Purpose: SQL Schema File | SQL Data File
-- Related: type=PostgreSQL table=workshop_tools   (optional)
--
```

`//File:` (`-- File:`) holds the file's name only, never a path: files and the projects that hold them move (a portable drive mounts at a different place on each OS), and a recorded path would go stale.

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

| # | Field          | Written       | Notes                                                       |
|---|----------------|---------------|-------------------------------------------------------------|
| 1 | filename       | when known    | The file's name, never a path (as `//File:`, Section 2.1)   |
| 2 | date           | always        | ISO 8601 recommended (`YYYY-MM-DD` or full datetime); the writer uses today when none is given |
| 3 | purpose        | when given    | Free-text description of what this file is for              |
| 4 | jtext_version  | always        | jText version the file is written to, e.g. `0.8` (see below) |
| 5 | case_mode      | always        | `Insensitive` (default) or `Sensitive`. How values compare (filters, lookups), like a database collation; values are always kept as written |
| 6 | sql_dialect    | when set      | e.g. `PostgreSQL`, `MySQL`, `SQLite` — informational only   |
| 7 | table_name     | when set      | Default table name; consumed by templates if desired        |
| 8 | auto_id        | always (full) | `true` (default) or `false` — informational                 |
| 9 | related_files  | when given    | Related files in a multi-file dataset, split on the line's splice (Section 4) |

Header lines split on their splice character into levels, as field declarations do (Section 3.3); only `related_files` has more than one. Line numbers beyond 9 are allowed; validators warn and ignore them.

**Version.** `jtext_version` is a number, `major.minor` (`0.8`), compared part by part as numbers (`0.10` is newer than `0.8`). Writers always declare the version they write. A reader reads the file with its own rules and **warns**, never refuses, when the version is:

- **missing** (files from before v0.8 often have none): read as the reader's version;
- **older** than the reader's: read with the reader's rules;
- **newer** than the reader's: parts of the file may use what the reader does not know;
- **not a version number**: read as the reader's version.

Saving upgrades: a writer declares its own version, so a missing, older or invalid declaration becomes the current one. A save that keeps the rest of the file as written (replacing one section's data) also upgrades the declaration, as the file it keeps has already been read with the current rules. A newer declaration is never lowered.

#### 2.3.1 File Header Example

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
 4. ?# 0.8                               # jtext_version
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
2. **Zero or more includes and templates** — `=== <#include#> <Kind>: <path> ===`, and `=== Template: <name> ===` followed by its body (Section 2.5)
3. A **fields block** — `=== Fields ===`, field declaration lines or a `Fields` include, then `=== End Fields ===` *(required if the section has a data block)*
4. A **data block** — `=== Data ===`, data lines, then `=== End Data ===`
5. The **section closer** — `=== End Section ===` (required)

**Structural rule:** the fields block is required whenever the data block is present. A section that opens a data block without first declaring its fields is a **structural validation error** — line numbers within the data block reference field positions declared in the fields block, so a data block without a fields block has no meaningful interpretation. A section with neither a fields block nor a data block (containing only templates, or completely empty) is structurally permissible but semantically unusual; the validator may surface it as a warning.

**A section is one table.** The section name is the table's name. Header field 7, `table_name`, is only a default when a template does not name a table. The fields block is the only definition of the columns. `Create Table`, `SQL Insert`, and `SQL Insert - no id` are renderings of that block, not a second schema. Writers emit, in order: the section banner, the includes and templates, the fields block, the data block, then the closers.

Section names within a file **should be unique**. Duplicate section names produce a **warning** during validation but do not block parsing, saving, or use. That warning names the section and has no record id and no field number. How duplicates are interpreted (last-wins, first-wins, merge, error) is a consumer-side decision.

### 2.4 Fields Block

Each line in the fields block declares one field. The line number is the field's position (1–99); the data part contains the field's name, type, optional length, and optional constraints, separated by the line's splice character (levels, Section 3.3).

Field declaration syntax:

```
<name> / <segment> [ / <segment> [ / <segment> [ / <segment> ] ] ]
```

The name comes first. The segments after it may appear **in any order**, each kind at most once:

- `<type>` — a type spelling from Section 2.4.1 (e.g. `String`, `Text`, `Integer`, `Bool`)
- `<auto>` — an auto designation (Section 2.4.3), e.g. `AutoID` or `Identity(1,1)`; it implies an integer type
- `<length>` — optional positive integer; validators may use this to detect over-length values (informational; consumers decide whether to enforce)
- `<constraint>` — optional: `Not Null`, `Not Empty`, both, or `Nullable` (the default) (Section 2.4.2)
- `<null date>` — optional, `Date` and `DateTime` fields only: `NullDate(YYYY-MM-DD)`, the date that stands for "no date" in this field (default `1900-01-01`; Section 2.6.1)

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
=== End Fields ===
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
| `Not Null`  | The field may not hold `{NULL}` (Section 2.6.1)                  |
| `Not Empty` | The field may not hold the empty string (an empty or missing line) |
| `Nullable`  | Neither: the field may hold `{NULL}` and the empty string (default if omitted) |

`Not Null` and `Not Empty` are independent; a field that needs a real value declares both (`name/String/Not Null/Not Empty`). Field 1, the record id, is always both (Section 2.4.3).

Future versions may add `Unique`, `Primary Key`, `Foreign Key`, and `Default` constraints. When added, they will follow the principles in Section 6.5 (Future-Feature Principles).

#### 2.4.3 Record Id (Field 1)

Every section's **field 1 is its record id**. The rule is about position and type, never the name:

- **Position:** field 1, declared first in the fields block. In the data it is an ordinary field value: in a per-field record it is the value on the `1.` line; in a compact row it is the first value after the formatter (`1. |# <id>|<f2>|...`). It is **never** the number to the left of the `.`, which is always a field position (Section 2.6).
- **Name:** free. `id`, `ID`, `recID`, `FK`, `event_id`, `customer_id` and so on are all fine; validators never check the name.
- **Type:** always an integer. It is declared as `Integer` (any spelling in that family), as `Number` (whose values must then be integers: optional sign, digits only, no decimal point or exponent, within 64 bits), or with an **auto designation**, which means "an integer the producer assigns". The auto designation is its own segment and may sit anywhere after the name, alone or together with an integer type (`row_id/Identity(1,1)/Integer/Not Null`). It is matched case-insensitively:

  | Auto designation | Meaning |
  |------------------|---------|
  | `AutoNumber`, `AutoID`, `AutoIncrement`, `Auto_Increment`, `Identity`, `Serial`, `SmallSerial`, `BigSerial` | integer ids from 1, step 1 |
  | `Identity(seed)`, `Identity(seed,step)` | integer ids from `seed`, step `step` (non-zero; may be negative) |

- **Never empty, never `{NULL}`:** field 1 is `Not Null` and `Not Empty` whether or not it says so. Declaring it `Nullable`, or with a non-integer type (`String`, `Date`, `Bool`), is a semantic error.
- **Always present in the file:** a record without a field-1 value is a semantic error. When the incoming data has no id, the **producer must make one** while writing: the first generated id in a section is the seed (default 1), and each later one is the previous id plus the step, continuing past the largest id already written in that section (the smallest, for a negative step). Ids restart with each section, since each section is its own table.

Examples:

```
=== Fields ===
 1. /# ID/Integer/Not Null
 1. /# recID/AutoID
 1. /# customer_id/Identity(1000,1)
 1. /# event_id/Number/Not Null/AutoNumber
=== End Fields ===
```

(Four alternatives for one field 1, shown together; a real block declares field 1 once.)

**Uniqueness is recommended, not enforced.** Field 1 may be a foreign key (for example, a child table whose rows each carry the parent's id), so validators do not reject repeated ids. Producers **should** nevertheless give every row its own unique id whenever they can, so that field 1 can serve as the row's primary key and each row maps to exactly one database row. A common pattern keeps the unique row key in field 1 and any repeating identifiers in later fields:

```
=== Fields ===
 1. /# row_id/Identity(1,1)          # unique row key, never reused
 2. /# version/Integer/Not Null      # list or data version
 3. /# option_id/Integer/Not Null    # stable id; repeats across versions
 4. /# code/Text/5/Not Null
=== End Fields ===
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
=== End Fields ===
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
=== End Data ===
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
[*
** CREATE TABLE customers (
**     customer_id INTEGER PRIMARY KEY,
**     name VARCHAR(40) NOT NULL,
**     email VARCHAR(255)
** );
*]

=== Template: SQL Insert ===
[*
** INSERT INTO customers (customer_id, name, email)
** VALUES ({1}, {2}, {3});
*]

=== Fields ===
 1. /# customer_id/Number/Not Null
 2. /# name/String/40/Not Null
 3. /# email/String/255/Nullable
=== End Fields ===
=== Data ===
... records ...
=== End Data ===
=== End Section ===
```

The combination above lets a single emit operation produce both the schema-creation SQL and the data-insertion SQL — making migration from jText to a real database a one-command operation.

Notes:

- A template's body is a multiline value (Section 5) opened by a bare `[*` on the line after its banner, without a line number
- The template name appears after `=== Template:` on the banner line
- `{N}` is replaced by the value of field N, formatted per the field's declared type (Section 2.4.1). A field holding exactly `{NULL}` is emitted as `NULL`, unquoted, whatever its type (Section 2.6.1)
- All other text in the template is emitted literally, including newlines

**Templates support `{N}` substitution only.** No modifiers, filters, or transformations. This is a deliberate design choice (Section 1.2): jText is target-agnostic, so it does not implement target-specific formatting features. Authors who need richer formatting handle it either in their template's target language (e.g., SQL functions like `UPPER()`, `COALESCE()`), through pre-processing of the data, or with a custom emitter.

Template error cases:

- A `{N}` placeholder referencing a field number outside the section's field range (e.g., `{6}` in a 5-field section) is a **structural validation error**
- A `{0}` placeholder is reserved for future use; tools should treat it as a structural error in this spec version
- Malformed placeholder syntax (e.g., `{name}`, `{1.foo}`, `{`-without-closing-`}`) is emitted as literal text (the parser does not interpret it as a placeholder)

### 2.6 Data Block

Each line in the data block represents one field's value for the current record. The line number references the field at the corresponding position in the fields block (e.g., line `3.` provides a value for the field declared at position 3). The line number is **always a field position (1–99), never a record identifier**; a record is identified by its field 1 value (Section 2.4.3).

A complete record uses field numbers 1 through N (where N is the field count); a partial record may skip field numbers. A line for field 1 starts a new record: field 1 is the record id, and a record has only one. A record written one line per field (vertical data) ends with a **blank line**, which separates it from the next. Multifield rows that start at field 1 (Section 2.6.3) follow one another directly: each is a whole record, and the next row's `1.` starts the next one. Within a record, each line's number must be past the last field already filled.

**Data stays data.** In a data line, the splice character separates **field values** (a compact row, Section 2.6.3), never the levels of one value. A path such as `Hand Tools/Hammers/Claw` is ordinary text: write it with a formatter whose splice it does not contain, `3. ?# Hand Tools/Hammers/Claw`. Splitting into levels happens only in the Fields block and the file header (Section 3.3).

A "blank line" for separation purposes is a line containing zero characters or only whitespace characters. Lines containing comments without data still count as content, not as separators.

#### 2.6.1 Values: text, empty, `{NULL}`

A jText file holds **no nulls**. Every field value is text, and the distinction the author makes is preserved faithfully (Principle 2):

- **Empty** — the field's line has no data (`3. ?#`), or the field's line does not appear in the record at all (a **missing** field). Both are the empty string, for every type.
- **`{NULL}`** — the author typed the six characters `{NULL}`. In the file and in every reader it is that text: read, edited and saved like any other value, also inside a compact row (`|{NULL}|`). It becomes a real `NULL` only on the way into SQL: template emission (Section 2.5) writes a field holding exactly `{NULL}` as `NULL`, unquoted.
- The words `NULL`, `null` and `\N` are ordinary text, in the file and in SQL.

| Field holds | Template emission (any type) |
|-------------|------------------------------|
| A value | Formatted per its type (Section 2.4.1) |
| Empty, or missing | `''` |
| `{NULL}` | `NULL` |

**A date field always holds a date.** A `Date` or `DateTime` field never holds `{NULL}` or the empty string: where there is no date, writers (and editors) fill in the field's null date, `NullDate(...)` from its declaration or `1900-01-01` (`1900-01-01 00:00:00` for `DateTime`). It is stored in the file as that date and emitted like any other. Validators report an empty or `{NULL}` date field as an error.

Type checks apply to other values that are neither empty nor `{NULL}`. An empty `Number`, `Integer`, `GST` or `Bool` field is emitted as `''`, which a database may refuse for such a column: write a value or `{NULL}` there. `Not Null` forbids `{NULL}`, and `Not Empty` forbids the empty string (Section 2.4.2).

Old files may hold U+001F (Unit Separator), the v0.7 null marker in a compact row. Readers take it as `{NULL}`, and writers write `{NULL}`.

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

The second record above omits field 5 (category): its value is empty, and a template emits `''` for it.

#### 2.6.3 Compact Rows

A record, or a run of its fields, may be written on one line, the splice character separating the values:

```
 1. ▮▌ 42▮sensor.imu▮roll=0.01 pitch=0.02▮1700000000000042
 1. |# 1|Stanley Hammer|Hand Tools/Hammers/Claw|2019-04-12
```

- The number before `.` is a **field position**, as on every data line: the position of the first value on the line. A whole record starts at `1.`, and its first value is field 1, the record id (Section 2.4.3). A row starting at `k.` carries fields k, k+1, ...; values past the last declared field are an error.
- Every splice character splits, and each piece fills the next field: a compact row is per-field lines written on one line. An empty value is two adjacent splices (`||`); a trailing splice ends with an empty value. Each value is trimmed of surrounding whitespace.
- Any formatter works. `▮▌` is the writers' default (Section 3.1); `|#` is common in hand-written files.
- Rows that each hold a whole record follow one another without blank lines. A record may mix forms: a compact row from `1.`, then a multiline value on its own numbered line (Section 5; it cannot be part of a row), then the record continues at the next field; such a record is vertical data and ends with a blank line.

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
| `<formatter>`      | Exactly 2 Unicode characters (code points): `<splice><comment>` (Section 3.1) |
| `<data>`           | The field's value, possibly empty (Section 3.3). A `'` is stored as `'`     |
| `<comment-char>`   | The second marker character, with a space on each side (Section 3.2)        |
| `<comment>`        | Free text to end of line. The text may be empty                              |

**Each line carries its own formatter.** Different lines in the same block may use different formatters. The formatter is a property of the line, not of the section or the block.

Two formatters are special forms: `[]` (raw data, Section 3.1 rule 5) and `[*` (a multiline value, Section 5).

### 3.1 The Formatter

The formatter is exactly two characters in the shape `<splice><comment>`.

A formatter character may be any Unicode punctuation or symbol, not only ASCII. The format never escapes: when data holds a structural character, the author picks a different one for that line (Section 1). With only ASCII punctuation to pick from, machine producers that write free text, such as event logs, keep running into their own structural characters: `|` appears in shell commands, `#` in "order #5" and "C#". `▮` (U+25AE) and `▌` (U+258C) almost never occur in real text, so they are the writers' default.

One character less than the old three-character marker, on every field line. A 10 million row table saves 10 million bytes when each record is one compact line, and that many bytes again for every extra field line. The saving is why new text is two characters, and why ingress drops the first character of an old three-character marker instead of keeping it.

**Rules:**

1. The first character is the record splicing character. On a compact line it splits fields. In a hierarchy it splits levels. It must differ from the comment character. A formatter whose two characters are the same, such as `##`, is a **structural validation error**.
2. The second character is the comment character. A comment starts where that character has a space on each side (Section 3.2).
3. Each character is a single Unicode code point whose General Category is **Punctuation** (`Pc`, `Pd`, `Ps`, `Pe`, `Pi`, `Pf`, `Po`) or **Symbol** (`Sm`, `Sc`, `Sk`, `So`). Any other character (a letter, a digit, a combining mark, whitespace, a control or format character such as a zero-width joiner, a private-use, unassigned or surrogate code point) is a **structural validation error**. A formatter that is not valid UTF-8 is a structural validation error. Every printable non-alphanumeric ASCII character is Punctuation or Symbol, so every v0.7 formatter remains valid.
4. There is no stand-in character. The file stores the data as entered. A `'` in a value remains a `'`.
5. The marker `[]` is raw data. It names no splicing character and no comment character. Comments are not allowed on that line. The rest of the line, after the space that follows `[]`, is the field value unchanged. A `#`, a `|`, a quote, and trailing spaces stay in the value. The word `NULL` on a raw line is the four characters N, U, L, L, not an empty database value.
6. Characters are compared by code point. No Unicode normalization is applied.
7. "Two characters" and "the right two characters" of a longer marker count code points, not bytes.
8. Writers should prefer characters that display one column wide in common monospace fonts. Emoji and other double-width characters are valid but break the column alignment of Section 3.7. They are accepted as written: neither readers nor the validator warn about them.
9. `[` as the splicing character is reserved for special forms: `[]` (raw data) and `[*` (a multiline value). No other formatter may use `[` as its splice.

Authors choose punctuation that is not doing other work in that line. Common conventions:

| Formatter | Typical use                                                                 |
|-----------|-----------------------------------------------------------------------------|
| `▮▌`      | The writers' default on every line they write. `▮` (U+25AE) splits; `▌` (U+258C) is the comment character. Both are rare in real text |
| `?#`      | One value, such as a path. The comment character is `#`                     |
| `/#`      | Field declarations (`name/Type/Not Null`) and header lists, split on `/`    |
| `|#`      | Compact record, fields split on `\|`                                        |
| `*#`      | Compact record split on `*`, when the values contain `/` and `\|`           |
| `[]`      | Raw data. No splice and no comment. The rest of the line is the value      |
| `[*`      | A multiline value (Section 5)                                               |

**Writers.** Where the caller leaves the formatter to the writer, it uses `▮` as the splice and `▌` as the comment character, unless the line's data holds that character; then it uses the first ASCII punctuation character that is not in the data. A splice the caller did give that appears in the data would split a value, so the writer refuses the row (the reference writer ends the process or skips the row, by policy). A comment character the caller gave is kept.

**Ingress of an old three-character marker.** A reader accepts a marker of three characters, `{1}{2}{3}`. On ingress it keeps only `{2}{3}`. `{1}` is dropped. The line is displayed with `{2}{3}`, and every later write emits `{2}{3}`. `#|#` arrives as `|#`. `^|#` arrives as `|#`. `#/#` arrives as `/#`. The data is not changed. A longer run is accepted the same way: the reader keeps the right two characters and drops what is in front. `#X##` ends in two `#` characters, so it is an error.

**New table schema.** A fields block that declares a table uses a two-character marker on every field line. A three-character marker is not a legal declaration of a new field. After ingress, an old schema is already the two-character form, and that is the form the author edits.

The formatter declares, for this line only:

- **The first character** splits fields or hierarchy levels when it appears in the data
- **The second character** opens a comment when a space sits on each side of it

### 3.2 Comment Marker

A comment begins when the comment character appears with a **space** (ASCII 0x20) immediately on each side. The comment text follows the right-hand space and may be empty. End of line is not a space. A tab is not a space, and neither are other Unicode spaces (such as U+00A0): with `▮▌`, the marker is ` ▌ `.

The parser scans the data from left to right. The first comment character with a space on each side opens the comment. Everything after the right-hand space, with leading whitespace stripped, is the comment. Trailing whitespace before that comment character is stripped from the data.

A comment character that is the **first character of the data**, followed by a space or the end of the line, also opens a comment. The space between the formatter and it is padding for the reader; nothing stands before it, so the value is the empty string. ` 4. ?# # Note` holds an empty field 4 with the comment `Note`. Only that first comment character is the indicator: the rest of the line is the comment, so ` 4. ?# # # Note` has the comment `# Note`.

Anywhere else, the comment character **without** a space on each side is data. It does not open a comment, it is not removed, and nothing warns about it: `C#` is the value `C#` when `#` is the comment character.

The formatter's own characters are not data. The `#` in `|#` does not open a comment, because it is not surrounded by spaces.

#### 3.2.1 Comments Are Data

Comments carry context the author wants passed on with the data, so readers keep them, every export carries them, and a round trip that loses one has lost data.

**A comment belongs to the line it is on.** That is the one rule:

- A comment written on a line, after its data (or as the first character of its data, above), belongs to that line. On a compact row it belongs to the row, not to one of its fields.
- A line that holds only a comment, starting with `#` or `//`, is a **comment line**: a line of its own. It stays where it stands, between the lines around it, and belongs to nothing else. Its text is the line without its leading `#` or `//` and the spaces after it.

Where a store keeps records and values but not the layout of lines (a database, an XML export), a line is named by its **record and the field it starts at**: lines of a record never overlap, so that pair is one line. A comment line is named by the line it stands before, or as standing at the end of its block.

Not comments: the `//` block at the top of a file (Section 2.1, which writers regenerate), the light profile's `#` header lines (Section 2.3) and its `# Fields:` includes, and a template's body (Section 5).

### 3.3 Splitting: Field Values and Levels

The data is everything between the formatter and the comment marker (or the end of the line, if no comment is present), with surrounding whitespace removed. If the data does not contain the splicing character, it is **one value**. A splicing character that does not appear in the data is legal (Section 3.5).

If it does, the data is split on that character. What the pieces are depends on where the line is:

**In a data block** (and a light-profile section) the pieces are **field values** (a compact row, Section 2.6.3). Every splice splits; an empty value is allowed (`a||c` is three values, the second empty); each value is trimmed. Data stays data: the pieces are never the levels of one value (Section 2.6).

**In the Fields block and the file header** the pieces are **levels** of one value: a field declaration's name, type, length and constraint (`id/Integer/Not Null`), or the file names of `related_files`.

- **Leading separator** → cosmetic, ignored. `/A/B/C` → `["A", "B", "C"]`
- **Trailing separator** → cosmetic, ignored. `A/B/C/` → `["A", "B", "C"]`
- **Whitespace around segments** → trimmed. `A / B / C` → `["A", "B", "C"]`
- **Empty segment anywhere** → **structural validation error**. There is no meaningful interpretation of a nameless level. This rule covers internal empty segments (`A//B`), all-separator data (`///`), and whitespace-only segments (`A/ /B`) uniformly.

### 3.4 Whitespace Handling

- Whitespace between the period and the formatter: one or more (parsers accept; writers emit one or a configurable amount for alignment)
- Whitespace between the formatter and the data: one or more (same rule)
- Whitespace inside a value: preserved verbatim; whitespace before and after it: removed (a `[]` raw line keeps trailing spaces)
- Whitespace around compact-row values and levels: trimmed per value
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

A comment character in the data, without a space on each side. It is data: the value is `C# compiler`:

```
 1. ?# C# compiler                        # language name
```

Compact record. `|` splits fields. The `#` in `|#` is not a comment:

```
 1. |# 1|Stanley Hammer|good              # the claw hammer
```

A path is one value: `?` is not in it, so nothing splits:

```
 3. ?# Personal Tools/Drills/Battery      # the category, as written
```

A compact row split on `*`, because the values contain `/`:

```
 1. *# 7*4/3 is four thirds*2/3 is two thirds  # fractions preserved
```

The writers' default formatter, on a compact row:

```
 1. ▮▌ 8▮Stanley Hammer▮Hand Tools/Hammers/Claw ▌ the claw hammer
```

A field declaration (levels), with cosmetic leading and trailing splicing characters:

```
 2. /# /name/String/40/                   # decorative slashes ignored
```

→ parses to `["name", "String", "40"]`

### 3.7 Line-Number Padding

Field numbers are constrained to the range **1 to 99** (a section may have up to 99 fields). On disk, the canonical form right-aligns line numbers in a 2-column space:

- Single-digit numbers are preceded by one space: ` 1.`, ` 2.`, ..., ` 9.`
- Double-digit numbers occupy both columns: `10.`, `11.`, ..., `99.`

This keeps the periods in the same column across all lines in a section, providing a stable visual anchor for the reader. Writers use this form in every section, however few lines it has.

Line numbers are right-aligned in two columns, so every line of a file spends the same width on its number. A 100th field position would make that three columns on every line, not only on the wide records: one more character per line, which adds up in long tables. A record of more than 99 fields is also past what a person reads as one record. Producers with wider rows split them into related tables (Section 4).

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
 4. ?# 0.8                                # jtext_version
 9. /# orders.jText/items.jText           # related_files

=== Section: Customers ===
=== Fields ===
 1. /# customer_id/Number/Not Null         # primary key
 2. /# name/String/40/Not Null
 3. /# email/String/255/Nullable
=== End Fields ===
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
 4. ?# 0.8                                # jtext_version
 9. /# customers.jText/items.jText        # related_files

=== Section: Orders ===
=== Fields ===
 1. /# order_id/Number/Not Null            # primary key
 2. /# customer_id/Number/Not Null         # references customers.jText
 3. /# total/Number/Not Null
=== End Fields ===
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
 4. ?# 0.8                                # jtext_version
 9. /# customers.jText/orders.jText       # related_files

=== Section: OrderItems ===
=== Fields ===
 1. /# item_id/Number/Not Null             # primary key
 2. /# order_id/Number/Not Null            # references orders.jText
 3. /# product/String/Not Null
 4. /# quantity/Number/Not Null
=== End Fields ===
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

**Comments in a database.** A table column holds values, so comments (Section 3.2.1) are stored beside the data, in a linked table named after the data table with `_comments` added. It holds no nulls:

```sql
CREATE TABLE workshop_tools_comments (
    block     TEXT    NOT NULL,   -- 'fields' or 'data'
    kind      TEXT    NOT NULL,   -- 'line' (the comment on a line) or 'comment-line'
    record_id INTEGER NOT NULL,   -- data block: field 1 of the record; fields block: 0
    position  INTEGER NOT NULL,   -- the field the line starts at; for a comment line, that of
                                  -- the line it stands before; 0: at the end of the block
    seq       INTEGER NOT NULL,   -- comment lines standing together: 1, 2, 3 ... in order
    comment   TEXT    NOT NULL
);
```

A line is named by its record and the field it starts at (Section 3.2.1), so every comment goes back on its own line, and every comment line back where it stood. Tools that write a section to a database write its comments there, and tools that read a table back put each one in its place. A record's comments are found by its record id, which is one more reason to keep field 1 unique (Section 2.4.3).

SQL text written for a database uses standard string literals: a `'` is doubled, and a backslash is an ordinary character. PostgreSQL and SQLite read them that way; MariaDB and MySQL do only in the `NO_BACKSLASH_ESCAPES` SQL mode, so such a file is loaded there with that mode set (for example `mariadb --init-command="SET SESSION sql_mode = CONCAT(@@sql_mode, ',NO_BACKSLASH_ESCAPES')"`).

**Declarations in a database.** A table's columns keep names, types, lengths and `NOT NULL`, but not every jText declaration (`Not Empty`, a `NullDate(...)`, an auto designation). So each field's declaration is stored as written, part by part, in a second linked table named after the data table with `_fields` added. It holds no nulls:

```sql
CREATE TABLE workshop_tools_fields (
    position  INTEGER NOT NULL,   -- the field (1, 2, ...)
    level     INTEGER NOT NULL,   -- the declaration's parts in order: 1 the name, 2 the type, ...
    text      TEXT    NOT NULL    -- e.g. 'name', 'String', '40', 'Not Null', 'Not Empty'
);
```

Tools that write a section to a database write its declarations there; tools that read a table back use a stored declaration when it names the same field at that position (a database may fold the name's case), and otherwise build one from the column. With both linked tables, everything survives a database round trip, values, declarations and comments; the reference tools test that through the canonical XML export (`XML_EXPORT.md`).

Values are stored as entered. A `'` in a field stays a `'`. A live insert or update binds that value with a prepared parameter. When a consumer writes SQL text, as the SQL Insert template does, each `'` in the raw value is doubled once, so `'` becomes `''` inside the SQL quotes. The jText file is not rewritten. Stored values are not passed through a loop that collapses `''` to `'`. A host other than that SQL text, such as VBA, receives the raw value and does its own quoting.

---

## 5. Multiline Data

When a single field's value spans multiple lines (a stack trace, a JSON payload, a code snippet, a template body), it is written as a **multiline value**. Every line of the value carries its own marker. A record whose payload (field 6) is a stack trace:

```
 1. |# 42|7|3|0x0000000000010002|worker.error
 6. [* # stack trace from worker 3
    ** Traceback (most recent call last):
    **   File "main.py", line 12, in <module>
    ** ValueError: bad input
    *]
 7. |# 1700000000000042
```

| Marker | Line |
|---|---|
| `[*` | Opens the value, on the numbered line of its field. An optional comment follows, introduced by `#` |
| `**` | One line of the value |
| `*]` | Closes the value. Nothing follows it |

The number is the field position (Section 2.6), as on every line: field 6 here. Field 1 is the record id (Section 2.4.3), always an integer, so it is never multiline.

### 5.1 Rules

1. Exactly one space separates a marker from what follows it. The space belongs to the marker: `**   File` holds `  File`. Everything after it is the value's line, verbatim, trailing spaces included.
2. `**` alone (or followed by one space) is an empty line of the value.
3. `[*` sits where a formatter would; `[` as a splice is reserved for it and for `[]` (Section 3.1, rule 9). Content lines and the closer carry no line number.
4. **Alignment.** Writers indent content lines and the closer to the column of the opener's marker, so every marker (`|#`, `[*`, `**`, `*]`) is in one column and every value starts in the next. Line numbers are two columns (Section 3.7), so that column is always the fifth. Readers ignore leading spaces and tabs before `**` and `*]`: alignment is cosmetic, and a hand edit that is out of line still reads.
5. A template (Section 2.5) opens with a bare `[*` on the line after its banner.
6. Comments are only on the opener. A `**` line is all value.
7. A value open at the end of the file, or followed by any line that is not `**` or `*]`, is a missing closer (Section 6.1).
8. A multiline value fills exactly one field, the one its line number names. A compact row is one line, so it cannot hold one: a record with a multiline value breaks there. The fields before it are a compact row from `1.`, whose values fill fields 1 to k-1; the multiline value is field k, on its own numbered line; after its closer the record continues at field k+1 (a per-field line or a compact row numbered k+1), or a blank line ends the record. Writers calculate those positions from the fields each line fills.
9. Writers write a value that contains a newline as a multiline value. No choice is needed: the markers never collide with the data.
10. Editors and viewers may show the marker and the line in separate columns; the space is part of the text form only. In the text form the closer is always alone on its line. An editor may show the `*]` marker on the value's last line instead, beside that line's data; writing out splits that row back into `** <data>` and a bare `*]`.

### 5.2 Why

- **No sentinel to choose, no collision.** Every value line is prefixed, so the value can contain anything, including a line that reads `*]` or `=== End File ===`.
- **Every line says what it is,** in keeping with the per-line grammar (Section 1). `grep '^ *\*\*'` finds every value line.
- **It closes like every other block**, so a cut-off value is detected and `jtext_close` can close it.
- **`*` starts nothing else.** Data lines start with a number, comments with `#` or `//`.

Older files used `<<< SENTINEL` blocks. `jtext_close` rewrites them line for line (a template's opener becomes a bare `[*`); readers no longer accept them.

---

## 6. Validation

A jText file is **valid** when both of the following hold:

1. **Structural validity** — the grammar of Sections 2, 3, and 5 is satisfied; a file that is not structurally valid does not parse
2. **Semantic validity** — the data in each section satisfies the field types and constraints declared in that section's fields block

### 6.1 Structural Validation

Structural rules govern parseability. A file that violates structural rules cannot be parsed; consumers must report a parse error and refuse to operate on such input.

Structural errors include:

- Malformed formatters (Section 3.1), including a character that is not Punctuation or Symbol, and a formatter that is not valid UTF-8
- Empty segments in levels (Section 3.3)
- Line numbers outside the range 1–99 (Section 3.7)
- A block not closed before the next block, the file's closer, or the end of the file (Sections 2.0.1, 2.0.2, 5): a missing `=== End Fields ===`, `=== End Data ===`, `=== End Section ===`, `=== End File ===`, `-- End Name --`, `-- EOF --` or `*]`
- `-- End Name --` that does not match the open section
- Anything other than blank lines and comments after the file's closer
- A marker where it does not belong, and any `===` marker this version does not define
- A data line outside a Data block (full profile) or outside a section (light profile)
- A `===` marker in a light-profile file
- Data block in a section without a preceding fields block (Section 2.3)
- Template placeholders referencing field numbers outside the section's field range (Section 2.5)

**Unknown structural markers are errors.** A reader cannot tell a marker it does not know from a damaged one, so it refuses the file rather than guess. A future marker therefore comes with a new `jtext_version` (Section 2.3).

### 6.2 Semantic Validation

Semantic rules govern data quality. A file that violates semantic rules **can still be parsed and edited**, but consumers that require trustworthy data must refuse to produce output until violations are resolved.

Semantic errors include:

- Field value does not match its declared type (e.g., non-numeric value in a `Number` field)
- Field value exceeds its declared length bound
- `Not Null` field holds `{NULL}`, or `Not Empty` field is empty or missing, in some record (Section 2.4.2)
- A `Date` or `DateTime` field is empty, missing or `{NULL}` (a date field always holds a date, Section 2.6.1); a `NullDate(...)` that is not a date, or on another type
- Field 1 (the record id) is declared with a non-integer type or as `Nullable`, is missing or empty in some record, or holds a value that is not an integer (Section 2.4.3)

Semantic **warnings** (non-blocking) include:

- Duplicate section names within a file (Section 2.3)
- A section with no records
- Unknown file-header line numbers (Section 2.3)
- A missing, older, newer or invalid `jtext_version` (Section 2.3)
- A `GST` value past one year, and past one year plus three leap seconds (Section 2.4.1)
- Other non-fatal anomalies that consumers may choose to surface or ignore

**Every warning names the record and the field that triggered it.**

- **Record id** is the field 1 value of that record. It is not the number to the left of the period. When that record has no field 1 value, the warning says the record id is missing.
- **Field number** is the `N` of the line, reported as the field number.

A warning that is not on a data line still follows the same report. A duplicate section name has no record id and no field number; it names the section. An unknown header field has no record id and names that header field's number. A semantic error on a data line names the same record id and field number.

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

When future versions add features such as `Default`, `Unique`, `Primary Key`, or `Foreign Key`, those features will obey the following principles:

1. **Faithful representation** — features must not cause the parsed in-memory data to differ from what is recorded in the file. Defaults and sentinels manifest only at emission time, never during parse or write.
2. **Emission-only substitutions** — `Default` values and similar substitutions apply when a template's placeholder is resolved, not when data is read from or written to the file, as `{NULL}` becomes `NULL` only there (Section 2.6.1).
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
 4. ?# 0.8                                   # jtext_version

=== Section: Validation ===
=== Fields ===
 1. /# id/Integer/Not Null
 2. /# key/String/Not Null
 3. /# value/String/Not Null
=== End Fields ===
=== Data ===
 1. |# 1|target_file|contacts.jText
 1. |# 2|target_hash_algorithm|xxh3-128
 1. |# 3|target_hash|a3f1b0c84e9d7a2c5f8e0b1d6a7c4e9f
 1. |# 4|validated_at|2026-05-11T14:32:01.000Z
 1. |# 5|validator_version|jtext-validator-0.4
 1. |# 6|result|passed
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
 4. ?# 0.8                                         # jtext_version
 5. ?# Insensitive                                 # case_mode
 6. ?# SQLite                                      # sql_dialect
 7. ?# tools                                       # table_name

=== Section: Hand Tools ===
=== Template: Create Table ===
[*
** CREATE TABLE tools (
**     id        INTEGER PRIMARY KEY,
**     name      VARCHAR(40) NOT NULL,
**     category  VARCHAR(100) NOT NULL,
**     acquired  DATE,
**     notes     TEXT
** );
*]

=== Template: SQL Insert ===
[*
** INSERT INTO tools (id, name, category, acquired, notes)
** VALUES ({1}, {2}, {3}, {4}, {5});
*]

=== Template: CSV ===
[*
** {1},{2},{3},{4},{5}
*]

=== Fields ===
 1. /# id/Number/Not Null                  # primary key, integer
 2. /# name/String/40/Not Null             # tool name
 3. /# category/String/100/Not Null        # category path, one value
 4. /# acquired/Date/Not Null             # 1900-01-01 when unknown
 5. /# notes/String/Nullable               # free-text notes
=== End Fields ===
=== Data ===
 1. ?# 1
 2. ?# Stanley Hammer
 3. ?# Hand Tools/Hammers/Claw
 4. ?# 2019-04-12
 5. ?# 16oz fiberglass handle

 1. ?# 2
 2. ?# DeWalt Drill DCD777
 3. ?# Power Tools/Drills/Cordless
 4. ?# 2022-11-30
 5. ?# 20V MAX, brushless

 1. ▮▌ 3▮Klein Screwdriver Set▮Hand Tools/Screwdrivers/Sets▮1900-01-01▮{NULL} ▌ date unknown, no notes
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
VALUES (3, 'Klein Screwdriver Set', 'Hand Tools/Screwdrivers/Sets', '1900-01-01', NULL);
```

Note how:

- The schema-creation SQL is generated from the same file as the data
- `Number` type emits `1`, `2`, `3` without quotes
- `String` type wraps values in single quotes; a category path is one value, because `?#` does not split on `/`
- `Date` type wraps the date string in single quotes
- Record 3 is one compact row in the writers' default formatter. Its date is unknown, so `acquired` holds the null date `1900-01-01` (a date field always holds a date), and `notes` holds `{NULL}`, which emits `NULL`

---

## 9. Open Questions

One open question remains for resolution in a future spec version:

1. **Companion data files for non-tabular content** — when narrative text and bulk structured data (e.g., 3D points, large numeric arrays, images, audio) belong together, what is the file-pairing convention? Section 4's multi-file relational mechanism handles tabular companions; non-tabular companions (large opaque payloads keyed by event ID) may warrant a different convention. The exact syntax for declaring such companions in the file header is deferred.

All other open questions from earlier drafts have been resolved.

---

## 10. Glossary

| Term | Definition |
|------|------------|
| **Splicing character** | The first character (one Unicode code point) of a two-character formatter. Splits fields on a compact line and levels in a hierarchy. |
| **Comment character** | The second character (one Unicode code point) of a two-character formatter. Opens a comment when a space sits on each side of it, or as the first character of the data. Anywhere else it is data. |
| **Formatter** | The 2-character declaration at the start of a line: `<splice><comment>`. A 3-character marker is accepted on ingress and kept as its last two characters. |
| **Field** | A named, typed slot declared in a section's fields block |
| **Record** | A set of field values within a data block, separated by blank lines |
| **Section** | A self-contained record set with its own schema, optional templates, and data |
| **Levels** | The parts of a field declaration or header line, split on the splice character. Data lines have no levels: their splice separates field values |
| **Compact row** | A data line carrying several consecutive fields, separated by the splice character |
| **Multiline value** | A field value or template body that spans multiple lines: `[*`, `**` lines, `*]` |
| **`{NULL}`** | The text an author writes for "no value"; emitted as SQL `NULL`, stored as text |
| **Template** | A named multiline value in a section's intro containing literal text with `{N}` placeholders, used to emit each record (or each section) in some target format |
| **Sidecar** | A `.jCheck` file co-located with a source `.jText` file that caches validation results |
| **Structural validity** | Conformance to the grammar rules; enforced at parse time |
| **Semantic validity** | Conformance to the data rules declared by field types and constraints; checked on demand |
| **Missing field** | A field whose data line does not appear in a record; its value is the empty string |
| **Empty field** | A field whose data line appears but contains no data characters; the empty string |
| **Multi-file dataset** | A set of related jText files linked by key references and declared in the `related_files` file-header field |

---

## 11. Changelog

**v0.8**
- Section 3 / 3.1: formatter characters are any Unicode Punctuation or Symbol code point, not only printable ASCII; rules 6-9 added (code-point comparison, code-point counting, one-column preference with wide characters accepted, `[` splice reserved for `[]` and `[*`). `▮▌` is the writers' default formatter on every line; the writer falls back to ASCII punctuation only when the data holds `▮` or `▌`.
- Section 3.2: the comment marker's spaces are ASCII U+0020 only. A comment character in the data without a space on each side is data, with no warning (v0.7 warned).
- Sections 2.0.1, 2.0.2, 6.1: every block closes (`=== End Fields ===`, `=== End Data ===`, `=== End Section ===`, `=== End File ===`, `-- End Name --`, `-- EOF --`, `*]`); a block left open is a structural error. `jtext_close` adds missing closers. Unknown `===` markers are errors.
- Section 5: multiline values are `[*`, `**` lines, `*]`, replacing `<<< SENTINEL` blocks and the `*** ^^^ ###` special block. Templates open with a bare `[*`.
- Sections 2.6, 3.3: data stays data. In a data line the splice separates field values (a compact row); levels exist only in the Fields block and header. A path is one value on a line whose splice it does not contain.
- Section 2.6.1: a file holds no nulls. Empty and missing are the empty string for every type; `{NULL}` is text that template emission writes as `NULL`. `NULL` and `\N` are ordinary text; the v0.7 U+001F marker is read as `{NULL}`.
- Section 2.4.2: `Not Null` forbids `{NULL}`; the new `Not Empty` forbids the empty string. Field 1 is both.
- Sections 2.4, 2.6.1: a `Date` or `DateTime` field always holds a date. Where there is none, writers store the field's null date: `NullDate(YYYY-MM-DD)` from its declaration, else `1900-01-01`.
- Section 3.2: a comment character as the first character of the data, followed by a space, opens a comment; the value is empty.
- Section 5.1: a multiline value fills one field; the record continues at the next field or ends.
- Section 2.6.3: compact rows take any formatter, may start at any field position, and continue after a multiline value.
- Section 2.6: a line for field 1 starts a new record (field 1 is the record id). Whole-record multifield rows follow one another without blank lines; vertical records end with a blank line.
- Section 2.3: `jtext_version` is written by every writer; readers warn on a missing, older, newer or invalid version and read on; saving upgrades it. The light profile writes `# jText Version:`.
- Section 2.0: removed banner-less data rows and the light profile's undefined inline field block; a file is one profile or the other.
- Section 3.2.1: comments are data, and a comment belongs to the line it is on; a whole comment line is a line of its own and stays where it stands. Readers keep them and every export carries them. Section 4.3: in a database they live in a linked `<table>_comments` table.
- Section 4.3: field declarations live in a linked `<table>_fields` table, part by part, so `Not Empty`, `NullDate(...)` and auto designations survive a database round trip.
- Section 2.1: `//File:` holds the file's name only. `.sql` and `.jschma` files carry the block as `-- ` SQL comments.
- Section 3.7: why field positions stop at 99; writers pad to two columns in every section.

**v0.7**
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

*End of jText Specification v0.8 (draft).*

