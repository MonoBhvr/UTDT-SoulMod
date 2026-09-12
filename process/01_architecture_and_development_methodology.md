# 01. 현재 개발 방식 및 핵심 C++ 런타임 아키텍처 (Architecture & Methodology)

## 1. 프레임워크 전체 구조 및 비전 (Overall Architecture & Project Vision)

UTDT-SoulMod 프로젝트의 최종 비전은 **"임의의 게임메이커(GameMaker 2.x x64) 모드 패키지를 자동으로 분석(Automated Analyzer)하고, 게임 원본 파일(`DELTARUNE.exe` 및 `data.win`)을 절대로 수정하지 않으며, 실시간 C++ 런타임 메모리 수술 및 오퍼레이션 주입(Real-time Runtime Injection)을 통해 완벽한 무결성(Non-invasive Modding)으로 게임을 구동하는 범용 모드 엔진을 수립하는 것"**입니다.

현재 진행 중인 **한글 패치(DTKR)** 실증 테스트는 이 범용 모드 분석/주입 엔진이 필요로 하는 가장 까다로운 요소 기술(64비트 메모리 수술, 가상 테이블 복사, 폰트/텍스처 동적 우회, 선언적 JSON 오퍼레이션 로딩, 텍스트 API 후킹)을 완벽하게 증명하기 위한 **최초의 핵심 테스트베드(Primary Testbed PoC)** 역할을 수행합니다.

```
+---------------------------------------------------------------------------------------+
|                               DELTARUNE.exe (x64)                                     |
|  +---------------------------------------------------------------------------------+  |
|  |                      GameMaker 2.x x64 C++ Core Runner                          |  |
|  |  +---------------------+  +---------------------+  +-------------------------+  |  |
|  |  |   g_ppFontAssets    |  |  draw_set_font /    |  |   file_text_open_read   |  |  |
|  |  |   (0x140687BB8)     |  |  draw_text Family   |  |   (File Redirection)    |  |  |
|  |  +----------+----------+  +----------+----------+  +------------+------------+  |  |
|  +-------------|------------------------|--------------------------|---------------+  |
+----------------|------------------------|--------------------------|------------------+
                 | Interception           | Hooking                  | Hooking
+----------------v------------------------v--------------------------v------------------+
|                               SoulModPlugin.dll                                       |
|  +---------------------------------------------------------------------------------+  |
|  | [Core Engine] Generic Automated GameMaker Mod Analyzer & Injection Engine       |  |
|  | 1. Declarative Operation Engine (manifest.json & operations.json Parsing)       |  |
|  | 2. font_add Dynamic FreeType Font Registration & Range (32..55203) Allocation    |  |
|  | 3. 128-Byte Struct & VTable Memory Surgery (CFont -> FTFont Cloning)             |  |
|  | 4. Global JSON Loading & Atomic State Epoch Engine (global.json.load)             |  |
|  | 5. Game Restart Coordinator (game_restart Synchronization)                        |  |
|  | 6. Complete 7-Routine Text API Hooking & Specifier Filter                         |  |
|  | [Testbed App] DTKR Korean Localizer Testbed (6,240 Translation Entries)          |  |
|  +---------------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

## 2. GameMaker 2.x x64 Runtime 폰트 메모리 구조 (Memory Layout)

게임메이커 x64 런타임 내 폰트 구조체(`CFont`)는 다음과 같은 64비트 메모리 레이아웃을 갖습니다.

### 2.1 `CFont` Header & Body Layout (128 Bytes)
- `+0x00`: **VTable Pointer** (C++ 가상 함수 테이블 포인터)
  - `CFont` (비트맵 폰트): `0x1404E8C70`
  - `FTFont` (FreeType 동적 폰트): FreeType GL/Glyph 탐색 메서드가 구현된 전용 VTable 주소
- `+0x08`: **Font Name String Pointer** (`char*`, 예: `"fnt_ja_main"`)
- `+0x10`: **`CFontGlyph**` Pointer Table** (64비트 포인터 배열 주소)
- `+0x18`: **`CTexturePageItem**` Texture Array Pointer** (텍스처 페이지 포인터 배열 주소)
- `+0x20` ~ `+0x28`: **Font Metric Fields** (Ascent, Descent, Height, Size 등)
- `+0x30`: **`first` Character Code** (유니코드 탐색 시작 범위, 기본 `32`)
- `+0x38`: **`last` Character Code** (유니코드 탐색 종료 범위, 기본 `12287` 또는 `127`)

### 2.2 `CFontGlyph` Struct Layout (24 Bytes, 2바이트 정렬)
GameMaker x64의 글리프 데이터는 힙에 24바이트 단위로 개별 할당되며 `CFontGlyph**` 테이블에 포인터가 저장됩니다.

```cpp
#pragma pack(push, 1)
struct GMGlyph24 {
    uint16_t ch;        // 유니코드 문자 코드 (예: 0xC81C = '제')
    uint16_t x;         // 텍스처 페이지 내 X 좌표
    uint16_t y;         // 텍스처 페이지 내 Y 좌표
    uint16_t w;         // 글리프 너비 (Width)
    uint16_t h;         // 글리프 높이 (Height)
    int16_t  shift;     // 다음 문자로의 X 오프셋 (Advance)
    int16_t  offset_x;  // 베이스라인 대비 X 오프셋
    int16_t  offset_y;  // 베이스라인 대비 Y 오프셋
    uint16_t tex_idx;   // 텍스처 페이지 인덱스
    uint16_t pad1;      // 정렬 패딩
    uint32_t pad2;      // 확장 패딩
};
#pragma pack(pop)
```

---

## 3. 메모리 수술 기법 (Memory Surgery Strategy)

`data.win` 파일의 바이너리를 직접 수정하는 경우 파일 무결성(SHA256)이 파괴되므로, 본 프레임워크는 **동적 FreeType 자원 주입 후 128바이트 필드 클로닝(Struct Field Cloning)** 기법을 사용합니다.

1. **Native FreeType 폰트 생성**:
   - `font_add("fnt_ja_main.ttf", 14, false, false, 32, 55203)`을 실행하여 유니코드 범위 `32..55203` (ASCII + 일어 + 완성형 한글 11,172자 포함) 전체를 커버하는 동적 `FTFont` 객체를 런타임 메모리에 생성합니다.
2. **`g_ppFontAssets` 배열 포인터 및 필드 동기화**:
   - 게임 내 전역 폰트 자원 테이블(`0x0000000140687BB8`)에서 바닐라 폰트 객체들의 주소를 탐색합니다.
   - 바닐라 폰트 구조체 메모리에 `VirtualProtect(PAGE_READWRITE)`를 적용한 후, 동적 `FTFont` 객체의 **VTable 포인터(`+0x00`)**, **글리프 테이블 포인터(`+0x10`)**, **텍스처 배열 포인터(`+0x18`)**, **유니코드 범주 범위(`first=32`, `last=55203`)** 등 128바이트 전체 필드를 복사 동기화합니다.
3. **결과**:
   - 게임메이커 엔진이 기존 폰트 ID를 사용하여 문자열을 그릴 때, 런타임 렌더러는 우회된 FreeType 폰트 엔진과 55,203 범위 글리프 테이블을 통해 깨짐이나 상자 현상 없이 원본 한글 텍스트를 출력합니다.

---

## 4. Text API Hooking 전수 확장 (7 Routines Coverage)

DELTARUNE의 텍스트 외곽선 렌더링 스크립트(`scr_84_draw_text_outline`)는 상황에 따라 다양한 텍스트 출력 함수를 호출합니다. 이에 따라 다음 7종의 GameMaker 내장 함수 전체를 Hooking하여 관리합니다.

1. `draw_text`
2. `draw_text_ext`
3. `draw_text_transformed`
4. `draw_text_ext_transformed`
5. `draw_text_colour`
6. `draw_text_ext_colour`
7. `draw_text_ext_transformed_colour`

### 인코딩 및 스펙시파이어 처리
- **MSVC UTF-8 인코딩**: C++20 `u8""` (`const char8_t*`) 리터럴을 명시적으로 도입하여 GameMaker VM `RValue` 문자열 생성 시 ANSI/UTF-8 간 이중 인코딩(Mojibake) 문제를 근본 차단했습니다.
- **포맷 스펙시파이어 필터링**: DELTARUNE GML 스크립트가 반환하는 원시 포맷 문자열(`"f 1 %:"`, `"f1%:"`)을 Hook 레벨에서 억제/제거하여 화면 상의 겹침 잔상을 완전 제거했습니다.

---

## 5. 오퍼레이션 드라이븐 모드 엔진 (Operation-Driven Engine)

현재 Production 코드베이스는 단일 폰트 주입을 넘어 **선언적 오퍼레이션 명세(`operations.json`)**를 엔진 차원에서 파싱하고 실행하는 확장 구조를 갖추고 있습니다.

1. **`file_text_open_read` Hooking**: 게임이 특정 텍스트 파일이나 설정 파일 열기를 시도할 때 `SelectFileRedirect()`를 통해 모드 내 에셋 경로로 동적 리다이렉트.
2. **`global.json.load` 런타임 로더**: `GlobalJsonLoadHost` 및 `GlobalJsonLoadEpoch` 상태 머신을 통하여 게임 실행 중 글로벌 데이터(`global.lang_ko` 등)를 원자적으로 로드 및 검증.
3. **`RestartCoordinator` 동기화**: 폰트 및 글로벌 JSON 자원이 완전 로드된 후 `game_restart` 내장 함수를 호출하여 게임 인스턴스를 무결하게 재시작.

---

## 6. 파일 무결성 및 검증 필드 (File Integrity Verification)

- **`data.win` SHA256**: `82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6` (변형 0%)
- **`DELTARUNE.exe` SHA256**: `0CF40FAEC70583D4E67EBFD6F49BE08D5473595B61A3CBED28AD1A02ED939495` (변형 0%)
- **크래시 덤프 검증**: 테스트 종료 후 `%LOCALAPPDATA%\CrashDumps` 증분 덤프 생성 0건 및 프로세스 정상 종료(`Exit Code = 0`, `CloseMainWindow() = True`).
