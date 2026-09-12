# SoulMod Package Format (.soulmod)

A `.soulmod` is a ZIP containing `manifest.json`, its declared `operations.json`, and referenced assets. Format version 1 is the only supported package and operation version. Unknown JSON properties, enum values, format versions, and operation versions are invalid.

## Manifest

`manifest.json` conforms to [`shared/schema/manifest.schema.json`](../shared/schema/manifest.schema.json). The runtime-compatible `targetFile` remains required. `targets` is the resolver-facing compatibility list: every target has a stable ID and target file, with an optional SHA-256 fingerprint when a specific game build is required.

```json
{
  "formatVersion": 1,
  "id": "dtkr.runtime",
  "name": "DELTARUNE Korean Patch",
  "version": "0.1.0",
  "targetFile": "data.win",
  "targetDataSha256": "64 hexadecimal characters when pinned",
  "targets": [
    {
      "id": "deltarune.chapter1",
      "targetFile": "data.win",
      "targetDataSha256": "64 hexadecimal characters when pinned"
    }
  ],
  "dependencies": [],
  "loadBefore": [],
  "loadAfter": [],
  "conflictPolicy": "stop",
  "operationsFile": "operations.json"
}
```

`dependencies`, `loadBefore`, and `loadAfter` are unique package IDs. `conflictPolicy` is `stop`, `warn`, or `overwrite`. All package paths use forward slashes, are relative, and cannot contain `.` or `..` segments.

## Operations

`operations.json` conforms to [`shared/schema/operations.schema.json`](../shared/schema/operations.schema.json). Every operation has a unique `id`, `operationVersion: 1`, the same `modId` as the manifest, a typed target, and nonempty unique `reads` and `writes` resource sets. Resource keys use `kind:name` and are the contract consumed by future deterministic resolution.

```json
[
  {
    "id": "dtkr.writer.font",
    "operationVersion": 1,
    "modId": "dtkr.runtime",
    "target": { "type": "code", "name": "gml_Object_obj_writer_Draw_0" },
    "operation": "call.argument.replace",
    "selector": { "callee": "draw_set_font", "occurrence": 0 },
    "precondition": { "expectedValue": "fnt_main" },
    "value": "fnt_korean",
    "reads": ["code:gml_Object_obj_writer_Draw_0"],
    "writes": ["code:gml_Object_obj_writer_Draw_0"]
  }
]
```

Asset-bearing operations (`global.json.load`, `resource.redirect`, `resource.replace`, `resource.add`, and `font.register`) require `assetPath` and an exact 64-character SHA-256. Packing and ZIP reading verify that the contained asset exists and matches that hash.

`string.replace` requires `precondition.expectedValue` and `value`. `resource.replace` requires `precondition.sha256`. `font.register` requires `selector.size`, `bold`, `italic`, `first`, and `last`. `font.redirect` targets `font`, has a `value`, and has no selector.

### `global.json.load`

`global.json.load` targets an unqualified global and requires a selector with `applyAt: "first.draw_set_font"`. Optional `mirrorGlobals` must be unique unqualified names distinct from the target. Optional `when` contains exactly a global name and a string `equals` value. The runtime verifies the JSON asset's exact bytes before atomically assigning the decoded value to the target and mirrors.

```json
{
  "id": "example.global-json",
  "operationVersion": 1,
  "modId": "example.mod",
  "target": { "type": "global", "name": "primary_global" },
  "operation": "global.json.load",
  "selector": { "applyAt": "first.draw_set_font", "mirrorGlobals": ["secondary_global"] },
  "assetPath": "assets/data.json",
  "assetSha256": "64 hexadecimal characters",
  "reads": ["asset:assets/data.json"],
  "writes": ["global:primary_global", "global:secondary_global"]
}
```

## Version 1 Migration

Older source examples that only had `games`, `stringsFile`, or a bare `targetFile` are not v1 package manifests. Package fixtures now declare `targets`; existing `targetFile` and `targetDataSha256` fields are retained for the strict runtime parser. Existing operations add `operationVersion`, `reads`, and `writes`; operation meaning and asset bytes are unchanged.
