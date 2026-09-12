[CmdletBinding()]
param(
    [string] $RepoRoot,
    [string] $ManifestPath
)

# Executes tools/journal_cleanup_manifest.json: removes only listed disposable
# revert paths, hard-aborts (deleting nothing) when a revert path overlaps a
# retained path, a retained path is missing, or a path is unsafe to interpret.
# POSIX-rooted (/...) revert paths are outside Windows reach here; they are
# reported as deferred so the caller verifies them on the native filesystem.

$ErrorActionPreference = 'Stop'

function Fail([string] $Code, [string] $Message) {
    throw [InvalidOperationException]::new("ERROR[$Code] $Message")
}

function Write-Receipt([string] $Status, [string] $Path) {
    Write-Output "RECEIPT|$Status|$Path"
}

function Test-Contained([string] $Child, [string] $Parent) {
    $child = $Child.TrimEnd('\')
    $parent = $Parent.TrimEnd('\')
    if ($child -eq $parent) {
        return $true
    }
    return $child.StartsWith($parent + '\', [StringComparison]::OrdinalIgnoreCase)
}

# Returns $null when the path is not a safe repo-relative Windows path.
function Convert-ToRepoRelative([string] $Path) {
    if ($Path.StartsWith('/')) {
        return $null
    }
    $normalized = $Path -replace '/', '\'
    if ([System.IO.Path]::IsPathRooted($normalized)) {
        return $null
    }
    $segments = $normalized -split '\\' | Where-Object { $_ -and $_ -ne '.' }
    if (-not $segments -or $segments -contains '..') {
        return $null
    }
    return ($segments -join '\')
}

function Split-ManifestPath([string] $Path, [string] $Kind, [string] $Entry) {
    if ($Path.StartsWith('/')) {
        if ($Path -notmatch '^[A-Za-z0-9._/-]+$') {
            Fail 'MANIFEST_PATH' "Unsafe POSIX path in manifest $Kind entry: $Entry"
        }
        return @{ Posix = $true; Rel = $null; Raw = $Path }
    }
    $rel = Convert-ToRepoRelative $Path
    if ($null -eq $rel) {
        Fail 'MANIFEST_PATH' "Path must be repo-relative or POSIX /tmp-rooted: $Entry"
    }
    return @{ Posix = $false; Rel = $rel; Raw = $Path }
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $RepoRoot 'tools/journal_cleanup_manifest.json'
}
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
$ManifestPath = (Resolve-Path -LiteralPath $ManifestPath -ErrorAction Stop).Path

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    Fail 'MANIFEST_SCHEMA' "Unsupported manifest schemaVersion: $($manifest.schemaVersion)"
}

$retainEntries = @()
foreach ($item in @($manifest.retain)) {
    $retainEntries += ,(Split-ManifestPath $item.path 'retain' $item.path)
}
$revertEntries = @()
foreach ($item in @($manifest.revert)) {
    $revertEntries += ,(Split-ManifestPath $item.path 'revert' $item.path)
}

# Protection guard: every retained repo-relative path must exist before any deletion.
foreach ($entry in $retainEntries | Where-Object { -not $_.Posix }) {
    $full = Join-Path $RepoRoot $entry.Rel
    if (-not (Test-Path -LiteralPath $full)) {
        Write-Output "PROTECTION-ABORT|retain-missing|$($entry.Rel)"
        exit 2
    }
}

# Protection guard: revert paths must not overlap retained paths in either direction.
foreach ($revert in $revertEntries) {
    if ($revert.Posix) {
        continue
    }
    foreach ($retain in $retainEntries) {
        if ($retain.Posix) {
            continue
        }
        if ((Test-Contained $revert.Rel $retain.Rel) -or (Test-Contained $retain.Rel $revert.Rel)) {
            Write-Output "PROTECTION-ABORT|revert-overlaps-retain|$($revert.Rel) <-> $($retain.Rel)"
            exit 2
        }
    }
}

foreach ($revert in $revertEntries) {
    if ($revert.Posix) {
        Write-Receipt 'posix-deferred-to-caller' $revert.Raw
        continue
    }
    $full = Join-Path $RepoRoot $revert.Rel
    if (-not $full.StartsWith($RepoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        Write-Output "PROTECTION-ABORT|revert-outside-repo|$($revert.Rel)"
        exit 2
    }
    if (-not (Test-Path -LiteralPath $full)) {
        Write-Receipt 'already-absent' $revert.Rel
        continue
    }
    try {
        Remove-Item -LiteralPath $full -Recurse -Force
    } catch {
        $escaped = '\\?\' + [System.IO.Path]::GetFullPath($full)
        if (Test-Path -LiteralPath $escaped -PathType Container) {
            [System.IO.Directory]::Delete($escaped, $true)
        } else {
            [System.IO.File]::Delete($escaped)
        }
    }
    if (Test-Path -LiteralPath $full) {
        Write-Output "PROTECTION-ABORT|delete-failed|$($revert.Rel)"
        exit 2
    }
    Write-Receipt 'removed' $revert.Rel
}

# Post-deletion verification: retained paths must still exist.
foreach ($entry in $retainEntries | Where-Object { -not $_.Posix }) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $entry.Rel))) {
        Write-Output "POST-FAIL|retain-missing-after-cleanup|$($entry.Rel)"
        exit 3
    }
    Write-Output "RETAIN-OK|$($entry.Rel)"
}

Write-Output "CLEANUP-COMPLETE|repo-root=$RepoRoot"
