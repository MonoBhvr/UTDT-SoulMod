using System.Text.Json;
using SoulMod.Package;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class PackageContractTests
{
    [Fact]
    public void PackDirectory_ValidatesSharedConformanceCases()
    {
        JsonElement[] cases = LoadCases();
        foreach (JsonElement testCase in cases)
        {
            bool valid = testCase.GetProperty("valid").GetBoolean();
            string temporaryRoot = Path.Combine(Path.GetTempPath(), $"SoulModPackageContract_{Path.GetRandomFileName()}");
            string packagePath = $"{temporaryRoot}.soulmod";
            try
            {
                Directory.CreateDirectory(temporaryRoot);
                WriteCase(testCase, temporaryRoot);
                if (valid)
                {
                    PackageBuilder.PackDirectory(temporaryRoot, packagePath);
                    PackageDocument package = PackageBuilder.ReadPackageFromZip(packagePath);
                    Assert.Equal("contract.fixture", package.Manifest.Id);
                }
                else
                {
                    InvalidDataException error = Assert.Throws<InvalidDataException>(
                        () => PackageBuilder.PackDirectory(temporaryRoot, packagePath));
                    Assert.False(string.IsNullOrWhiteSpace(error.Message));
                }
            }
            finally
            {
                if (File.Exists(packagePath)) File.Delete(packagePath);
                if (Directory.Exists(temporaryRoot)) Directory.Delete(temporaryRoot, true);
            }
        }
    }

    private static JsonElement[] LoadCases()
    {
        string repositoryRoot = FindRepositoryRoot();
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(repositoryRoot, "tests", "fixtures", "package-contract", "cases.json")));
        return document.RootElement.EnumerateArray().Select(element => element.Clone()).ToArray();
    }

    private static void WriteCase(JsonElement testCase, string directory)
    {
        string manifest = testCase.TryGetProperty("rawManifest", out JsonElement rawManifest)
            ? rawManifest.GetString()!
            : testCase.GetProperty("manifest").GetRawText();
        File.WriteAllText(Path.Combine(directory, "manifest.json"), manifest);
        if (testCase.TryGetProperty("operations", out JsonElement operations))
        {
            File.WriteAllText(Path.Combine(directory, "operations.json"), operations.GetRawText());
        }
    }

    private static string FindRepositoryRoot()
    {
        for (DirectoryInfo? current = new(AppContext.BaseDirectory); current is not null; current = current.Parent)
        {
            if (Directory.Exists(Path.Combine(current.FullName, "tests", "fixtures"))) return current.FullName;
        }

        throw new DirectoryNotFoundException("Could not locate the repository root.");
    }
}
