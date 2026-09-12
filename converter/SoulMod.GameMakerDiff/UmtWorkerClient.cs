using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

namespace SoulMod.GameMakerDiff;

internal static class UmtWorkerClient
{
    internal const int ProtocolVersion = 2;
    internal const string ParserVersion = "undertalemodlib-0.8.4.1";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    internal static WorkerResult Run(string basePath, string patchedPath, string stagingDirectory)
    {
        string requestPath = Path.Combine(stagingDirectory, "request.json");
        var request = new WorkerRequest
        {
            ProtocolVersion = ProtocolVersion,
            ParserVersion = ParserVersion,
            BasePath = basePath,
            PatchedPath = patchedPath,
            ExpectedBaseSha256 = GameMakerDiffer.ComputeSha256File(basePath),
            ExpectedPatchedSha256 = GameMakerDiffer.ComputeSha256File(patchedPath),
            OutputStagingDirectory = stagingDirectory
        };
        File.WriteAllBytes(requestPath, JsonSerializer.SerializeToUtf8Bytes(request, JsonOptions));

        string workerExecutable = ResolveWorkerExecutable();
        var startInfo = new ProcessStartInfo
        {
            FileName = workerExecutable,
            WorkingDirectory = Path.GetDirectoryName(workerExecutable)!,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("--request");
        startInfo.ArgumentList.Add(requestPath);

        using Process process = Process.Start(startInfo)
            ?? throw new InvalidOperationException("Could not start the UndertaleModLib worker.");
        Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync();
        Task<string> stderrTask = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit((int)TimeSpan.FromMinutes(2).TotalMilliseconds))
        {
            process.Kill(true);
            process.WaitForExit();
            throw new TimeoutException("UndertaleModLib worker timed out.");
        }

        string stdout = stdoutTask.GetAwaiter().GetResult();
        string stderr = stderrTask.GetAwaiter().GetResult();
        if (process.ExitCode != 0)
        {
            throw new InvalidDataException($"UndertaleModLib worker failed: {stderr}");
        }

        WorkerResult result = JsonSerializer.Deserialize<WorkerResult>(stdout, JsonOptions)
            ?? throw new InvalidDataException("UndertaleModLib worker returned no result.");
        ValidateResult(result, request, stagingDirectory);
        return result;
    }

    internal static List<GeneratedAsset> LoadAssets(WorkerResult result, string stagingDirectory)
    {
        var assets = new List<GeneratedAsset>();
        foreach (ChangedFontResource resource in result.Resources)
        {
            assets.Add(ReadAsset(resource.Metadata, stagingDirectory));
            assets.Add(ReadAsset(resource.Atlas, stagingDirectory));
        }

        return assets;
    }

    private static void ValidateResult(WorkerResult result, WorkerRequest request, string stagingDirectory)
    {
        if (result.ProtocolVersion != ProtocolVersion || result.ParserVersion != ParserVersion ||
            result.BaseSha256 != request.ExpectedBaseSha256 || result.PatchedSha256 != request.ExpectedPatchedSha256)
        {
            throw new InvalidDataException("UndertaleModLib worker result did not match the request.");
        }

        var names = new HashSet<string>(StringComparer.Ordinal);
        var paths = new HashSet<string>(StringComparer.Ordinal);
        foreach (ChangedFontResource resource in result.Resources)
        {
            if (resource.Type != "font" || string.IsNullOrWhiteSpace(resource.Name) || !names.Add(resource.Name) ||
                resource.ChangeKind is not ("replace" or "add"))
            {
                throw new InvalidDataException("UndertaleModLib worker reported an invalid resource.");
            }

            ValidateArtifact(resource.Metadata, stagingDirectory, paths);
            ValidateArtifact(resource.Atlas, stagingDirectory, paths);
            if (resource.ChangeKind == "replace" &&
                (resource.BaseGraphSha256 is not string baseGraphSha256 || !IsSha256(baseGraphSha256)))
            {
                throw new InvalidDataException("UndertaleModLib worker reported an invalid base graph hash.");
            }
            if (resource.ChangeKind == "add" && resource.BaseGraphSha256 is not null)
            {
                throw new InvalidDataException("UndertaleModLib worker reported a base graph hash for an added resource.");
            }
        }
    }

    private static GeneratedAsset ReadAsset(WorkerArtifact artifact, string stagingDirectory)
    {
        string path = GetSafeArtifactPath(stagingDirectory, artifact.RelativePath);
        byte[] content = File.ReadAllBytes(path);
        if (HashBytes(content) != artifact.Sha256)
        {
            throw new InvalidDataException($"Worker artifact hash mismatch: {artifact.RelativePath}");
        }

        return new GeneratedAsset(artifact.RelativePath, content, artifact.Sha256);
    }

    private static void ValidateArtifact(WorkerArtifact artifact, string stagingDirectory, ISet<string> paths)
    {
        if (!paths.Add(artifact.RelativePath) || !IsSha256(artifact.Sha256))
        {
            throw new InvalidDataException("UndertaleModLib worker reported an invalid artifact.");
        }

        string path = GetSafeArtifactPath(stagingDirectory, artifact.RelativePath);
        if (!File.Exists(path) || HashFile(path) != artifact.Sha256)
        {
            throw new InvalidDataException($"Worker artifact is missing or invalid: {artifact.RelativePath}");
        }
    }

    private static string GetSafeArtifactPath(string stagingDirectory, string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath) ||
            relativePath.Split('/').Any(part => part is "" or "." or ".."))
        {
            throw new InvalidDataException("Worker artifact path is unsafe.");
        }

        string root = Path.GetFullPath(stagingDirectory) + Path.DirectorySeparatorChar;
        string path = Path.GetFullPath(Path.Combine(stagingDirectory, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!path.StartsWith(root, StringComparison.Ordinal))
        {
            throw new InvalidDataException("Worker artifact path escapes staging.");
        }

        return path;
    }

    private static string ResolveWorkerExecutable()
    {
        string? explicitPath = Environment.GetEnvironmentVariable("SOULMOD_UMT_WORKER_PATH");
        if (!string.IsNullOrWhiteSpace(explicitPath) && File.Exists(explicitPath))
        {
            return Path.GetFullPath(explicitPath);
        }

        for (DirectoryInfo? directory = new(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
        {
            string candidate = Path.Combine(directory.FullName, "artifacts", "umtdiff", "win-x64", "SoulMod.UmtWorker.exe");
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        throw new FileNotFoundException("Published UndertaleModLib worker was not found.");
    }

    private static bool IsSha256(string value) => value.Length == 64 && value.All(character => char.IsAsciiHexDigit(character));
    private static string HashFile(string path) { using FileStream stream = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant(); }
    private static string HashBytes(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
}

internal sealed class WorkerRequest
{
    public int ProtocolVersion { get; set; }
    public string ParserVersion { get; set; } = string.Empty;
    public string BasePath { get; set; } = string.Empty;
    public string PatchedPath { get; set; } = string.Empty;
    public string ExpectedBaseSha256 { get; set; } = string.Empty;
    public string ExpectedPatchedSha256 { get; set; } = string.Empty;
    public string OutputStagingDirectory { get; set; } = string.Empty;
}

internal sealed class WorkerResult
{
    public int ProtocolVersion { get; set; }
    public string ParserVersion { get; set; } = string.Empty;
    public string BaseSha256 { get; set; } = string.Empty;
    public string PatchedSha256 { get; set; } = string.Empty;
    public List<ChangedFontResource> Resources { get; set; } = [];
}

internal sealed class ChangedFontResource
{
    public string Type { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public string ChangeKind { get; set; } = string.Empty;
    public string? BaseGraphSha256 { get; set; }
    public WorkerArtifact Metadata { get; set; } = new();
    public WorkerArtifact Atlas { get; set; } = new();
}

internal sealed class WorkerArtifact
{
    public string RelativePath { get; set; } = string.Empty;
    public string Sha256 { get; set; } = string.Empty;
}
