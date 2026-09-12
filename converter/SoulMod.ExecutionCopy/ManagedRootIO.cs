using System.Security.Cryptography;
using System.Text.Json;

namespace SoulMod.ExecutionCopy;

/// <summary>
/// Filesystem and manifest IO for <see cref="ExecutionCopyManager"/>. Kept separate so the
/// manager file owns the boundary contract and this file owns the mechanical operations.
/// Every method here is best-effort or typed-failure at the boundary; none mutate the source.
/// </summary>
internal static class ManagedRootIO
{
    private const string Owner = "SoulMod.ExecutionCopyManager";

    /// <summary>
    /// Recursively copies <paramref name="sourceRoot"/> into <paramref name="destRoot"/>, rejecting
    /// any file or directory reparse point (symlink) with <see cref="SymlinkInSourceException"/> so
    /// the boundary reports a typed SymlinkRejected, not a misleading CopyFailed.
    /// </summary>
    public static void CopyTree(string sourceRoot, string destRoot, CancellationToken cancellationToken)
    {
        var source = new DirectoryInfo(sourceRoot);
        foreach (FileSystemInfo entry in source.EnumerateFileSystemInfos("*", SearchOption.TopDirectoryOnly))
        {
            cancellationToken.ThrowIfCancellationRequested();
            string destEntry = Path.Combine(destRoot, entry.Name);

            if (PathContainment.IsSymbolicLink(entry.FullName))
                throw new SymlinkInSourceException(entry.FullName);

            if (entry is DirectoryInfo dir)
            {
                Directory.CreateDirectory(destEntry);
                CopyTree(dir.FullName, destEntry, cancellationToken);
            }
            else if (entry is FileInfo file)
            {
                file.CopyTo(destEntry, overwrite: false);
            }
        }
    }

    /// <summary>
    /// Writes an ownership marker into a freshly created staging directory, bound to the specific
    /// managed target this staging tree is intended to promote into. Cleanup only deletes a sibling
    /// staging tree whose marker resolves to the target being cleaned, so arbitrary user-created
    /// prefix-matching directories are preserved.
    /// </summary>
    public static void WriteStagingMarker(string stagingRoot, string markerFileName, string intendedManagedRoot, string runId)
    {
        var document = new { ownedBy = Owner, kind = "staging", intendedManagedRoot, runId };
        File.WriteAllText(Path.Combine(stagingRoot, markerFileName), JsonSerializer.Serialize(document));
    }

    /// <summary>
    /// Removes sibling staging directories whose ownership marker is a valid SoulMod staging marker
    /// bound to <paramref name="normalizedTarget"/>. Siblings without a valid bound marker are
    /// preserved (they may be user data or belong to another run/target). Returns the count removed.
    /// </summary>
    public static int RemoveSiblingStagingTrees(string normalizedTarget, string stagingPrefix, string markerFileName)
    {
        string? parent = Directory.GetParent(normalizedTarget)?.FullName;
        if (parent is null || !Directory.Exists(parent))
            return 0;

        int removed = 0;
        foreach (string sibling in Directory.EnumerateDirectories(parent, stagingPrefix + "*"))
        {
            if (!StagingBelongsToTarget(sibling, markerFileName, normalizedTarget))
                continue;
            try { Directory.Delete(sibling, recursive: true); removed++; }
            catch (IOException) { /* best-effort; an unremovable owned staging surfaces again next run */ }
        }
        return removed;
    }

    /// <summary>True if <paramref name="stagingRoot"/> carries a SoulMod staging marker bound to <paramref name="normalizedTarget"/>.</summary>
    public static bool StagingBelongsToTarget(string stagingRoot, string markerFileName, string normalizedTarget)
    {
        string markerPath = Path.Combine(stagingRoot, markerFileName);
        if (!File.Exists(markerPath))
            return false;
        try
        {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(markerPath));
            return doc.RootElement.TryGetProperty("ownedBy", out JsonElement owner)
                && owner.ValueKind == JsonValueKind.String
                && owner.GetString() == Owner
                && doc.RootElement.TryGetProperty("intendedManagedRoot", out JsonElement intended)
                && intended.ValueKind == JsonValueKind.String
                && string.Equals(intended.GetString(), normalizedTarget, PathContainment.PathComparison);
        }
        catch (JsonException)
        {
            return false;
        }
    }

    /// <summary>True if <paramref name="managedRoot"/> carries a SoulMod copy manifest (owned by us).</summary>
    public static bool OwnsManifest(string managedRoot, string manifestFileName)
    {
        string manifestPath = Path.Combine(managedRoot, manifestFileName);
        if (!File.Exists(manifestPath))
            return false;
        try
        {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(manifestPath));
            return doc.RootElement.TryGetProperty("ownedBy", out JsonElement owner)
                && owner.ValueKind == JsonValueKind.String
                && owner.GetString() == Owner;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    /// <summary>
    /// Promotes <paramref name="stagingRoot"/> to <paramref name="normalizedTarget"/> with no
    /// delete-before-success window: an existing target is moved aside to a backup, the staging is
    /// promoted via <paramref name="promote"/>, and on failure the backup is moved back. The prior
    /// owned root is only deleted after the new root is durably in place.
    /// </summary>
    public static void PromoteWithRollback(string stagingRoot, string normalizedTarget, string runId, Action<string, string> promote)
    {
        string backup = $"{normalizedTarget}.soulmod-rollback-{runId}";
        bool movedToBackup = false;
        if (Directory.Exists(normalizedTarget))
        {
            Directory.Move(normalizedTarget, backup);
            movedToBackup = true;
        }

        try
        {
            promote(stagingRoot, normalizedTarget);
        }
        catch (Exception)
        {
            if (movedToBackup && Directory.Exists(backup))
            {
                try { Directory.Move(backup, normalizedTarget); }
                catch (IOException) { /* rollback restore failed; surface original failure below */ }
            }
            throw;
        }

        if (movedToBackup && Directory.Exists(backup))
        {
            try { Directory.Delete(backup, recursive: true); }
            catch (IOException) { /* best-effort; leftover backup is not a contract violation */ }
        }
    }

    public static void WriteManifest(
        string managedRoot,
        string manifestFileName,
        GameInstall install,
        InstallIntegrity before,
        InstallIntegrity after,
        InstallIntegrity copyIntegrity,
        string persistedManifestSha256,
        string runId)
    {
        var document = new
        {
            ownedBy = Owner,
            runId,
            formatVersion = 2,
            source = new
            {
                root = install.RootPath,
                executable = install.ExecutableRelativePath,
                dataWin = install.DataWinRelativePath,
            },
            sourceIntegrityBefore = SerializeIntegrity(before),
            sourceIntegrityAfter = SerializeIntegrity(after),
            copyIntegrity = SerializeIntegrity(copyIntegrity),
            persistedDirectoryManifestSha256 = persistedManifestSha256,
        };
        File.WriteAllText(Path.Combine(managedRoot, manifestFileName), JsonSerializer.Serialize(document));
    }

    public static void TryDelete(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); }
        catch (IOException) { /* best-effort during error recovery */ }
    }

    public static string GenerateRunId() =>
        Convert.ToHexString(RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();

    private static object SerializeIntegrity(InstallIntegrity integrity) => new
    {
        executable = new { path = integrity.Executable.Path, sha256 = integrity.Executable.Sha256 },
        dataWin = new { path = integrity.DataWin.Path, sha256 = integrity.DataWin.Sha256 },
        directoryManifestSha256 = integrity.DirectoryManifestSha256,
    };
}
