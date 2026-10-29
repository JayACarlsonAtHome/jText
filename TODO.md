# jText — TODO

## jText ↔ SQL round trip: done

`tool_round_trip_sqlite`, `tool_round_trip_postgres` and `tool_round_trip_mariadb` take one
fixture (`tools/tests/fixtures/roundtrip/`) through `jtext_xtox_sql` into each database and
back through `jtext_from_sql` (through Qlite: libpq / SQLite / MariaDB Connector/C, never a text dump),
export the original and the copy as data-scope XML (`XML_EXPORT.md`), and compare the bytes.
PostgreSQL works in a throwaway schema, MariaDB in a throwaway database (the login needs
`` GRANT ALL ON `jtext\_round\_trip\_%`.* ``); each is reported as skipped where no server
answers. MariaDB loads the SQL with `NO_BACKSLASH_ESCAPES` (SPEC 4.3).

What a database table cannot hold does not come back: `Not Empty`, a `NullDate(...)` other
than the default, and auto designations (`Identity(1,1)`). Keeping them would need the
declarations stored beside the table, as the comments are.

**Never CSV**, for reading, writing or testing: a stray delimiter or quote corrupts data
silently, which is why jText exists. Nor any other delimiter-separated dump (`psql -A -F`).

## Import / export targets

Every target reads and writes through a proper library or a fully defined format, never CSV.

| Target | Status |
|---|---|
| SQLite | done: `jtext_xtox_sql` / `jtext_from_sql --sqlite`, round-trip tested |
| PostgreSQL | done: `jtext_xtox_sql` / `jtext_from_sql`, round-trip tested |
| MariaDB | done: `jtext_xtox_sql` / `jtext_from_sql --mariadb`, round-trip tested |
| LibreOffice Calc (`.ods`) | done: `jtext_to_ods` / `jtext_from_ods` (`ODS.md`), round-trip tested directly and through LibreOffice; declarations a database loses survive here |
| XML | export done (`XML_EXPORT.md`, `jtext_to_xml`): the compare standard for every round trip; import later |
| HTML | later (jac313's Store writes HTML event logs; jText itself has no HTML export) |
| Text | later: the form (aligned columns, a report) is still to be defined |

**Related:** the binary-reader `convert_to_sql()` idea (same SQL theme).
