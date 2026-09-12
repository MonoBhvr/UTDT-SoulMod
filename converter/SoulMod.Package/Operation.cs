using System.Text.Json.Serialization;

namespace SoulMod.Package;

public class OperationTarget
{
    [JsonPropertyName("type")]
    public string Type { get; set; } = string.Empty;

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;
}

public class Operation
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = string.Empty;

    [JsonPropertyName("modId")]
    public string ModId { get; set; } = string.Empty;

    [JsonPropertyName("operationVersion")]
    public int OperationVersion { get; set; } = 1;

    [JsonPropertyName("target")]
    public OperationTarget Target { get; set; } = new();

    [JsonPropertyName("operation")]
    [JsonConverter(typeof(OperationKindJsonConverter))]
    public OperationKind Kind { get; set; }

    [JsonIgnore]
    public string OperationType => OperationKindJsonConverter.ToWire(Kind);

    [JsonPropertyName("selector")]
    public Dictionary<string, object>? Selector { get; set; }

    [JsonPropertyName("precondition")]
    public OperationPrecondition? Precondition { get; set; }

    [JsonPropertyName("value")]
    public string? Value { get; set; }

    [JsonPropertyName("assetPath")]
    public string? AssetPath { get; set; }

    [JsonPropertyName("assetSha256")]
    public string? AssetSha256 { get; set; }

    [JsonPropertyName("priority")]
    public int Priority { get; set; } = 100;

    [JsonPropertyName("reads")]
    public List<string> Reads { get; set; } = new();

    [JsonPropertyName("writes")]
    public List<string> Writes { get; set; } = new();
}

public sealed class OperationPrecondition
{
    [JsonPropertyName("expectedValue")]
    public string? ExpectedValue { get; set; }

    [JsonPropertyName("sha256")]
    public string? Sha256 { get; set; }
}

public enum OperationKind
{
    StringReplace,
    StringAdd,
    CodeReplace,
    ResourceRedirect,
    ResourceReplace,
    ResourceAdd,
    SpriteReplace,
    SpriteFrameReplace,
    FontRegister,
    FontRedirect,
    TextureReplace,
    AudioReplace,
    HookBefore,
    HookAfter,
    HookReplace,
    CallArgumentReplace,
    GlobalJsonLoad
}
