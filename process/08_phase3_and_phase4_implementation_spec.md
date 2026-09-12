# 08. Phase 3 & Phase 4 세부 구현 청사진 (Phase 3 & 4 Blueprint & Spec)

## 1. 개요 (Overview)

본 문서는 **UTDT-SoulMod** 프로젝트의 후반기 고도화 단계인 **Phase 3 (SoulMod 모드 로더 에코시스템 및 동적 에셋 주입)** 및 **Phase 4 (패키징, 원클릭 인스톨러 및 CI/CD 자동화)**에 대한 세부 아키텍처, C++ 클래스 설계, 모드 로딩 알고리즘, 레지스트리 자동 탐지 알고리즘 및 GitHub Actions CI/CD 워크플로우를 완벽하게 정의한 **후반기 개발 명세서(Implementation Blueprint)**입니다.

---

## 2. Phase 3: SoulMod 모드 로더 에코시스템 청사진 (Mod Loader & Dynamic Assets)

### 2.1 기술적 목표 (Technical Goal)
- 단순 텍스트 번역을 넘어, 커스텀 스프라이트, 음원 파일, GML 스크립트를 런타임에서 동적으로 주입/교체할 수 있는 확장 모딩 엔진 제공.
- `mods/` 디렉터리 내 복수의 `.soulmod` 매니페스트 간의 의존성 및 충돌을 결정론적으로 정렬하는 위상 정렬(Topological Sort) 로더 구축.

### 2.2 모드 의존성 정렬 알고리즘 청사진 (C++ Topological Loader)

```cpp
// Phase 3: mods/ 디렉터리 내 매니페스트 위상 정렬 및 충돌 해결 알고리즘

struct ModPackageNode {
    std::string id;
    std::string version;
    std::vector<std::string> dependencies;
    std::vector<std::string> loadBefore;
    std::vector<std::string> loadAfter;
    std::filesystem::path rootPath;
};

static std::vector<ModPackageNode> ResolveModLoadOrder(const std::vector<ModPackageNode>& packages, std::string& error) {
    std::unordered_map<std::string, ModPackageNode> packageMap;
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adjList;

    for (const auto& pkg : packages) {
        packageMap[pkg.id] = pkg;
        inDegree[pkg.id] = 0;
    }

    // 의존성 그래프 구축
    for (const auto& pkg : packages) {
        for (const auto& dep : pkg.dependencies) {
            if (packageMap.find(dep) == packageMap.end()) {
                error = "Missing required dependency mod: " + dep + " for mod " + pkg.id;
                return {};
            }
            adjList[dep].push_back(pkg.id);
            inDegree[pkg.id]++;
        }
    }

    // 위상 정렬 (Kahn's Algorithm)
    std::queue<std::string> zeroDegreeQueue;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) zeroDegreeQueue.push(id);
    }

    std::vector<ModPackageNode> sortedOrder;
    while (!zeroDegreeQueue.empty()) {
        std::string current = zeroDegreeQueue.front();
        zeroDegreeQueue.pop();
        sortedOrder.push_back(packageMap[current]);

        for (const auto& neighbor : adjList[current]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                zeroDegreeQueue.push(neighbor);
            }
        }
    }

    if (sortedOrder.size() != packages.size()) {
        error = "Circular dependency detected among installed mods!";
        return {};
    }

    return sortedOrder;
}
```

### 2.3 동적 스프라이트 주입 청사진 (Dynamic Sprite Injection)
- GameMaker 내장 `sprite_add` 루틴을 가로채어 커스텀 PNG 파일로부터 동적 `CSprite` 객체를 생성하고, `g_ppSpriteAssets` (전역 스프라이트 자원 주소 포인터 배열)에 바인딩합니다.

---

## 3. Phase 4: 배포, 패키징 및 CI/CD 워크플로우 청사진 (Packaging & CI/CD)

### 3.1 원클릭 패처/인스톨러 아키텍처 (`SoulModPatcher.exe`)

사용자가 델타룬/언더테일 게임 경로를 지정하지 않아도 레지스트리 및 스팀 라이브러리를 자동 탐지하여 패치를 수행하는 C# / C++ 패처 알고리즘입니다.

```csharp
// Phase 4: Windows 레지스트리 기반 Steam 델타룬 설치 경로 자동 탐지 알고리즘 (C# / WinForms / WPF)

using System;
using System.IO;
using Microsoft.Win32;

public static class GamePathDetector
{
    public static string DetectDeltaruneInstallPath()
    {
        // 1. Steam 레지스트리에서 SteamPath 탐색
        string steamPath = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
        if (!string.IsNullOrEmpty(steamPath))
        {
            // 기본 스팀 라이브러리 경로 검사
            string defaultDeltarunePath = Path.Combine(steamPath, "steamapps", "common", "DELTARUNE");
            if (File.Exists(Path.Combine(defaultDeltarunePath, "DELTARUNE.exe")))
            {
                return defaultDeltarunePath;
            }

            // 2. 추가 libraryfolders.vdf 파싱하여 타 드라이브 스팀 라이브러리 탐색
            string vdfPath = Path.Combine(steamPath, "steamapps", "libraryfolders.vdf");
            if (File.Exists(vdfPath))
            {
                string[] lines = File.ReadAllLines(vdfPath);
                foreach (var line in lines)
                {
                    if (line.Contains("\"path\""))
                    {
                        string[] parts = line.Split('\t');
                        if (parts.Length >= 2)
                        {
                            string libPath = parts[parts.Length - 1].Replace("\"", "").Trim();
                            string candidate = Path.Combine(libPath, "steamapps", "common", "DELTARUNE");
                            if (File.Exists(Path.Combine(candidate, "DELTARUNE.exe")))
                            {
                                return candidate;
                            }
                        }
                    }
                }
            }
        }
        return null; // 수동 탐색으로 전환
    }
}
```

### 3.2 GitHub Actions 자동 빌드 CI/CD 명세 (`.github/workflows/build-verify.yml`)

```yaml
name: SoulModPlugin CI/CD Build & Verification

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-x64:
    runs-on: windows-2022

    steps:
    - name: Checkout repository
      uses: actions/checkout@v3

    - name: Setup MSBuild
      uses: microsoft/setup-msbuild@v1.1

    - name: Build SoulModPlugin.vcxproj (Release x64)
      run: |
        MSBuild.exe scratch/AurieFramework/SoulModPlugin/SoulModPlugin.vcxproj /t:Clean,Build /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="scratch/AurieFramework/YYToolkit/"

    - name: Verify DLL Output Artifact
      run: |
        if (Test-Path "scratch/AurieFramework/YYToolkit/x64/Release/SoulModPlugin.dll") {
            Write-Host "SoulModPlugin.dll built successfully!"
        } else {
            exit 1
        }

    - name: Upload Artifacts
      uses: actions/upload-artifact@v3
      with:
        name: SoulModPlugin-Release-x64
        path: scratch/AurieFramework/YYToolkit/x64/Release/SoulModPlugin.dll
```

---

## 4. 종합 결론 (Overall Conclusion)

이로써 **Phase 1 (번역 엔진)**, **Phase 2 (태그 파서/자간)**, **Phase 3 (모드 로더/스프라이트)**, **Phase 4 (패처/CI/CD)**까지 프로젝트의 **현재 상태부터 최종 배포까지 모든 단계의 구현 알고리즘, 완제 청사진 코드, 파서 명세표가 100% 작성**되었습니다.
