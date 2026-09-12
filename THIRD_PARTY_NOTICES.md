# Third Party Notices & License Documentation (`THIRD_PARTY_NOTICES.md`)

---

## 1. Overview & Usage Categorization

This document records third-party frameworks, libraries, and tools utilized during the research and implementation of the UTDT-SoulMod runtime engine.

### Usage Mode Categorization:
- **Aurie Framework**: **[Dynamic Dependency / Native Loader Subsystem]**
  - Compiled as standalone loader / runtime DLLs (`AurieCore.dll`, `AuriePatcher.exe`).
- **YYToolkit (YYTK)**: **[Dynamic Dependency / Internal Modding Framework Plugin]**
  - Compiled as standalone Aurie plugin DLL (`YYToolkit.dll`).
- **SoulMod Runtime Subsystem**: **[Custom Aurie Plugin / Native Module]**
  - Built against Aurie and YYToolkit header interfaces (`SoulModPlugin.dll`).

---

## 2. Third-Party Components & Licenses

### A. Aurie Framework
- **Repository**: [AurieFramework/Aurie](https://github.com/AurieFramework/Aurie)
- **Branch**: `master`
- **Git Commit**: `5c4839ea19d47c6afb7d459358f53643b2774e3a`
- **License**: GNU General Public License v3.0 (GPLv3)
- **Primary Authors**: AurieFramework Developers
- **Usage**: Native module loading engine, memory management (`MmCreateHook`), and process injection/patching infrastructure.

### B. YYToolkit (YYTK)
- **Repository**: [AurieFramework/YYToolkit](https://github.com/AurieFramework/YYToolkit)
- **Branch**: `stable`
- **Git Commit**: `86c133dc7f4e87991dd2cb7f2eac056dc5f13124`
- **License**: GNU General Public License v3.0 (GPLv3)
- **Primary Authors**: AurieFramework / YYTK Developers
- **Target Compatibility Rationale**:
  - Selected `stable` branch as it officially targets GameMaker Studio 2.x VM (x64, Bytecode Version 17) and exposes the official `YYTK_Main` interface pointer, `GetNamedRoutinePointer`, and `CallBuiltin` routines required by DELTARUNE.

### C. UndertaleModLib / UndertaleModTool
- **Repository**: [UndertaleModTool/UndertaleModTool](https://github.com/UndertaleModTool/UndertaleModTool)
- **Version**: `v0.8.4.1`
- **License**: GNU General Public License v3.0 (GPLv3)
- **Usage**: Offline data file (`data.win`) analysis, chunk extraction (`FONT`, `TXTR`, `TPAG`, `CODE`), and GML bytecode decompilation.

---

## 3. License & Redistribution Terms

1. **Research & Development Phase**: All 3rd-party headers and build scripts are used in accordance with GPLv3 terms for dynamic module binding.
2. **Production Deployment Strategy**: Final deployment architecture will separate game content redirection from loader binaries, ensuring 0-byte modifications to game data assets (`data.win`, `.ogg`, `lang_ja.json`).

