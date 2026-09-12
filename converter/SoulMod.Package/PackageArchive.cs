using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace SoulMod.Package;

internal sealed record ArchiveEntryFingerprint(string Path, string Sha256);
internal sealed record VerifiedPackageArchive(
    PackageDocument Document,
    string ArchiveSha256,
    string ContentSha256,
    IReadOnlyList<ArchiveEntryFingerprint> Entries);

internal sealed class ArchiveStructureException : Exception
{
    public ArchiveStructureException(string message, Exception? innerException = null) : base(message, innerException) { }
}

internal static class PackageArchive
{
    public static VerifiedPackageArchive Read(string archivePath)
    {
        string archiveSha256 = Sha256File(archivePath);
        using FileStream input = OpenRead(archivePath);
        using ZipArchive archive = OpenArchive(input);
        return ReadVerified(archive, archiveSha256);
    }

    public static VerifiedPackageArchive Extract(string archivePath, string targetRoot, CancellationToken cancellationToken)
    {
        string archiveSha256 = Sha256File(archivePath);
        using FileStream input = OpenRead(archivePath);
        using ZipArchive archive = OpenArchive(input);
        VerifiedPackageArchive verified = ReadVerified(archive, archiveSha256);
        string normalizedRoot = Path.GetFullPath(targetRoot);
        string rootWithSeparator = normalizedRoot + Path.DirectorySeparatorChar;

        foreach (ZipArchiveEntry entry in archive.Entries.OrderBy(entry => entry.FullName, StringComparer.Ordinal))
        {
            cancellationToken.ThrowIfCancellationRequested();
            string relativePath = NormalizeEntryPath(entry.FullName);
            string targetPath = Path.GetFullPath(Path.Combine(normalizedRoot, relativePath.Replace('/', Path.DirectorySeparatorChar)));
            if (!targetPath.StartsWith(rootWithSeparator, StringComparison.Ordinal))
                throw new ArchiveStructureException($"ZIP entry escapes extraction root: '{entry.FullName}'.");
            Directory.CreateDirectory(Path.GetDirectoryName(targetPath)!);
            using Stream source = entry.Open();
            using FileStream output = new(targetPath, FileMode.CreateNew, FileAccess.Write, FileShare.None);
            CopyTo(source, output, cancellationToken);
        }

        return verified;
    }

    public static bool MatchesExtractedRoot(string root, VerifiedPackageArchive verified, string metadataFileName)
    {
        try
        {
            var actual = new Dictionary<string, string>(StringComparer.Ordinal);
            if (!CollectFiles(root, root, metadataFileName, actual))
                return false;

            return actual.Count == verified.Entries.Count && verified.Entries.All(entry =>
                actual.TryGetValue(entry.Path, out string? hash) && hash.Equals(entry.Sha256, StringComparison.OrdinalIgnoreCase));
        }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    public static string Sha256File(string path)
    {
        using FileStream input = OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
    }

    private static VerifiedPackageArchive ReadVerified(ZipArchive archive, string archiveSha256)
    {
        Dictionary<string, ZipArchiveEntry> entries = IndexEntries(archive);
        Manifest manifest = ReadManifest(entries);
        List<Operation> operations = ReadOperations(entries, manifest);
        PackageContract.ValidateAssetHashes(operations, path => entries.TryGetValue(path, out ZipArchiveEntry? entry) ? entry.Open() : null);
        ArchiveEntryFingerprint[] fingerprints = entries.OrderBy(pair => pair.Key, StringComparer.Ordinal)
            .Select(pair => new ArchiveEntryFingerprint(pair.Key, Sha256Entry(pair.Value))).ToArray();
        string content = string.Join('\n', fingerprints.Select(entry => $"{entry.Path}\0{entry.Sha256}"));
        return new VerifiedPackageArchive(new PackageDocument(manifest, operations), archiveSha256,
            Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(content))).ToLowerInvariant(), fingerprints);
    }

    private static Dictionary<string, ZipArchiveEntry> IndexEntries(ZipArchive archive)
    {
        var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.Ordinal);
        foreach (ZipArchiveEntry entry in archive.Entries)
        {
            try
            {
                string path = NormalizeEntryPath(entry.FullName);
                if (!entries.TryAdd(path, entry))
                    throw new ArchiveStructureException($"Duplicate ZIP entry '{path}'.");
            }
            catch (InvalidDataException error)
            {
                throw new ArchiveStructureException("Could not enumerate ZIP entries.", error);
            }
        }
        return entries;
    }

    private static Manifest ReadManifest(IReadOnlyDictionary<string, ZipArchiveEntry> entries)
    {
        if (!entries.TryGetValue("manifest.json", out ZipArchiveEntry? entry))
            throw new InvalidDataException("Package does not contain manifest.json.");
        using Stream input = entry.Open();
        return PackageContract.ParseManifest(input);
    }

    private static List<Operation> ReadOperations(IReadOnlyDictionary<string, ZipArchiveEntry> entries, Manifest manifest)
    {
        if (!entries.TryGetValue(manifest.OperationsFile, out ZipArchiveEntry? entry))
            throw new InvalidDataException($"Package does not contain '{manifest.OperationsFile}'.");
        using Stream input = entry.Open();
        return PackageContract.ParseOperations(input, manifest);
    }

    private static string NormalizeEntryPath(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Contains('\\') || value.Contains('\0') || value.StartsWith('/') || Path.IsPathRooted(value))
            throw new ArchiveStructureException($"Unsafe ZIP entry '{value}'.");
        string[] parts = value.Split('/');
        if (parts.Any(part => part is "" or "." or ".."))
            throw new ArchiveStructureException($"Unsafe ZIP entry '{value}'.");
        return string.Join('/', parts);
    }

    private static FileStream OpenRead(string path) => new(path, FileMode.Open, FileAccess.Read, FileShare.Read);
    private static ZipArchive OpenArchive(Stream input)
    {
        try { return new ZipArchive(input, ZipArchiveMode.Read, leaveOpen: false); }
        catch (InvalidDataException error) { throw new ArchiveStructureException("Package is not a readable ZIP archive.", error); }
    }
    private static string Sha256Entry(ZipArchiveEntry entry)
    {
        using Stream input = entry.Open();
        return Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
    }

    private static bool CollectFiles(string root, string current, string metadataFileName, IDictionary<string, string> files)
    {
        foreach (string entry in Directory.EnumerateFileSystemEntries(current, "*", SearchOption.TopDirectoryOnly))
        {
            if ((File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0)
                return false;
            if (Directory.Exists(entry))
            {
                if (!CollectFiles(root, entry, metadataFileName, files))
                    return false;
                continue;
            }
            string relative = Path.GetRelativePath(root, entry).Replace(Path.DirectorySeparatorChar, '/');
            if (relative != metadataFileName)
                files.Add(relative, Sha256File(entry));
        }
        return true;
    }

    private static void CopyTo(Stream input, Stream output, CancellationToken cancellationToken)
    {
        byte[] buffer = new byte[81920];
        int read;
        while ((read = input.Read(buffer, 0, buffer.Length)) != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            output.Write(buffer, 0, read);
        }
    }
}
