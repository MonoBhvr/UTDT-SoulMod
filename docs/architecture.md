# UTDT SoulMod Architectural Overview

## 1. High-Level Architecture

UTDT SoulMod consists of two main decoupled components:

```text
+-------------------------------------------------------------+
|                     SoulMod Converter                       |
|  xdelta / .csx / Patched data.win                           |
|       │                                                     |
|       ▼                                                     |
|  UndertaleModLib Resource Diff Engine                       |
|       │                                                     |
|       ▼                                                     |
|  .soulmod Package Builder (Zip Container + JSON Operations) |
+------------------------------┬------------------------------+
                               │
                               │ (.soulmod Package)
                               ▼
+-------------------------------------------------------------+
|                      SoulMod Runtime                        |
|  DELTARUNE.exe (GameMaker VM Engine)                        |
|       │                                                     |
|       ├── Aurie Framework (Module Injector)                 |
|       ├── YYToolkit (GameMaker Structure Adapter)           |
|       │                                                     |
|       ▼                                                     |
|  SoulMod Composer & Operations Resolution                   |
|       ├── Dependency & Topological Load Sorting             |
|       ├── Read/Write Conflict Resolution                    |
|       └── Atomic Applied Memory Hooks & Virtual Assets      |
+-------------------------------------------------------------+
```

---

## 2. SoulMod Runtime Lifecycle

The runtime operates in a strict transactional pipeline:

1. **Initialization Phase (`Prepare`)**:
   - Aurie loads `SoulMod.Runtime.dll`.
   - Initialize internal logger (`SoulMod.log`).
   - Query GameMaker Runner via YYToolkit to verify VM architecture (x64).
   - Read active mod packages from `mods/` directory.

2. **Composition & Validation Phase (`Resolve` & `Validate`)**:
   - Parse `manifest.json` for each mod.
   - Resolve dependency graph (check `loadBefore`, `loadAfter`, version constraints).
   - Compute operation read/write sets.
   - Analyze duplicate operations (deduplicate) and conflicting operations (report & abort).

3. **Allocation Phase (`Allocate`)**:
   - Load virtual font glyphs / TTF files into memory.
   - Load external PNG textures into runtime surfaces.
   - Prepare dynamic detour trampolines for strings and GML code functions.

4. **Commit Phase (`Commit`)**:
   - Atomically swap GameMaker function pointers / string pointers / font ID maps.
   - If any validation or allocation step fails, abort and trigger complete **Rollback**.

---

## 3. Guarantees & Constraints

- **Zero-Modification Guarantee**: `data.win` on disk is read-only.
- **No Temporary win files**: No modified `data_temp.win` is created during runtime execution.
- **Pure VM Focus**: Initial engine scope focuses on GameMaker VM (bytecode runner).
