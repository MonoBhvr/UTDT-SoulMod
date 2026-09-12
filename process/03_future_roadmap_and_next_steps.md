# 03. 향후 로드맵, 남은 과제 및 세부 이행 계획 (Future Roadmap & Next Steps)

## 1. 프로젝트 현황 요약 (Current Status Summary)

현재 UTDT-SoulMod 프로젝트는 **Stage F-6 (Native GameMaker Font Pipeline 최종 검증 및 DTKR 런타임 한글 패치 기틀 완성)** 단계까지 통과한 상태입니다. 

- **달성된 핵심 성과**:
  1. `data.win` 및 `DELTARUNE.exe` 수정 없는 100% 비침습적(Non-invasive) 런타임 우회 인젝션 아키텍처 수립.
  2. FreeType TTF Native 폰트 주입 및 128바이트 C++ 구조체/VTable 동기화를 통한 한글 유니코드 11,172자 깨짐/상자 현상 완전 해결.
  3. `lang_ko.json` (6,240개 엔트리) 로드 및 Text API 7종 계열전수 Hooking 검증.
  4. 크래시 덤프 0건, Exit Code 0, 파일 무결성 100% 유지.

---

## 2. 세부 개발 단계별 향후 로드맵 (Detailed Roadmap)

```
[Phase 1: 런타임 번역엔진 통합]
   ├── scr_84_get_lang_string VM 인터셉션
   └── lang_ko.json (6,240 항목) 실시간 키-값 매핑
            │
            ▼
[Phase 2: UI, 포맷팅 태그 및 연출 최적화]
   ├── 제어 태그(\c, \y, \w, ^, %, &) 파싱 지원
   ├── DTKR 원본 피셀 자간(Kerning) 미세 조정
   └── 전투 UI / 대화창 / 메뉴 / 저장 화면 연출 최적화
            │
            ▼
[Phase 3: SoulMod 모드 로더 에코시스템]
   ├── mods/ 디렉터리 동적 스캐너 및 핫 리로딩
   └── 커스텀 스프라이트 / 음원 / GML 스크립트 우회 API
            │
            ▼
[Phase 4: 패키징 및 CI/CD 자동화]
   ├── 델타룬/언더테일 통합 원클릭 인스톨러 배포
   └── GitHub Actions 자동 빌드 및 검증 시스템
```

---

## 3. 세부 단계별 구현 과제 (Phase Implementation Details)

### 3.1 Phase 1: 런타임 번역 엔진 완전 통합 (Game-Wide Localizer)

- **핵심 과제**: 챕터 선택 화면 외에 게임 내부의 모든 대화, 아이템 설명, 전투 메시지, 시스템 UI를 실시간 치환.
- **세부 기술 과제**:
  1. **`scr_84_get_lang_string` GML 스크립트 런타임 인터셉션**:
     - GameMaker VM 내부의 `scr_84_get_lang_string` 스크립트 함수 주소를 탐색하거나, `YYTK` 스크립트 훅 API를 이용하여 GML 키 문자열(예: `"DEVICE_CONTACT_slash_Step_0_gml_126_0"`)을 가로챕니다.
  2. **`g_LangMap` 룩업 및 치환**:
     - 가로챈 키에 대응하는 한국어 번역 텍스트를 `g_LangMap`에서 찾아 GameMaker VM `RValue` 반환값으로 신속하게 치환합니다.

---

### 3.2 Phase 2: 인게임 UI, 제어 태그 및 폰트 연출 최적화 (Formatting & Aesthetics)

- **핵심 과제**: DELTARUNE 특유의 대화창 연출 제어 태그와 DTKR 원본 피셀 폰트 자간을 정교하게 복원.
- **세부 기술 과제**:
  1. **대화 제어 태그 파서 구현**:
     - 델타룬 대화창에서 사용하는 텍스트 제어 태그 지원:
       - `\c` / `\y` / `\w` / `\R` (텍스트 색상 변경: Cyan, Yellow, White, Red 등)
       - `^1` ~ `^9` (대화 출력 타이밍 지연/Pause)
       - `%` / `%%` (줄바꿈 및 대화 페이지 넘김)
       - `&` (행 바꿈)
       - `\M0` ~ `\M9` (초상화 및 표정 변경 애니메이션 태그)
     - 해당 태그들이 폰트 렌더러에 의해 문자 그대로 그려지지 않고 GML 연출 엔진에 정상 전달되도록 태그 보존형 텍스트 파서를 구현합니다.
  2. **DTKR 피셀 자간(Kerning) 및 오프셋 조정**:
     - `fnt_ja_main.ttf` 폰트의 글리프 메트릭(`shift`, `offset_x`)을 DTKR 비트맵 폰트 원본 가로 폭과 1:1로 맞추어, 텍스트가 대화창 영역을 벗어나거나 자간이 어색해지는 현상을 교정합니다.
  3. **다양한 폰트 패밀리 확장 지원**:
     - `fnt_main` (기본 대화), `fnt_ja_main` (일어/한글 대화), `fnt_comicsans` (샌즈 대화), `fnt_papyrus` (파피루스 대화), `fnt_dotum` (시스템 메뉴) 등 폰트 자원별 전용 TTF 폰트 파일 매핑 인터페이스 구축.

---

### 3.3 Phase 3: SoulMod 모드 로더 에코시스템 확장 (Modding Framework Extension)

- **핵심 과제**: 한글 패치 기능에서 확장하여, 유저 모더들이 에셋과 스크립트를 자유롭게 추가할 수 있는 네이티브 모드 로더 제공.
- **세부 기술 과제**:
  1. **`mods/` 디렉터리 동적 스캐너**:
     - `mods/` 하위의 폴더 구조를 탐색하여 폰트, 언어 파일(`lang_ko.json`), 커스텀 스프라이트, 음원 파일을 동적으로 패치하는 모드 매니저 구현.
  2. **스프라이트 및 텍스처 파이프라인 우회**:
     - `sprite_add` 및 `g_ppSpriteAssets` 포인터 교체 방식을 체계화하여 모드 스프라이트를 런타임 주입.
  3. **GML Script Hooking API 노출**:
     - C++ 모드 개발자가 YYToolkit 인프라를 통해 GameMaker 스크립트를 Hooking할 수 있는 C/C++ Header SDK 작성.

---

### 3.4 Phase 4: 패키징, 배포 및 CI/CD 파이프라인 (Packaging & Deployment)

- **핵심 과제**: 일반 유저들이 클릭 한 번으로 한글 패치 및 모드 프레임워크를 적용할 수 있는 사용자 친화적 환경 제공.
- **세부 기술 과제**:
  1. **원클릭 GUI 인스톨러/패처 개발**:
     - 사용자의 스팀/기타 델타룬 및 언더테일 설치 디렉터리를 자동 탐지하여 `Aurie.dll`, `SoulModPlugin.dll`, `mods/` 폴더를 안전하게 배치하는 패처 프로그램 제작.
  2. **GitHub Actions CI/CD 빌드 및 검증 자동화**:
     - 코드가 커밋될 때마다 MSBuild를 통해 `SoulModPlugin.dll`을 자동으로 빌드하고, 무결성 검사 및 정적 분석을 수행하는 자동화 워크플로우 구축.

---

## 4. 결론 (Conclusion)

UTDT-SoulMod 프로젝트는 Stage F-6를 통해 기술적 난제였던 **GameMaker x64 C++ Native 폰트 메모리 수술 및 FreeType 런타임 한글 출력**을 성공적으로 완수했습니다. 

향후 위의 **Phase 1~4 로드맵**을 차례대로 수행하여 완전한 인게임 6,240개 문장 실시간 번역, 제어 태그 연출 복원, 그리고 최종 사용자 배포형 네이티브 모딩 프레임워크를 완성할 예정입니다.
