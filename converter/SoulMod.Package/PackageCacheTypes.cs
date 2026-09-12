namespace SoulMod.Package;

public enum PackageCacheFailureReason
{
    InvalidArchive,
    InvalidPackage,
    CacheRootInvalid,
    CacheRootUnowned,
    CacheIntegrityFailed,
    Cancelled,
    ExtractionFailed,
}

public sealed record PackageCacheFailure(PackageCacheFailureReason Reason, string Message);

public enum PackageCacheDisposition
{
    Reused,
    Extracted,
    Rebuilt,
}

/// <summary>
/// Immutable, cache-owned package root. Runtime callers receive this resolved root rather than a
/// source archive or staging path; only <see cref="PackageArchiveCache.Clean"/> may remove it.
/// </summary>
public sealed class ResolvedPackageRoot
{
    internal ResolvedPackageRoot(string path, string cacheKey, string archiveSha256, PackageDocument document)
    {
        Path = path;
        CacheKey = cacheKey;
        ArchiveSha256 = archiveSha256;
        Document = document;
    }

    public string Path { get; }
    public string CacheKey { get; }
    public string ArchiveSha256 { get; }
    public PackageDocument Document { get; }
}

public abstract record PackageCacheOutcome
{
    private PackageCacheOutcome() { }

    public sealed record Resolved(ResolvedPackageRoot Root, PackageCacheDisposition Disposition) : PackageCacheOutcome;
    public sealed record Failed(PackageCacheFailure Failure) : PackageCacheOutcome;
}

public sealed record PackageCacheCleanupReceipt(string Path, bool Removed);

public abstract record PackageCacheCleanupOutcome
{
    private PackageCacheCleanupOutcome() { }

    public sealed record Cleaned(PackageCacheCleanupReceipt Receipt) : PackageCacheCleanupOutcome;
    public sealed record Failed(PackageCacheFailure Failure) : PackageCacheCleanupOutcome;
}
