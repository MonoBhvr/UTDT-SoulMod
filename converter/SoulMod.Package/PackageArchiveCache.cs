namespace SoulMod.Package;

/// <summary>Atomically materializes verified ZIP packages into cache-owned immutable roots.</summary>
public sealed class PackageArchiveCache
{
    private readonly string _cacheRoot;
    private readonly Action<string, string> _move;

    public PackageArchiveCache(string cacheRoot) : this(cacheRoot, Directory.Move) { }

    internal PackageArchiveCache(string cacheRoot, Action<string, string> move)
    {
        _cacheRoot = cacheRoot;
        _move = move;
    }

    public PackageCacheOutcome Resolve(string archivePath, CancellationToken cancellationToken = default)
    {
        string staging = string.Empty;
        try
        {
            string cacheRoot = PackageCacheIO.NormalizeRoot(_cacheRoot);
            VerifiedPackageArchive archive = PackageArchive.Read(archivePath);
            EnsureStableArchive(archivePath, archive.ArchiveSha256);
            string key = $"v1-{archive.ArchiveSha256}";
            string target = Path.Combine(cacheRoot, key);
            var expected = new CacheMarker(key, archive.ArchiveSha256, archive.ContentSha256);
            PackageCacheIO.RecoverOwnedBackup(target, expected);

            bool rebuilt = false;
            if (Directory.Exists(target))
            {
                if (!PackageCacheIO.TryReadResolvedMarker(target, out CacheMarker marker))
                    return Failed(PackageCacheFailureReason.CacheRootUnowned, $"Cache root is not owned by SoulMod: {target}");
                if (marker == expected && PackageArchive.MatchesExtractedRoot(target, archive, PackageCacheIO.ResolvedMarkerName))
                    return new PackageCacheOutcome.Resolved(new ResolvedPackageRoot(target, key, archive.ArchiveSha256, archive.Document), PackageCacheDisposition.Reused);
                PackageCacheIO.DeleteTree(target);
                rebuilt = true;
            }

            PackageCacheIO.RemoveOwnedStaging(target);
            staging = Path.Combine(cacheRoot, $".{key}.soulmod-cache-staging-{PackageCacheIO.NewRunId()}");
            Directory.CreateDirectory(staging);
            PackageCacheIO.WriteStagingMarker(staging, target, key);
            VerifiedPackageArchive extracted = PackageArchive.Extract(archivePath, staging, cancellationToken);
            EnsureSameArchive(archive, extracted);
            EnsureStableArchive(archivePath, archive.ArchiveSha256);
            File.Delete(Path.Combine(staging, PackageCacheIO.StagingMarkerName));
            PackageCacheIO.WriteResolvedMarker(staging, expected);
            PackageCacheIO.SealReadOnly(staging);
            PackageCacheIO.Promote(staging, target, _move);
            staging = string.Empty;

            if (!PackageArchive.MatchesExtractedRoot(target, archive, PackageCacheIO.ResolvedMarkerName))
            {
                PackageCacheIO.DeleteTree(target);
                return Failed(PackageCacheFailureReason.CacheIntegrityFailed, $"Promoted cache root failed verification: {target}");
            }

            PackageCacheDisposition disposition = rebuilt ? PackageCacheDisposition.Rebuilt : PackageCacheDisposition.Extracted;
            return new PackageCacheOutcome.Resolved(new ResolvedPackageRoot(target, key, archive.ArchiveSha256, archive.Document), disposition);
        }
        catch (OperationCanceledException)
        {
            RemoveStaging(staging);
            return Failed(PackageCacheFailureReason.Cancelled, "Package extraction was cancelled.");
        }
        catch (ArchiveStructureException error)
        {
            RemoveStaging(staging);
            return Failed(PackageCacheFailureReason.InvalidArchive, error.Message);
        }
        catch (InvalidDataException error)
        {
            RemoveStaging(staging);
            return Failed(PackageCacheFailureReason.InvalidPackage, error.Message);
        }
        catch (IOException error)
        {
            RemoveStaging(staging);
            return Failed(PackageCacheFailureReason.ExtractionFailed, error.Message);
        }
        catch (UnauthorizedAccessException error)
        {
            RemoveStaging(staging);
            return Failed(PackageCacheFailureReason.ExtractionFailed, error.Message);
        }
    }

    public PackageCacheCleanupOutcome Clean(ResolvedPackageRoot root)
    {
        try
        {
            string cacheRoot = PackageCacheIO.NormalizeRoot(_cacheRoot);
            string target = Path.GetFullPath(root.Path);
            if (!target.StartsWith(cacheRoot + Path.DirectorySeparatorChar, PathComparison))
                return CleanupFailed(PackageCacheFailureReason.CacheRootInvalid, $"Cache root escapes the configured cache: {target}");
            PackageCacheIO.RemoveOwnedStaging(target);
            if (!Directory.Exists(target))
                return new PackageCacheCleanupOutcome.Cleaned(new PackageCacheCleanupReceipt(target, false));
            if (!PackageCacheIO.TryReadResolvedMarker(target, out CacheMarker marker) || marker.CacheKey != root.CacheKey)
                return CleanupFailed(PackageCacheFailureReason.CacheRootUnowned, $"Cache root is not owned by this resolved package: {target}");
            PackageCacheIO.DeleteTree(target);
            return new PackageCacheCleanupOutcome.Cleaned(new PackageCacheCleanupReceipt(target, true));
        }
        catch (InvalidDataException error) { return CleanupFailed(PackageCacheFailureReason.CacheRootInvalid, error.Message); }
        catch (IOException error) { return CleanupFailed(PackageCacheFailureReason.ExtractionFailed, error.Message); }
        catch (UnauthorizedAccessException error) { return CleanupFailed(PackageCacheFailureReason.ExtractionFailed, error.Message); }
    }

    private static void EnsureStableArchive(string path, string expectedSha256)
    {
        if (!PackageArchive.Sha256File(path).Equals(expectedSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Package archive changed during validation or extraction.");
    }

    private static void EnsureSameArchive(VerifiedPackageArchive expected, VerifiedPackageArchive actual)
    {
        if (expected.ArchiveSha256 != actual.ArchiveSha256 || expected.ContentSha256 != actual.ContentSha256)
            throw new InvalidDataException("Package archive changed before extraction completed.");
    }

    private static void RemoveStaging(string staging)
    {
        if (staging.Length != 0 && Directory.Exists(staging))
            PackageCacheIO.DeleteTree(staging);
    }

    private static StringComparison PathComparison =>
        OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

    private static PackageCacheOutcome.Failed Failed(PackageCacheFailureReason reason, string message) => new(new PackageCacheFailure(reason, message));
    private static PackageCacheCleanupOutcome.Failed CleanupFailed(PackageCacheFailureReason reason, string message) => new(new PackageCacheFailure(reason, message));
}
