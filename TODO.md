# jText — TODO

## jText ↔ SQL round-trip test (SQLite and PostgreSQL)

**Goal:** prove jText round-trips through SQL and back with full fidelity: one jText fixture
saved to SQLite and to PostgreSQL, read back from each into its own jText file, and all three
compared equal (field names, types, constraints, every value: `{NULL}`, empty, quotes, `|`,
`#`, `▮`, multiline values, null dates, paths).

**Plan (in progress):**
1. `jtext_xtox_sql` also emits the section's `Create Table` template, so its `.sql` builds
   the table as well as filling it.
2. `jtext_from_sql` reads through the client libraries, never through a text dump: libpq
   (`PQgetvalue` / `PQgetisnull`) and SQLite (`sqlite3_column_text` / `SQLITE_NULL`) hand over
   each value and its NULL flag with no delimiter parsing. Names go into queries as bound
   parameters. It writes the data rows with `JTextWriter` (SQL NULL becomes `{NULL}`; a NULL
   date becomes the null date). A `--sqlite DB` mode alongside PostgreSQL.
3. A canonical XML exporter (`XML_EXPORT.md`): every round trip ends there. The ctest
   exports the original and both read-backs in `data` scope and compares the bytes.
4. PostgreSQL runs in a throwaway schema; it is reported as skipped, never passed, where
   no server is available. SQLite runs on every machine.

**Never CSV**, for reading, writing or testing: a stray delimiter or quote corrupts data
silently, which is why jText exists. Nor any other delimiter-separated dump (`psql -A -F`).

**Where:** here (standalone jText, the canonical repo — see `PROCESS.md`); v002 first,
then forward-port to jac313.

## Import / export targets

Every target reads and writes through a proper library or a fully defined format, never CSV.

| Target | Status |
|---|---|
| SQLite | in progress (round-trip above) |
| PostgreSQL | in progress (round-trip above) |
| MariaDB | later: a third client-library backend (the editor already connects to MariaDB) |
| Excel on Linux | later: the editor already imports/exports `.xlsx`; LibreOffice Calc's own `.ods` is not written yet |
| XML | export specified (`XML_EXPORT.md`, draft): the compare standard for every round trip; import later |
| HTML | later (jac313's Store writes HTML event logs; jText itself has no HTML export) |
| Text | later: the form (aligned columns, a report) is still to be defined |

**Related:** the binary-reader `convert_to_sql()` idea (same SQL theme).
