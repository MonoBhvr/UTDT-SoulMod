using System.Text.Json.Serialization;

namespace SoulMod.UmtWorker;

public sealed class WorkerRequest
{
    [JsonPropertyName("protocolVersion")]
    public int ProtocolVersion { get; set; }
    [JsonPropertyName("parserVersion")]
    public string ParserVersion { get; set; } = string.Empty;
    [JsonPropertyName("basePath")]
    public string BasePath { get; set; } = string.Empty;
    [JsonPropertyName("patchedPath")]
    public string PatchedPath { get; set; } = string.Empty;
    [JsonPropertyName("expectedBaseSha256")]
    public string ExpectedBaseSha256 { get; set; } = string.Empty;
    [JsonPropertyName("expectedPatchedSha256")]
    public string ExpectedPatchedSha256 { get; set; } = string.Empty;
    [JsonPropertyName("outputStagingDirectory")]
    public string OutputStagingDirectory { get; set; } = string.Empty;
}

public sealed class WorkerResult
{
    [JsonPropertyName("protocolVersion")]
    public int ProtocolVersion { get; set; }
    [JsonPropertyName("parserVersion")]
    public string ParserVersion { get; set; } = string.Empty;
    [JsonPropertyName("baseSha256")]
    public string BaseSha256 { get; set; } = string.Empty;
    [JsonPropertyName("patchedSha256")]
    public string PatchedSha256 { get; set; } = string.Empty;
    [JsonPropertyName("resources")]
    public List<ChangedFontResource> Resources { get; set; } = [];
}

public sealed class ChangedFontResource
{
    [JsonPropertyName("type")]
    public string Type { get; set; } = "font";
    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;
    [JsonPropertyName("changeKind")]
    public string ChangeKind { get; set; } = string.Empty;
    [JsonPropertyName("baseGraphSha256")]
    public string? BaseGraphSha256 { get; set; }
    [JsonPropertyName("metadata")]
    public WorkerArtifact Metadata { get; set; } = new();
    [JsonPropertyName("atlas")]
    public WorkerArtifact Atlas { get; set; } = new();
}

public sealed class WorkerArtifact
{
    [JsonPropertyName("relativePath")]
    public string RelativePath { get; set; } = string.Empty;
    [JsonPropertyName("sha256")]
    public string Sha256 { get; set; } = string.Empty;
}
