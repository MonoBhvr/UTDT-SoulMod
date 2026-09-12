using SoulMod.GameMakerDiff;
using SoulMod.Package;

namespace SoulMod.Importer.XDelta;

public class XDeltaImporterOptions
{
    public required string XDeltaExecutablePath { get; set; }
    public required string BaseDataWinPath { get; set; }
    public required string PatchFilePath { get; set; }
    public required string OutputSoulModPath { get; set; }
    public string ModId { get; set; } = "generated.mod";
    public string ModName { get; set; } = "Generated Mod";
    public string ModVersion { get; set; } = "0.1.0";
    public string? ExpectedBaseHash { get; set; }
    public string? ExpectedPatchedHash { get; set; }
}

public class XDeltaImporter
{
    public static bool ImportXDeltaPatch(XDeltaImporterOptions options)
    {
        if (!File.Exists(options.XDeltaExecutablePath))
        {
            throw new FileNotFoundException("xdelta executable not found.", options.XDeltaExecutablePath);
        }

        if (!File.Exists(options.BaseDataWinPath))
        {
            throw new FileNotFoundException("Base data.win file not found.", options.BaseDataWinPath);
        }

        if (!File.Exists(options.PatchFilePath))
        {
            throw new FileNotFoundException("Patch xdelta file not found.", options.PatchFilePath);
        }

        // 1. Verify Base SHA-256
        string actualBaseHash = GameMakerDiffer.ComputeSha256File(options.BaseDataWinPath);
        if (!string.IsNullOrEmpty(options.ExpectedBaseHash) && 
            !actualBaseHash.Equals(options.ExpectedBaseHash, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"Base data.win SHA-256 mismatch! Expected: {options.ExpectedBaseHash}, Actual: {actualBaseHash}");
        }

        // 2. Prepare Temporary Workspace
        string tempDir = Path.Combine(Path.GetTempPath(), "SoulMod_XDelta_" + Path.GetRandomFileName());
        string tempPatchedFile = Path.Combine(tempDir, "patched_data.win");
        string tempPackageDir = Path.Combine(tempDir, "package_contents");
        string tempPackageFile = Path.Combine(tempDir, "output.soulmod");

        try
        {
            Directory.CreateDirectory(tempDir);
            Directory.CreateDirectory(tempPackageDir);

            XDeltaDecoder.Decode(
                options.XDeltaExecutablePath,
                options.BaseDataWinPath,
                options.PatchFilePath,
                tempPatchedFile);

            string actualPatchedHash = GameMakerDiffer.ComputeSha256File(tempPatchedFile);
            if (!string.IsNullOrEmpty(options.ExpectedPatchedHash) &&
                !actualPatchedHash.Equals(options.ExpectedPatchedHash, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException($"Patched data.win SHA-256 mismatch! Expected: {options.ExpectedPatchedHash}, Actual: {actualPatchedHash}");
            }

            // 4. Extract Diff & Create Package Operations
            {
                var diffOptions = new GameMakerDiffOptions
                {
                    ModId = options.ModId,
                    ModName = options.ModName,
                    ModVersion = options.ModVersion
                };
                GameMakerDiffResult result = GameMakerDiffer.CompareDataFiles(options.BaseDataWinPath, tempPatchedFile, diffOptions);
                foreach (GeneratedAsset asset in result.Assets)
                {
                    string assetFile = GetSafeAssetFilePath(tempPackageDir, asset.Path);
                    Directory.CreateDirectory(Path.GetDirectoryName(assetFile)!);
                    File.WriteAllBytes(assetFile, asset.Content);
                    if (!GameMakerDiffer.ComputeSha256File(assetFile).Equals(asset.Sha256, StringComparison.Ordinal))
                    {
                        throw new InvalidDataException($"Generated asset hash mismatch: {asset.Path}");
                    }
                }

                string manifestJson = System.Text.Json.JsonSerializer.Serialize(result.Manifest, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
                string opsJson = System.Text.Json.JsonSerializer.Serialize(result.Operations, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(Path.Combine(tempPackageDir, "manifest.json"), manifestJson);
                File.WriteAllText(Path.Combine(tempPackageDir, "operations.json"), opsJson);
            }

            // 5. Pack into .soulmod
            PackageBuilder.PackDirectory(tempPackageDir, tempPackageFile);
            File.Move(tempPackageFile, options.OutputSoulModPath, true);
            return true;
        }
        finally
        {
            // 6. Mandatory Cleanup of Temporary Patched Files
            if (Directory.Exists(tempDir))
            {
                Directory.Delete(tempDir, true);
            }
        }
    }

    private static string GetSafeAssetFilePath(string packageDirectory, string assetPath)
    {
        if (string.IsNullOrWhiteSpace(assetPath) || Path.IsPathRooted(assetPath))
        {
            throw new InvalidDataException($"Invalid generated asset path: {assetPath}");
        }

        string normalizedPath = assetPath.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
        if (normalizedPath.Split(Path.DirectorySeparatorChar).Any(segment => segment is "" or "." or ".."))
        {
            throw new InvalidDataException($"Invalid generated asset path: {assetPath}");
        }

        string root = Path.GetFullPath(packageDirectory) + Path.DirectorySeparatorChar;
        string destination = Path.GetFullPath(Path.Combine(packageDirectory, normalizedPath));
        if (!destination.StartsWith(root, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Generated asset path escapes package: {assetPath}");
        }

        return destination;
    }

}
