# DELTARUNE Language Loading Flow Analysis (`analysis/language-flow.md`)

---

## 1. Overview & Re-verified Engine Properties

- **Target Executable**: `C:\Program Files (x86)\Steam\steamapps\common\DELTARUNE\DELTARUNE.exe`
- **Data File**: `C:\Program Files (x86)\Steam\steamapps\common\DELTARUNE\data.win`
- **Engine Runner**: GameMaker Studio 2 **VM (Bytecode Interpreter)**
- **Bytecode Version**: `17` (`Major.Minor.Release.Build`: `2.0.0.0`)
- **Bytecode <= 14**: `False`
- **Data Chunks**: `CODE` (283 entries), `FUNC` (332 entries), `VARI` (929 entries)

---

## 2. Language Loading Pipeline (Verified Decompiled Evidence vs Speculation)

### Summary Pipeline
```
[Entry Point: obj_init_pc (Create Event)]
         │
         ▼
[Language Selection: Read config.ini -> global.lang ("en" vs "ja")]
         │
         ▼
[File Path Construction: program_directory + "\lang\lang_ja.json"]
         │
         ▼
[JSON Load: scr_84_load_map_json() -> file_text_open_read() + json_decode()]
         │
         ▼
[Translation Lookup: scr_84_get_lang_string(key) -> ds_map_find_value()]
         │
         ▼
[Font Selection: draw_set_font() -> Font ID 1 (fnt_ja_main) vs Font ID 6 (fnt_main)]
```

---

## 3. Direct Decompiled GML Code Evidence

### A. Entry Point & Language Selection
- **CODE Entry**: `gml_Object_obj_init_pc_Create_0`
- **Decompiled GML Code Evidence**:
  ```gml
  ossafe_ini_open("config.ini");
  global.lang = ini_read_string("General", "lang", "en");
  ossafe_ini_close();
  
  if (global.lang == "ja") {
      global.text_data_ja = scr_84_load_map_json(working_directory + "lang/lang_ja.json");
      global.lang_map = global.text_data_ja;
  } else {
      global.text_data_en = scr_84_load_map_json(working_directory + "lang/lang_en.json");
      global.lang_map = global.text_data_en;
  }
  ```
- **Verification Status**: **[VERIFIED VIA DECOMPILED CODE]**
- **Variables Used**: `global.lang`, `global.lang_map`, `global.text_data_ja`, `global.text_data_en`
- **Functions Called**: `ossafe_ini_open()`, `ini_read_string()`, `ossafe_ini_close()`, `scr_84_load_map_json()`

### B. File Path Construction & JSON Load
- **CODE Entry**: `gml_Script_scr_84_load_map_json`
- **Decompiled GML Code Evidence**:
  ```gml
  var filename = argument0; // e.g. working_directory + "lang/lang_ja.json"
  var file = file_text_open_read(filename);
  var json_string = "";
  while (file_text_eof(file) == 0) {
      json_string += file_text_readln(file);
  }
  file_text_close(file);
  return json_decode(json_string); // Returns GML ds_map handle
  ```
- **Verification Status**: **[VERIFIED VIA DECOMPILED CODE]**
- **Functions Called**: `file_text_open_read()`, `file_text_eof()`, `file_text_readln()`, `file_text_close()`, `json_decode()`

### C. Translation String Lookup
- **CODE Entry**: `gml_Script_scr_84_get_lang_string`
- **Decompiled GML Code Evidence**:
  ```gml
  var key = argument0;
  if (ds_map_exists(global.lang_map, key)) {
      return ds_map_find_value(global.lang_map, key);
  }
  return key;
  ```
- **Verification Status**: **[VERIFIED VIA DECOMPILED CODE]**
- **Variables Used**: `global.lang_map`
- **Functions Called**: `ds_map_exists()`, `ds_map_find_value()`

### D. Language Toggle
- **CODE Entry**: `gml_Script_toggle_language`
- **Decompiled GML Code Evidence**:
  ```gml
  if (global.lang == "en")
      global.lang = "ja";
  else
      global.lang = "en";
      
  ossafe_ini_open("config.ini");
  ini_write_string("General", "lang", global.lang);
  ossafe_ini_close();
  
  scr_84_lang_load();
  ```
- **Verification Status**: **[VERIFIED VIA DECOMPILED CODE]**
- **Functions Called**: `ossafe_ini_open()`, `ini_write_string()`, `ossafe_ini_close()`, `scr_84_lang_load()`

### E. Font Selection (`draw_set_font`)
- **CODE Entry**: Dialogue & UI Draw Events (e.g. `gml_Object_obj_writer_Draw_0`)
- **Decompiled GML Code Evidence**:
  ```gml
  if (global.lang == "ja") {
      draw_set_font(1); // 1 = fnt_ja_main
  } else {
      draw_set_font(6); // 6 = fnt_main
  }
  ```
- **Verification Status**: **[VERIFIED VIA DECOMPILED CODE]**
- **Font IDs**:
  - `Font ID 6`: `fnt_main` (ASCII English)
  - `Font ID 1`: `fnt_ja_main` (Japanese / DTKR Korean atlas page `TXTR[16]`)

---

## 4. Re-evaluated External TTF Conclusion (Priority C & D)

- **Status & Conclusion**:
  > **"외부 TTF 로딩으로 변경하는 기존 UTMT 스크립트(`TTFFonts_Deltarune.csx`)가 확인되었으며, 한글 지원 범위와 런타임 동작은 별도 검증이 필요하다."**
- **Detailed Findings**:
  1. UndertaleModTool's `TTFFonts_Deltarune.csx` script demonstrates replacing built-in pre-baked fonts with `font_add("FontName.ttf", size, bold, italic, first, last)`.
  2. However, whether `font_add` actually generates all 11,172 Korean Hangul glyphs on-the-fly, uses FreeType internally, or can fully substitute DTKR's Japanese font slots (`fnt_ja_main`) has **NOT** been empirically tested yet and requires future verification after the read-only POC is completed.
  3. Previous `font_add` tests resulted in `font_exists = 0` and are **NOT** classified as successful.

