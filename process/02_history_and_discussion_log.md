# 02. 종합 개발 이력 및 기술 토의 기록 (History & Discussion Log)

## 1. 단계별 개발 진행 역경 및 기술 이력 (Chronological History)

### 1.1 초기 기반 구축 단계 (Stage A ~ Stage D)
- **목표**: GameMaker 2.x x64 런타임 환경에서 Aurie Framework 및 YYToolkit 기반 C++ DLL 인젝션 파이프라인 수립.
- **결과**: `DELTARUNE.exe` 프로세스 메모리에 `SoulModPlugin.dll` 성공적 인젝션, YYTK 인터페이스 획득, 루틴 포인터 해시 탐색 아키텍처 완성 (Stage A ~ D PASS).

---

### 1.2 텍스처 무단 교체 시도 및 시행착오 (Stage E ~ Stage F-4)
- **초기 가설**: DTKR의 `atlas.png` 텍스처를 `CTexturePageItem` 수준에서 직접 교체하면 한글 글리프가 그려질 것이다.
- **결과 및 실패 원인**:
  1. `CTexturePageItem` 교체 시 DirectX 11 텍스처 UV 정규화 스케일러가 일치하지 않아 UV 샘플링이 1.0을 초과(`UV > 1.0`), 화면 전체가 **노란색/흰색 사각형(Solid Box)**으로 채워짐.
  2. `CFont` 구조체 메모리 접근 방식 오류로 인해 Access Violation (`0xC0000005`) 및 게임 프로세스 비정상 종료 발생.
- **사용자 지침**: 외부 DirectX Overlay, 독립 Text Renderer, Sprite Font 렌더링 우회 금지, 원본 바이너리 수정 금지 재확인.

---

### 1.3 기술적 연속 돌파구 (Stage F-5 Empirical Breakthroughs)

#### ① `CFontGlyph**` 64비트 포인터 배열 구조 발견 (v13.1.0)
- **실험**: 런타임 힙 메모리 원시 바이트 덤프를 수행하여 `CFont +0x10` 필드 정밀 조사.
- **발견**: `CFont +0x10`은 연속 구조체 배열이 아닌, 힙에 개별 할당된 24바이트 글리프 구조체들을 가리키는 **64비트 포인터 배열 (`CFontGlyph**`)** 구조임.
- **결과**: 동적 포인터 배열 및 24바이트 글리프 메모리 할당 적용으로 **Access Violation 및 크래시 덤프 100% 제거 (Exit Code = 0, Crash Dump = 0)**.

#### ② FreeType Native TTF 동적 폰트 생성 및 리다이렉션 (v13.4.0 ~ v13.7.0)
- **실험**: GameMaker 내장 함수 `font_add("fnt_ja_main.ttf", 14, false, false, 32, 55203)` 호출.
- **발견**: `font_add`로 생성된 동적 FreeType 폰트(Font ID 10)는 유니코드 32~55203 범위 전체의 올바른 UV 좌표와 메트릭을 자동 생성함.
- **결과**: 전역 폰트 자원 포인터 배열 `g_ppFontAssets[1, 2, 5, 8]`를 동적 FreeType 폰트 객체로 우회 리다이렉션.

#### ③ Text API 7종전수 Hooking & 스펙시파이어 억제 (v13.8.0 ~ v13.9.0)
- **실험**: DELTARUNE GML 스크립트 `scr_84_draw_text_outline` 분석.
- **발견**: GML 엔진이 `draw_text_transformed`, `draw_text_ext_transformed` 등 다양한 API를 혼용 호출하며, 원시 포맷 문자열(`"f 1 %:"`)이 텍스트 아래에 잔상으로 그려짐.
- **결과**: Text API 7종전수 Hooking 완료 및 원시 스펙시파이어 문자열 필터링 적용.

#### ④ `CFont` vs `FTFont` C++ VTable & 128바이트 필드 클로닝 (v14.0.0 ~ v14.3.0)
- **실험**: FreeType 동적 폰트 복사 후 글리프 미출력 원인 조사.
- **발견**: `data.win` 폰트(`CFont`)와 `font_add` 폰트(`FTFont`)는 C++ VTable 및 글리프 탐색 가상 메서드가 상이함.
- **결과**: `FTFont`의 C++ Vtable(`+0x00`), FreeType 글리프 테이블(`+0x10`), `pTexArray`(`+0x18`), 범주 범위 제한(`first=32`, `last=55203`) 전체 128바이트 구조체를 기존 폰트 ID 0~9에 복사 동기화.

#### ⑤ MSVC UTF-8 명시적 리터럴 & `pFont->last` 유니코드 제한 해제 (v14.4.0 ~ v14.6.0)
- **발견**: MSVC의 ANSI(CP949) 바이트와 GameMaker VM UTF-8 디코더 간 바이트 불일치(Mojibake) 및 바닐라 `pFont->last` 필드가 `12287`로 제한되어 한글 유니코드(`U+AC00`~`U+D7A3`)가 범위 초과 차단됨.
- **결과**: `u8""` 명시적 리터럴 및 `last = 55203` 필드 동기화 적용. 화면 캡처(`v146_screen.png`)를 통해 완전한 한글 출력 검증 완료.

---

### 1.4 Stage F-6 최종 검증 및 패치 완성 (Stage F-6 Verification)

Stage F-6 지시사항에 따라 작성된 최종 검증 결과입니다.

#### 1.4.1 Task 1: FTFont 한글 Glyph 검증 로그 (`[FTFONT VERIFY]`)
6종 유니코드 한글 검증 문자(`가`, `제`, `1`, `장`, `한`, `글`)의 글리프 탐색 및 오프셋 검증 완료.

```log
[FTFONT VERIFY]
char=가
unicode=0xAC00
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0

[FTFONT VERIFY]
char=제
unicode=0xC81C
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0

[FTFONT VERIFY]
char=1
unicode=0x0031
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0

[FTFONT VERIFY]
char=장
unicode=0xC7A5
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0

[FTFONT VERIFY]
char=한
unicode=0xD55C
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0

[FTFONT VERIFY]
char=글
unicode=0xAE00
glyph=FOUND
fallback=false
advance=14
width=12
height=14
offset_x=0
offset_y=0
```

#### 1.4.2 Task 2: Font Resource Redirect 검증 로그 (`[FONT_REDIRECT]`)
```log
[FONT_REDIRECT]
font_id=0
old_type=CFont
new_type=FTFont
status=SUCCESS

[FONT_REDIRECT]
font_id=1
old_type=CFont
new_type=FTFont
status=SUCCESS

[FONT_REDIRECT]
font_id=2
old_type=CFont
new_type=FTFont
status=SUCCESS
```

#### 1.4.3 Task 3: DTKR 번역 데이터 연결
- `mods/test/lang_ko.json` (6,240개 엔트리) 로드 및 이스케이프 문자열 해제 완료.
- 챕터 메뉴 및 인게임 서브타이틀("제 1 장: 시작", "제 2 장: 사이버 월드" 등) 정상 치환 출력 및 스펙시파이어 잔상 소멸 확인.

#### 1.4.4 무결성 검증 기록
- `data.win` SHA256: `82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6` (Unchanged)
- `DELTARUNE.exe` SHA256: `0CF40FAEC70583D4E67EBFD6F49BE08D5473595B61A3CBED28AD1A02ED939495` (Unchanged)
- Crash Dumps: **0개**
- Exit Code: **0**
- `CloseMainWindow()`: **True**
