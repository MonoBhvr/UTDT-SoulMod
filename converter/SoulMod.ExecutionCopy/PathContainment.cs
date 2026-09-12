using System.Security.Cryptography;

namespace SoulMod.ExecutionCopy;

/// <summary>
/// Canonical path containment for the execution-copy boundary. All normalization goes through
/// one funnel so source/target overlap and symlink-escape are caught exactly once. Comparisons
/// are platform-aware: Windows paths are case-insensitive, POSIX paths are case-sensitive.
/// </summary>
internal static class PathContainment
{
    /// <summary>
    /// Path comparison for the current platform. Windows filesystems are case-insensitive
    /// (DELTARUNE.exe == deltarune.exe); POSIX filesystems are case-sensitive. Using the wrong
    /// comparison would let a case-variant root evade the overlap/containment check on Windows.
    /// </summary>
    public static StringComparison PathComparison =>
        OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

    /// <summary>
    /// Resolves a root path to a canonical absolute form without resolving symlinks, preserves
    /// volume/root trailing separators, then asserts the root itself is not a symlink/reparse
    /// point. We intentionally do NOT follow links: a link inside the install pointing outside
    /// would copy data the boundary must not move.
    /// </summary>
    public static (string Normalized, ExecutionCopyFailureReason? Reason) NormalizeRoot(string rootPath)
    {
        if (string.IsNullOrWhiteSpace(rootPath))
            return (string.Empty, ExecutionCopyFailureReason.InvalidPath);

        string normalized;
        try
        {
            // GetFullPath normalizes separators and rejects most malformed input (.., invalid chars).
            normalized = Path.GetFullPath(rootPath);
        }
        catch (Exception)
        {
            return (string.Empty, ExecutionCopyFailureReason.InvalidPath);
        }

        // Trim trailing separators only when the path is NOT a volume/root; trimming "C:\" to "C:"
        // changes its meaning and would break containment/parent derivation on Windows.
        bool isVolumeRoot = string.Equals(normalized, Directory.GetDirectoryRoot(normalized), PathComparison);
        if (!isVolumeRoot)
            normalized = normalized.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

        if (IsSymbolicLink(normalized))
            return (normalized, ExecutionCopyFailureReason.SymlinkRejected);

        return (normalized, null);
    }

    /// <summary>
    /// Resolves a relative child path under an already-normalized root, asserting containment
    /// and rejecting symlinks. Returns a reason if the child escapes (via ..) or is a link.
    /// </summary>
    public static (string Absolute, ExecutionCopyFailureReason? Reason) NormalizeChild(
        string normalizedRoot,
        string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath))
            return (string.Empty, ExecutionCopyFailureReason.InvalidPath);

        string absolute;
        try
        {
            absolute = Path.GetFullPath(Path.Combine(normalizedRoot, relativePath));
        }
        catch (Exception)
        {
            return (string.Empty, ExecutionCopyFailureReason.InvalidPath);
        }

        if (!IsWithin(normalizedRoot, absolute))
            return (absolute, ExecutionCopyFailureReason.InvalidPath);

        if (IsSymbolicLink(absolute))
            return (absolute, ExecutionCopyFailureReason.SymlinkRejected);

        return (absolute, null);
    }

    /// <summary>
    /// Returns the overlap failure reason if source and target roots are equal or nested,
    /// else null. Both inputs must already be normalized. Comparison is platform-aware.
    /// </summary>
    public static ExecutionCopyFailureReason? CheckOverlap(string normalizedSource, string normalizedTarget)
    {
        if (string.Equals(normalizedSource, normalizedTarget, PathComparison))
            return ExecutionCopyFailureReason.SourceTargetOverlap;

        if (IsWithin(normalizedSource, normalizedTarget) || IsWithin(normalizedTarget, normalizedSource))
            return ExecutionCopyFailureReason.SourceTargetOverlap;

        return null;
    }

    /// <summary>True if <paramref name="candidate"/> is the same as or lives inside <paramref name="root"/>.</summary>
    public static bool IsWithin(string root, string candidate)
    {
        string rootWithSeparator = root.EndsWith(Path.DirectorySeparatorChar) || root.EndsWith(Path.AltDirectorySeparatorChar)
            ? root
            : root + Path.DirectorySeparatorChar;
        return candidate.StartsWith(rootWithSeparator, PathComparison);
    }

    /// <summary>
    /// True if the path is a symlink/reparse point. Uses FileAttributes so it works on files
    /// and directories alike without following the link.
    /// </summary>
    public static bool IsSymbolicLink(string path)
    {
        try
        {
            return (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    /// <summary>
    /// SHA-256 of a single file, hex-lowercase. Throws only on genuine I/O failure; callers
    /// convert that to a typed failure at the boundary.
    /// </summary>
    public static string Sha256File(string absolutePath)
    {
        using FileStream stream = File.OpenRead(absolutePath);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }
}
