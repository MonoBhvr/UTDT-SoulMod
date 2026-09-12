[CmdletBinding()]
param(
    [string] $RepoRoot,
    [string] $UmtDependencyDirectory,
    [string] $AurieRoot,
    [ValidateSet('Release')]
    [string] $Configuration = 'Release',
    [ValidateRange(1, 240)]
    [int] $TimeoutMinutes = 30,
    [switch] $PrerequisitesOnly
)

$ErrorActionPreference = 'Stop'
$script:RunStartedUtc = [DateTime]::UtcNow

function Fail([string] $Code, [string] $Message) {
    throw [InvalidOperationException]::new("ERROR[$Code] $Message")
}

function Resolve-RequiredDirectory([string] $Path, [string] $Name) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        Fail 'CONFIGURATION' "$Name must be supplied."
    }
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($null -eq $resolved -or -not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        Fail 'PREREQUISITE_MISSING' "$Name does not exist: $Path"
    }
    return $resolved.Path
}

function Require-File([string] $Path, [string] $Name) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Fail 'PREREQUISITE_MISSING' "$Name is missing: $Path"
    }
}

function Stop-ProcessTree([Diagnostics.Process] $Process) {
    if ($Process.HasExited) {
        $Process.WaitForExit()
        return
    }
    $killTreeMethod = [Diagnostics.Process].GetMethod('Kill', [Type[]] @([bool]))
    if ($null -ne $killTreeMethod) {
        $killTreeMethod.Invoke($Process, @($true)) | Out-Null
    } else {
        $taskkill = Get-Command 'taskkill.exe' -CommandType Application -ErrorAction SilentlyContinue
        if ($null -eq $taskkill) {
            Fail 'COMMAND_TIMEOUT_CLEANUP' 'Process-tree termination is unavailable because taskkill.exe is missing.'
        }
        & $taskkill.Source '/PID' $Process.Id '/T' '/F' | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Fail 'COMMAND_TIMEOUT_CLEANUP' "taskkill.exe could not terminate process tree $($Process.Id)."
        }
    }
    $Process.WaitForExit()
}

function Quote-ProcessArgument([string] $Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-Tool([string] $FileName, [string[]] $Arguments, [string] $Name) {
    $argumentText = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join ' '
    Write-Host "[RUN] ${Name}: $FileName $argumentText"
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FileName
    $startInfo.Arguments = $argumentText
    $startInfo.WorkingDirectory = $script:RepoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) {
            Fail 'COMMAND_START' "Could not start $Name ($FileName)."
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            try {
                Stop-ProcessTree $process
            } catch {
                Fail 'COMMAND_TIMEOUT_CLEANUP' "Could not terminate the $Name process tree: $($_.Exception.Message)"
            }
            Fail 'COMMAND_TIMEOUT' "$Name exceeded the $TimeoutMinutes minute timeout."
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($stdout) { Write-Host $stdout.TrimEnd() }
        if ($process.ExitCode -ne 0) {
            $detail = $stderr.Trim()
            if ([string]::IsNullOrWhiteSpace($detail)) { $detail = $stdout.Trim() }
            Fail 'COMMAND_FAILED' "$Name exited with code $($process.ExitCode). $detail"
        }
        return $stdout
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                Stop-ProcessTree $process
            }
        } finally {
            $process.Dispose()
        }
    }
}

function Resolve-Tool([string] $CommandName, [string] $Name) {
    $command = Get-Command $CommandName -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        Fail 'PREREQUISITE_MISSING' "$Name is not on PATH. Start a VS2022 Developer PowerShell and install the required workload."
    }
    return $command.Source
}

function Test-Prerequisites {
    Require-File (Join-Path $script:RepoRoot 'converter/SoulMod.Converter.sln') '.NET 8 converter solution'
    Require-File (Join-Path $script:RepoRoot 'converter/SoulMod.UmtWorker/SoulMod.UmtWorker.csproj') '.NET 10 worker project'
    Require-File (Join-Path $script:RepoRoot 'runtime/CMakeLists.txt') 'runtime CMake project'

    $script:Dotnet = Resolve-Tool 'dotnet.exe' '.NET SDK'
    $sdkList = Invoke-Tool $script:Dotnet @('--list-sdks') '.NET SDK discovery'
    if ($sdkList -notmatch '(?m)^8\.') { Fail 'PREREQUISITE_MISSING' '.NET 8 SDK is required for converter build/tests.' }
    if ($sdkList -notmatch '(?m)^10\.') { Fail 'PREREQUISITE_MISSING' '.NET 10 SDK is required to publish SoulMod.UmtWorker.' }

    $script:CMake = Resolve-Tool 'cmake.exe' 'CMake'
    $cmakeVersion = Invoke-Tool $script:CMake @('--version') 'CMake version discovery'
    $versionMatch = [Regex]::Match($cmakeVersion, 'cmake version (\d+\.\d+(?:\.\d+)?)')
    if (-not $versionMatch.Success -or [Version]$versionMatch.Groups[1].Value -lt [Version]'3.22.0') {
        Fail 'PREREQUISITE_MISSING' 'CMake 3.22 or newer is required.'
    }

    if ($env:VSCMD_VER -notlike '17.*' -or [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir)) {
        Fail 'PREREQUISITE_MISSING' 'VS2022/MSVC environment is not loaded. Start a VS2022 Developer PowerShell with the Desktop development with C++ workload.'
    }
    $script:MsBuild = Resolve-Tool 'msbuild.exe' 'MSBuild'
    Resolve-Tool 'cl.exe' 'MSVC compiler' | Out-Null

    $script:UmtDependencyDirectory = Resolve-RequiredDirectory $UmtDependencyDirectory 'UmtDependencyDirectory'
    foreach ($dependency in @(
        'UndertaleModLib.dll',
        'Underanalyzer.dll',
        'Magick.NET.Core.dll',
        'Magick.NET-Q8-AnyCPU.dll',
        'ICSharpCode.SharpZipLib.dll',
        'Magick.Native-Q8-x64.dll'
    )) {
        Require-File (Join-Path $script:UmtDependencyDirectory $dependency) "UMT dependency $dependency"
    }

    $script:AurieRoot = Resolve-RequiredDirectory $AurieRoot 'AurieRoot'
    $script:AurieIncludeRoot = Join-Path $script:AurieRoot 'Aurie/TestModule/source'
    $script:YYToolkitIncludeRoot = Join-Path $script:AurieRoot 'YYToolkit/ExamplePlugin/include'
    foreach ($prerequisite in @(
        @{ Path = 'Aurie/Aurie/Aurie.sln'; Name = 'Aurie solution' },
        @{ Path = 'Aurie/Aurie/Aurie/AurieCore.vcxproj'; Name = 'AurieCore project' },
        @{ Path = 'Aurie/TestModule/source/Aurie/shared.hpp'; Name = 'Aurie module headers' },
        @{ Path = 'YYToolkit/YYToolkit.sln'; Name = 'YYToolkit solution' },
        @{ Path = 'YYToolkit/ExamplePlugin/include/YYToolkit/YYTK_Shared.hpp'; Name = 'YYToolkit headers' },
        @{ Path = 'SoulModPlugin/tests/RuntimeOperationsTests.vcxproj'; Name = 'native proof project' },
        @{ Path = 'SoulModPlugin/tests/RuntimeOperationsTests.cpp'; Name = 'native proof source' }
    )) {
        Require-File (Join-Path $script:AurieRoot $prerequisite.Path) $prerequisite.Name
    }
}

function Remove-Artifact([string] $Path) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force -Recurse
    }
}

function Require-FreshArtifact([string] $Path, [string] $Name) {
    Require-File $Path $Name
    $artifact = Get-Item -LiteralPath $Path
    if ($artifact.LastWriteTimeUtc -lt $script:RunStartedUtc) {
        Fail 'STALE_ARTIFACT' "$Name was not produced during this run: $Path"
    }
}

function Publish-Worker {
    $outputDirectory = Join-Path $script:RepoRoot 'artifacts/umtdiff/win-x64'
    Remove-Artifact $outputDirectory
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $workerProject = Join-Path $script:RepoRoot 'converter/SoulMod.UmtWorker/SoulMod.UmtWorker.csproj'
    Invoke-Tool $script:Dotnet @(
        'publish', $workerProject, '--configuration', $Configuration, '--runtime', 'win-x64',
        '--self-contained', 'true', '--output', $outputDirectory,
        "-p:UmtWorkerDependencyDirectory=$script:UmtDependencyDirectory", '--nologo'
    ) 'publish .NET 10 UMT worker' | Out-Null
    Require-FreshArtifact (Join-Path $outputDirectory 'SoulMod.UmtWorker.exe') 'published SoulMod.UmtWorker.exe'
    Require-FreshArtifact (Join-Path $outputDirectory 'SoulMod.UmtWorker.dll') 'published SoulMod.UmtWorker.dll'
    $script:WorkerExecutable = Join-Path $outputDirectory 'SoulMod.UmtWorker.exe'
}

function Build-Converter {
    $solution = Join-Path $script:RepoRoot 'converter/SoulMod.Converter.sln'
    Invoke-Tool $script:Dotnet @('build', $solution, '--configuration', $Configuration, '--nologo') '.NET 8 converter solution' | Out-Null
    $hadWorkerEnvironment = Test-Path Env:SOULMOD_UMT_WORKER_PATH
    $previousWorkerEnvironment = $env:SOULMOD_UMT_WORKER_PATH
    try {
        $env:SOULMOD_UMT_WORKER_PATH = $script:WorkerExecutable
        Invoke-Tool $script:Dotnet @(
            'test', $solution, '--configuration', $Configuration, '--no-build',
            "-p:SoulModUmtWorkerPath=$script:WorkerExecutable", '--nologo'
        ) '.NET 8 converter tests' | Out-Null
    } finally {
        if ($hadWorkerEnvironment) {
            $env:SOULMOD_UMT_WORKER_PATH = $previousWorkerEnvironment
        } else {
            Remove-Item Env:SOULMOD_UMT_WORKER_PATH -ErrorAction SilentlyContinue
        }
    }
}

function Build-Runtime {
    $sourceDirectory = Join-Path $script:RepoRoot 'runtime'
    $buildDirectory = Join-Path $sourceDirectory 'build'
    Remove-Artifact $buildDirectory
    $runtimeArtifact = Join-Path $buildDirectory "SoulMod.Runtime/$Configuration/SoulMod.Runtime.dll"
    Remove-Artifact $runtimeArtifact
    Invoke-Tool $script:CMake @(
        '-S', $sourceDirectory, '-B', $buildDirectory, '-A', 'x64',
        "-DSOULMOD_AURIE_ROOT=$script:AurieIncludeRoot",
        "-DSOULMOD_YYTOOLKIT_ROOT=$script:YYToolkitIncludeRoot"
    ) 'configure CMake x64 runtime' | Out-Null
    Invoke-Tool $script:CMake @('--build', $buildDirectory, '--config', $Configuration, '--parallel') 'build CMake x64 runtime' | Out-Null
    Require-FreshArtifact $runtimeArtifact 'CMake runtime DLL'
    Invoke-Tool $script:CMake @('--test-dir', $buildDirectory, '--build-config', $Configuration, '--output-on-failure') 'run promoted CTest contracts' | Out-Null
    $identityPath = Join-Path $buildDirectory "SoulMod.Runtime/SoulMod.Runtime.build-identity-$Configuration.txt"
    Require-FreshArtifact $identityPath 'CMake runtime build identity'
    $identity = Get-Content -LiteralPath $identityPath -Raw
    $identityBinary = ([Regex]::Match($identity, '(?m)^binary=(.+)$')).Groups[1].Value.Trim().Replace('/', '\\')
    if ([string]::IsNullOrWhiteSpace($identityBinary) -or
        -not ([string]::Equals((Resolve-Path -LiteralPath $identityBinary).Path, (Resolve-Path -LiteralPath $runtimeArtifact).Path, [StringComparison]::OrdinalIgnoreCase))) {
        Fail 'BUILD_IDENTITY_MISMATCH' "CMake identity does not name the produced runtime DLL: $identityPath"
    }
    if ($identity -notmatch 'target=SoulMod.Runtime' -or $identity -notmatch 'core_objects=' -or
        $identity -notmatch 'sources=' -or $identity -match 'PocHook.cpp|SoulModEngine.cpp|HookRegistry.cpp') {
        Fail 'BUILD_IDENTITY_MISMATCH' "CMake identity is missing target provenance: $identityPath"
    }
    $runtimeHash = (Get-FileHash -LiteralPath $runtimeArtifact -Algorithm SHA256).Hash
    if ([string]::IsNullOrWhiteSpace($runtimeHash)) {
        Fail 'BUILD_IDENTITY_MISMATCH' "Could not compute SHA-256 for produced runtime DLL: $runtimeArtifact"
    }
    Set-Content -LiteralPath "$runtimeArtifact.identity.sha256" -Value $runtimeHash -NoNewline
    Require-FreshArtifact "$runtimeArtifact.identity.sha256" 'runtime DLL identity hash'
}

function Run-NativeProof {
    $project = Join-Path $script:AurieRoot 'SoulModPlugin/tests/RuntimeOperationsTests.vcxproj'
    $proofDirectory = Join-Path $script:AurieRoot "SoulModPlugin/tests/x64/$Configuration"
    $proofExecutable = Join-Path $proofDirectory 'RuntimeOperationsTests.exe'
    Remove-Artifact $proofExecutable
    Invoke-Tool $script:MsBuild @($project, "/p:Configuration=$Configuration", '/p:Platform=x64', '/m') 'build native proof tests' | Out-Null
    Require-FreshArtifact $proofExecutable 'native proof executable'
    $packageDirectory = Join-Path $script:RepoRoot 'mods/runtime-proof'
    Invoke-Tool $proofExecutable @($packageDirectory) 'run native proof tests' | Out-Null
}

try {
    if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = Join-Path $PSScriptRoot '..' }
    $resolvedRepoRoot = Resolve-Path -LiteralPath $RepoRoot -ErrorAction SilentlyContinue
    if ($null -eq $resolvedRepoRoot) { Fail 'CONFIGURATION' "RepoRoot does not exist: $RepoRoot" }
    $script:RepoRoot = $resolvedRepoRoot.Path
    if ([string]::IsNullOrWhiteSpace($UmtDependencyDirectory)) { $UmtDependencyDirectory = Join-Path $script:RepoRoot 'scratch/lib' }
    if ([string]::IsNullOrWhiteSpace($AurieRoot)) { $AurieRoot = Join-Path $script:RepoRoot 'scratch/AurieFramework' }
    Test-Prerequisites
    Write-Host "[PASS] prerequisites validated for $script:RepoRoot"
    if ($PrerequisitesOnly) { exit 0 }
    Publish-Worker
    Build-Converter
    Build-Runtime
    Run-NativeProof
    Write-Host '[PASS] Windows bootstrap/build/test entrypoint completed.'
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
