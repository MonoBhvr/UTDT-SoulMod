using SoulMod.GameMakerDiff;
using SoulMod.Importer.XDelta;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class XDeltaImporterTests
{
    [Fact]
    public void ImportXDeltaPatch_WhenDecoderIsUnavailable_Throws_AndDoesNotProducePackage()
    {
        string tempDirectory = Path.Combine(Path.GetTempPath(), $"SoulMod_XDelta_Test_{Path.GetRandomFileName()}");
        string basePath = Path.Combine(tempDirectory, "data.win");
        string patchPath = Path.Combine(tempDirectory, "patch.xdelta");
        string outputPath = Path.Combine(tempDirectory, "output.soulmod");

        try
        {
            Directory.CreateDirectory(tempDirectory);
            File.WriteAllBytes(basePath, [0x46, 0x4F, 0x52, 0x4D]);
            File.WriteAllBytes(patchPath, [0xD6, 0xC3, 0xC4]);
            string originalBaseHash = GameMakerDiffer.ComputeSha256File(basePath);

            Assert.Throws<FileNotFoundException>(() => XDeltaImporter.ImportXDeltaPatch(new XDeltaImporterOptions
            {
                XDeltaExecutablePath = Path.Combine(tempDirectory, "missing-xdelta3.exe"),
                BaseDataWinPath = basePath,
                PatchFilePath = patchPath,
                OutputSoulModPath = outputPath
            }));

            Assert.False(File.Exists(outputPath));
            Assert.Equal(originalBaseHash, GameMakerDiffer.ComputeSha256File(basePath));
        }
        finally
        {
            if (Directory.Exists(tempDirectory))
            {
                Directory.Delete(tempDirectory, true);
            }
        }
    }
}
