using System.IO.Compression;
using System.Security.Cryptography;
using SoulMod.Package;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class PackageArchiveCacheTests
{
    [Fact]
    public void Resolve_reuses_the_same_verified_archive_root()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("one");
        PackageArchiveCache cache = new(fixture.CacheRoot);

        PackageCacheOutcome.Resolved first = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));
        PackageCacheOutcome.Resolved second = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));

        Assert.Equal(PackageCacheDisposition.Extracted, first.Disposition);
        Assert.Equal(PackageCacheDisposition.Reused, second.Disposition);
        Assert.Equal(first.Root.Path, second.Root.Path);
        Assert.True(File.Exists(Path.Combine(second.Root.Path, "assets", "global.json")));
    }

    [Fact]
    public void Resolve_invalidates_a_changed_archive_at_the_same_source_path()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("one");
        PackageArchiveCache cache = new(fixture.CacheRoot);
        PackageCacheOutcome.Resolved first = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));

        fixture.CreatePackage("two");
        PackageCacheOutcome.Resolved second = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));

        Assert.NotEqual(first.Root.CacheKey, second.Root.CacheKey);
        Assert.NotEqual(first.Root.Path, second.Root.Path);
        Assert.Equal(PackageCacheDisposition.Extracted, second.Disposition);
    }

    [Fact]
    public void Resolve_rebuilds_a_stale_owned_cache_root()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("original");
        PackageArchiveCache cache = new(fixture.CacheRoot);
        PackageCacheOutcome.Resolved first = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));
        string asset = Path.Combine(first.Root.Path, "assets", "global.json");
        File.SetAttributes(asset, FileAttributes.Normal);
        File.WriteAllText(asset, "tampered");

        PackageCacheOutcome.Resolved rebuilt = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));

        Assert.Equal(PackageCacheDisposition.Rebuilt, rebuilt.Disposition);
        Assert.Equal(first.Root.Path, rebuilt.Root.Path);
        Assert.Equal("original", File.ReadAllText(Path.Combine(rebuilt.Root.Path, "assets", "global.json")));
    }

    [Fact]
    public void Clean_removes_only_the_resolved_owned_cache_root()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("cleanup");
        PackageArchiveCache cache = new(fixture.CacheRoot);
        PackageCacheOutcome.Resolved resolved = Assert.IsType<PackageCacheOutcome.Resolved>(cache.Resolve(archive));

        PackageCacheCleanupOutcome.Cleaned cleaned = Assert.IsType<PackageCacheCleanupOutcome.Cleaned>(cache.Clean(resolved.Root));

        Assert.True(cleaned.Receipt.Removed);
        Assert.False(Directory.Exists(resolved.Root.Path));
        Assert.True(File.Exists(archive));
    }

    [Fact]
    public void Resolve_rejects_zip_slip_without_creating_a_cache_root()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreateRawZip("../outside.txt", "outside");
        PackageArchiveCache cache = new(fixture.CacheRoot);

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(cache.Resolve(archive));

        Assert.Equal(PackageCacheFailureReason.InvalidArchive, failed.Failure.Reason);
        Assert.Empty(Directory.EnumerateDirectories(fixture.CacheRoot));
    }

    [Fact]
    public void Resolve_rejects_a_corrupt_zip_without_creating_a_cache_root()
    {
        using CacheFixture fixture = new();
        string archive = Path.Combine(fixture.Root, "corrupt.soulmod");
        File.WriteAllText(archive, "not a zip");
        PackageArchiveCache cache = new(fixture.CacheRoot);

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(cache.Resolve(archive));

        Assert.Equal(PackageCacheFailureReason.InvalidArchive, failed.Failure.Reason);
        Assert.Empty(Directory.EnumerateDirectories(fixture.CacheRoot));
    }

    [Fact]
    public void Resolve_cleans_its_staging_tree_when_extraction_is_cancelled()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("cancelled");
        PackageArchiveCache cache = new(fixture.CacheRoot);

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(cache.Resolve(archive, new CancellationToken(true)));

        Assert.Equal(PackageCacheFailureReason.Cancelled, failed.Failure.Reason);
        Assert.Empty(Directory.EnumerateDirectories(fixture.CacheRoot, ".*.soulmod-cache-staging-*"));
    }

    [Fact]
    public void Resolve_rejects_a_package_with_a_missing_required_asset()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreateRawZip();
        PackageArchiveCache cache = new(fixture.CacheRoot);

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(cache.Resolve(archive));

        Assert.Equal(PackageCacheFailureReason.InvalidPackage, failed.Failure.Reason);
        Assert.Empty(Directory.EnumerateDirectories(fixture.CacheRoot));
    }

    [Fact]
    public void Resolve_preserves_a_user_owned_root_at_the_deterministic_cache_key()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreatePackage("owned");
        string userRoot = Path.Combine(fixture.CacheRoot, "v1-" + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(archive))).ToLowerInvariant());
        Directory.CreateDirectory(userRoot);
        string sentinel = Path.Combine(userRoot, "user-data.txt");
        File.WriteAllText(sentinel, "preserve");

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(new PackageArchiveCache(fixture.CacheRoot).Resolve(archive));

        Assert.Equal(PackageCacheFailureReason.CacheRootUnowned, failed.Failure.Reason);
        Assert.True(File.Exists(sentinel));
    }

    [Fact]
    public void Resolve_rejects_duplicate_zip_entries_without_creating_a_cache_root()
    {
        using CacheFixture fixture = new();
        string archive = fixture.CreateDuplicateZip();

        PackageCacheOutcome.Failed failed = Assert.IsType<PackageCacheOutcome.Failed>(new PackageArchiveCache(fixture.CacheRoot).Resolve(archive));

        Assert.Equal(PackageCacheFailureReason.InvalidArchive, failed.Failure.Reason);
        Assert.Empty(Directory.EnumerateDirectories(fixture.CacheRoot));
    }
}

internal sealed class CacheFixture : IDisposable
{
    internal string Root { get; } = Path.Combine(Path.GetTempPath(), "soulmod-package-cache-" + Guid.NewGuid().ToString("N"));
    internal string CacheRoot => Path.Combine(Root, "cache");

    internal CacheFixture()
    {
        Directory.CreateDirectory(Root);
        Directory.CreateDirectory(CacheRoot);
    }

    internal string CreatePackage(string assetText)
    {
        string source = Path.Combine(Root, "source");
        string asset = Path.Combine(source, "assets", "global.json");
        Directory.CreateDirectory(Path.GetDirectoryName(asset)!);
        File.WriteAllText(asset, assetText);
        File.WriteAllText(Path.Combine(source, "manifest.json"), ManifestJson());
        File.WriteAllText(Path.Combine(source, "operations.json"), OperationsJson(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(asset))).ToLowerInvariant()));
        string archive = Path.Combine(Root, "fixture.soulmod");
        PackageBuilder.PackDirectory(source, archive);
        return archive;
    }

    internal string CreateRawZip(string? extraPath = null, string? extraContent = null)
    {
        string archive = Path.Combine(Root, "raw.soulmod");
        using ZipArchive zip = ZipFile.Open(archive, ZipArchiveMode.Create);
        Write(zip, "manifest.json", ManifestJson());
        Write(zip, "operations.json", OperationsJson(new string('0', 64)));
        if (extraPath is not null) Write(zip, extraPath, extraContent!);
        return archive;
    }

    internal string CreateDuplicateZip()
    {
        string archive = Path.Combine(Root, "duplicate.soulmod");
        using ZipArchive zip = ZipFile.Open(archive, ZipArchiveMode.Create);
        Write(zip, "manifest.json", "{}");
        Write(zip, "manifest.json", "{}");
        return archive;
    }

    public void Dispose()
    {
        try
        {
            foreach (string file in Directory.EnumerateFiles(Root, "*", SearchOption.AllDirectories))
                File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
            Directory.Delete(Root, recursive: true);
        }
        catch (IOException) { }
    }

    private static void Write(ZipArchive zip, string path, string content)
    {
        using StreamWriter output = new(zip.CreateEntry(path).Open());
        output.Write(content);
    }

    private static string ManifestJson() => """
        {"formatVersion":1,"id":"cache.fixture","name":"Cache Fixture","version":"1.0.0","targetFile":"data.win","targets":[{"id":"fixture","targetFile":"data.win"}],"operationsFile":"operations.json"}
        """;

    private static string OperationsJson(string assetSha256) => $$"""
        [{"id":"cache.global","operationVersion":1,"modId":"cache.fixture","target":{"type":"global","name":"cache_global"},"operation":"global.json.load","selector":{"applyAt":"first.draw_set_font"},"assetPath":"assets/global.json","assetSha256":"{{assetSha256}}","reads":["global:cache_global"],"writes":["global:cache_global"]}]
        """;
}
