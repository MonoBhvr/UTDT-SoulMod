using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using UndertaleModLib;
using UndertaleModLib.Models;
using UndertaleModLib.Util;

namespace SoulMod.UmtWorker;

public static class Program
{
    private const int ProtocolVersion = 2;
    private const string ParserVersion = "undertalemodlib-0.8.4.1";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    public static int Main(string[] args)
    {
        try
        {
            if (args.Length != 2 || args[0] != "--request")
            {
                throw new ArgumentException("Usage: SoulMod.UmtWorker --request <absolute-request-path>");
            }

            WorkerRequest request = ReadRequest(args[1]);
            WorkerResult result = Compare(request);
            Console.Out.Write(JsonSerializer.Serialize(result, JsonOptions));
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    private static WorkerRequest ReadRequest(string requestPath)
    {
        if (!Path.IsPathFullyQualified(requestPath) || !File.Exists(requestPath))
        {
            throw new InvalidDataException("Request path must be an existing absolute file path.");
        }

        WorkerRequest request = JsonSerializer.Deserialize<WorkerRequest>(File.ReadAllBytes(requestPath), JsonOptions)
            ?? throw new InvalidDataException("Worker request was empty.");
        if (request.ProtocolVersion != ProtocolVersion || request.ParserVersion != ParserVersion)
        {
            throw new InvalidDataException("Unsupported worker protocol or parser version.");
        }

        ValidateAbsoluteFile(request.BasePath, "base");
        ValidateAbsoluteFile(request.PatchedPath, "patched");
        if (!Path.IsPathFullyQualified(request.OutputStagingDirectory))
        {
            throw new InvalidDataException("Output staging directory must be absolute.");
        }

        return request;
    }

    private static WorkerResult Compare(WorkerRequest request)
    {
        string baseHash = HashFile(request.BasePath);
        string patchedHash = HashFile(request.PatchedPath);
        if (baseHash != request.ExpectedBaseSha256 || patchedHash != request.ExpectedPatchedSha256)
        {
            throw new InvalidDataException("Input SHA-256 did not match the request.");
        }

        Directory.CreateDirectory(request.OutputStagingDirectory);
        using UndertaleData baseData = ReadData(request.BasePath);
        using UndertaleData patchedData = ReadData(request.PatchedPath);
        Dictionary<string, UndertaleFont> baseFonts = GetNamedFonts(baseData, "base");
        Dictionary<string, UndertaleFont> patchedFonts = GetNamedFonts(patchedData, "patched");
        string[] removedNames = baseFonts.Keys.Except(patchedFonts.Keys, StringComparer.Ordinal).OrderBy(name => name, StringComparer.Ordinal).ToArray();
        if (removedNames.Length != 0)
        {
            throw new InvalidDataException($"Base font names are missing from the patched data: {string.Join(", ", removedNames)}.");
        }

        var resources = new List<ChangedFontResource>();
        var artifactNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (string name in patchedFonts.Keys.OrderBy(value => value, StringComparer.Ordinal))
        {
            FontSnapshot patchedSnapshot = Snapshot(name, patchedFonts[name]);
            string? baseDigest = null;
            string changeKind = "add";
            if (baseFonts.TryGetValue(name, out UndertaleFont? baseFont))
            {
                changeKind = "replace";
                baseDigest = HashJson(Snapshot(name, baseFont));
                if (baseDigest == HashJson(patchedSnapshot))
                {
                    continue;
                }
            }

            string safeName = SanitizePathSegment(name);
            if (!artifactNames.Add(safeName))
            {
                throw new InvalidDataException("Distinct font names produced the same artifact path.");
            }
            WorkerArtifact metadata = WriteArtifact(request.OutputStagingDirectory, $"assets/fonts/{safeName}.json",
                JsonSerializer.SerializeToUtf8Bytes(patchedSnapshot, JsonOptions));
            WorkerArtifact atlas = WriteArtifact(request.OutputStagingDirectory, $"assets/fonts/{safeName}.atlas.png",
                GetAtlasBytes(patchedFonts[name]));
            resources.Add(new ChangedFontResource
            {
                Name = name,
                ChangeKind = changeKind,
                BaseGraphSha256 = baseDigest,
                Metadata = metadata,
                Atlas = atlas
            });
        }

        return new WorkerResult
        {
            ProtocolVersion = ProtocolVersion,
            ParserVersion = ParserVersion,
            BaseSha256 = baseHash,
            PatchedSha256 = patchedHash,
            Resources = resources
        };
    }

    private static UndertaleData ReadData(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return UndertaleIO.Read(stream);
    }

    private static Dictionary<string, UndertaleFont> GetNamedFonts(UndertaleData data, string inputName)
    {
        var fonts = new Dictionary<string, UndertaleFont>(StringComparer.Ordinal);
        foreach (UndertaleFont font in data.Fonts)
        {
            string? name = font.Name?.Content;
            if (string.IsNullOrWhiteSpace(name) || !fonts.TryAdd(name, font))
            {
                throw new InvalidDataException($"{inputName} has a missing or duplicate font name.");
            }
        }

        return fonts;
    }

    private static FontSnapshot Snapshot(string name, UndertaleFont font)
    {
        UndertaleTexturePageItem texture = font.Texture
            ?? throw new InvalidDataException($"Font '{name}' has no texture page item.");
        byte[] atlas = GetAtlasBytes(font);
        return new FontSnapshot(name, font.DisplayName?.Content, font.EmSize, font.EmSizeIsFloat, font.Bold,
            font.Italic, font.RangeStart, font.Charset, font.AntiAliasing, font.RangeEnd, font.ScaleX, font.ScaleY,
            new TexturePageSnapshot(texture.SourceX, texture.SourceY, texture.SourceWidth, texture.SourceHeight,
                texture.TargetX, texture.TargetY, texture.TargetWidth, texture.TargetHeight,
                texture.BoundingWidth, texture.BoundingHeight),
            font.Glyphs.Select(glyph => new GlyphSnapshot(glyph.Character, glyph.SourceX, glyph.SourceY,
                glyph.SourceWidth, glyph.SourceHeight, glyph.Shift, glyph.Offset,
                glyph.Kerning.Select(kerning => new KerningSnapshot(kerning.Character, kerning.ShiftModifier)).ToList())).ToList(),
            HashBytes(atlas));
    }

    private static byte[] GetAtlasBytes(UndertaleFont font) =>
        font.Texture?.TexturePage?.TextureData?.Image is null
            ? throw new InvalidDataException($"Font '{font.Name?.Content}' has no connected embedded texture.")
            : ExportAtlas(font.Texture);

    private static byte[] ExportAtlas(UndertaleTexturePageItem texture)
    {
        string path = Path.Combine(Path.GetTempPath(), $"SoulMod_UmtWorker_{Path.GetRandomFileName()}.png");
        var textureWorker = new TextureWorker();
        try
        {
            textureWorker.ExportAsPNG(texture, path);
            return File.ReadAllBytes(path);
        }
        finally
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
    }

    private static WorkerArtifact WriteArtifact(string stagingDirectory, string relativePath, byte[] content)
    {
        string destination = GetSafeOutputPath(stagingDirectory, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.WriteAllBytes(destination, content);
        return new WorkerArtifact { RelativePath = relativePath, Sha256 = HashFile(destination) };
    }

    private static string GetSafeOutputPath(string stagingDirectory, string relativePath)
    {
        if (Path.IsPathRooted(relativePath) || relativePath.Split('/').Any(part => part is "" or "." or ".."))
        {
            throw new InvalidDataException("Artifact path is unsafe.");
        }

        string root = Path.GetFullPath(stagingDirectory) + Path.DirectorySeparatorChar;
        string destination = Path.GetFullPath(Path.Combine(stagingDirectory, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!destination.StartsWith(root, StringComparison.Ordinal))
        {
            throw new InvalidDataException("Artifact path escapes the staging directory.");
        }

        return destination;
    }

    private static string SanitizePathSegment(string name) => Regex.Replace(name, "[^A-Za-z0-9._-]", "_");
    private static void ValidateAbsoluteFile(string path, string kind)
    {
        if (!Path.IsPathFullyQualified(path) || !File.Exists(path))
        {
            throw new FileNotFoundException($"{kind} input must be an existing absolute file path.", path);
        }
    }

    private static string HashJson<T>(T value) => HashBytes(JsonSerializer.SerializeToUtf8Bytes(value, JsonOptions));
    private static string HashFile(string path) { using FileStream stream = File.OpenRead(path); return HashStream(stream); }
    private static string HashStream(Stream stream) => Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    private static string HashBytes(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
}

public sealed record FontSnapshot(string Name, string? DisplayName, float EmSize, bool EmSizeIsFloat, bool Bold,
    bool Italic, ushort RangeStart, byte Charset, byte AntiAliasing, uint RangeEnd, float ScaleX, float ScaleY,
    TexturePageSnapshot Texture, List<GlyphSnapshot> Glyphs, string AtlasSha256);
public sealed record TexturePageSnapshot(ushort SourceX, ushort SourceY, ushort SourceWidth, ushort SourceHeight,
    ushort TargetX, ushort TargetY, ushort TargetWidth, ushort TargetHeight, ushort BoundingWidth, ushort BoundingHeight);
public sealed record GlyphSnapshot(ushort Character, ushort SourceX, ushort SourceY, ushort SourceWidth, ushort SourceHeight,
    short Shift, short Offset, List<KerningSnapshot> Kerning);
public sealed record KerningSnapshot(short Character, short ShiftModifier);
