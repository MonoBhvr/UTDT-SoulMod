using System.IO;
using Xunit;
using SoulMod.Package;
using SoulMod.GameMakerDiff;

namespace SoulMod.Converter.Tests;

public class IntegrationTests
{
    private const string DeltarunePath = @"C:\Program Files (x86)\Steam\steamapps\common\DELTARUNE";
    private const string ExpectedCleanSha256 = "82A7041A530407C5F17E439A97BDF53E4A24E30775A7ECBC2879369B9D2441D6";

    [Fact]
    public void Deltarune_RestoredCleanDataWin_HashValidation_Passes()
    {
        string dataWinPath = Path.Combine(DeltarunePath, "data.win");
        Assert.True(File.Exists(dataWinPath), $"DELTARUNE data.win not found at {dataWinPath}");

        string actualHash = GameMakerDiffer.ComputeSha256File(dataWinPath);
        Assert.Equal(ExpectedCleanSha256, actualHash.ToUpperInvariant());
    }

    [Fact]
    public void SampleModPackage_Verification_Passes()
    {
        string repoRoot = FindRepositoryRoot();
        string modSampleDir = Path.Combine(repoRoot, "tests", "fixtures", "package-minimal");
        string manifestPath = Path.Combine(modSampleDir, "manifest.json");
        string opsPath = Path.Combine(modSampleDir, "operations.json");

        Assert.True(File.Exists(manifestPath), $"manifest.json missing at {manifestPath}");
        Assert.True(File.Exists(opsPath), $"operations.json missing at {opsPath}");

        string packagePath = Path.Combine(Path.GetTempPath(), $"SoulModFixture_{Path.GetRandomFileName()}.soulmod");
        try
        {
            PackageBuilder.PackDirectory(modSampleDir, packagePath);
            PackageDocument package = PackageBuilder.ReadPackageFromZip(packagePath);
            Manifest manifest = package.Manifest;
            Operation operation = Assert.Single(package.Operations);

            Assert.Equal("minimal-fixture", manifest.Id);
            Assert.Equal("operations.json", manifest.OperationsFile);
            Assert.Equal(manifest.Id, operation.ModId);
            Assert.Equal("string.replace", operation.OperationType);
            Assert.Equal("string", operation.Target.Type);
            Assert.Equal("fixture.message", operation.Target.Name);
        }
        finally
        {
            if (File.Exists(packagePath)) File.Delete(packagePath);
        }
    }

    private static string FindRepositoryRoot()
    {
        for (DirectoryInfo? current = new(AppContext.BaseDirectory); current is not null; current = current.Parent)
        {
            if (File.Exists(Path.Combine(current.FullName, "README.md")) &&
                Directory.Exists(Path.Combine(current.FullName, "tests", "fixtures")))
            {
                return current.FullName;
            }
        }

        throw new DirectoryNotFoundException("Could not locate the repository root from the test output directory.");
    }
}
