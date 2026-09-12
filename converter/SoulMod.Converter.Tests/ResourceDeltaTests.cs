using SoulMod.GameMakerDiff;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class ResourceDeltaTests
{
    [Fact]
    public void CompareDataFiles_Chapter1Patch_EmitsNamedFontReplacementsAndAddition()
    {
        string repositoryRoot = FindRepositoryRoot();
        string basePath = Path.Combine(repositoryRoot, "scratch", "test_game", "chapter1_windows", "data.win");
        string patchedPath = Path.Combine(repositoryRoot, "mods", "test", "dtkr_diff", "data_dtkr_ch1.win");
        Assert.True(File.Exists(basePath), $"Missing clean Chapter 1 fixture: {basePath}");
        Assert.True(File.Exists(patchedPath), $"Missing patched Chapter 1 fixture: {patchedPath}");

        GameMakerDiffResult result = GameMakerDiffer.CompareDataFiles(
            basePath,
            patchedPath,
            new GameMakerDiffOptions
            {
                ModId = "fixture.semantic-diff",
                ModName = "Semantic Diff Fixture",
                ModVersion = "1.0.0"
            });

        Assert.Equal(7, result.Operations.Count);
        SoulMod.Package.Operation replacement = Assert.Single(result.Operations, operation =>
            operation.OperationType == "resource.replace" && operation.Target.Type == "font" &&
            operation.Target.Name == "fnt_ja_main");
        SoulMod.Package.Operation addition = Assert.Single(result.Operations, operation =>
            operation.OperationType == "resource.add" && operation.Target.Type == "font" &&
            operation.Target.Name == "fnt_ja_legend");
        Assert.NotNull(replacement.Precondition);
        Assert.Null(addition.Precondition);
        Assert.All(result.Operations, operation => Assert.StartsWith("fnt_ja_", operation.Target.Name, StringComparison.Ordinal));
        Assert.DoesNotContain(result.Operations, operation => operation.Target.Name.StartsWith("index_", StringComparison.Ordinal));
        Assert.DoesNotContain(result.Operations, operation => operation.OperationType == "font.redirect");
        Assert.Equal(result.Operations.Count * 2, result.Assets.Count);
        AssertAssets(result, replacement);
        AssertAssets(result, addition);

        GameMakerDiffResult repeated = GameMakerDiffer.CompareDataFiles(basePath, patchedPath, new GameMakerDiffOptions
        {
            ModId = "fixture.semantic-diff",
            ModName = "Semantic Diff Fixture",
            ModVersion = "1.0.0"
        });
        Assert.Equal(
            result.Operations.Select(operation => (operation.Target.Name, operation.OperationType, operation.AssetSha256)),
            repeated.Operations.Select(operation => (operation.Target.Name, operation.OperationType, operation.AssetSha256)));
    }

    private static void AssertAssets(GameMakerDiffResult result, SoulMod.Package.Operation operation)
    {
        Assert.NotNull(operation.AssetPath);
        Assert.NotNull(operation.AssetSha256);
        GeneratedAsset metadata = Assert.Single(result.Assets, asset => asset.Path == operation.AssetPath);
        Assert.Equal(operation.AssetSha256, metadata.Sha256);
        Assert.NotEmpty(metadata.Content);
        Assert.NotNull(operation.Selector);
        string atlasPath = (string)operation.Selector!["atlasPath"];
        string atlasSha256 = (string)operation.Selector["atlasSha256"];
        GeneratedAsset atlas = Assert.Single(result.Assets, asset => asset.Path == atlasPath);
        Assert.Equal(atlasSha256, atlas.Sha256);
        Assert.NotEmpty(atlas.Content);
    }

    private static string FindRepositoryRoot()
    {
        for (DirectoryInfo? current = new(AppContext.BaseDirectory); current is not null; current = current.Parent)
        {
            if (File.Exists(Path.Combine(current.FullName, "README.md")) &&
                Directory.Exists(Path.Combine(current.FullName, "scratch", "test_game")))
            {
                return current.FullName;
            }
        }

        throw new DirectoryNotFoundException("Could not locate the repository root from the test output directory.");
    }
}
