# jText — TODO

## jText ↔ SQL round-trip ctest (self-contained, SQLite)

**Goal:** prove jText round-trips through SQL and back with full fidelity, as a **ctest
that runs on every machine** — no external database.

**Why it isn't done yet:** `jtext_xtox_sql` handles jText → SQL (emits text), but
`jtext_from_sql` is **PostgreSQL-only** (`psql`), so the return leg gets *skipped* in CI
without a live DB — a test that's green because it never ran. (The bloopers warn about
exactly this.)

**Plan:**
1. Add the missing piece — a **SQLite → jText emitter** (read rows via Qlite and emit
   jText, or teach `jtext_from_sql` a `--sqlite` mode).
2. Round-trip ctest:
   `jText fixture → jtext_xtox_sql → SQL text → sqlite3 load → SQLite→jText → compare`,
   asserting the round-tripped jText equals the original (semantic equality).
3. SQLite is a baseline dependency, so this runs everywhere and actually *exercises* the
   round trip instead of skipping it.

**Where:** here (standalone jText, the canonical repo — see `PROCESS.md`); v002 first,
then forward-port to jac313.

**Related:** pairs with the binary-reader `convert_to_sql()` idea (same SQLite-as-the-
self-contained-DB theme).
