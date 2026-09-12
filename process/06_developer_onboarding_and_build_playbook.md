# 06. 신규 개발자 온보딩 및 빌드/디버깅 플레이북 (Developer Onboarding & Playbook)

## 1. 개요 (Overview)

본 문서는 **UTDT-SoulMod** 프로젝트에 새롭게 참여하는 C++ / GameMaker 개발자 또는 AI 에이전트가 사전 지식 없이도 즉시 현 환경을 구동하고, 빌드 및 디버깅을 수행하며, 다음 개발 단계로 차질 없이 진입할 수 있도록 작성된 **실전 단계별 실행 가이드(Step-by-Step Playbook)**입니다.

---

## 2. 필수 개발 환경 설정 (Environment Requirements)

- **OS**: Windows 10 / 11 x64 (Build 19041 이상)
- **컴파일러/IDE**: Microsoft Visual Studio 2022 (MSBuild v17.x, MSVC v143 toolset)
  - 필수 C++ 컴파일러 표준: `/std:c++20` (C++20 표준 ISO 탭 지원 필수)
- **스크립팅 Shell**: PowerShell 5.1 또는 PowerShell 7+
- **대상 게임 디렉터리**: `d:\UTDT-SoulMod\scratch\test_game\`
  - `DELTARUNE.exe` (x64 Native GM Core Runner)
  - `data.win` (GML 자원 아카이브)

---

## 3. 전체 프로젝트 경로 지도 (Project Directory Sitemap)

```
d:\UTDT-SoulMod\
├── process/                                         # [본 문서 지식 베이스 경로]
│   ├── README.md                                    # 종합 개발 마스터 목차
│   ├── 01_architecture_and_development_methodology.md
│   ├── 02_history_and_discussion_log.md
│   ├── 03_future_roadmap_and_next_steps.md
│   ├── 04_mod_packaging_and_application_guide.md
│   ├── 05_existing_codebase_walkthrough.md
│   ├── 06_developer_onboarding_and_build_playbook.md # [본 온보딩 플레이북]
│   └── 07_phase1_and_phase2_implementation_spec.md   # [Phase 1 & 2 구현 청사진]
│
├── scratch/
│   ├── AurieFramework/
│   │   ├── SoulModPlugin/
│   │   │   ├── include/                             # 플러그인 C++ 헤더 파일
│   │   │   │   ├── RuntimeOperations.hpp
│   │   │   │   ├── GlobalJsonLoad.hpp
│   │   │   │   └── RestartCoordinator.hpp
│   │   │   ├── source/
│   │   │   │   └── ModuleMain.cpp                   # C++ 핵심 구현 파일
│   │   │   └── SoulModPlugin.vcxproj                # MSBuild C++ 프로젝트
│   │   └── YYToolkit/                              # YYTK SDK 및 빌드 출력 디렉터리
│   └── test_game/                                   # 테스트 구동 게임 디렉터리
│       ├── DELTARUNE.exe
│       ├── data.win
│       └── mods/
│           ├── aurie/
│           │   └── SoulModPlugin.dll                # 인젝션 대상 최종 C++ DLL
│           └── test/
│               ├── lang_ko.json                     # 번역 텍스트 데이터 (6,240 항목)
│               └── fonts/fnt_ja_main_dtkr/fnt_ja_main.ttf # 한글 TTF 폰트
```

---

## 4. 빌드, 배치 및 테스트 실행 워크플로우 (Build & Test Workflow)

### Step 1: C++ 플러그인 MSBuild 파이프라인
PowerShell 터미널에서 다음 명령어를 실행하여 MSVC 컴파일 및 배치작업을 수행합니다.

```powershell
# 1. 소스코드 UTF-8 BOM 인코딩 보장
$path = "d:\UTDT-SoulMod\scratch\AurieFramework\SoulModPlugin\source\ModuleMain.cpp"
$content = Get-Content $path -Raw -Encoding UTF8
[System.IO.File]::WriteAllText($path, $content, [System.Text.Encoding]::UTF8)

# 2. MSBuild 2022 빌드 실행
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    "d:\UTDT-SoulMod\scratch\AurieFramework\SoulModPlugin\SoulModPlugin.vcxproj" `
    /t:Clean,Build /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="d:\UTDT-SoulMod\scratch\AurieFramework\YYToolkit\\"

# 3. DLL 테스트 게임 디렉터리로 복사
Copy-Item "d:\UTDT-SoulMod\scratch\AurieFramework\YYToolkit\x64\Release\SoulModPlugin.dll" `
          "d:\UTDT-SoulMod\scratch\test_game\mods\aurie\SoulModPlugin.dll" -Force
```

### Step 2: 테스트 게임 프로세스 구동 및 로그 검증
게임 구동 후 6초간 자동 대기 후 윈도우 스크린샷 캡처 및 로그를 검사합니다.

```powershell
# 1. 기존 로그 초기화
Remove-Item -Force "C:\Users\Public\SoulModPlugin.log" -ErrorAction SilentlyContinue

# 2. 게임 프로세스 실행
$p = Start-Process "d:\UTDT-SoulMod\scratch\test_game\DELTARUNE.exe" -WorkingDirectory "d:\UTDT-SoulMod\scratch\test_game" -PassThru
Start-Sleep -Seconds 6

# 3. 정상 종료 검증 (CloseMainWindow)
$closeRes = $p.CloseMainWindow()
$exitedBeforeTimeout = $p.WaitForExit(10000)

Write-Host "CloseMainWindow Status: $closeRes"
Write-Host "Exited Cleanly:        $exitedBeforeTimeout"
Write-Host "Exit Code:             $($p.ExitCode)"

# 4. 로그 확인
Get-Content "C:\Users\Public\SoulModPlugin.log"
```

---

## 5. 트러블슈팅 및 예외 대응 매뉴얼 (Troubleshooting Playbook)

| 증상 / 장애 현상 | 원인 (Root Cause) | 조치 및 해결 방법 (Solution) |
| :--- | :--- | :--- |
| **Access Violation (`0xC0000005`) 발생** | `CFont` 구조체의 `+0x10` 필드를 1D 배열로 오인하여 직렬 메모리 읽기 시도 | `+0x10`은 `CFontGlyph**` 64비트 포인터 배열 주소임. 개별 글리프 메모리는 24바이트 구조체로 힙 할당해야 함. |
| **화면에 노란색/흰색 사각형(Solid Box) 출력** | 텍스처 UV 좌표 스케일링 미맞춤으로 인한 DirectX 11 UV 범위 초과 (`UV > 1.0`) | `CTexturePageItem` 텍스처 교체 방식 대신 `font_add` + `g_ppFontAssets` 포인터 128B 복사 방식으로 우회할 것. |
| **한글 문자가 깨지거나 `?`로 출력됨 (Mojibake)** | MSVC CP949 ANSI 문자와 GameMaker VM UTF-8 간 이중 인코딩 불일치 | C++ 소스코드 내 한국어 문자열에 C++20 `u8""` 명시적 리터럴 (`(const char*)u8"텍스트"`)을 반드시 적용할 것. |
| **한글 글리프 일부가 출력되지 않고 잘림** | 바닐라 `CFont` 구조체의 `last` 필드가 `12287`로 제한되어 유니코드 `U+AC00` 이상 차단 | 동적 `FTFont` 수술 시 `pFont->last` 범주 필드를 `55203`으로 확장 동기화할 것. |
| **화면 아래에 `"f 1 %:"` 문자열 잔상 남아있음** | GML `scr_84_draw_text_outline` 다중 패스 오프셋 출력 시 원시 포맷 스펙시파이어 통과 | `ProcessTextHook` 함수 상단에서 `strContent == "f 1 %:"` 문자열을 검사하여 `return`으로 가로채 억제할 것. |
