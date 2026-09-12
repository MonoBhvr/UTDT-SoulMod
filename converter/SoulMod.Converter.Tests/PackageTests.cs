using System.IO;
using Xunit;
using SoulMod.Package;

namespace SoulMod.Converter.Tests;

public class PackageTests
{
    [Fact]
    public void Pack_And_Unpack_Package_Succeeds()
    {
        string tempDir = Path.Combine(Path.GetTempPath(), "SoulModTest_" + Path.GetRandomFileName());
        string sourceDir = Path.Combine(tempDir, "SourceMod");
        string unpackDir = Path.Combine(tempDir, "UnpackedMod");
        string zipFile = Path.Combine(tempDir, "test.soulmod");

        try
        {
            Directory.CreateDirectory(sourceDir);
            
            string manifestContent = @"{
              ""formatVersion"": 1,
              ""id"": ""test.mod"",
              ""name"": ""Test Mod"",
              ""version"": ""1.0.0"",
              ""targetFile"": ""data.win"",
              ""targets"": [{ ""id"": ""deltarune"", ""targetFile"": ""data.win"" }],
              ""operationsFile"": ""operations.json""
            }";
            File.WriteAllText(Path.Combine(sourceDir, "manifest.json"), manifestContent);

            string opsContent = @"[
              {
                ""id"": ""test.op.1"",
                ""operationVersion"": 1,
                ""modId"": ""test.mod"",
                ""target"": { ""type"": ""string"", ""name"": ""txt_intro"" },
                ""operation"": ""string.replace"",
                ""precondition"": { ""expectedValue"": ""Once upon a time..."" },
                ""value"": ""옛날 옛적에..."",
                ""reads"": [""string:txt_intro""],
                ""writes"": [""string:txt_intro""]
              }
            ]";
            File.WriteAllText(Path.Combine(sourceDir, "operations.json"), opsContent);

            // 1. Pack
            PackageBuilder.PackDirectory(sourceDir, zipFile);
            Assert.True(File.Exists(zipFile));

            // 2. Read Zip Manifest & Ops directly
            var manifest = PackageBuilder.ReadManifestFromZip(zipFile);
            Assert.Equal("test.mod", manifest.Id);
            Assert.Equal("Test Mod", manifest.Name);

            var ops = PackageBuilder.ReadOperationsFromZip(zipFile, manifest);
            Assert.Single(ops);
            Assert.Equal("string.replace", ops[0].OperationType);
            Assert.Equal("옛날 옛적에...", ops[0].Value);

            // 3. Unpack
            PackageBuilder.UnpackPackage(zipFile, unpackDir);
            Assert.True(File.Exists(Path.Combine(unpackDir, "manifest.json")));
            Assert.True(File.Exists(Path.Combine(unpackDir, "operations.json")));
        }
        finally
        {
            if (Directory.Exists(tempDir))
            {
                Directory.Delete(tempDir, true);
            }
        }
    }
}
