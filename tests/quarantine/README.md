# Quarantined tests (skipped by oktest)

Harness skips any `.ok` under this directory. Reasons:

| File | Why |
|------|-----|
| `is.ok` | `is` / `is not` not lexer keywords; parse error. Native `object::is` exists but infix syntax is unfinished. |
| `try_catch.ok` | Segfaults at runtime (exit 139). |
| `empty.ok` | Empty program exits 2 with no stderr; no expects. |
| `fu_mut.ok` | Stub (`// TODO`); no expects. |
| `fu_async.ok` | Parse-only stub; no expects. |
| `fu_export.ok` | Parse-only stub; no expects. |
