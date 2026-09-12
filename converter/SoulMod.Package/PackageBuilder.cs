using System.IO.Compression;

namespace SoulMod.Package;

public static class PackageBuilder
{
    public static void PackDirectory(string sourceDir, string outputSoulModFile)
    {
        if (!Directory.Exists(sourceDir))
        {
            throw new DirectoryNotFoundException($"Source directory '{sourceDir}' not found.");
        }

        string manifestPath = Path.Combine(sourceDir, "manifest.json");
        if (!File.Exists(manifestPath))
        {
            throw new FileNotFoundException("manifest.json missing in source directory.", manifestPath);
        }

        ValidateDirectory(sourceDir);

        if (File.Exists(outputSoulModFile)) File.Delete(outputSoulModFile);

        ZipFile.CreateFromDirectory(sourceDir, outputSoulModFile, CompressionLevel.Optimal, false);
    }

    public static void UnpackPackage(string soulModFile, string targetDir)
    {
        if (!File.Exists(soulModFile))
        {
            throw new FileNotFoundException($"Package file '{soulModFile}' not found.");
        }

        _ = ReadPackageFromZip(soulModFile);

        if (!Directory.Exists(targetDir))
        {
            Directory.CreateDirectory(targetDir);
        }

        ZipFile.ExtractToDirectory(soulModFile, targetDir, true);
    }

    public static Manifest ReadManifestFromZip(string soulModFile)
    {
        return PackageArchive.Read(soulModFile).Document.Manifest;
    }

    public static List<Operation> ReadOperationsFromZip(string soulModFile, Manifest manifest)
    {
        return PackageArchive.Read(soulModFile).Document.Operations.ToList();
    }

    public static PackageDocument ReadPackageFromZip(string soulModFile)
    {
        return PackageArchive.Read(soulModFile).Document;
    }

    private static void ValidateDirectory(string sourceDir)
    {
        using FileStream manifestStream = File.OpenRead(Path.Combine(sourceDir, "manifest.json"));
        Manifest manifest = PackageContract.ParseManifest(manifestStream);
        string operationsPath = ResolveContainedPath(sourceDir, manifest.OperationsFile);
        using FileStream operationsStream = File.OpenRead(operationsPath);
        List<Operation> operations = PackageContract.ParseOperations(operationsStream, manifest);
        PackageContract.ValidateAssetHashes(operations, path => File.OpenRead(ResolveContainedPath(sourceDir, path)));
    }

    private static Manifest ReadManifest(ZipArchive archive)
    {
        ZipArchiveEntry entry = archive.GetEntry("manifest.json")
            ?? throw new InvalidDataException("Package does not contain manifest.json.");
        using Stream stream = entry.Open();
        return PackageContract.ParseManifest(stream);
    }

    private static List<Operation> ReadOperations(ZipArchive archive, Manifest manifest)
    {
        ZipArchiveEntry entry = archive.GetEntry(manifest.OperationsFile)
            ?? throw new InvalidDataException($"Package does not contain '{manifest.OperationsFile}'.");
        using Stream stream = entry.Open();
        return PackageContract.ParseOperations(stream, manifest);
    }

    private static string ResolveContainedPath(string root, string relativePath)
    {
        string rootPath = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
        string candidate = Path.GetFullPath(Path.Combine(root, relativePath));
        if (!candidate.StartsWith(rootPath, StringComparison.Ordinal) || !File.Exists(candidate))
        {
            throw new InvalidDataException($"Missing asset or package file '{relativePath}'.");
        }

        return candidate;
    }
}
