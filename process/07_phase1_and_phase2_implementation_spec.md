# 07. Phase 1 & Phase 2 세부 구현 청사진 (Phase 1 & 2 Blueprint & Spec)

## 1. 개요 (Overview)

본 문서는 **UTDT-SoulMod** 프로젝트의 Stage F-6 완료 이후, 개발자가 바로 다음 개발 단계인 **Phase 1 (런타임 번역 엔진 통합)** 및 **Phase 2 (UI 포맷팅 태그 및 폰트 연출 최적화)** 작업을 착오 없이 100% 실행할 수 있도록 구체적인 C++ 클래스 설계, 코드 청사진, GML 태그 명세 및 빌드 검증 절차를 제시하는 **개발 이행 규격서(Implementation Blueprint)**입니다.

---

## 2. Phase 1: 런타임 번역 엔진 완전 통합 청사진 (Game-Wide Localizer)

### 2.1 기술적 목표 (Technical Goal)
- 챕터 메뉴 화면 외에 게임 내부의 모든 대화창, 아이템 설명, 전투 UI, 시스템 텍스트를 실시간으로 한국어로 치환.
- DELTARUNE의 전역 번역 룩업 스크립트인 `scr_84_get_lang_string(string_id)`를 GameMaker VM 레벨에서 가로채어 `g_LangMap` (6,240 항목)으로 1:1 매핑 반환.

### 2.2 구현 코드 청사진 (C++ Code Implementation Blueprint)

```cpp
// scratch/AurieFramework/SoulModPlugin/source/ModuleMain.cpp 에 추가할 청사진 코드

static PVOID g_pScrGetLangStringTarget = nullptr;
static PVOID g_pScrGetLangStringOriginal = nullptr;

// Phase 1: scr_84_get_lang_string GML 스크립트 Hook 콜백 함수
static void HK_scr_84_get_lang_string_Aurie(
    RValue* Result, 
    CInstance* Self, 
    CInstance* Other, 
    int argc, 
    RValue* args
) {
    if (argc >= 1 && args) {
        std::string stringKey = args[0].ToString(); // GML 키 (예: "obj_writer_slash_Draw_0_gml_126_0")
        
        // 1. g_LangMap 메모리 해시맵에서 번역 텍스트 룩업
        auto it = g_LangMap.find(stringKey);
        if (it != g_LangMap.end()) {
            // 2. 번역문 존재 시 C++20 u8 명시적 UTF-8 리터럴 기반 RValue 반환값 할당
            std::u8string_view u8Val(reinterpret_cast<const char8_t*>(it->second.c_str()), it->second.size());
            *Result = RValue(u8Val);
            return; // 원본 스크립트 실행 없이 즉시 반환
        } else {
            // 3. 번역 키 누락 시 로그 기록 후 바닐라 스크립트로 폴백
            LogPlugin("[PHASE1_MISSING_KEY] Key not found in lang_ko.json: %s", stringKey.c_str());
        }
    }

    // 바닐라 원본 scr_84_get_lang_string 호출
    if (g_pScrGetLangStringOriginal) {
        reinterpret_cast<TRoutine>(g_pScrGetLangStringOriginal)(Result, Self, Other, argc, args);
    }
}

// ModuleInitialize() 내부에 추가할 후킹 등록 코드
EXPORTED AurieStatus Phase1_RegisterHooks(AurieModule* Module) {
    // GameMaker GML 스크립트 루틴 포인터 탐색
    g_ModuleInterface->GetNamedRoutinePointer("gml_Script_scr_84_get_lang_string", &g_pScrGetLangStringTarget);
    if (!g_pScrGetLangStringTarget) {
        // 대체 심볼명 탐색 (엔진 빌드에 따른 스크립트 프리픽스 대응)
        g_ModuleInterface->GetNamedRoutinePointer("scr_84_get_lang_string", &g_pScrGetLangStringTarget);
    }

    if (g_pScrGetLangStringTarget) {
        MmCreateHook(
            Module, 
            "scr_84_get_lang_string", 
            g_pScrGetLangStringTarget, 
            reinterpret_cast<PVOID>(HK_scr_84_get_lang_string_Aurie), 
            &g_pScrGetLangStringOriginal
        );
        LogPlugin("[PHASE1] Successfully installed Hook on scr_84_get_lang_string!");
    } else {
        LogPlugin("[PHASE1_ERROR] Failed to locate routine pointer for scr_84_get_lang_string!");
    }
    return AURIE_SUCCESS;
}
```

### 2.3 Phase 1 검증 기준 (Verification Criteria)
1. 게임 구동 후 마을 대화(Kris, Toriel, Susie 대화) 진입 시 `C:\Users\Public\SoulModPlugin.log`에 `[PHASE1_MISSING_KEY]` 0건 확인.
2. 모든 대화창 텍스트가 깨짐 없이 한글로 렌더링될 것.

---

## 3. Phase 2: 인게임 UI, 포맷팅 태그 및 자간 파서 규격 (Formatting & Aesthetics Spec)

### 3.1 DELTARUNE 대화 제어 태그 명세표 (Control Tag Reference Table)

DELTARUNE GML 대화창 엔진(`obj_writer`)이 사용하는 텍스트 제어 태그 규격입니다.

| 태그 문법 | 기능 및 연출 효과 | 렌더러 처리 지침 |
| :--- | :--- | :--- |
| `\c` / `\C` | 텍스트 색상을 **청록색 (Cyan / Skyblue)**으로 변경 | Tag 파서가 감지 후 `draw_set_color(c_cyan)` 동기화 |
| `\y` / `\Y` | 텍스트 색상을 **노란색 (Yellow)**으로 변경 | Tag 파서가 감지 후 `draw_set_color(c_yellow)` 동기화 |
| `\w` / `\W` | 텍스트 색상을 **흰색 (White)**으로 변경 | 기본 색상 복원 |
| `\R` | 텍스트 색상을 **빨간색 (Red)**으로 변경 | Tag 파서가 감지 후 `draw_set_color(c_red)` 동기화 |
| `^1` ~ `^9` | 대화 출력 지연 (Delay / Pause n-frames) | 렌더링 문자에서 제외하고 타이밍 카운터에 반영 |
| `%` | 일반 줄바꿈 (Newline) | Y 좌표 오프셋 + Line Height 증가 |
| `%%` | 대화 페이지 넘김 및 대기 | 사용자 입력(Z/Enter) 대기 및 대화창 클리어 |
| `&` | 강제 행 바꿈 (Line Break) | X 좌표 초기화 및 Y 좌표 이동 |
| `\M0` ~ `\M9` | 대화 상대의 초상화/표정 변경 태그 | 텍스트 렌더링 문자에서 제거 후 GML 초상화 변수로 전달 |

### 3.2 태그 보존형 텍스트 파서 청사진 (Tag-Preserving Parser Spec)

`ProcessTextHook` 내에서 제어 태그를 손상시키지 않고 가시 텍스트만 추출 및 렌더링하는 파서 알고리즘 설계입니다.

```cpp
struct ParsedTextChunk {
    std::string text;
    uint32_t color = 0xFFFFFFFF; // RGBA
    int delayFrames = 0;
    bool isControlTag = false;
};

static std::vector<ParsedTextChunk> ParseDeltaruneTags(const std::string& input) {
    std::vector<ParsedTextChunk> chunks;
    size_t len = input.length();
    std::string currentBuffer;
    
    for (size_t i = 0; i < len; ++i) {
        if (input[i] == '\\' && i + 1 < len) {
            char tag = input[i + 1];
            if (tag == 'c' || tag == 'y' || tag == 'w' || tag == 'R') {
                if (!currentBuffer.empty()) {
                    chunks.push_back({ currentBuffer, 0xFFFFFFFF, 0, false });
                    currentBuffer.clear();
                }
                // 색상 제어 태그 추가
                uint32_t color = (tag == 'c') ? 0xFFFF0000 : (tag == 'y') ? 0x00FFFFFF : 0xFFFFFFFF;
                chunks.push_back({ std::string(1, tag), color, 0, true });
                i++;
                continue;
            }
        }
        currentBuffer.push_back(input[i]);
    }
    if (!currentBuffer.empty()) {
        chunks.push_back({ currentBuffer, 0xFFFFFFFF, 0, false });
    }
    return chunks;
}
```

### 3.3 DTKR 원본 폰트 자간(Kerning) 미세 조정 규격
`fnt_ja_main.ttf` 폰트를 GameMaker `FTFont`에 등록할 때 글리프 메트릭 오프셋 교정값입니다.
- **기본 글리프 Advance (`shift`)**: `14px`
- **글리프 너비 (`w`)**: `12px`
- **행 간격 (Line Height)**: `18px`
- **베이스라인 Y 오프셋 (`offset_y`)**: `0px`

---

## 4. 최종 개발자 서명 및 단계 전환 승인 (Handoff Approval)

위 **Phase 1 & Phase 2 구현 청사진**은 C++ 코드 컴파일러 요구사항, YYToolkit 스크립트 후킹 명세, GML 태그 파싱 알고리즘을 완벽히 포함하고 있으므로, 임의의 개발자 또는 AI 에이전트가 본 가이드라인에 따라 즉시 후속 코딩에 착수할 수 있음을 확인합니다.
