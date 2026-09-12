using System.Text.Json;
using System.Text.Json.Serialization;

namespace SoulMod.Package;

public sealed class OperationKindJsonConverter : JsonConverter<OperationKind>
{
    public override OperationKind Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        string value = reader.GetString() ?? throw new JsonException("operation must be a string.");
        return FromWire(value);
    }

    public override void Write(Utf8JsonWriter writer, OperationKind value, JsonSerializerOptions options) =>
        writer.WriteStringValue(ToWire(value));

    public static OperationKind FromWire(string value) => value switch
    {
        "string.replace" => OperationKind.StringReplace,
        "string.add" => OperationKind.StringAdd,
        "code.replace" => OperationKind.CodeReplace,
        "resource.redirect" => OperationKind.ResourceRedirect,
        "resource.replace" => OperationKind.ResourceReplace,
        "resource.add" => OperationKind.ResourceAdd,
        "sprite.replace" => OperationKind.SpriteReplace,
        "sprite.frame.replace" => OperationKind.SpriteFrameReplace,
        "font.register" => OperationKind.FontRegister,
        "font.redirect" => OperationKind.FontRedirect,
        "texture.replace" => OperationKind.TextureReplace,
        "audio.replace" => OperationKind.AudioReplace,
        "hook.before" => OperationKind.HookBefore,
        "hook.after" => OperationKind.HookAfter,
        "hook.replace" => OperationKind.HookReplace,
        "call.argument.replace" => OperationKind.CallArgumentReplace,
        "global.json.load" => OperationKind.GlobalJsonLoad,
        _ => throw new JsonException($"Unknown operation enum '{value}'.")
    };

    public static string ToWire(OperationKind value) => value switch
    {
        OperationKind.StringReplace => "string.replace",
        OperationKind.StringAdd => "string.add",
        OperationKind.CodeReplace => "code.replace",
        OperationKind.ResourceRedirect => "resource.redirect",
        OperationKind.ResourceReplace => "resource.replace",
        OperationKind.ResourceAdd => "resource.add",
        OperationKind.SpriteReplace => "sprite.replace",
        OperationKind.SpriteFrameReplace => "sprite.frame.replace",
        OperationKind.FontRegister => "font.register",
        OperationKind.FontRedirect => "font.redirect",
        OperationKind.TextureReplace => "texture.replace",
        OperationKind.AudioReplace => "audio.replace",
        OperationKind.HookBefore => "hook.before",
        OperationKind.HookAfter => "hook.after",
        OperationKind.HookReplace => "hook.replace",
        OperationKind.CallArgumentReplace => "call.argument.replace",
        OperationKind.GlobalJsonLoad => "global.json.load",
        _ => throw new ArgumentOutOfRangeException(nameof(value), value, "Unknown operation enum.")
    };
}
