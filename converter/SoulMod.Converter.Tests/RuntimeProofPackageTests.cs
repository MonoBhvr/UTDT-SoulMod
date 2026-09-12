using System.Security.Cryptography;
using System.Text.Json;
using SoulMod.Package;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class Chapter1PackageTests
{
    [Fact]
    public void Chapter1Package_DeclaresJapaneseScopedLanguageAndFontOperations()
    {
        string packageDirectory = FindRepositoryPath("mods", "dtkr.ch1");
        string manifestPath = Path.Combine(packageDirectory, "manifest.json");
        string operationsPath = Path.Combine(packageDirectory, "operations.json");
        string packageArchive = Path.Combine(Path.GetTempPath(), "SoulModChapter1Package_" + Path.GetRandomFileName() + ".soulmod");

        Assert.True(File.Exists(manifestPath), $"Missing manifest: {manifestPath}");
        Assert.True(File.Exists(operationsPath), $"Missing operations: {operationsPath}");

        try
        {
            PackageBuilder.PackDirectory(packageDirectory, packageArchive);
            Assert.True(File.Exists(packageArchive));
        }
        finally
        {
            if (File.Exists(packageArchive)) File.Delete(packageArchive);
        }

        using JsonDocument manifest = JsonDocument.Parse(File.ReadAllText(manifestPath));
        Assert.Equal(1, manifest.RootElement.GetProperty("formatVersion").GetInt32());
        Assert.Equal("dtkr.ch1", manifest.RootElement.GetProperty("id").GetString());
        Assert.Equal("chapter1_windows/data.win", manifest.RootElement.GetProperty("targetFile").GetString());
        Assert.Equal("82c2bb61b8d78cd287120f6301588fecba34ec5a890bac711b7a8774c760ec70",
            manifest.RootElement.GetProperty("targetDataSha256").GetString());
        JsonElement target = Assert.Single(manifest.RootElement.GetProperty("targets").EnumerateArray());
        Assert.Equal("deltarune.chapter1", target.GetProperty("id").GetString());
        Assert.Equal("chapter1_windows/data.win", target.GetProperty("targetFile").GetString());
        Assert.Equal("82c2bb61b8d78cd287120f6301588fecba34ec5a890bac711b7a8774c760ec70",
            target.GetProperty("targetDataSha256").GetString());

        using JsonDocument operations = JsonDocument.Parse(File.ReadAllText(operationsPath));
        JsonElement[] entries = operations.RootElement.EnumerateArray().ToArray();

        Assert.Equal(
            new[] { "font.redirect", "font.register", "global.json.load" },
            entries.Select(entry => entry.GetProperty("operation").GetString()).Order().ToArray());
        Assert.DoesNotContain(entries, entry =>
            entry.GetProperty("operation").GetString() == "string.replace");
        Assert.DoesNotContain(entries, entry =>
            entry.GetProperty("operation").GetString() == "resource.redirect");

        AssertAsset(entries, packageDirectory, "global.json.load", "assets/lang/lang_ja.json",
            "78a7f9f2fece7fd7e4ebc6fb44cb0c0a2adb42f6a27ff364d6b93342259b7425");
        JsonElement globalJsonLoad = Assert.Single(entries, entry =>
            entry.GetProperty("operation").GetString() == "global.json.load");
        Assert.Equal("global", globalJsonLoad.GetProperty("target").GetProperty("type").GetString());
        Assert.Equal("lang_map", globalJsonLoad.GetProperty("target").GetProperty("name").GetString());
        JsonElement globalSelector = globalJsonLoad.GetProperty("selector");
        Assert.Equal("first.draw_set_font", globalSelector.GetProperty("applyAt").GetString());
        Assert.False(globalSelector.TryGetProperty("mirrorGlobals", out _));
        JsonElement when = globalSelector.GetProperty("when");
        Assert.Equal("lang", when.GetProperty("global").GetString());
        Assert.Equal("ja", when.GetProperty("equals").GetString());
        Assert.True(AppliesForLanguage(when, "ja"));
        Assert.False(AppliesForLanguage(when, "en"));
        AssertAsset(entries, packageDirectory, "font.register", "assets/fonts/Galmuri11.ttf",
            "2c709890595668f7bdb6df408420fda957dde0288e95b31a1cc17a2ab98b4b4f");

        Dictionary<string, string?> fontRedirects = entries
            .Where(entry => entry.GetProperty("operation").GetString() == "font.redirect")
            .ToDictionary(
                entry => entry.GetProperty("target").GetProperty("name").GetString()!,
                entry => entry.GetProperty("value").GetString());
        Assert.DoesNotContain("fnt_main", fontRedirects.Keys);
        Assert.Equal("soulmod.galmuri11", fontRedirects["fnt_ja_main"]);
        Assert.DoesNotContain(entries, entry =>
            entry.GetProperty("operation").GetString() == "font.redirect" &&
            entry.TryGetProperty("selector", out _));

        string licensePath = Path.Combine(packageDirectory, "assets", "fonts", "LICENSE.txt");
        Assert.Contains("SIL OPEN FONT LICENSE Version 1.1", File.ReadAllText(licensePath));
    }

    [Fact]
    public void Chapter1Package_RejectsAssetHashMismatchBeforePacking()
    {
        string packageDirectory = FindRepositoryPath("mods", "dtkr.ch1");
        string tempDirectory = Path.Combine(Path.GetTempPath(), "SoulModChapter1Package_" + Path.GetRandomFileName());

        try
        {
            Directory.CreateDirectory(Path.Combine(tempDirectory, "assets", "fonts"));
            Directory.CreateDirectory(Path.Combine(tempDirectory, "assets", "lang"));
            File.Copy(Path.Combine(packageDirectory, "manifest.json"), Path.Combine(tempDirectory, "manifest.json"));
            File.Copy(Path.Combine(packageDirectory, "assets", "fonts", "Galmuri11.ttf"), Path.Combine(tempDirectory, "assets", "fonts", "Galmuri11.ttf"));
            File.Copy(Path.Combine(packageDirectory, "assets", "lang", "lang_ja.json"), Path.Combine(tempDirectory, "assets", "lang", "lang_ja.json"));
            string operations = File.ReadAllText(Path.Combine(packageDirectory, "operations.json"));
            File.WriteAllText(Path.Combine(tempDirectory, "operations.json"), operations.Replace(
                "78a7f9f2fece7fd7e4ebc6fb44cb0c0a2adb42f6a27ff364d6b93342259b7425", new string('0', 64), StringComparison.Ordinal));

            Assert.Throws<InvalidDataException>(() => PackageBuilder.PackDirectory(tempDirectory, Path.Combine(tempDirectory, "chapter1.soulmod")));
        }
        finally
        {
            if (Directory.Exists(tempDirectory)) Directory.Delete(tempDirectory, true);
        }
    }

    private static bool AppliesForLanguage(JsonElement condition, string activeLanguage) =>
        condition.GetProperty("equals").GetString() == activeLanguage;

    private static void AssertAsset(
        IEnumerable<JsonElement> entries,
        string packageDirectory,
        string operationType,
        string expectedAssetPath,
        string expectedSha256)
    {
        JsonElement operation = Assert.Single(entries, entry =>
            entry.GetProperty("operation").GetString() == operationType);
        Assert.Equal(expectedAssetPath, operation.GetProperty("assetPath").GetString());
        Assert.Equal(expectedSha256, operation.GetProperty("assetSha256").GetString());

        string assetPath = Path.Combine(
            packageDirectory,
            expectedAssetPath.Replace('/', Path.DirectorySeparatorChar));
        using FileStream stream = File.OpenRead(assetPath);
        Assert.Equal(expectedSha256, Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant());
    }

    private static string FindRepositoryPath(params string[] relativeSegments)
    {
        DirectoryInfo? directory = new(AppContext.BaseDirectory);
        while (directory is not null && !Directory.Exists(Path.Combine(directory.FullName, "mods")))
        {
            directory = directory.Parent;
        }

        Assert.NotNull(directory);
        return Path.Combine([directory.FullName, .. relativeSegments]);
    }
}
