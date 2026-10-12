using System.Text.Json;
using SoulMod.GameMakerDiff;
using SoulMod.Package;
using Xunit;

namespace SoulMod.Converter.Tests;

public sealed class CodeSemanticDiffTests
{
    [Theory]
    [InlineData("unchanged")]
    [InlineData("changed")]
    [InlineData("renamed")]
    [InlineData("ambiguous")]
    [InlineData("malformed")]
    public void Fixture_has_expected_typed_code_diff_outcome(string fixtureName)
    {
        CodeFixture fixture = LoadFixture(fixtureName);

        CodeSemanticDiffResult result = CodeSemanticDiffer.Compare(fixture.Base, fixture.Patched);

        Assert.Equal(fixture.ExpectedCandidates, result.Candidates.Count);
        Assert.Equal(fixture.ExpectedReports, result.Reports.Count);
        Assert.All(result.Reports, report => Assert.Equal(CodeDiffReportKind.ManualReviewRequired, report.Kind));
        Assert.Equal(fixture.ExpectedReportReasons, result.Reports.Select(report => report.Reason));
        if (fixtureName == "changed")
        {
            CodeReplacementCandidate candidate = Assert.Single(result.Candidates);
            Operation operation = ToOperation(candidate);
            Assert.Equal("code.replace", operation.OperationType);
            Assert.Equal("code", operation.Target.Type);
            Assert.Equal(fixture.ExpectedBaseGraphSha256, operation.Precondition!.Sha256);
            Assert.Equal(candidate.Base.GraphSha256, operation.Precondition!.Sha256);
            Assert.Equal(candidate.Patched.BytecodeSha256, operation.Value);
            Assert.True(PackageContractReflection.IsValid(operation));
        }
        else
        {
            Assert.Empty(result.Candidates);
        }
    }

    [Fact]
    public void Changed_fixture_is_deterministic_across_two_runs()
    {
        CodeFixture fixture = LoadFixture("changed");
        string first = JsonSerializer.Serialize(CodeSemanticDiffer.Compare(fixture.Base, fixture.Patched));
        string second = JsonSerializer.Serialize(CodeSemanticDiffer.Compare(fixture.Base, fixture.Patched));

        Assert.Equal(first, second);
    }

    [Theory]
    [InlineData("unchanged")]
    [InlineData("changed")]
    [InlineData("renamed")]
    [InlineData("ambiguous")]
    [InlineData("malformed")]
    public void Worker_fixture_converts_code_diff_to_safe_operations_or_typed_reports(string fixtureName)
    {
        CodeFixture fixture = LoadFixture(fixtureName);
        string workerResult = JsonSerializer.Serialize(new
        {
            protocolVersion = 5,
            parserVersion = "undertalemodlib-0.8.4.1",
            baseCodes = fixture.Base,
            patchedCodes = fixture.Patched
        });

        GameMakerDiffResult result = GameMakerDiffer.ConvertWorkerResultForTesting(workerResult, "fixture.code", 100);

        Assert.Empty(result.Operations);
        int expectedTransportReports = fixture.ExpectedReports + (fixtureName == "changed" ? 1 : 0);
        Assert.Equal(expectedTransportReports, result.Reports.Count);
        Assert.All(result.Reports, report => Assert.Equal(CodeDiffReportKind.ManualReviewRequired, report.Kind));
        List<string> expectedReasons = fixture.ExpectedReportReasons.ToList();
        if (fixtureName == "changed") expectedReasons.Add("RuntimeCodeReplaceUnsupported");
        Assert.Equal(expectedReasons, result.Reports.Select(report => report.Reason));
    }

    private static Operation ToOperation(CodeReplacementCandidate candidate) => new()
    {
        Id = "op_code_" + candidate.Base.Name,
        ModId = "fixture.code",
        Kind = OperationKind.CodeReplace,
        Target = new() { Type = "code", Name = candidate.Base.Name },
        Precondition = new() { Sha256 = candidate.Base.GraphSha256 },
        Value = candidate.Patched.BytecodeSha256,
        Selector = new()
        {
            ["bytecodeSha256"] = candidate.Patched.BytecodeSha256,
            ["instructionCount"] = candidate.Patched.InstructionCount
        },
        Reads = [$"code:{candidate.Base.Name}"],
        Writes = [$"code:{candidate.Base.Name}"]
    };

    private static CodeFixture LoadFixture(string name)
    {
        string path = Path.Combine(FindRepositoryRoot(), "tests", "fixtures", "golden", "code", name + ".json");
        return JsonSerializer.Deserialize<CodeFixture>(File.ReadAllBytes(path), new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true
        })
            ?? throw new InvalidDataException($"Fixture '{name}' was empty.");
    }

    private static string FindRepositoryRoot()
    {
        for (DirectoryInfo? current = new(AppContext.BaseDirectory); current is not null; current = current.Parent)
        {
            if (File.Exists(Path.Combine(current.FullName, "README.md"))) return current.FullName;
        }
        throw new DirectoryNotFoundException("Could not locate repository root.");
    }

    public sealed class CodeFixture
    {
        public List<CodeFingerprint> Base { get; set; } = [];
        public List<CodeFingerprint> Patched { get; set; } = [];
        public int ExpectedCandidates { get; set; }
        public int ExpectedReports { get; set; }
        public List<string> ExpectedReportReasons { get; set; } = [];
        public string? ExpectedBaseGraphSha256 { get; set; }
    }
}

internal static class PackageContractReflection
{
    internal static bool IsValid(Operation operation)
    {
        string root = Path.Combine(Path.GetTempPath(), "soulmod-code-contract-" + Guid.NewGuid().ToString("N"));
        string archivePath = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".soulmod");
        try
        {
            Directory.CreateDirectory(root);
            File.WriteAllText(Path.Combine(root, "manifest.json"), """
                {"formatVersion":1,"id":"fixture.code","name":"Fixture","version":"1.0.0","targetFile":"data.win","targets":[{"id":"fixture","targetFile":"data.win"}],"operationsFile":"operations.json"}
                """);
            File.WriteAllText(Path.Combine(root, "operations.json"), JsonSerializer.Serialize(new[] { operation }));
            try
            {
                PackageBuilder.PackDirectory(root, archivePath);
                return true;
            }
            catch (InvalidDataException)
            {
                return false;
            }
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
            if (File.Exists(archivePath)) File.Delete(archivePath);
        }
    }
}
