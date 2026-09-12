using System.Text.Json.Serialization;

namespace SoulMod.Package;

public class Manifest
{
    [JsonPropertyName("formatVersion")]
    public int FormatVersion { get; set; } = 1;

    [JsonPropertyName("id")]
    public string Id { get; set; } = string.Empty;

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("version")]
    public string Version { get; set; } = string.Empty;

    [JsonPropertyName("targetFile")]
    public string TargetFile { get; set; } = string.Empty;

    [JsonPropertyName("targetDataSha256")]
    public string? TargetDataSha256 { get; set; }

    [JsonPropertyName("targets")]
    public List<CompatibilityTarget> Targets { get; set; } = new();

    [JsonPropertyName("authors")]
    public List<string> Authors { get; set; } = new();

    [JsonPropertyName("license")]
    public string? License { get; set; }

    [JsonPropertyName("dependencies")]
    public List<string> Dependencies { get; set; } = new();

    [JsonPropertyName("loadBefore")]
    public List<string> LoadBefore { get; set; } = new();

    [JsonPropertyName("loadAfter")]
    public List<string> LoadAfter { get; set; } = new();

    [JsonPropertyName("conflictPolicy")]
    public string ConflictPolicy { get; set; } = "stop";

    [JsonPropertyName("operationsFile")]
    public string OperationsFile { get; set; } = "operations.json";
}

public class CompatibilityTarget
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = string.Empty;

    [JsonPropertyName("targetFile")]
    public string TargetFile { get; set; } = string.Empty;

    [JsonPropertyName("targetDataSha256")]
    public string? TargetDataSha256 { get; set; }
}
