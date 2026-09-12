using System.Security.Cryptography;
using System.Text;

namespace SoulMod.ExecutionCopy;

/// <summary>
/// Raised when a symlink/reparse point is encountered inside a tree the boundary must hash or
/// copy. The manager catches it and maps it to a typed
/// <see cref="ExecutionCopyFailureReason.SymlinkRejected"/> so the rejection happens before any
/// copy attempt (or mid-copy), never as a misleading CopyFailed.
/// </summary>
internal sealed class SymlinkInSourceException : Exception
{
    public SymlinkInSourceException(string path) : base(path) => Path = path;
    public new string Path { get; }
}

/// <summary>
/// Deterministic integrity snapshots of a game install. The directory manifest hashes the sorted
/// (relative path, file sha256) pairs so a single value proves the install directory was not
/// mutated across a copy operation. SoulMod metadata files (ownership marker, copy manifest) are
/// excluded so the manifest measures source content and equals the persisted managed-root hash.
/// </summary>
internal static class IntegrityHashing
{
    /// <summary>
    /// Builds a full integrity snapshot of the install: executable hash, data.win hash, and a
    /// deterministic manifest hash over the source content (excluding any SoulMod metadata names).
    /// </summary>
    public static InstallIntegrity Snapshot(GameInstall install, IReadOnlySet<string>? excludedRelativePaths = null)
    {
        FileIntegrity executable = HashFile(install.ExecutablePath);
        FileIntegrity dataWin = HashFile(install.DataWinPath);
        string manifest = DirectoryManifestSha256(install.RootPath, excludedRelativePaths);
        return new InstallIntegrity(executable, dataWin, manifest);
    }

    /// <summary>Hashes one absolute file. Caller ensures the path is valid and present.</summary>
    public static FileIntegrity HashFile(string absolutePath) =>
        new(absolutePath, PathContainment.Sha256File(absolutePath));

    /// <summary>
    /// Deterministic manifest hash over the directory tree rooted at <paramref name="rootPath"/>.
    /// Walks top-level only and recurses explicitly so directory reparse points are caught before
    /// descent. Entries are sorted by normalized forward-slash relative path; each line is
    /// "<paramref name="relativePath"/>\0<sha256>\n". <paramref name="excludedRelativePaths"/>
    /// (forward-slash, root-relative) are skipped so the manifest measures source content only.
    /// </summary>
    public static string DirectoryManifestSha256(string rootPath, IReadOnlySet<string>? excludedRelativePaths = null)
    {
        excludedRelativePaths ??= new HashSet<string>(StringComparer.Ordinal);
        var entries = new List<(string RelativePath, string Sha256)>();
        Walk(rootPath, rootPath, entries, excludedRelativePaths);
        entries.Sort((left, right) => string.CompareOrdinal(left.RelativePath, right.RelativePath));

        using IncrementalHash hasher = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach ((string relativePath, string sha256) in entries)
        {
            hasher.AppendData(Encoding.UTF8.GetBytes(relativePath));
            hasher.AppendData([(byte)0]);
            hasher.AppendData(Encoding.UTF8.GetBytes(sha256));
            hasher.AppendData([(byte)'\n']);
        }

        return Convert.ToHexString(hasher.GetHashAndReset()).ToLowerInvariant();

        static void Walk(string root, string current, List<(string, string)> entries, IReadOnlySet<string> excluded)
        {
            var dir = new DirectoryInfo(current);
            foreach (FileSystemInfo entry in dir.EnumerateFileSystemInfos("*", SearchOption.TopDirectoryOnly))
            {
                // Reparse points on files OR directories must be rejected before reading/descending.
                if (PathContainment.IsSymbolicLink(entry.FullName))
                    throw new SymlinkInSourceException(entry.FullName);

                if (entry is FileInfo file)
                {
                    string relative = Path.GetRelativePath(root, file.FullName).Replace('\\', '/');
                    if (excluded.Contains(relative))
                        continue;
                    entries.Add((relative, PathContainment.Sha256File(file.FullName)));
                }
                else if (entry is DirectoryInfo sub)
                {
                    Walk(root, sub.FullName, entries, excluded);
                }
            }
        }
    }
}
