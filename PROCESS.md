# Process — where Qlite and jText changes happen

> **Mirrored doc.** This file is kept identical in the **jac313**, **Qlite**, and **jText**
> repositories so the process is discoverable from whichever repo you're standing in. If you
> change it, change all three copies.

**The rule (effective now):** all changes to **Qlite** and **jText** happen in their own
**independent repositories**, never inside jac313. jac313 carries *vendored snapshots* of
these libraries; it consumes them, it does not author them.

```
   Qlite repo  (canonical)  ──forward-port──▶  jac313/vNNN/Qlite      (embedded snapshot)
   jText repo  (canonical)  ──forward-port──▶  jac313/vNNN/jText      (embedded snapshot)
```

Flow is **one-way only: canonical → jac313.** Never edit `jac313/vNNN/Qlite` or
`jac313/vNNN/jText` in place, and never port a change back the other way.

---

## Why

- **The standalone repo is the runway.** You build a small library *out* and *up* on its
  own terms — its own tests, its own bootstrap, green in isolation — and only then bring a
  proven snapshot into jac313, at an integration point you choose. A standalone repo that
  runs *ahead* of jac313's embedded copy is normal and expected; that head-room is the
  entire reason the small repos exist.

- **It kills silent reverse-drift.** The one drift that's actually dangerous is a fix made
  *in* jac313's embedded copy (because the symptom showed up under Store) that never makes
  it back to canonical — now the trunk is quietly behind on a real fix, or the bug gets
  fixed twice, differently. Forbidding in-place edits removes that failure mode by
  construction.

- **It preserves the no-outside-dependency guarantee.** jac313 embeds a *copy*, not a
  build-time dependency on a sibling checkout. The snapshot can't be broken by upstream
  churn, a moved tag, or a missing repo. Self-contained version worlds stay self-contained.

---

## When a library is brought in

The trigger is **solid and green** — never a half-finished change, never "I'll stabilize it
once it's in jac313." A snapshot is eligible to be forward-ported only when, *in its own
repo*, it is fully green on its own terms:

- both version worlds build and pass (`v001` C++23, `v002` C++26),
- both front-ends — textual **and** C++ modules — build and pass,
- the library's own tests, tools, and examples are green (and, where it matters, on both
  gcc and clang).

Green in the standalone repo is the **entry ticket** to jac313. The forward-port is a
mechanical copy of an already-proven state, not a place where stabilization happens. If it
isn't solid standalone, it isn't ready to come in — keep building it *out and up* in its own
repo until it is.

## How a forward-port is done

Bringing a library's changes into jac313 is a **copy + namespace transform**, per version:

| Canonical (standalone) | jac313 (embedded) |
|------------------------|-------------------|
| `Qlite::v001` / `Qlite::v002` | `jac313::Qlite::v001` / `jac313::Qlite::v002` |
| `jText::v001` / `jText::v002` | `jac313::jText::v001` / `jac313::jText::v002` |
| module `Qlite.vNNN`, `jText.vNNN` | module `jac313.qlite.vNNN`, `jac313.jtext.vNNN` |
| `#include <Qlite/vNNN.hpp>`, `<jText/vNNN.hpp>` | `<jac313/Qlite/vNNN.hpp>`, `<jac313/jText/vNNN.hpp>` |

The transform is a uniform **prepend `jac313::`** (and the matching module / include-path
rename). Standards are fixed per version on both sides: **v001 = C++23, v002 = C++26.**

On each forward-port into jac313:
1. Apply the namespace/module/include transform to the copied files.
2. Bump the package `version()` literal (`vNNN.NNN`) in the same commit — the
   `jac313_test_cli version-check` gate enforces it.
3. Keep the frozen consumer surface that Store depends on (the SQL sink for Qlite, the
   `JTextWriter` path for jText); additive change is fine, signature breakage is not unless
   the consumer is updated in the same commit.
4. Build + test both the textual and module front-ends before pushing.

## What this forbids

- Editing `jac313/vNNN/Qlite/**` or `jac313/vNNN/jText/**` to add features or fix bugs.
- "I'll fix it here for now and backport later" — later doesn't happen; do it in the
  canonical repo and forward-port.

If a bug only reproduces under jac313/Store, reproduce or add the regression test in the
**canonical** repo (the parser/validator and the SQLite wrapper are identical there), fix
it canonical-side, then forward-port.

## Optional future guard

Because the rule is "embedded never leads canonical," a cheap, *non-fatal* check could live
in the runner: report (don't fail) when an embedded copy contains changes the canonical
repo lacks — the signature of an accidental in-place edit. It's a safety net, not a gate;
the process above is the real mechanism.
