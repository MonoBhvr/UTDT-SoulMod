# Conflict Policy & Read/Write Resource Sets

## 1. Conflict Classification

When combining multiple active `.soulmod` packages, operations are grouped into four categories:

### A. Duplicate Operations
- **Definition**: Two separate mods perform the exact same modification on the exact same resource to the exact same value.
- **Handling**: Retain one operation, log duplicate notice in `SoulMod.log`, do not error out.

### B. Compatible Operations
- **Definition**: Two operations modify the same code or resource, but affect different independent selectors (e.g. Mod A modifies `draw_set_font` while Mod B modifies `draw_set_color` within `gml_Object_obj_writer_Draw_0`).
- **Handling**: Merge operations sequentially.

### C. Ordered Operations
- **Definition**: Both operations can coexist, but their evaluation order dictates the final output.
- **Handling**: Sort using topological load order rules (`loadBefore`, `loadAfter`, `priority`, stable mod ID fallback).

### D. Direct Conflict Operations
- **Definition**: Two operations target the same resource selector with conflicting values (e.g., Mod A changes font to `fnt_korean`, Mod B changes font to `fnt_hd`).
- **Handling**: Abort execution prior to runtime commit, output detailed `conflict_report.json`.

---

## 2. Read/Write Resource Paths

Conflict detection checks intersection between write and read sets:

```text
A.write ∩ B.write != empty   ==> Direct Conflict Candidate
A.write ∩ B.read  != empty   ==> Ordering Dependency Candidate
```

### Resource Path Schemes
- `code:<name>`
- `code:<name>/statement:<hash>`
- `code:<name>/call:<function>/arg:<index>`
- `sprite:<name>`
- `sprite:<name>/frame:<index>`
- `font:<name>`
- `audio:<name>`
- `string:<stable-key>`
- `hook:<function-name>/before`
- `hook:<function-name>/after`
- `hook:<function-name>/replace`
