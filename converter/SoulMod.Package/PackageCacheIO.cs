using System.Security.Cryptography;
using System.Text.Json;

namespace SoulMod.Package;

internal sealed record CacheMarker(string CacheKey, string ArchiveSha256, string ContentSha256);

internal static class PackageCacheIO
{
    internal const string Owner = "SoulMod.PackageArchiveCache";
    internal const string ResolvedMarkerName = ".soulmod-package-cache.json";
    internal const string StagingMarkerName = ".soulmod-package-cache-staging.json";

    public static string NormalizeRoot(string root)
    {
        if (string.IsNullOrWhiteSpace(root))
            throw new InvalidDataException("Cache root is not a usable path.");
        string normalized;
        try { normalized = Path.GetFullPath(root); }
        catch (Exception error) when (error is ArgumentException or NotSupportedException)
        {
            throw new InvalidDataException("Cache root is not a usable path.", error);
        }
        Directory.CreateDirectory(normalized);
        if ((File.GetAttributes(normalized) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("Cache root may not be a symbolic link.");
        return string.Equals(normalized, Directory.GetDirectoryRoot(normalized), PathComparison)
            ? normalized
            : normalized.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }

    public static string NewRunId() => Convert.ToHexString(RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();

    public static void WriteStagingMarker(string path, string intendedRoot, string cacheKey)
    {
        Write(path, StagingMarkerName, new { ownedBy = Owner, kind = "staging", intendedRoot, cacheKey });
    }

    public static void WriteResolvedMarker(string path, CacheMarker marker)
    {
        Write(path, ResolvedMarkerName, new
        {
            ownedBy = Owner,
            kind = "resolved",
            formatVersion = 1,
            cacheKey = marker.CacheKey,
            archiveSha256 = marker.ArchiveSha256,
            contentSha256 = marker.ContentSha256,
        });
    }

    public static bool TryReadResolvedMarker(string path, out CacheMarker marker)
    {
        marker = default!;
        string markerPath = Path.Combine(path, ResolvedMarkerName);
        if (!File.Exists(markerPath))
            return false;
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(markerPath));
            JsonElement root = document.RootElement;
            if (!PropertyEquals(root, "ownedBy", Owner) || !PropertyEquals(root, "kind", "resolved") ||
                !root.TryGetProperty("formatVersion", out JsonElement version) || version.GetInt32() != 1 ||
                !TryString(root, "cacheKey", out string key) || !TryString(root, "archiveSha256", out string archive) ||
                !TryString(root, "contentSha256", out string content))
                return false;
            marker = new CacheMarker(key, archive, content);
            return true;
        }
        catch (JsonException) { return false; }
        catch (IOException) { return false; }
        catch (InvalidOperationException) { return false; }
        catch (FormatException) { return false; }
    }

    public static int RemoveOwnedStaging(string target)
    {
        string parent = Directory.GetParent(target)?.FullName ?? throw new InvalidDataException("Cache target has no parent.");
        string name = Path.GetFileName(target);
        int removed = 0;
        foreach (string staging in Directory.EnumerateDirectories(parent, $".{name}.soulmod-cache-staging-*"))
        {
            if (!StagingTargets(staging, target))
                continue;
            DeleteTree(staging);
            removed++;
        }
        return removed;
    }

    public static void RecoverOwnedBackup(string target, CacheMarker expected)
    {
        if (Directory.Exists(target))
            return;
        string parent = Directory.GetParent(target)?.FullName ?? throw new InvalidDataException("Cache target has no parent.");
        string name = Path.GetFileName(target);
        string[] backups = Directory.EnumerateDirectories(parent, $".{name}.soulmod-cache-rollback-*")
            .Where(path => TryReadResolvedMarker(path, out CacheMarker marker) && marker == expected).ToArray();
        if (backups.Length == 1)
            Directory.Move(backups[0], target);
    }

    public static void Promote(string staging, string target, Action<string, string> move)
    {
        string backup = $".{Path.GetFileName(target)}.soulmod-cache-rollback-{NewRunId()}";
        string backupPath = Path.Combine(Directory.GetParent(target)!.FullName, backup);
        bool movedTarget = false;
        if (Directory.Exists(target))
        {
            Directory.Move(target, backupPath);
            movedTarget = true;
        }
        try { move(staging, target); }
        catch
        {
            if (movedTarget && Directory.Exists(backupPath))
                Directory.Move(backupPath, target);
            throw;
        }
        if (movedTarget && Directory.Exists(backupPath))
            DeleteTree(backupPath);
    }

    public static void DeleteTree(string path)
    {
        ClearReadOnly(path);
        Directory.Delete(path, recursive: true);
    }

    public static void SealReadOnly(string root)
    {
        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            File.SetAttributes(file, File.GetAttributes(file) | FileAttributes.ReadOnly);
    }

    private static bool StagingTargets(string staging, string target)
    {
        string marker = Path.Combine(staging, StagingMarkerName);
        if (!File.Exists(marker))
            return false;
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(marker));
            return PropertyEquals(document.RootElement, "ownedBy", Owner) && PropertyEquals(document.RootElement, "kind", "staging") &&
                PropertyEquals(document.RootElement, "intendedRoot", target);
        }
        catch (JsonException) { return false; }
        catch (IOException) { return false; }
    }

    private static void Write(string path, string name, object document) =>
        File.WriteAllText(Path.Combine(path, name), JsonSerializer.Serialize(document));

    private static void ClearReadOnly(string root)
    {
        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
    }

    private static bool PropertyEquals(JsonElement element, string name, string expected) =>
        TryString(element, name, out string actual) && actual == expected;

    private static bool TryString(JsonElement element, string name, out string value)
    {
        value = string.Empty;
        return element.TryGetProperty(name, out JsonElement property) && property.ValueKind == JsonValueKind.String &&
            !string.IsNullOrWhiteSpace(value = property.GetString()!);
    }

    private static StringComparison PathComparison =>
        OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
}
