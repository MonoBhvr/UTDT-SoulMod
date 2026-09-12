using System.Text;

namespace SoulMod.ExecutionCopy.Tests;

/// <summary>Shared fixtures for execution-copy test classes: temp dirs, fake installs, overlap kinds.</summary>
internal static class TestSupport
{
    public const string ExeName = "FakeGame.exe";
    public const string DataWinName = "data.win";
}

internal enum OverlapKind { TargetEqualsSource, TargetInsideSource, SourceInsideTarget }

internal sealed class FakeInstall : IDisposable
{
    private readonly TempDir _dir;
    internal string RootPath => _dir.Path;

    private FakeInstall(TempDir dir) => _dir = dir;

    internal static FakeInstall Create(string exeName, string dataWinName, bool withExtraFiles = false)
    {
        TempDir dir = new();
        File.WriteAllBytes(Path.Combine(dir.Path, exeName), [0x4D, 0x5A, 0x90, 0]);
        File.WriteAllText(Path.Combine(dir.Path, dataWinName), "FORM fake data.win payload", Encoding.UTF8);
        if (withExtraFiles)
        {
            Directory.CreateDirectory(Path.Combine(dir.Path, "mus"));
            File.WriteAllText(Path.Combine(dir.Path, "mus", "legend.ogg"), "audio");
            File.WriteAllText(Path.Combine(dir.Path, "audiogroup1.dat"), "ag1");
        }
        return new FakeInstall(dir);
    }

    internal GameInstall AsInstall() => new(RootPath, TestSupport.ExeName, TestSupport.DataWinName);

    public void Dispose() => _dir.Dispose();
}

internal sealed class TempDir : IDisposable
{
    internal string Path { get; }

    internal TempDir()
    {
        Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "soulmod-exec-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path);
    }

    public void Dispose()
    {
        try { if (Directory.Exists(Path)) Directory.Delete(Path, recursive: true); }
        catch (IOException) { }
    }
}
