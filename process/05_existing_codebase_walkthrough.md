# 05. 기존 코드베이스 세부 분석 및 구현 설명 (Codebase Walkthrough)

## 1. 코드베이스 구조 개요 (Codebase Overview)

UTDT-SoulMod의 핵심 C++ 플러그인 구현체는 `scratch/AurieFramework/SoulModPlugin/source/ModuleMain.cpp` 및 조력 헤더 파일들(`RuntimeOperations.hpp`, `GlobalJsonLoad.hpp`, `RestartCoordinator.hpp`)로 구성되며, Aurie 모듈 시스템 및 YYToolkit API를 활용하여 GameMaker 2.x x64 런타임 엔진을 제어합니다.

```
d:\UTDT-SoulMod\scratch\AurieFramework\SoulModPlugin\
├── include/
│   ├── RuntimeOperations.hpp     # 선언적 오퍼레이션(manifest/operations) 데이터 구조체 및 파일 리다이렉션 선택기
│   ├── GlobalJsonLoad.hpp        # global.json.load 상태 머신 및 에포크(Epoch) 수명주기 관리자
│   ├── RestartCoordinator.hpp    # game_restart 호출 동기화 및 폰트/JSON 완료 시그널 코디네이터
│   └── (YYToolkit 인터페이스 헤더)
├── source/
│   └── ModuleMain.cpp            # C++ 플러그인 엔트리, 루틴 후킹 및 런타임 제어 구현체
└── SoulModPlugin.vcxproj         # MSVC 2022 C++20 프로젝트 파일
```

---

## 2. `ModuleMain.cpp` Production 오퍼레이션 엔진 분석 (Operation Engine Analysis)

### 2.1 패키지 발견 및 오퍼레이션 데이터 로드 (`ModuleInitialize`)

```cpp
EXPORTED AurieStatus ModuleInitialize(IN AurieModule* module, IN const fs::path& modulePath)
{
    LogPlugin("==================================================");
    LogPlugin("SoulModPlugin operation-driven runtime proof v0.1.0");
    LogPlugin("==================================================");

    std::string error;
    // 1. 모듈 디렉터리 내 manifest.json / operations.json 발견 및 구조체 파싱
    const auto packageRoot = soulmod::DiscoverPackageRoot(modulePath, error);
    if (!packageRoot || !soulmod::LoadPackageOperations(*packageRoot, g_Operations, error))
    {
        LogPlugin("[OP_RUNTIME] package load failed reason=%s", error.c_str());
        return AURIE_MODULE_INITIALIZATION_FAILED;
    }
    
    // 2. global.json.load 에포크 객체 초기화
    g_GlobalJsonLoadEpochs.clear();
    for (size_t index = 0; index < g_Operations.globalJsonLoads.size(); ++index)
        g_GlobalJsonLoadEpochs.push_back(std::make_unique<soulmod::GlobalJsonLoadEpoch>(g_Operations.globalJsonLoads[index]));

    // 3. YYToolkit 메인 인터페이스 획득
    const AurieStatus interfaceStatus = ObGetInterface("YYTK_Main", reinterpret_cast<AurieInterfaceBase*&>(g_ModuleInterface));
    if (!AurieSuccess(interfaceStatus) || !g_ModuleInterface) return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

    // 4. 폰트 바인딩 및 런타임 루틴 Hooking 설치
    if (!ResolveFontRedirects()) return AURIE_MODULE_INITIALIZATION_FAILED;

    if (!g_Operations.fileRedirects.empty())
        InstallHook(module, "file_text_open_read", reinterpret_cast<PVOID>(HK_file_text_open_read), &g_FileTextOpenReadOriginal);
    InstallHook(module, "draw_set_font", reinterpret_cast<PVOID>(HK_draw_set_font), &g_DrawSetFontOriginal);

    return AURIE_SUCCESS;
}
```
- **역할**: DLL이 게임에 인젝션될 때 모드 디렉터리의 `.soulmod` 매니페스트/오퍼레이션 파일을 로드하고, `file_text_open_read` 및 `draw_set_font` C++ 루틴 포인터를 후킹하여 동적 자원 리다이렉트 엔진을 준비시킵니다.

---

### 2.2 동적 파일 리다이렉션 후킹 (`HK_file_text_open_read`)

```cpp
void HK_file_text_open_read(RValue* result, CInstance* self, CInstance* Other, int argumentCount, RValue* arguments)
{
    if (g_InFileHook) { CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, arguments); return; }

    ReentrancyScope scope(g_InFileHook); // 재귀 호출 방지 스코프
    if (argumentCount >= 1 && arguments)
    {
        const std::string requestedPath = arguments[0].ToString();
        const auto redirect = soulmod::SelectFileRedirect(g_Operations, requestedPath);
        if (redirect)
        {
            const std::string replacementPath = redirect->string();
            LogPlugin("[OP_RUNTIME] file redirect request=%s asset=%s", requestedPath.c_str(), replacementPath.c_str());
            RValue replacement(replacementPath);
            std::vector<RValue> redirectedArguments(arguments, arguments + argumentCount);
            redirectedArguments[0] = replacement; // 1번째 인자(파일 경로) 교체
            CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, redirectedArguments.data());
            return;
        }
    }
    CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, arguments);
}
```
- **역할**: 게임이 GML `file_text_open_read` 함수로 언어 파일이나 에셋 파일 열기를 시도할 때 경로를 가로채어, 오퍼레이션 명세서에 정의된 모드 에셋 물리 경로로 즉시 리다이렉션합니다.

---

### 2.3 폰트 등록 및 동적 주입 (`EnsureRegisteredFont`, `HK_draw_set_font`)

```cpp
bool EnsureRegisteredFont(const std::string& fontName, double& fontId)
{
    const auto existing = g_RegisteredFontIds.find(fontName);
    if (existing != g_RegisteredFontIds.end()) { fontId = existing->second; return true; }

    const auto registration = g_Operations.registeredFonts.find(fontName);
    if (registration == g_Operations.registeredFonts.end()) return false;

    const soulmod::FontRegistration& font = registration->second;
    // 1. GameMaker font_add 내장 루틴 호출하여 동적 FreeType CFont 생성
    const RValue fontResult = g_ModuleInterface->CallBuiltin("font_add", {
        RValue(font.assetPath.string()), RValue(static_cast<double>(font.size)), RValue(font.bold), RValue(font.italic),
        RValue(static_cast<double>(font.first)), RValue(static_cast<double>(font.last))
    });
    const double candidateId = fontResult.ToDouble();
    if (candidateId < 0) return false;

    // 2. font_exists 검증 후 등록 맵에 추가
    const RValue existsResult = g_ModuleInterface->CallBuiltin("font_exists", { RValue(candidateId) });
    if (!existsResult.ToBoolean()) return false;

    g_RegisteredFontIds.emplace(font.name, candidateId);
    fontId = candidateId;
    return true;
}
```
- **역할**: `draw_set_font`가 실행될 때 바닐라 폰트 자원에 연결된 폰트명을 탐색하고, 필요 시 `font_add`를 통해 동적 FreeType TTF 폰트를 즉각 생성한 후 생성된 Font ID로 폰트 설정을 바인딩합니다.

---

### 2.4 재시작 코디네이터 및 상태 동기화 (`RequestRestartIfReady`)

```cpp
void RequestRestartIfReady()
{
    std::vector<soulmod::GlobalJsonLoadState> globalJsonLoadStates;
    for (const auto& epoch : g_GlobalJsonLoadEpochs)
        globalJsonLoadStates.push_back(epoch->State());

    // 폰트 및 모든 global.json.load 오퍼레이션이 Applied 상태에 도달했는지 확인
    if (!g_RestartCoordinator.TryClaimRestart(soulmod::AreSelectedGlobalJsonLoadsApplied(globalJsonLoadStates)))
        return;

    LogPlugin("[OP_RUNTIME] restart requested builtin=game_restart reason=global-json-and-font-ready");
    g_ModuleInterface->CallBuiltin("game_restart", {}); // game_restart 호출
}
```
- **역할**: 모드 폰트와 글로벌 JSON 데이터가 메모리에 모두 정상 로드되었을 때 `game_restart()`를 호출하여 게임 전체를 안정된 런타임 상태로 동기화 재시작합니다.

---

## 3. 메모리 수술 C++ 구조체 기술 분석 (`CFont` vs `FTFont` 128바이트 수술)

R&D 단계에서 규명되어 폰트 우회에 적용되는 64비트 C++ 구조체 및 글리프 탐색 함수 코드 구조입니다.

```cpp
// GameMaker 2.x x64 24바이트 글리프 구조체 규격
#pragma pack(push, 1)
struct GMGlyph24 {
    uint16_t ch;        // 유니코드 16비트 문자 코드
    uint16_t x; uint16_t y; uint16_t w; uint16_t h;
    int16_t  shift; int16_t offset_x; int16_t offset_y;
    uint16_t tex_idx; uint16_t pad1; uint32_t pad2;
};
#pragma pack(pop)

// CFont -> FTFont 128바이트 포인터 복사 수술 로직
uintptr_t* pKorFields = reinterpret_cast<uintptr_t*>(pKorCFont); // FTFont (font_add 생성)
uintptr_t* pFields = reinterpret_cast<uintptr_t*>(pCFont);       // CFont (data.win 바닐라 폰트)

DWORD oldFontProt = 0;
VirtualProtect((void*)pCFont, 128, PAGE_READWRITE, &oldFontProt);

for (int i = 0; i < 16; i++) {
    pFields[i] = pKorFields[i]; // +0x00 VTable, +0x10 pGlyphs, +0x18 pTexArray 복사
}

VirtualProtect((void*)pCFont, 128, oldFontProt, &oldFontProt);
```

---

## 4. 빌드 파이프라인 및 PowerShell 배포 자동화

MSBuild 2022와 C++20 컴파일러를 사용하여 플러그인을 빌드하고 테스트 게임 경로로 자동 복사하는 배포 파이프라인입니다.

```powershell
# 1. ModuleMain.cpp UTF-8 BOM 인코딩 보장
$path = "d:\UTDT-SoulMod\scratch\AurieFramework\SoulModPlugin\source\ModuleMain.cpp"
$content = Get-Content $path -Raw -Encoding UTF8
[System.IO.File]::WriteAllText($path, $content, [System.Text.Encoding]::UTF8)

# 2. MSBuild 2022 Release x64 빌드
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    "d:\UTDT-SoulMod\scratch\AurieFramework\SoulModPlugin\SoulModPlugin.vcxproj" `
    /t:Clean,Build /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="d:\UTDT-SoulMod\scratch\AurieFramework\YYToolkit\\"

# 3. DLL 결과물을 델타룬 모드 디렉터리로 복사
Copy-Item "d:\UTDT-SoulMod\scratch\AurieFramework\YYToolkit\x64\Release\SoulModPlugin.dll" `
          "d:\UTDT-SoulMod\scratch\test_game\mods\aurie\SoulModPlugin.dll" -Force
```
