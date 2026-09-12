# 04. 모드 패키징 규격 및 런타임 적용 가이드 (Mod Packaging & Application Guide)

## 1. 모드 패키지 형식 개요 (.soulmod Package Format)

UTDT-SoulMod 프레임워크는 모드 배포 및 관리를 위해 표준 ZIP 압축 규격 기반의 **`.soulmod` 패키지 포맷**을 사용합니다. `.soulmod` 파일은 모드의 정보와 대상 게임을 정의하는 `manifest.json`, 구체적인 런타임 수술 및 자원 교체 작업을 정의하는 `operations.json`, 그리고 폰트나 텍스트 데이터 등의 물리적 에셋으로 구성됩니다.

```
ExampleMod.soulmod (ZIP Archive)
├── manifest.json              # 모드 메타데이터 및 의존성/대상 게임 정의
├── operations.json            # 런타임 자원 오퍼레이션 명세서
└── assets/
    ├── lang_ko.json           # 번역 키-값 데이터베이스 (UTF-8 JSON)
    └── fnt_ja_main.ttf        # 동적 FreeType 한글 Vector TTF 폰트 파일
```

---

## 2. 매니페스트 명세 (`manifest.json`)

`manifest.json`은 모드의 고유 ID, 버전을 표기하고, Target Game의 바이너리 및 `data.win` 파일 무결성을 지정하는 역할을 합니다.

```json
{
  "formatVersion": 1,
  "id": "dtkr.runtime",
  "name": "DELTARUNE Korean Runtime Patch",
  "version": "1.0.0",
  "targetFile": "data.win",
  "targetDataSha256": "82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6",
  "targets": [
    {
      "id": "deltarune.chapter1_2",
      "targetFile": "data.win",
      "targetDataSha256": "82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6"
    }
  ],
  "dependencies": [],
  "loadBefore": [],
  "loadAfter": [],
  "conflictPolicy": "stop",
  "operationsFile": "operations.json"
}
```

### 핵심 필드 설명
- **`formatVersion`**: 패키지 포맷 버전 (버전 `1` 고정).
- **`id`**: 모드의 고유 식별자 문자열 (예: `dtkr.runtime`).
- **`targetFile`**: 패치가 적용될 핵심 자원 아카이브 파일명 (기본 `data.win`).
- **`targetDataSha256`**: 대상 `data.win` 파일의 해시 핑거프린트 (`82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6`). 무결성 검증 시 1글자라도 다르면 실행을 차단하여 비호환 버전 패치 사고를 방지함.
- **`conflictPolicy`**: 모드 충돌 시 정책 (`stop`: 실행 중단, `warn`: 경고 후 진행, `overwrite`: 덮어쓰기).

---

## 3. 오퍼레이션 명세 (`operations.json`)

`operations.json`은 런타임 주입 시 수행할 폰트 등록, 글로벌 JSON 데이터 로드, 자원 리다이렉션 등의 최소 단위 작업을 정의합니다.

```json
[
  {
    "id": "dtkr.font.register",
    "operationVersion": 1,
    "modId": "dtkr.runtime",
    "target": { "type": "font", "name": "fnt_ja_main" },
    "operation": "font.register",
    "selector": {
      "size": 14,
      "bold": false,
      "italic": false,
      "first": 32,
      "last": 55203
    },
    "assetPath": "assets/fnt_ja_main.ttf",
    "assetSha256": "64_HEXADECIMAL_SHA256_HASH_OF_TTF_FILE",
    "reads": ["asset:assets/fnt_ja_main.ttf"],
    "writes": ["font:fnt_ja_main"]
  },
  {
    "id": "dtkr.global.lang_ko",
    "operationVersion": 1,
    "modId": "dtkr.runtime",
    "target": { "type": "global", "name": "lang_ko" },
    "operation": "global.json.load",
    "selector": {
      "applyAt": "first.draw_set_font"
    },
    "assetPath": "assets/lang_ko.json",
    "assetSha256": "64_HEXADECIMAL_SHA256_HASH_OF_JSON_FILE",
    "reads": ["asset:assets/lang_ko.json"],
    "writes": ["global:lang_ko"]
  }
]
```

### 주요 오퍼레이션 종류 (`operation`)
1. **`font.register`**: 외부 TTF 폰트 파일을 런타임 메모리에 등록하고 지정된 유니코드 범위(`32..55203`)를 할당.
2. **`global.json.load`**: UTF-8 번역 JSON 파일(`lang_ko.json`)을 로드하여 런타임 메모리 룩업 테이블에 원자적(Atomically) 할당.
3. **`resource.redirect`**: 바닐라 폰트 자원 포인터를 새로 주입된 FreeType 동적 폰트 객체 주소로 교체.

---

## 4. 실제 모드 적용 및 런타임 실행 방식 (Runtime Patching Workflow)

유저가 패처를 실행하거나 모드를 적용할 때 내부에서 수행되는 실제 로직 순서입니다.

```
[1. 패키지 검증 (Package Validation)]
   │ `.soulmod` ZIP 압축 해제 후 manifest.json 해시 검사
   │ targetDataSha256 <-> 실제 data.win SHA256 대조 (불일치 시 차단)
   ▼
[2. Aurie / YYToolkit DLL 주입 (Injection)]
   │ DELTARUNE.exe (SHA256: 0CF40FAEC70583D4E67EBFD6F49BE08D5473595B61A3CBED28AD1A02ED939495) 실행 시
   │ Aurie Loader가 SoulModPlugin.dll 주입 및 ModuleInitialize() 호출
   ▼
[3. assets/ 자원 런타임 메모리 등록 (Resource Registration)]
   │ lang_ko.json (6,240 항목) C++ std::unordered_map 및 global.json.load 수명주기로 로드
   │ font_add("fnt_ja_main.ttf", 14, false, false, 32, 55203) 호출하여 FTFont 객체 생성
   ▼
[4. 런타임 메모리 수술 및 리다이렉트 (Memory Surgery & Redirection)]
   │ g_ppFontAssets (0x140687BB8) 탐색
   │ FTFont의 VTable(+0x00), pGlyphs(+0x10), pTexArray(+0x18), range(32..55203)를
   │ 바닐라 CFont 자원 위치에 128바이트 복사 동기화 및 g_ResolvedFontRedirects 바인딩
   ▼
[5. Draw Text API Hooking & 렌더링 (Native Pipeline Rendering)]
   │ GML draw_set_font 및 draw_text 계열 7개 함수 Hooking 실행
   │ f1%: 원시 스펙시파이어 제거 및 u8"" 한글 번역문 전달
   └─> GameMaker 내장 C++ 렌더러가 화면에 깨끗한 한글 출력!
```
