"""Pure path-classification policy for the SoulMod repository hygiene tool.

Defines the retention :class:`Category` enum and the pure :func:`classify`
function. No filesystem I/O, no side effects -- the entire policy is a function
of the path string. Kept separate so the test suite and future rule edits have
one focused seam.

The category policy encodes the commit/ignore boundaries established in
``.omo/plans/soulmod-platform-roadmap.md`` (Wave 1, checkbox 1).
"""

from __future__ import annotations

import enum
import os
from dataclasses import dataclass

RETENTION_TRACK = "track"
RETENTION_IGNORE = "ignore"


class Category(enum.Enum):
    """Path retention category. Retention is either ``track`` or ``ignore``."""

    # --- track ---
    SOURCE = (RETENTION_TRACK, "product source (.cs/.cpp/.hpp/.h/.cmake/.py/.csx/.csproj/.sln/CMakeLists.txt)")
    SCHEMA = (RETENTION_TRACK, "shared JSON schema definitions")
    MOD_SOURCE = (RETENTION_TRACK, "declarative mod package sources and mod assets")
    FIXTURE = (RETENTION_TRACK, "test fixtures, golden, integration scaffolding")
    DOC = (RETENTION_TRACK, "documentation and analysis")
    LICENSE = (RETENTION_TRACK, "license, notice, and third-party attribution")
    CONFIG = (RETENTION_TRACK, "repository boundary config (.gitattributes, .gitignore)")
    TOOL_SCRIPT = (RETENTION_TRACK, "repository hygiene tooling shipped in tools/")
    ORACLE = (RETENTION_TRACK, "tracked scratch runtime oracle source and tests pending parity")

    # --- ignore ---
    GENERATED_BUILD = (RETENTION_IGNORE, "generated build trees (runtime/build, **/bin, **/obj)")
    GENERATED_PUBLISH = (RETENTION_IGNORE, "published self-contained worker artifacts (artifacts/)")
    GAME_MATERIAL = (RETENTION_IGNORE, "user-owned/licensed game copy (never commit copyrighted data)")
    SCRATCH = (RETENTION_IGNORE, "scratch/POC area incl. vendored third-party framework clones")
    PACKAGE = (RETENTION_IGNORE, "generated canonical .soulmod package archives")
    THIRD_PARTY_TOOL = (RETENTION_IGNORE, "third-party binary tools provisioned by the build/bootstrap")
    THIRD_PARTY_SCRIPT = (RETENTION_IGNORE, "unattributed third-party reference scripts duplicated in scratch")
    SCRATCH_DATA = (RETENTION_IGNORE, "unreferenced scratch extraction data")
    LOG = (RETENTION_IGNORE, "runtime and build logs")
    OMO_STATE = (RETENTION_IGNORE, ".omo orchestrator state (local agent state)")
    DEBUG_JOURNAL = (RETENTION_IGNORE, "local debug ledger (.debug-journal.md)")
    EMPTY_STUB = (RETENTION_IGNORE, "accidental empty stub file at repo root")
    OS_JUNK = (RETENTION_IGNORE, "operating-system junk (Thumbs.db, .DS_Store, ...)")
    IDE = (RETENTION_IGNORE, "editor-private state (.vs/, .vscode/, *.user, ...)")
    VCS_STATE = (RETENTION_IGNORE, "git internal state (.git/)")
    UNCLASSIFIED = (RETENTION_IGNORE, "no rule matched; requires explicit human classification")

    def __init__(self, retention: str, description: str) -> None:
        self.retention = retention
        self.description = description

    @property
    def name_for_report(self) -> str:
        return self.name.lower()


# Extensions recognised as product source anywhere in the tree.
_TEXT_SOURCE_EXTS = frozenset({
    ".cs", ".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh", ".hxx",
    ".cmake", ".csproj", ".sln", ".py", ".csx", ".sh", ".ps1",
})


@dataclass(frozen=True)
class PathRule:
    """A single classification decision for a relative path."""

    rel_path: str
    category: Category
    size: int


def _split_segments(rel_path: str) -> list[str]:
    """Normalise a repository-relative path to forward-slash segments.

    Only a single leading ``./`` and leading slashes are stripped; leading dots
    that are part of a name (``.git``, ``.omo``, ``.gitattributes``) are kept.
    """
    norm = rel_path.replace(os.sep, "/")
    if norm.startswith("./"):
        norm = norm[2:]
    norm = norm.lstrip("/")
    return [seg for seg in norm.split("/") if seg]


def classify(rel_path: str) -> Category:
    """Return the retention :class:`Category` for a repository-relative path.

    Pure: inspects only the path string. File-type rules with the highest
    reporting signal (``.soulmod``, ``.log``) are evaluated before container
    rules, and container rules before generic extension rules, so a path is
    labelled by its most specific characteristic.
    """
    segs = _split_segments(rel_path)
    if not segs:
        return Category.UNCLASSIFIED

    leaf = segs[-1]

    # ----- VCS / orchestrator state (absolute precedence) -----
    if segs[0] == ".git":
        return Category.VCS_STATE
    if segs[0] == ".omo":
        return Category.OMO_STATE

    if segs[:5] == ["scratch", "AurieFramework", "SoulModPlugin", "tests", "x64"]:
        return Category.GENERATED_BUILD
    if segs[:4] == ["scratch", "AurieFramework", "SoulModPlugin", "x64"]:
        return Category.GENERATED_BUILD

    oracle_prefix = ["scratch", "AurieFramework", "SoulModPlugin"]
    if segs[:3] == oracle_prefix:
        if len(segs) == 4 and segs[3] == "SoulModPlugin.vcxproj":
            return Category.ORACLE
        if len(segs) >= 5 and segs[3] == "source":
            return Category.ORACLE
        if len(segs) == 5 and segs[3] == "tests" and segs[4] in {
            "RuntimeOperationsTests.cpp", "RuntimeOperationsTests.vcxproj",
        }:
            return Category.ORACLE

    # ----- High-signal file types anywhere in the tree -----
    if leaf.endswith(".soulmod"):
        return Category.PACKAGE
    if leaf.endswith(".log"):
        return Category.LOG
    if leaf.endswith(".pyc") or leaf.endswith(".pyo"):
        return Category.GENERATED_BUILD

    # ----- Generated build / publish subtrees (directory-level) -----
    if segs[0] == "runtime" and len(segs) > 1 and segs[1] == "build":
        return Category.GENERATED_BUILD
    if segs[0] == "artifacts":
        return Category.GENERATED_PUBLISH
    for seg in segs:
        if seg in {"bin", "obj", "__pycache__"}:
            return Category.GENERATED_BUILD

    # ----- User-owned game material -----
    if len(segs) >= 2 and segs[0] == "scratch" and segs[1] == "test_game":
        return Category.GAME_MATERIAL

    # ----- Scratch / POC / vendored third-party clones -----
    if segs[0] == "scratch":
        return Category.SCRATCH

    # ----- Editor / OS junk anywhere -----
    if leaf in {".DS_Store", "Thumbs.db", "Desktop.ini", "ehthumbs.db"}:
        return Category.OS_JUNK
    if leaf.endswith(".user") or leaf.endswith(".suo"):
        return Category.IDE
    for seg in segs:
        if seg in {".vs", ".vscode", ".idea", ".cache"}:
            return Category.IDE

    # ----- Third-party binary tools (tools/*.exe, tools/*.zip) -----
    if segs[0] == "tools" and len(segs) == 2 and (leaf.endswith(".exe") or leaf.endswith(".zip")):
        return Category.THIRD_PARTY_TOOL

    # ----- Repo-root specific files -----
    if len(segs) == 1:
        if leaf == ".debug-journal.md":
            return Category.DEBUG_JOURNAL
        if leaf == "dotnet":
            return Category.EMPTY_STUB
        if leaf in {".gitattributes", ".gitignore"}:
            return Category.CONFIG
        if leaf in {"README.md", "CHANGELOG.md"}:
            return Category.DOC
        if leaf in {"LICENSE", "LICENSE.md", "LICENSE.txt", "NOTICE", "NOTICE.md", "THIRD_PARTY_NOTICES.md"}:
            return Category.LICENSE

    if segs[0] == ".github":
        return Category.CONFIG

    # ----- Unattributed third-party reference scripts (duplicated in scratch) -----
    if segs[0] == "CommunityScripts":
        return Category.THIRD_PARTY_SCRIPT

    # ----- Tools scratch extraction data -----
    if segs[0] == "tools" and leaf == "ko_ch2.txt":
        return Category.SCRATCH_DATA

    # ----- Documentation trees -----
    if segs[0] in {"docs", "analysis", "process"}:
        return Category.DOC

    # ----- Shared schema -----
    if segs[0] == "shared" and len(segs) > 1 and segs[1] == "schema":
        return Category.SCHEMA

    # ----- Test fixtures -----
    if segs[0] == "tests":
        return Category.FIXTURE

    # ----- Declarative mod sources and assets -----
    if segs[0] == "mods":
        return Category.MOD_SOURCE

    # ----- Hygiene tooling shipped with the repo (the repo_hygiene* family) -----
    if segs[0] == "tools" and leaf.startswith("repo_hygiene") and leaf.endswith(".py"):
        return Category.TOOL_SCRIPT
    if segs == ["tools", "journal_cleanup_manifest.json"]:
        return Category.TOOL_SCRIPT

    # ----- Product source by name / extension -----
    if leaf == "CMakeLists.txt":
        return Category.SOURCE
    _, ext = os.path.splitext(leaf)
    if ext.lower() in _TEXT_SOURCE_EXTS:
        return Category.SOURCE

    # Anything else is flagged for explicit human classification.
    return Category.UNCLASSIFIED
