//File:    README.md
//Date:    2026-06-05
//Purpose: README for jText Format and Tools
//
# jText

**A plain-text file format for structured tabular records.**
*The format CSV should have been.*

---

## What is jText?

jText is a text format for storing structured records — the kind of data you'd otherwise put in a CSV file, a SQL table, or a small spreadsheet. It's plain text, line-oriented, hand-editable in any text editor, and designed to handle real-world content without escaping.

jText has two **file profiles** (see `SPEC.md` §2.0). Both start with the same `//` filesystem wrapper; they differ in how sections and schema are expressed.

### Light profile (human-facing — default for summaries and manifests)

The `//` block is for humans (`head`, git, grep). The `#` block is for jText tooling. Sections use `-- name --`; shared field lists use `# Fields: path.jtFlds`.

```
//File:    run_manifest.jtext
//Date:    2026-06-08
//Purpose: ts_store test matrix run manifest
//Related: type=ts_store table=ts_run_manifest
//
# JText File - created 2026-06-08T00:00:00Z
# Purpose - ts_store test matrix run manifest
# Case: Sensitive
# Table Name: ts_run_manifest

-- RunMeta --

# Fields: tests/jtext_includes/run_manifest_runmeta_fields.jtFlds

 1. |# OS_001|ssd|Smoke|2026-06-08T00:00:00Z|gcc|113|113|0

-- Scenarios --

# Fields: tests/jtext_includes/run_manifest_scenarios_fields.jtFlds

  1. |# TS_STORE_TEST_001|gcc|binary|off|100|0.005|PASS|logs/...
 10. |# TS_STORE_TEST_002|gcc|binary|off|100|0.005|PASS|logs/...
```

See `samples/light_profile/run_manifest.jtext` for a complete reference file.

### Full profile (DB tooling and high-throughput logs)

Uses `===` section markers, optional SQL templates, and `=== Fields ===` / `=== Data ===` blocks. Example — a hand-edited tool catalog:

```
//File:    tools.jText
//Date:    2026-06-05
//Purpose: jText Data File
//
=== jText File ===
 1. ?# tools.jText                       # filename
 2. ?# 2026-05-11                        # date
 3. ?# Personal workshop tool catalog    # purpose

=== Section: Hand Tools ===
=== Fields ===
 1. /# id/Number/Not Null
 2. /# name/String/40/Not Null
 3. /# category/String/100/Not Null
 4. /# notes/String/Nullable
=== Data ===
 1. ?# 1
 2. ?# Stanley Hammer
 3. /# Hand Tools/Hammers/Claw
 4. ?# 16oz fiberglass handle
=== End Data ===
=== End Section ===
```

You can read either profile top to bottom and understand exactly what it contains. You can edit it in Notepad. You can pipe it through `grep`. You can commit it to git and get meaningful diffs. And — unlike CSV — a stray comma, slash, or quote in your data won't silently corrupt it.

---

## Why does jText exist?

Every common format for tabular data has a fatal flaw for the kind of work jText is meant for.

### CSV: silent corruption

CSV is the universal default for tabular data, but it has a notorious failure mode: **a single stray comma in a field can shift every subsequent column out of alignment, producing wrong data with no error**. The quoting rules that are supposed to fix this are themselves inconsistent — Excel, Python's `csv` module, PostgreSQL's `COPY`, and `awk -F,` all disagree about edge cases. CSV has no comments, no types, no schema, no way to distinguish `NULL` from empty string, and no way to express hierarchy.

If your data contains commas, quotes, newlines, or any other punctuation, CSV is a minefield.

### JSON: verbose for records, hard to manipulate manually

JSON is excellent for nested object graphs (API payloads, configuration, document databases). It's poorly suited to repetitive tabular data: every record repeats every key name, which is wasteful for a million-row dataset and visually exhausting for a hundred-row one. JSON has no comments, fragile syntax (trailing commas, mismatched braces), and offers no help with the "schema lives somewhere else" problem.

If your data is **records** rather than **objects**, JSON is the wrong shape.

### TOML: too verbose for repetitive data

TOML is a great configuration format. It's structurally honest, handles nesting well, and is much more readable than JSON for hand-editing. But it optimizes for "many keys, each used once" — exactly backwards from the record-set case where the same schema repeats thousands of times. Every record header (`[[person]]`) and every key (`first_name = "..."`) is overhead.

If you have 500 records sharing the same schema, TOML wastes space and visual real estate on every one of them.

### YAML: fragile, complex, surprising

YAML's significant whitespace is a load-bearing feature that breaks in subtle ways. The spec is famously complex. Security issues are common. Norway becomes `false`. Different YAML libraries disagree on what valid input means.

YAML is not a serious choice for data you need to trust.

### What's missing

There's a real niche between these formats: **structured tabular records, with real-world content, edited by humans, occasionally migrating to a database**. None of CSV, JSON, TOML, or YAML serves this niche well. jText is the format designed for it.

---

## What makes jText different?

### Author-chosen delimiters

Every other text format has *fixed* reserved characters: CSV reserves `,` and `"`, JSON reserves `{` and `"`, TOML reserves `=` and `[`. When your data contains those characters, you must escape them — and the escaping rules are the source of most format bugs.

jText reverses this. **Each line carries its own two-character marker:** `<splice><comment>`. The older marker was three characters. Dropping one character on every field line is 10 million bytes on a 10 million row compact table, and it multiplies by the number of field lines.

- The first character splits a data line into fields (a multifield line: each piece fills the next field), and a field declaration into its parts (`name/Type/Not Null`). Data stays data: a path such as `Hand Tools/Hammers` is one value, so write it with a splicing character it does not contain (`?#`).
- The second character starts a comment when a space sits on each side of it. If the data contains `#`, choose a different comment character. The same character in the data without a space on each side stays data: `C#` is just `C#`.
- A `'` is stored as `'`.

```
 1. /# /usr/local/bin/node      # path, three levels, split on /
 2. |# 4/3 = 1.333              # one value; | does not appear, so the line is not split
 3. ?| She said "hello" today   ? the quotes stay in the text
```

An old file may still have a three-character marker, `{1}{2}{3}`. The reader accepts it and, on ingress, keeps `{2}{3}`. Display and every later write use those two characters. `#|#` is shown and saved as `|#`. A new table schema allows only the two-character marker.

### Schema declared once, referenced by position

Instead of repeating field names on every record, jText declares the schema once at the top of each section, then identifies fields by their position number:

```
=== Fields ===
 1. /# customer_id/Number/Not Null
 2. /# name/String/40/Not Null
 3. /# email/String/255/Nullable
=== Data ===
 1. ?# 1
 2. ?# Alice
 3. ?# alice@example.com

 1. ?# 2
 2. ?# Bob
=== End Data ===
```

For a thousand records with twenty fields, this saves significant space and effort compared to JSON's per-record key repetition. It also gives the eye a stable visual anchor: every record looks the same, with the field positions aligned in the same columns.

### Multi-file relational datasets

A jText file can declare other jText files it relates to, using key fields to link records across files. This gives you the equivalent of database tables — one-to-many, many-to-one, many-to-many — entirely in plain text:

```
customers.jText  →  orders.jText  →  order_items.jText
```

Each file is independently valid and readable. The set is a relational dataset that can be browsed in any text editor, version-controlled, emailed, or migrated to a real database when needed.

### Painless on-ramp and off-ramp to databases

Each section in a jText file can declare templates for emitting its content in any target format. A common pattern combines schema-creation and data-insertion templates so the same file can populate a real database in one command:

```
jtext emit tools.jText --templates "Create Table" "SQL Insert" | sqlite3 inventory.db
```

The schema travels with the data. The `CREATE TABLE` and `INSERT` statements are right there in the file alongside the records they describe. There's no separate schema document that drifts out of sync with the data.

And the migration is bidirectional: an export tool reading from the database can produce a new jText file using the same schema declarations. The data isn't trapped in the database; it can come back to plain text whenever editing or sharing makes that more convenient.

### Comments on every line

```
 1. /# customer_id/Number/Not Null     # primary key, auto-incremented
 2. /# name/String/40/Not Null         # legal name; may include suffixes
 3. /# email/String/255/Nullable       # primary contact email
```

Comments belong in data files. CSV and JSON don't have them; jText does, on every line.

### Save anything, use only when valid

jText separates *creating* a file from *using* a file. You can save a half-finished, mid-edit, or inconsistent file at any time — the editor doesn't block you, the format doesn't punish you. Validation happens when someone tries to *use* the file (emit SQL, import into another system, etc.), at which point any errors are reported clearly.

This matches how humans actually work with data: imperfect, iterative, in-progress. The format respects that workflow.

---

## What jText is good at

- **Hand-curated record sets** (tool inventories, recipe collections, contact lists, hierarchical taxonomies)
- **Structured log files** where humans need to review what happened
- **Configuration with many similar entries** (rules, scheduling, mappings)
- **Migration sources and destinations** for database tables (schema travels with data)
- **Files that round-trip through spreadsheets** (the line-positional structure maps cleanly to columns)
- **Data with content that breaks other formats** (paths, fractions, free text, punctuation-heavy content)
- **Multi-file relational datasets** that you want to read and edit in plain text

## What jText is not good at

- **Nested object graphs** (use JSON — jText doesn't try)
- **Wide tables with hundreds of columns** (use a database or Parquet)
- **High-volume querying** (use a database; export back to jText when you want plain text)
- **Pure configuration with no repetition** (TOML is better for that)
- **Single-value scalars or trees** (use whatever your application uses)
- **Adversarial parsing scenarios** where you need cryptographic integrity (jText's hash check is for change detection, not tamper resistance)

---

## A complete example: from hand-edit to database

Here's the lifecycle jText is designed for.

**1. Start in a text editor.** Type a file like the tools inventory above. Save it whenever you want, in whatever state. The format doesn't second-guess you.

**2. Validate when you're ready.** A jText tool checks the file against the schema, reports any issues with line numbers and clear messages. The file isn't modified; you fix and re-check.

**3. Emit to whatever you need.** Templates in the file describe how to convert each record to SQL, CSV, JSON, or anything else. One command produces the output.

**4. Migrate to a database when scale demands it.** The same `Create Table` and `SQL Insert` templates that produced your output statements populate a fresh database. No translation step, no schema rewriting, no escaping bugs.

**5. Come back to text when you want to.** An export tool reads from the database and produces jText files using the original schema. The data isn't trapped.

This is a workflow that current formats don't support well. CSV can't carry the schema. JSON is awkward for tabular data. TOML can't handle the repetition. A database is overkill until it isn't.

---

## Status

jText is currently in **specification development**. The format itself is stable enough to commit to (see `SPEC.md` for the formal rules), but the reference implementation is in progress. The repository contains:

- `SPEC.md` — the formal format specification
- `XML_EXPORT.md`, `ODS.md` — the canonical XML export and the OpenDocument spreadsheet layout
- `README.md` — this document
- Reference C++ implementation (`v001` C++23, `v002` C++26), with tools to and from SQL
  (SQLite, PostgreSQL, MariaDB), XML and `.ods`
- `v00x/Qlite` — [Qlite](https://github.com/JayACarlsonAtHome/Qlite), one SQL API over SQLite,
  PostgreSQL and MariaDB, brought in from its own repository (see its `VENDORED.jtext`); the
  database tools read through it
- Contracts (`v00x/src/contracts.hpp`, `JTEXT_PRE/POST/ASSERT`): calling `JTextWriter` out of
  order — a header setter after the header, a Full-profile block (include, template, Fields,
  Data) in a Light file, outside a section or after the Data block — ends the program. With
  GCC 16, v002 builds with `-fcontracts` and these are native C++26 contracts; elsewhere they
  are runtime checks

If you're interested in the format itself — to implement a parser, write a tool, or just give feedback — the spec is the authoritative document. If you want to use jText for real work today, expect rough edges in the tooling.

---

## Frequently asked questions

**Q: Why a new format? Aren't there enough already?**
A: There are many formats; none of them serve the niche jText targets well. CSV silently corrupts on commas in data. JSON is verbose for records and hard to edit by hand. TOML is wrong for repetitive data. YAML is a foot-gun. jText is specifically designed for hand-edited, schema-driven, tabular records with real-world content — a use case that all four of those formats handle poorly.

**Q: Is this competing with JSON?**
A: No. JSON is excellent for nested object graphs. jText doesn't try to compete in that space. jText handles tabular records; JSON handles trees and structures. Different jobs.

**Q: Why the funny `?#` and `/#` syntax?**
A: It's the heart of the format. Those two characters declare, for that line only, the splicing character and the comment character. By making this per-line and author-chosen, jText eliminates every escaping rule that other formats require. There's no `\"` or `\\n` or `&amp;` anywhere — because the author picks structural characters that don't appear in their data. An older file may still say `#?#` or `#/#`. The reader keeps the last two characters.

**Q: How is this related to ts_store?**
A: [ts_store](https://github.com/JayACarlsonAtHome/ts_store) is a multithreaded ordered logging library by the same author. jText is the natural plain-text format for ts_store's output: humans can read the logs, tools can validate them, and the same file can be loaded into a SQL database when bulk analysis is needed. The two projects are designed to work together but are independently useful.

ts_store emits multifield data lines for efficiency:
` 1. ▮▌ <id>▮f2▮...`
where the number is the first field's position and each value fills the next field. `▮▮` is an empty string. A file holds no nulls: `{NULL}` is the text for "no value", which SQL output writes as `NULL` (see SPEC.md §2.6.1, §2.6.3). Older logs used `\x1F` (ASCII Unit Separator) for null; readers take it as `{NULL}`.

**Q: Will jText files always be parseable by future versions of jText?**
A: The `jtext_version` field in the file header names each file's spec version. A reader warns when it is missing, older or newer, and reads on; saving upgrades it. Forms a version removes are converted by `jtext_close` (for example v0.7's `<<<` multiline blocks), so an older file can always be brought forward.

**Q: Can I use jText commercially?**
A: Yes. The specification and reference implementation are Apache-2.0 licensed.

---

## Comparison at a glance

| Concern | CSV | JSON | TOML | YAML | jText |
|---------|-----|------|------|------|-------|
| Plain text, hand-editable | Yes | Yes | Yes | Yes | Yes |
| Comments | No | No | Yes | Yes | Yes |
| Types | No | Limited | Yes | Limited | Yes |
| NULL distinct from empty | No | Yes | No | Yes | Yes |
| Hierarchy in values | No | Yes (nested) | Yes (tables) | Yes | Yes (per-line) |
| Repetitive records | OK | Verbose | Verbose | OK | **Compact** |
| Real-world content (slashes, quotes, etc.) | **Fragile** | Escape-heavy | Escape-heavy | Surprising | **Native** |
| Schema declared in file | No | No | Implicit | No | **Explicit** |
| Validation built into the format | No | Limited | Limited | Limited | Yes |
| Multi-file relational structure | No | No | No | No | **Yes** |
| Database migration support | No | Manual | Manual | Manual | **Templated** |
| Indentation matters | No | No | No | **Yes (fragile)** | No |
| Best for | Simple tables | Object graphs | Configuration | Configuration | Tabular records |

---

## Project links

- **Repository**: [github.com/JayACarlsonAtHome/jText](https://github.com/JayACarlsonAtHome/jText)
- **Companion project**: [github.com/JayACarlsonAtHome/ts_store](https://github.com/JayACarlsonAtHome/ts_store)
- **License**: Apache-2.0

---

*"It's the format CSV should have been."*
