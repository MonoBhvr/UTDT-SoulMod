# UTDT SoulMod

**UTDT SoulMod**는 UNDERTALE·DELTARUNE을 비롯한 GameMaker (VM) 기반 게임을 위한 **실행 중(Runtime) 모드 적용 플랫폼**입니다.

기존 GameMaker 모딩 방식(xdelta 패치 적용 후 `data.win` 파일 덮어쓰기)과 달리, UTDT SoulMod는 **원본 게임 파일(`data.win`)을 단 1바이트도 수정하지 않고** 게임 프로세스 런타임 메모리 상에서 코드 및 리소스를 동적으로 교체·패치합니다.

---

## 🎯 주요 목표 및 특징

- **비파괴 런타임 모딩 (Non-Destructive Runtime Modding)**
  - 원본 `data.win` 파일의 SHA-256 해시가 항상 동일하게 유지됩니다.
  - 실행용 임시/복제 `data.win`을 생성하지 않습니다.
  - 모드를 비활성화하거나 제거하면 즉시 순수 원본 상태로 게임이 실행됩니다.
- **다중 모드 병합 및 동시 활성화**
  - 여러 모드를 동시에 로드할 수 있습니다.
  - 모드 패키지 간 중복 변경 사항(Duplicate) 자동 통합 및 충돌(Conflict) 사전 감지.
  - 명시적 loadOrder (`loadBefore`, `loadAfter`), 우선순위(Priority) 기반 모드 작성 지원.
- **안전한 원자적 적용 (Atomic Apply & Rollback)**
  - 모드 적용 실패 시 부분 적용으로 인한 게임 crash나 불안정성을 방지하고 안전하게 원본 포인터로 롤백.
- **기존 패치 모드 가져오기 (Converter)**
  - 기존 xdelta 패치, `.csx` 스크립트, 수정된 `data.win`을 분석하여 공통 `.soulmod` 패키지로 자동 추출.

---

## 🏗️ 1차 마일스톤 (POC)

- **목표 대상**: DELTARUNE (x64 GameMaker VM Engine)
- **대상 모드**: DELTARUNE 한국어 패치 (xdelta)
- **검증 항목**:
  1. 원본 `data.win` SHA-256 해시 변경 없음.
  2. 런타임 시 한글 대사, UI 문자열, 한글 폰트 정상 출력.
  3. 모드 비활성화 시 영어 원본으로 원상 복귀.

---

## 🧱 기술 스택 및 라이선스 고지

- **SoulMod Runtime** (`runtime/`): C++20 / CMake x64 Native Dynamic Link Library (`SoulMod.Runtime.dll`)
  - 모듈 로딩 및 주입: **Aurie Framework**
  - GameMaker 런타임 접근: **YYToolkit Framework**
  - 후킹 계층: **SafetyHook / Aurie Hooks**
- **SoulMod Converter** (`converter/`): C# / .NET 8 CLI & Library
  - GameMaker 데이터 분석 및 리소스 Diff: **UndertaleModLib**

> [!NOTE]
> 제3자 오픈소스 의존성 라이선스 고지는 [THIRD_PARTY_NOTICES.md](file:///d:/UTDT-SoulMod/THIRD_PARTY_NOTICES.md)를 참조하십시오.

---

## 📂 저장소 구조

```text
UTDTSoulMod/
├─ README.md
├─ LICENSE
├─ NOTICE
├─ THIRD_PARTY_NOTICES.md
├─ docs/                      # 프로젝트 및 런타임 연구 문서
├─ runtime/                   # C++20 Runtime DLL (Aurie/YYToolkit 기반)
├─ converter/                 # C# .NET 8 Converter CLI & Diff Engine
├─ shared/                    # JSON Schema (.soulmod 매니페스트 및 오퍼레이션)
├─ mods/                      # 모드 패키지 예시
└─ tests/                     # 단위/골든/통합 테스트 피스처
```

---

## 🛠️ 빌드 방법 (Build Guide)

### 1. Prerequisite
- **Windows 10/11 x64**
- **Visual Studio 2022** (Desktop development with C++, .NET Desktop Development)
- **CMake** (3.22 이상)
- **.NET 8.0 SDK**

### 2. Runtime (C++) 빌드
```cmd
cd runtime
cmake -B build -A x64
cmake --build build --config Release
```
결과물: `runtime/build/SoulMod.Runtime/Release/SoulMod.Runtime.dll`

### 3. Converter (C#) 빌드
```cmd
cd converter
dotnet build SoulMod.Converter.sln -c Release
```
결과물: `converter/SoulMod.Converter.Cli/bin/Release/net8.0/soulmod.exe`

### 4. Windows 전체 부트스트랩/빌드/검증

Visual Studio 2022 Developer PowerShell (Desktop development with C++ workload)에서 저장소 루트에 있는 다음 명령 하나를 실행합니다. `-UmtDependencyDirectory`는 `UndertaleModLib.dll`, `Underanalyzer.dll`, ImageMagick 및 SharpZipLib DLL이 있는 디렉터리이고, `-AurieRoot`는 Aurie/YYToolkit과 native proof 프로젝트가 있는 디렉터리입니다.

```powershell
pwsh -File tools/build_windows.ps1 -UmtDependencyDirectory .\scratch\lib -AurieRoot .\scratch\AurieFramework
```

이 엔트리포인트는 .NET 8/10 SDK, CMake 3.22+, VS2022/MSVC v143, Aurie/YYToolkit 파일을 먼저 검증하고, .NET 10 UMT worker를 `artifacts/umtdiff/win-x64`에 새로 publish한 뒤 .NET 8 converter build/test, CMake x64 runtime build, native proof build/test 순서로 실행합니다. `-PrerequisitesOnly`는 빌드하지 않고 prerequisite 검증만 수행합니다. 누락된 prerequisite나 새로 생성되지 않은 artifact는 `ERROR[...]` 메시지와 exit code 1로 종료합니다.

Converter tests require the MSBuild property `SoulModUmtWorkerPath`; the entrypoint supplies the freshly published executable and temporarily overrides `SOULMOD_UMT_WORKER_PATH` for that test process, restoring any prior value afterward. The native proof lane is Release/x64, matching its checked-in Visual Studio project.

```powershell
pwsh -File tools/build_windows.ps1 -PrerequisitesOnly -UmtDependencyDirectory .\scratch\lib -AurieRoot .\scratch\AurieFramework
python tools/build_windows_test.py
```

---

## ⚠️ 안전 및 백업 안내 (Safety Notice)

1. UTDT SoulMod Runtime은 **GameMaker VM 런타임 전용**입니다. (YYC 컴파일 빌드는 1차 지원 범위에서 제외됩니다.)
2. 본 모드는 원본 게임의 파일 시스템 저작권을 보호하기 위해 원본 `data.win`을 결코 교체하거나 개작하여 저장하지 않습니다.
