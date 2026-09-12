# GameMaker Runtime Research & Function Hooking Strategy

## 1. GameMaker VM Runner Overview

GameMaker games compiled with the Virtual Machine (VM) target include a runner executable (`DELTARUNE.exe`) that executes GameMaker bytecode instructions stored inside `data.win`.

- **Architecture**: x64 Windows Native Executable
- **Script/Code Storage**: VM Bytecode chunks (`CODE` chunk)
- **Built-in Functions**: C++ exported or internal functions (e.g. `draw_text`, `draw_text_ext`, `draw_set_font`, `font_add`) registered in runner export tables.

---

## 2. Function Hooking Strategy (Phase 1 & Phase 2 POC)

To alter game text and fonts without modifying bytecode on disk, SoulMod Runtime hooks built-in GameMaker functions:

```text
GameMaker Engine Event Loop
          │
          ▼
   [draw_text Calls]
          │
          ▼
   SoulMod Central Hook (YYToolkit / SafetyHook Trampoline)
          │
          ├── 1. Before Callbacks (Inspecting/Modifying Arguments: UTF-8 String Translation)
          ├── 2. Core Execution (Redirected String / Font Pointer)
          └── 3. After Callbacks (Restoring State / Metrics Logging)
```

### Key Target Functions for Korean Patch POC
1. `draw_text` / `draw_text_ext` / `draw_text_transformed`
   - Intercept string argument (Arg 2)
   - Lookup translated Korean string in active `.soulmod` string map
2. `draw_set_font`
   - Intercept font ID argument (Arg 0)
   - Redirect to registered Korean font ID (`fnt_korean`)

---

## 3. Advanced Strategy: VM Code Entry Replacement (Phase 7)

For complex GML logic changes that cannot be handled via function call detours alone:
1. Locate GameMaker `YYGMLScript` / `CCode` structure in runner heap via YYToolkit.
2. Store original bytecode entry point.
3. Allocate dynamic bytecode or dynamic detour function for script execution.
4. Atomically swap `CCode::i_pCode` pointer at safe frame boundary (e.g., BeginStep event).
5. Maintain full rollback capability upon mod unloading.
