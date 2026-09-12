using Xunit;
using SoulMod.Package;

namespace SoulMod.Converter.Tests;

public class ManifestTests
{
    [Fact]
    public void Manifest_DefaultValues_AreValid()
    {
        var manifest = new Manifest
        {
            Id = "dtkr.runtime",
            Name = "DELTARUNE Korean Patch",
            Version = "0.1.0",
            TargetFile = "data.win",
            Targets = [new CompatibilityTarget { Id = "deltarune.chapter1", TargetFile = "data.win" }]
        };

        Assert.Equal("dtkr.runtime", manifest.Id);
        Assert.Equal("DELTARUNE Korean Patch", manifest.Name);
        Assert.Equal("0.1.0", manifest.Version);
        Assert.Equal(1, manifest.FormatVersion);
        Assert.Equal("stop", manifest.ConflictPolicy);
        Assert.Single(manifest.Targets);
    }
}
