# Milestone 1: DELTARUNE Korean Patch POC Plan

## 1. Goal

Demonstrate non-destructive runtime localization for DELTARUNE using SoulMod Runtime and a converted `.soulmod` package derived from the official DELTARUNE Korean xdelta patch.

---

## 2. Execution Pipeline

```text
1. Original DELTARUNE data.win (SHA-256 Verified)
                    │
                    ▼
2. SoulMod Converter (Diff against patched xdelta temp binary)
                    │
                    ▼
3. Generates dtkr.soulmod (Contains string maps, font glyphs, operations.json)
                    │
                    ▼
4. DELTARUNE.exe launched with SoulMod Runtime DLL (Aurie/YYToolkit)
                    │
                    ▼
5. Runtime intercepts draw_text / draw_set_font
                    │
                    ▼
6. Korean 대사, 메뉴 UI, 폰트 정상 출력 (data.win untouched)
```

---

## 3. Success Verification Criteria

- [ ] `data.win` SHA-256 hash remains unchanged before and after game execution.
- [ ] Korean text renders correctly in main menu and in-game dialogues.
- [ ] Custom Korean font renders without corrupted glyphs or missing characters.
- [ ] Korean UI sprites render correctly.
- [ ] Disabling mod cleanly reverts game to original English text on next launch.
- [ ] Game exits cleanly without memory leaks or crashes.
