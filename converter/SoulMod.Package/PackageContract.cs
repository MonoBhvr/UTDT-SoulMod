using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace SoulMod.Package;

internal static partial class PackageContract
{
    private const int SupportedFormatVersion = 1;
    private const int SupportedOperationVersion = 1;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = false,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public static Manifest ParseManifest(Stream input)
    {
        using JsonDocument document = ParseDocument(input, "manifest");
        JsonElement root = document.RootElement;
        RequireObject(root, "manifest");
        RequireProperties(root, ["formatVersion", "id", "name", "version", "targetFile", "targets", "operationsFile"]);
        Manifest manifest = Deserialize<Manifest>(root, "manifest");
        ValidateManifest(manifest);
        return manifest;
    }

    public static List<Operation> ParseOperations(Stream input, Manifest manifest)
    {
        using JsonDocument document = ParseDocument(input, "operations");
        if (document.RootElement.ValueKind != JsonValueKind.Array) Fail("operations must be a JSON array.");
        var operations = new List<Operation>();
        foreach (JsonElement element in document.RootElement.EnumerateArray())
        {
            RequireObject(element, "operation");
            RequireProperties(element, ["id", "operationVersion", "modId", "target", "operation", "reads", "writes"]);
            ValidateOperationWireValues(element);
            operations.Add(Deserialize<Operation>(element, "operation"));
        }

        ValidateOperations(operations, manifest);
        return operations;
    }

    public static void ValidateAssetHashes(IEnumerable<Operation> operations, Func<string, Stream?> openAsset)
    {
        foreach (Operation operation in operations.Where(RequiresAsset))
        {
            using Stream asset = openAsset(operation.AssetPath!)
                ?? throw new InvalidDataException($"Missing asset '{operation.AssetPath}' for operation '{operation.Id}'.");
            string actual = Convert.ToHexString(SHA256.HashData(asset)).ToLowerInvariant();
            if (!actual.Equals(operation.AssetSha256, StringComparison.OrdinalIgnoreCase))
            {
                Fail($"Asset hash mismatch for operation '{operation.Id}'.");
            }
        }
    }

    private static JsonDocument ParseDocument(Stream input, string name)
    {
        try { return JsonDocument.Parse(input); }
        catch (JsonException exception) { throw new InvalidDataException($"Malformed {name} JSON.", exception); }
    }

    private static T Deserialize<T>(JsonElement element, string name)
    {
        try { return element.Deserialize<T>(JsonOptions) ?? throw new InvalidDataException($"Empty {name}."); }
        catch (JsonException exception) { throw new InvalidDataException($"Invalid {name}: {exception.Message}", exception); }
    }

    private static void ValidateManifest(Manifest manifest)
    {
        if (manifest.FormatVersion != SupportedFormatVersion) Fail($"Unknown formatVersion '{manifest.FormatVersion}'.");
        if (!PackageId().IsMatch(manifest.Id)) Fail("Invalid manifest id.");
        if (string.IsNullOrWhiteSpace(manifest.Name) || !SemanticVersion().IsMatch(manifest.Version)) Fail("Invalid manifest name or version.");
        if (!IsSafeRelativePath(manifest.TargetFile) || !IsSafeRelativePath(manifest.OperationsFile)) Fail("Invalid manifest package path.");
        if (manifest.TargetDataSha256 is not null && !IsSha256(manifest.TargetDataSha256)) Fail("Invalid target fingerprint.");
        if (manifest.Targets.Count == 0 || manifest.Targets.Any(target => !PackageId().IsMatch(target.Id) ||
            !IsSafeRelativePath(target.TargetFile) || (target.TargetDataSha256 is not null && !IsSha256(target.TargetDataSha256))))
        {
            Fail("Invalid compatibility target fingerprint.");
        }

        if (manifest.Targets.Select(target => target.Id).Distinct(StringComparer.Ordinal).Count() != manifest.Targets.Count) Fail("Duplicate compatibility target id.");
        if (manifest.ConflictPolicy is not ("stop" or "warn" or "overwrite")) Fail("Unknown conflictPolicy enum.");
        ValidatePackageIds(manifest.Dependencies, "dependency");
        ValidatePackageIds(manifest.LoadBefore, "loadBefore");
        ValidatePackageIds(manifest.LoadAfter, "loadAfter");
    }

    private static void ValidateOperations(List<Operation> operations, Manifest manifest)
    {
        var ids = new HashSet<string>(StringComparer.Ordinal);
        foreach (Operation operation in operations)
        {
            if (!PackageId().IsMatch(operation.Id) || !ids.Add(operation.Id)) Fail("Duplicate or invalid operation id.");
            if (operation.OperationVersion != SupportedOperationVersion) Fail($"Unknown operationVersion '{operation.OperationVersion}'.");
            if (!operation.ModId.Equals(manifest.Id, StringComparison.Ordinal)) Fail("Operation modId does not match manifest id.");
            if (!TargetKinds.Contains(operation.Target.Type) || string.IsNullOrWhiteSpace(operation.Target.Name)) Fail("Unknown target enum or invalid target name.");
            ValidateReadWriteSet(operation.Reads, "reads");
            ValidateReadWriteSet(operation.Writes, "writes");
            ValidateOperationRequirements(operation);
        }
    }

    private static void ValidateOperationWireValues(JsonElement element)
    {
        string kind = RequiredString(element, "operation");
        try { OperationKindJsonConverter.FromWire(kind); }
        catch (JsonException exception) { throw new InvalidDataException(exception.Message, exception); }
        if (kind == "font.redirect" && element.TryGetProperty("selector", out _)) Fail("Invalid font.redirect selector.");
        JsonElement target = element.GetProperty("target");
        RequireObject(target, "operation target");
        if (!TargetKinds.Contains(RequiredString(target, "type"))) Fail("Unknown target enum.");
    }

    private static void ValidateOperationRequirements(Operation operation)
    {
        if (RequiresAsset(operation) && (!IsSafeRelativePath(operation.AssetPath) || !IsSha256(operation.AssetSha256))) Fail("Missing or invalid asset hash.");
        switch (operation.Kind)
        {
            case OperationKind.StringReplace:
                if (operation.Precondition?.ExpectedValue is null || operation.Value is null) Fail("string.replace requires expectedValue and value.");
                break;
            case OperationKind.ResourceReplace:
                if (!IsSha256(operation.Precondition?.Sha256)) Fail("resource.replace requires a sha256 precondition.");
                break;
            case OperationKind.GlobalJsonLoad:
                ValidateGlobalJsonOperation(operation);
                break;
            case OperationKind.FontRegister:
                ValidateFontRegisterOperation(operation);
                break;
            case OperationKind.FontRedirect:
                if (operation.Target.Type != "font" || operation.Selector is not null || string.IsNullOrWhiteSpace(operation.Value)) Fail("Invalid font.redirect selector or target.");
                break;
        }
    }

    private static void ValidateGlobalJsonOperation(Operation operation)
    {
        if (operation.Target.Type != "global" || !GlobalName().IsMatch(operation.Target.Name)) Fail("Malformed global.json.load selector.");
        Dictionary<string, object> selector = operation.Selector ?? throw new InvalidDataException("Malformed global.json.load selector.");
        if (selector.Keys.Any(key => key is not ("applyAt" or "mirrorGlobals" or "when"))) Fail("Malformed global.json.load selector.");
        if (!selector.TryGetValue("applyAt", out object? applyAt) || applyAt is not JsonElement { ValueKind: JsonValueKind.String } applyAtValue ||
            applyAtValue.GetString() != "first.draw_set_font") Fail("Malformed global.json.load selector.");
        if (selector.TryGetValue("mirrorGlobals", out object? mirrors)) ValidateMirrorGlobals(mirrors, operation.Target.Name);
        if (selector.TryGetValue("when", out object? condition)) ValidateGlobalCondition(condition);
    }

    private static void ValidateFontRegisterOperation(Operation operation)
    {
        if (operation.Target.Type != "font") Fail("Malformed font.register selector.");
        Dictionary<string, object> selector = operation.Selector ?? throw new InvalidDataException("Malformed font.register selector.");
        string[] required = ["size", "bold", "italic", "first", "last"];
        if (required.Any(key => !selector.ContainsKey(key)) || selector.Keys.Any(key => !required.Contains(key))) Fail("Malformed font.register selector.");
        if (!TryGetInt(selector, "size", out int size) || !TryGetInt(selector, "first", out int first) || !TryGetInt(selector, "last", out int last) ||
            !TryGetBoolean(selector, "bold") || !TryGetBoolean(selector, "italic") || size < 1 || first < 0 || last < first)
        {
            Fail("Malformed font.register selector.");
        }
    }

    private static void ValidateMirrorGlobals(object? value, string targetName)
    {
        if (value is not JsonElement { ValueKind: JsonValueKind.Array } mirrors) throw new InvalidDataException("Malformed global.json.load selector.");
        string?[] names = mirrors.EnumerateArray().Select(element => element.ValueKind == JsonValueKind.String ? element.GetString() : null).ToArray();
        if (names.Any(name => name is null || !GlobalName().IsMatch(name) || name == targetName) || names.Distinct(StringComparer.Ordinal).Count() != names.Length)
        {
            Fail("Malformed global.json.load selector.");
        }
    }

    private static void ValidateGlobalCondition(object? value)
    {
        if (value is not JsonElement { ValueKind: JsonValueKind.Object } condition || condition.EnumerateObject().Any(property => property.Name is not ("global" or "equals")) ||
            !condition.TryGetProperty("global", out JsonElement global) || global.ValueKind != JsonValueKind.String || !GlobalName().IsMatch(global.GetString()!) ||
            !condition.TryGetProperty("equals", out JsonElement equals) || equals.ValueKind != JsonValueKind.String)
        {
            Fail("Malformed global.json.load selector.");
        }
    }

    private static bool TryGetInt(Dictionary<string, object> selector, string name, out int value)
    {
        value = default;
        return selector.TryGetValue(name, out object? raw) && raw is JsonElement { ValueKind: JsonValueKind.Number } number && number.TryGetInt32(out value);
    }

    private static bool TryGetBoolean(Dictionary<string, object> selector, string name) => selector.TryGetValue(name, out object? raw) &&
        raw is JsonElement { ValueKind: JsonValueKind.True or JsonValueKind.False };

    private static bool RequiresAsset(Operation operation) => operation.Kind is OperationKind.GlobalJsonLoad or OperationKind.ResourceRedirect or
        OperationKind.ResourceReplace or OperationKind.ResourceAdd or OperationKind.FontRegister;

    private static void ValidateReadWriteSet(List<string> values, string name)
    {
        if (values.Count == 0 || values.Any(value => !ResourceKey().IsMatch(value)) || values.Distinct(StringComparer.Ordinal).Count() != values.Count)
        {
            Fail($"Invalid {name} set.");
        }
    }

    private static void ValidatePackageIds(List<string> values, string name)
    {
        if (values.Any(value => !PackageId().IsMatch(value)) || values.Distinct(StringComparer.Ordinal).Count() != values.Count) Fail($"Invalid {name} list.");
    }

    private static void RequireObject(JsonElement element, string name)
    {
        if (element.ValueKind != JsonValueKind.Object) Fail($"{name} must be a JSON object.");
    }

    private static void RequireProperties(JsonElement element, string[] names)
    {
        foreach (string name in names) if (!element.TryGetProperty(name, out _)) Fail($"Missing required property '{name}'.");
    }

    private static string RequiredString(JsonElement element, string name) => element.TryGetProperty(name, out JsonElement value) &&
        value.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(value.GetString()) ? value.GetString()! : throw new InvalidDataException($"Missing or invalid '{name}'.");

    private static bool IsSafeRelativePath(string? value) => !string.IsNullOrWhiteSpace(value) && !value.Contains('\\') && !Path.IsPathRooted(value) &&
        value.Split(['/', '\\']).All(part => part is not "" and not "." and not "..");

    private static bool IsSha256(string? value) => value is not null && Sha256().IsMatch(value);
    private static void Fail(string message) => throw new InvalidDataException(message);
    [GeneratedRegex("^[a-zA-Z0-9._-]+$")] private static partial Regex PackageId();
    [GeneratedRegex("^[0-9a-fA-F]{64}$")] private static partial Regex Sha256();
    [GeneratedRegex("^[0-9]+\\.[0-9]+\\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$")] private static partial Regex SemanticVersion();
    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_]*$")] private static partial Regex GlobalName();
    [GeneratedRegex("^[a-z]+:[^\\s:]+$")] private static partial Regex ResourceKey();
    private static readonly HashSet<string> TargetKinds = ["code", "string", "font", "sprite", "texture", "audio", "hook", "file", "global"];
}
