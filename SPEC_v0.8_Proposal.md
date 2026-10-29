//File:    SPEC_v0.8_Proposal.md
//Date:    2026-10-28
//Purpose: Proposed changes to SPEC.md for v0.8
//
# Proposal: jText v0.8

**Status:** Proposal. Not merged into `SPEC.md`.
**Changes:** Sections 2.0, 2.1, 2.3, 2.6, 3, 3.1, 3.2, 3.6, 5, 6.1, 10, 11.

| Part | Change | Code (canonical jText, v001 + v002) |
|---|---|---|
| A | Unicode formatter characters; standard formatter `▮▌` | implemented |
| B | Every block closes, like XML | implemented, plus `jtext_close` |
| C | Multiline values: `[*` … `**` … `*]` | implemented |
| D | Removed: banner-less rows, special blocks, `<<<` sentinel blocks, the light profile's undefined inline field block | implemented; `jtext_close` rewrites `<<<` blocks |
| E | Writers emit the two-character formatter | implemented |
| F | Why field positions stop at 99 (rationale only) | no code change |
| G | `//File:` holds the file's name only | implemented |

---

## Part A — Unicode formatter characters

### A.1 Summary

A formatter character may be any Unicode punctuation or symbol character, not only
ASCII punctuation. Every v0.7 formatter stays valid. One new standard formatter is
added for machine-written records:

```
 1. ▮▌ 42▮7▮sensor.imu▮roll=0.01 pitch=0.02▮1700000000000042 ▌ a comment
```

`▮` (U+25AE BLACK VERTICAL RECTANGLE) splits fields. `▌` (U+258C LEFT HALF BLOCK) is
the comment character.

### A.2 Motivation

The format never escapes. When data holds a structural character, the author picks a
different one for that line (Section 1). With only ASCII punctuation to pick from,
machine producers that write free text, such as event logs, keep running into their
own structural characters: `|` appears in shell commands, `#` in "order #5" and "C#".

Measured on this machine (occurrences per million characters):

| Character | System journal (377 KB) | /usr/share/doc (30 MB) |
|---|---|---|
| `\|` | 13 | 427 |
| `#` | 114 | 2,447 |
| `~` | 0 | 712 |
| `` ` `` | 8 | 1,999 |
| Block elements, U+2580–U+259F (includes `▌`) | 0 | 20 |

`▮` (U+25AE) did not occur in either. Section 2.2 already makes every jText file
UTF-8, so these characters are already legal in data; only the formatter rule was
ASCII-only.

### A.3 Changes

**Section 3, token table.** `<formatter>`: "Exactly 2 Unicode characters (code
points): `<splice><comment>`".

**Section 3.1, rule 3** is replaced:

> 3. Each character is a single Unicode code point whose General Category is
>    **Punctuation** (`Pc`, `Pd`, `Ps`, `Pe`, `Pi`, `Pf`, `Po`) or **Symbol** (`Sm`,
>    `Sc`, `Sk`, `So`). Any other character — a letter, a digit, a combining mark,
>    whitespace, a control or format character (such as a zero-width joiner), a
>    private-use, unassigned or surrogate code point — is a **structural validation
>    error**. A formatter that is not valid UTF-8 is a structural validation error.

Every printable non-alphanumeric ASCII character is Punctuation or Symbol, so every
v0.7 formatter remains valid.

Added rules:

> 6. Characters are compared by code point. No Unicode normalization is applied.
> 7. "Two characters" and "the right two characters" of a longer marker count code
>    points, not bytes.
> 8. Writers should prefer characters that display one column wide in common
>    monospace fonts. Emoji and other double-width characters are valid but break the
>    column alignment of Section 3.7. They are accepted as written: neither readers nor
>    the validator warn about them.
> 9. `[` as the splicing character is reserved for special forms: `[]` (raw data) and
>    `[*` (a multiline value, Part C). No other formatter may use `[` as its splice.

The common-conventions table gains one row:

| Formatter | Typical use |
|-----------|-------------|
| `▮▌`      | Machine-written compact records whose data is free text. `▮` (U+25AE) splits fields; `▌` (U+258C) is the comment character. Both are rare in real text |

**Section 3.2.** No change in meaning. The comment marker's spaces are ASCII U+0020
only: with `▮▌`, the marker is ` ▌ `. Other Unicode spaces (such as U+00A0) are not
spaces for this rule.

**Section 2.3.** A file whose formatters use any character outside ASCII **must**
declare `jtext_version` `0.8` or later: header line 4 in the full profile, and the line
`# jText Version: 0.8` in the light profile's `#` block (after `# Purpose`, the full
profile's order). Readers implementing v0.8 parse such lines regardless of the declared
version. *(Both writers declare `0.8`.)* A reader warns when the version is missing,
older, newer or not a number, and reads on; saving upgrades a missing or older
declaration to the writer's version and never lowers a newer one (SPEC 2.3, "Version").

**Section 6.1.** "Malformed formatters" now includes a character that is not
Punctuation or Symbol, and a formatter that is not valid UTF-8.

**Section 10.** **Splicing character** and **Comment character**: "character" means
one Unicode code point.

### A.4 Implementation

- `src/parser/utf8.hpp`: strict UTF-8 decoding and encoding.
- `src/parser/unicode_ps_table.hpp`: the 349 Punctuation/Symbol ranges of Unicode
  16.0, generated by `tools/gen_unicode_ps_table.py` (no library dependency).
- `JTextEntry::delimiter`, `JTextEntry::level_sep` and `append_row`'s separator are
  `char32_t`. Code assigning `'|'` still compiles.
- `▮` is `E2 96 AE` and `▌` is `E2 96 8C` in UTF-8. `cut -d` takes a single-byte
  delimiter and cannot split on `▮`; `awk -F'▮'` works with gawk in a UTF-8 locale.

---

## Part B — Every block closes, like XML

### B.1 Summary

Every block that opens must close. A block left open is a structural error. This is
what a file cut off mid-write looks like, so a truncated file is never mistaken for a
complete one.

| Profile | Opens | Closes with |
|---|---|---|
| Full | `=== jText File ===` | `=== End File ===` |
| Full | `=== Section: name ===` | `=== End Section ===` |
| Full | `=== Fields ===` | `=== End Fields ===` |
| Full | `=== Data ===` | `=== End Data ===` |
| Light | `# JText File - created …` | `-- EOF --` |
| Light | `-- Name --` | `-- End Name --` (the name must match) |
| Either | `[*` (Part C) | `*]` |

### B.2 Changes

**Section 2.0.2.** "End markers are recommended but not required" becomes "End
markers are required". The template gains `=== End Fields ===` after the field lines;
`=== Data ===` no longer closes a Fields block.

**Section 2.0.1.** Light-profile rules: each section closes with `-- End Name --`;
`-- EOF --` is required (it was optional).

**Section 6.1.** New structural errors:
- A block not closed before the next block, the file's closer or the end of the file.
- `-- End Name --` that does not match the open section.
- Anything other than blank lines and comments after the file's closer.

### B.3 Closing a file

A file cut off mid-write, or written before closers were required, is closed by
adding its missing closers. `jtext_close FILE...` (library:
`jtext::add_missing_closers`) puts each where its block ends — `=== End Fields ===`
before `=== Data ===`, a section's closer before the next section, and everything
still open at the end of the file — lists every line it added, and changes nothing
else. `--check` only reports. A last line with no newline is reported (it may have
been cut off) and kept.

---

## Part C — Multiline values: `[*` `**` `*]`

### C.1 Summary

Replaces the `<<< SENTINEL` block (Section 5) and the legacy special block
(`*** ^^^ ###`). Every line of the value carries its own marker. A Store record
whose payload (field 6) is a stack trace:

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

The number is the field position (Section 2.6), as on every line: field 6 here.
Field 1 is the record id (Section 2.4.3), always an integer, so it is never multiline.

### C.2 Rules

1. In the file, exactly one space separates a marker from what follows it. The space
   belongs to the marker: `**   File` holds `  File`. Everything after it is the
   value's line, verbatim, trailing spaces included.
2. `**` alone (or followed by one space) is an empty line of the value.
3. `[*` sits where a formatter would; `[` is reserved for it (Part A, rule 9). Content
   lines and the closer carry no line number.
4. Alignment. Writers indent content lines and the closer to the column of the
   opener's marker, so every marker (`|#`, `[*`, `**`, `*]`) is in one column and
   every value starts in the next. Line numbers are two columns (Section 3.7), so
   that column is always the fifth (` 6. ` is four characters). Readers ignore
   leading spaces and tabs before `**` and `*]`: alignment is cosmetic, and a hand
   edit that is out of line still reads.
5. A template (Section 2.5) opens with a bare `[*` on the line after its banner.
6. Comments are only on the opener. A `**` line is all value.
7. A value open at the end of the file, or at a structural marker, is a missing
   closer (Part B).
8. A compact row is one line, so it cannot hold a multiline value. A record with one
   breaks there: the fields before it are a compact row from `1.`, the multiline
   field is on its own numbered line, and the fields after it are a compact row
   starting at the next position (Section 2.6.3 already allows a row to start at any
   field position).
9. Editors and viewers may show the marker and the line in separate columns; the
   space is part of the text form only.
10. In the text form the closer is always alone on its line: nothing follows `*]`.
   An editor shows no empty closer row: it puts the `*]` marker on the value's last
   line, beside that line's data. Writing out splits that row back into `** <data>`
   and a bare `*]`; reading puts the marker back on the last `**` row.

   ```
   editor (marker | line)              text form
   [*  | # stack trace                  6. [* # stack trace
   **  | Traceback (most recent call)      ** Traceback (most recent call)
   *]  | ValueError: bad input             ** ValueError: bad input
                                           *]
   ```

### C.3 Why

- **No sentinel to choose, no collision.** Every value line is prefixed, so the value
  can contain anything, including a line that reads `*]` or `=== End File ===`.
- **Every line says what it is,** in keeping with the per-line grammar (Section 1).
  `grep '^\*\*'` finds every value line.
- **It closes like XML**, so a cut-off value is detected and `jtext_close` can close it.
- **`*` starts nothing else.** Data lines start with a number, comments with `#` or
  `//`.

### C.4 Writers

A value that contains a newline is written as a multiline value (rule 8 for a
compact row), aligned (rule 4). No choice is needed: the markers never collide with
the data. Writers also pad line numbers to two columns in every section (Section
3.7); the reference writer only did so in sections of more than nine lines.

---

## Part D — Removed

1. **Banner-less data rows.** Rows after a section's includes or templates with no
   `=== Data ===`, and rows with no section at all. Data rows live inside a Data block
   (full profile) or a `-- Name --` section (light profile). *(Removed.)*
2. **Special blocks** (`*** ^^^ ###`). Not in the SPEC; one sample file uses one.
   Replaced by Part C. *(Removed; the sample was converted by hand.)*
3. **`<<< SENTINEL` blocks** (Section 5). Replaced by Part C; `jtext_close` rewrites
   existing ones, line for line (a template's opener becomes a bare `[*`). *(Removed.)*
4. **"An inline field block" in the light profile** (Section 2.0.1). The SPEC named it
   but never gave it a form, and no reader accepted one. A light-profile section names
   its fields with `# Fields: <path>.jtFlds`; inline field definitions are the full
   profile's `=== Fields ===` block. A file is one profile or the other, never a mix.
   *(Removed from the text; nothing read it.)*

---

## Part E — Writers emit the two-character formatter

v0.7 already makes the two-character `<splice><comment>` form the written form; the
reference writer emitted the old three-character one. It now writes `|#`, `|^`,
`▮▌`. Readers still accept a three-character marker (Section 3.1 ingress rule).

The writer's default formatter is `▮▌`, on every line it writes: data rows, Fields
lines and header lines. Where the caller leaves a character to it, the writer uses `▮`
as the separator and `▌` as the comment character, unless the row's data holds that
character; then it uses the first ASCII punctuation character that is not in the data.
A separator the caller did give that appears in the data is a bad row, which the
writer refuses (`BadRowPolicy`: terminate the process, or skip the row). A comment
character the caller gave is kept as given.

---

## Part F — Why field positions stop at 99 (Section 3.7)

No rule changes; the limit stays. Section 3.7 gains its reason:

> Line numbers are right-aligned in two columns, so every line of a file spends the
> same width on its number. A 100th field position would make that three columns on
> every line, not only on the wide records: one more character per line, which adds
> up in long tables. A record of more than 99 fields is also past what a person reads
> as one record. Producers with wider rows split them into related tables (Section 4).

---

## Part G — `//File:` holds the file's name only (Section 2.1)

`//File:    <full-or-relative-path>` becomes `//File:    <the file's name>`, with:

> `//File:` holds the file's name only, never a path: files and the projects that hold them
> move (a portable drive mounts at a different place on each OS), and a recorded path would go
> stale.

The writer (`write_file_comment_header`) writes the name of the path it is given. *(Implemented.)*

---

## Compatibility

Every v0.7 file without the removed forms (Part D) reads under v0.8 once it is
closed: `jtext_close` adds the closers v0.7 left optional. v0.7 readers cannot read
files that use non-ASCII formatters or `[*` values.

The library has one parser. `JTextFile::read_full` and `read_section` read through
`parse_file_structure`, the same parser the validator and tools use, so every reader
agrees on every line and enforces closers. Before, `JTextFile::read_full` read
`1. ▮▌ 1▮a|b # c ▌ x` silently as one value while `parse_line` rejected it.
