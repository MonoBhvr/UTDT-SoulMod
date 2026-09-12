# UTDT-SoulMod 개발 프로세스 및 기술 문서 종합 마스터 목차

## 1. 개요 및 프로젝트 최종 비전 (Project Vision & Core Goal)

본 프로젝트 **UTDT-SoulMod**의 궁극적인 최종 목표는 **"임의의 게임메이커(GameMaker 2.x x64) 모드를 자동으로 분석(Automated Analysis)하고, 게임 파일(바이너리 `.exe` 및 에셋 `data.win`)을 직접 수정하지 않으며, 실시간 C++ 런타임 메모리 주입(Real-time Runtime Injection)을 통해 무결하게 모드가 적용된 상태로 게임을 플레이할 수 있도록 만드는 범용 런타임 모딩 엔진을 구축하는 것"**입니다.

현재 진행 중인 **DELTARUNE 한글 패치(DTKR)** 작업은 이 범용 모드 분석/주입 엔진의 핵심 기능(메모리 수술, C++ 루틴 후킹, 동적 에셋 주입, 선언적 오퍼레이션 로딩, 무결성 유지)을 실전 검증하기 위한 **최초의 플래그십 테스트베드(Flagship Testbed / Reference Proof-of-Concept)**입니다.

---

## 2. 프로세스 문서 구성 (Documentation Sitemap)

`process/` 디렉터리 내 세부 문서들은 다음과 같이 총 8개 문서로 구성되어 있습니다.

```
d:\UTDT-SoulMod\process\
├── README.md                                          # 본 마스터 목차 및 프로젝트 비전
├── 01_architecture_and_development_methodology.md   # 범용 모드 분석/주입 엔진 아키텍처 및 C++ 런타임 메모리 수술 구조
├── 02_history_and_discussion_log.md                  # Stage A ~ Stage F-6 종합 개발 이력 및 기술 토의 기록
├── 03_future_roadmap_and_next_steps.md              # 범용 모드 분석기/주입기 완성 로드맵 및 세부 이행 계획
├── 04_mod_packaging_and_application_guide.md        # 범용 모드 패키징 규격 (.soulmod), manifest/operations 명세 및 주입 로직
├── 05_existing_codebase_walkthrough.md              # 기존 C++ 코드베이스 (ModuleMain.cpp) 함수별 구현 및 컴파일 분석
├── 06_developer_onboarding_and_build_playbook.md     # 신규 개발자 온보딩, MSBuild/PowerShell 명령어 및 트러블슈팅
├── 07_phase1_and_phase2_implementation_spec.md       # Phase 1 (scr_84_get_lang_string) & Phase 2 (태그 파서) C++ 구현 청사진
└── 08_phase3_and_phase4_implementation_spec.md       # Phase 3 (범용 모드 분석기/스프라이트) & Phase 4 (원클릭 인스톨러/CI-CD) 청사진
```

### 디렉터리별 핵심 내용 요약

1. **[01_architecture_and_development_methodology.md](file:///d:/UTDT-SoulMod/process/01_architecture_and_development_methodology.md)**
   - 범용 게임메이커 모드 런타임 주입 엔진 비전 및 메모리 구조 분석 (`CFont`, `FTFont`, `CFontGlyph**` 힙 포인터 테이블).
   - 런타임 메모리 수술(Memory Surgery) 방식: `font_add` + `g_ppFontAssets` 자원 주소 리다이렉션.
   - GameMaker 내장 Text API 계열 7종 Hooking 구조 및 MSVC UTF-8(`u8""`) / VM `RValue` 문자열 호환성 아키텍처.
   - Production 오퍼레이션 드라이븐 엔진 (`file_text_open_read`, `global.json.load`, `RestartCoordinator`).

2. **[02_history_and_discussion_log.md](file:///d:/UTDT-SoulMod/process/02_history_and_discussion_log.md)**
   - Stage A ~ Stage D: 기반 프레임워크 및 프로세스 인젝션 검증.
   - Stage E ~ Stage F-4: 초기 텍스처 교체 시도, Access Violation(0xC0000005) 분석, DirectX 11 UV 샘플링 밖 참조(Solid Bounding Box) 원인 규명.
   - Stage F-5: `CFontGlyph**` 64비트 포인터 배열 발견, FreeType Native TTF 우회, C++ Vtable/128바이트 구조체 동기화, `pFont->last` 유니코드 범위 제한 해제 등 연속적 기술적 돌파구.
   - Stage F-6: 6종 유니코드 한글 검증(`[FTFONT VERIFY]`), 폰트 우회 로그(`[FONT_REDIRECT]`), `lang_ko.json` 6,240개 엔트리 연결 및 최종 무결성 검증.

3. **[03_future_roadmap_and_next_steps.md](file:///d:/UTDT-SoulMod/process/03_future_roadmap_and_next_steps.md)**
   - Phase 1: `scr_84_get_lang_string` VM 함수 차단을 통한 테스트베드 번역 텍스트 실시간 치환.
   - Phase 2: DTKR 원본 피셀 폰트 자간(Kerning) 미세조정, 제어 태그(`\c`, `\y`, `\w`, `^`, `%`, `&`) 파서 및 쉐이더 최적화.
   - Phase 3: 범용 모드 로더(Mod Loader) & 자동 분석기 에코시스템 확장, 에셋/스프라이트/음원 동적 우회 주입 및 모더용 GML Hooking API 제공.
   - Phase 4: 배포용 원클릭 패처/패키징 및 CI/CD 자동화 빌드 파이프라인.

4. **[04_mod_packaging_and_application_guide.md](file:///d:/UTDT-SoulMod/process/04_mod_packaging_and_application_guide.md)**
   - `.soulmod` 범용 모드 압축 포맷 아키텍처 (ZIP 기반 `manifest.json`, `operations.json`, `assets/`).
   - `manifest.json` 매니페스트 규격: `targetFile`, `targetDataSha256`, 의존성 및 충돌 처리 정책 (`conflictPolicy`).
   - `operations.json` 명세: `font.register`, `global.json.load`, `resource.redirect` 오퍼레이션 및 해시 검증.
   - 런타임 적용 워크플로우 (패키지 검증 -> DLL 주입 -> 폰트/JSON 주입 -> 메모리 수술 -> Text API Hook 렌더링).

5. **[05_existing_codebase_walkthrough.md](file:///d:/UTDT-SoulMod/process/05_existing_codebase_walkthrough.md)**
   - `ModuleMain.cpp` 세부 소스코드 줄별 분석 및 구현 이유 설명.
   - `ModuleInitialize` 루틴 주소 해시 탐색 및 `MmCreateHook` 후킹 설치.
   - `HK_file_text_open_read` 파일 리다이렉트 후킹 및 `EnsureRegisteredFont` 동적 폰트 주입 분석.
   - `RestartCoordinator` 및 `game_restart` 런타임 동기화 매커니즘.

6. **[06_developer_onboarding_and_build_playbook.md](file:///d:/UTDT-SoulMod/process/06_developer_onboarding_and_build_playbook.md)**
   - 필수 컴파일러 및 실행 환경 표준 (Visual Studio 2022 Build Tools `/std:c++20`, PowerShell 5.1/7+).
   - 프로젝트 디렉터리 전체 지도 및 파일 경로 매핑.
   - 원클릭 MSBuild 및 테스트 구동 PowerShell 명령어 세트.
   - 트러블슈팅 매뉴얼: Access Violation, 노란색 사각형, Mojibake, 잘림 현상 원인 및 1:1 해결책.

7. **[07_phase1_and_phase2_implementation_spec.md](file:///d:/UTDT-SoulMod/process/07_phase1_and_phase2_implementation_spec.md)**
   - Phase 1: `scr_84_get_lang_string` GML 스크립트 인터셉터 C++ 완제 코드 청사진 (`HK_scr_84_get_lang_string_Aurie`).
   - Phase 2: DELTARUNE 대화 제어 태그 명세표 (`\c`, `\y`, `\w`, `\R`, `^1`~`^9`, `%`, `&`, `\M0`~`\M9`).
   - 태그 보존형 텍스트 토크나이저 파서 C++ 구현 청사진 (`ParseDeltaruneTags`) 및 폰트 자간(Kerning) 미세 조정 표.

8. **[08_phase3_and_phase4_implementation_spec.md](file:///d:/UTDT-SoulMod/process/08_phase3_and_phase4_implementation_spec.md)**
   - Phase 3: 범용 모드 매니페스트 위상 정렬(Topological Sort) C++ 알고리즘 (`ResolveModLoadOrder`) 및 동적 스프라이트 주입 명세.
   - Phase 4: Windows 레지스트리 기반 Steam 델타룬 설치 경로 C# 자동 탐지 알고리즘 (`GamePathDetector`) 및 GitHub Actions CI/CD 명세 (`.github/workflows/build-verify.yml`).

---

## 3. 핵심 개발 철학 및 제약 조건 (Core Guidelines)

1. **절대 금지 사항**:
   - `data.win` 및 `DELTARUNE.exe` 파일의 직/간접 바이너리 수정 금지.
   - 외부 DirectX 11 Overlay, 독립 텍스트 렌더러, Sprite Font 우회 렌더링 사용 금지 (GameMaker 내장 렌더링 파이프라인 100% 활용).
2. **안정성 검증**:
   - 모든 런타임 테스트는 %LOCALAPPDATA%\CrashDumps 증분 덤프 0건, Exit Code 0, `CloseMainWindow()` 정상 닫기를 통과해야 함.
