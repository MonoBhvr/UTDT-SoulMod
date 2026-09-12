using System.Security.Cryptography;
using SoulMod.Package;

namespace SoulMod.GameMakerDiff;

public class GameMakerDiffOptions
{
    public string ModId { get; set; } = "generated.mod";
    public string ModName { get; set; } = "Generated Mod";
    public string ModVersion { get; set; } = "0.1.0";
    public int Priority { get; set; } = 100;
}

public static class GameMakerDiffer
{
    public static GameMakerDiffResult CompareDataFiles(
        string baseDataWinPath,
        string patchedDataWinPath,
        GameMakerDiffOptions options)
    {
        string basePath = Path.GetFullPath(baseDataWinPath);
        string patchedPath = Path.GetFullPath(patchedDataWinPath);
        var manifest = new Manifest
        {
            FormatVersion = 1,
            Id = options.ModId,
            Name = options.ModName,
            Version = options.ModVersion,
            TargetFile = Path.GetFileName(basePath),
            Targets = [new CompatibilityTarget
            {
                Id = "generated.target",
                TargetFile = Path.GetFileName(basePath),
                TargetDataSha256 = ComputeSha256File(basePath)
            }],
            ConflictPolicy = "stop"
        };
        if (ComputeSha256File(basePath) == ComputeSha256File(patchedPath))
        {
            return new GameMakerDiffResult(manifest, [], []);
        }

        string stagingDirectory = Path.Combine(Path.GetTempPath(), $"SoulMod_UmtDiff_{Path.GetRandomFileName()}");
        try
        {
            Directory.CreateDirectory(stagingDirectory);
            WorkerResult workerResult = UmtWorkerClient.Run(basePath, patchedPath, stagingDirectory);
            List<GeneratedAsset> assets = UmtWorkerClient.LoadAssets(workerResult, stagingDirectory);
            var operations = new List<Operation>();
            foreach (ChangedFontResource resource in workerResult.Resources)
            {
                operations.Add(new Operation
                {
                    Id = $"op_font_{SanitizeOperationId(resource.Name)}",
                    ModId = options.ModId,
                    Kind = resource.ChangeKind switch
                    {
                        "replace" => OperationKind.ResourceReplace,
                        "add" => OperationKind.ResourceAdd,
                        _ => throw new InvalidDataException($"Unsupported resource change kind '{resource.ChangeKind}'.")
                    },
                    Target = new OperationTarget { Type = resource.Type, Name = resource.Name },
                    AssetPath = resource.Metadata.RelativePath,
                    AssetSha256 = resource.Metadata.Sha256,
                    Precondition = resource.ChangeKind == "replace"
                        ? new OperationPrecondition { Sha256 = resource.BaseGraphSha256! }
                        : null,
                    Selector = new Dictionary<string, object>
                    {
                        ["atlasPath"] = resource.Atlas.RelativePath,
                        ["atlasSha256"] = resource.Atlas.Sha256
                    },
                    Priority = options.Priority,
                    Reads = [
                        $"resource:{resource.Type}:{resource.Name}",
                        $"asset:{resource.Atlas.RelativePath}"
                    ],
                    Writes = [$"resource:{resource.Type}:{resource.Name}"]
                });
            }

            return new GameMakerDiffResult(manifest, operations, assets);
        }
        finally
        {
            if (Directory.Exists(stagingDirectory))
            {
                Directory.Delete(stagingDirectory, true);
            }
        }
    }

    public static string ComputeSha256File(string filePath)
    {
        using FileStream stream = File.OpenRead(filePath);
        return ComputeSha256(stream);
    }

    public static string ComputeSha256(Stream stream) => Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();

    private static string SanitizeOperationId(string name) => string.Concat(name.Select(character =>
        char.IsLetterOrDigit(character) || character is '_' or '-' ? character : '_'));
}

public sealed record GameMakerDiffResult(Manifest Manifest, List<Operation> Operations, List<GeneratedAsset> Assets);
public sealed record GeneratedAsset(string Path, byte[] Content, string Sha256);
