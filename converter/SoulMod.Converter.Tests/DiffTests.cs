using System.IO;
using Xunit;
using SoulMod.GameMakerDiff;
using SoulMod.Importer.XDelta;

namespace SoulMod.Converter.Tests;

public class DiffTests
{
    [Fact]
    public void SHA256_Calculation_IsCorrect()
    {
        byte[] testBytes = System.Text.Encoding.UTF8.GetBytes("UTDT SoulMod Test");
        using var ms = new MemoryStream(testBytes);

        string hash = GameMakerDiffer.ComputeSha256(ms);
        Assert.NotNull(hash);
        Assert.Equal(64, hash.Length);
    }

    [Fact]
    public void CompareDataFiles_WhenInputsMatch_ReturnsNoOperationsWithoutWorker()
    {
        string path = Path.Combine(Path.GetTempPath(), $"SoulMod_Diff_{Path.GetRandomFileName()}.win");
        try
        {
            File.WriteAllBytes(path, [0x46, 0x4F, 0x52, 0x4D]);
            GameMakerDiffResult result = GameMakerDiffer.CompareDataFiles(path, path, new GameMakerDiffOptions());

            Assert.Empty(result.Operations);
            Assert.Empty(result.Assets);
            Assert.Equal("generated.mod", result.Manifest.Id);
            Assert.Equal("Generated Mod", result.Manifest.Name);
        }
        finally
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
    }

    [Fact]
    public void ImportOptions_DefaultToNeutralGeneratedMetadata()
    {
        var options = new XDeltaImporterOptions
        {
            XDeltaExecutablePath = "xdelta3.exe",
            BaseDataWinPath = "base.win",
            PatchFilePath = "patch.xdelta",
            OutputSoulModPath = "output.soulmod"
        };

        Assert.Equal("generated.mod", options.ModId);
        Assert.Equal("Generated Mod", options.ModName);
    }
}
