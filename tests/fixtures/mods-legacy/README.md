# Legacy Mod Fixtures (pre-v1 contract)

These directories are the original Phase 1 test packages moved out of `mods/`
so that `mods/` contains only v1-schema packages. They are preserved verbatim
(reference fixtures, not runnable packages) and are intentionally **not** loaded
by the runtime or converter. `tools/package_contract_test.py` scans every
immediate package directory in `mods/` and rejects their legacy-only fields.

## Contents

- `test/` — Phase 1 string/font test package (`id: "test.mod"`), including its
  font directories (`test/fonts/`), `strings.json`, `lang_ko.json`,
  `ja_to_ko_map.json`, `font.ttf`, and the historical `dtkr_diff/` diff inputs.
- `override_test/` — minimal high-priority override package (`id: "override.mod"`)
  with its own `strings.json`.

## Legacy manifest contract (rejected by the v1 schema)

Both manifests use fields that the closed v1 schema
(`shared/schema/manifest.schema.json`, `additionalProperties: false`) no longer
accepts:

1. **`stringsFile`** — a relative path (here always `"strings.json"`) pointing at
   a flat `{"source text": "replaced text"}` JSON map. The runtime loaded this
   map and substituted each key verbatim in game strings. The v1 contract
   replaced string substitution with declarative `operations.json` entries
   (`global.json.load` / `font.register` / `font.redirect`) validated against
   `shared/schema/operations.schema.json`.
2. **`priority`** — used by `override_test/manifest.json` with value `200` to
   win string-map merge conflicts against lower-priority packages (default
   priority 100). Higher `priority` overrides lower on identical keys; that is
   why `override_test/strings.json` only needs the two keys it wants to
   override. The v1 contract replaced numeric priority with explicit
   `loadBefore`/`loadAfter` ordering and duplicate/conflict detection.
3. `test/manifest.json` also carries the pre-`targets` shape: a bare
   `"targetFile": "DELTARUNE.exe"` with no `targets`, `targetDataSha256`, or
   `operationsFile`.

## Large binaries

`test/dtkr_diff/data_dtkr_ch1.win` (17 MiB) and `test/font.ttf` (13 MiB) are
historical diff/font inputs kept for provenance; see the repo hygiene report
note for provisioning guidance (Git LFS or external hosting) before adding more.
