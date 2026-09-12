using SoulMod.ExecutionCopy;
using Xunit;

namespace SoulMod.ExecutionCopy.Tests;

public sealed class ExecutionCopyManagerTests
{
    [Fact]
    public void PrepareCopy_happy_path_produces_managed_copy_and_proves_source_unchanged()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        using TempDir managedRoot = new();
        ExecutionCopyManager manager = new();

        PrepareOutcome outcome = manager.PrepareCopy(install.AsInstall(), managedRoot.Path);

        var prepared = Assert.IsType<PrepareOutcome.Prepared>(outcome);
        Assert.True(File.Exists(prepared.Copy.ExecutablePath), "managed EXE exists");
        Assert.True(File.Exists(prepared.Copy.DataWinPath), "managed data.win exists");
        Assert.True(File.Exists(Path.Combine(prepared.Copy.ManagedRootPath, ".soulmod-copy-manifest.json")));

        Assert.Equal(prepared.Copy.SourceIntegrityBefore, prepared.Copy.SourceIntegrityAfter);
        Assert.Equal(prepared.Copy.SourceIntegrityBefore.Executable.Sha256, prepared.Copy.CopyIntegrity.Executable.Sha256);
        Assert.Equal(prepared.Copy.SourceIntegrityBefore.DataWin.Sha256, prepared.Copy.CopyIntegrity.DataWin.Sha256);
        Assert.Equal(prepared.Copy.SourceIntegrityBefore.DirectoryManifestSha256, prepared.Copy.CopyIntegrity.DirectoryManifestSha256);
    }

    [Theory]
    [InlineData(OverlapKind.TargetEqualsSource)]
    [InlineData(OverlapKind.TargetInsideSource)]
    [InlineData(OverlapKind.SourceInsideTarget)]
    public void PrepareCopy_rejects_source_target_overlap_without_mutating_source(OverlapKind kind)
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir outer = new();
        string managedRoot = kind switch
        {
            OverlapKind.TargetEqualsSource => install.RootPath,
            OverlapKind.TargetInsideSource => Path.Combine(install.RootPath, "managed"),
            OverlapKind.SourceInsideTarget => Path.Combine(outer.Path, "container", new DirectoryInfo(install.RootPath).Name),
            _ => throw new ArgumentOutOfRangeException(nameof(kind)),
        };

        var failed = Assert.IsType<PrepareOutcome.Failed>(new ExecutionCopyManager().PrepareCopy(install.AsInstall(), managedRoot));
        Assert.Equal(ExecutionCopyFailureReason.SourceTargetOverlap, failed.Failure.Reason);

        Assert.True(File.Exists(Path.Combine(install.RootPath, TestSupport.ExeName)), "source EXE untouched after rejected copy");
        Assert.True(File.Exists(Path.Combine(install.RootPath, TestSupport.DataWinName)), "source data.win untouched after rejected copy");
    }

    [Fact]
    public void PrepareCopy_rejects_missing_executable_and_data_win()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        File.Delete(Path.Combine(install.RootPath, TestSupport.ExeName));
        using TempDir managedRoot = new();

        var failedExe = Assert.IsType<ResolveOutcome.Failed>(
            new ExecutionCopyManager().ResolveInstall(install.RootPath, TestSupport.ExeName, TestSupport.DataWinName));
        Assert.Equal(ExecutionCopyFailureReason.SourceExecutableMissing, failedExe.Failure.Reason);

        File.WriteAllText(Path.Combine(install.RootPath, TestSupport.ExeName), "exe");
        File.Delete(Path.Combine(install.RootPath, TestSupport.DataWinName));
        var failedDataWin = Assert.IsType<ResolveOutcome.Failed>(
            new ExecutionCopyManager().ResolveInstall(install.RootPath, TestSupport.ExeName, TestSupport.DataWinName));
        Assert.Equal(ExecutionCopyFailureReason.SourceDataWinMissing, failedDataWin.Failure.Reason);
    }

    [Fact]
    public void PrepareCopy_rejects_nonexistent_install_root()
    {
        var failed = Assert.IsType<ResolveOutcome.Failed>(
            new ExecutionCopyManager().ResolveInstall(Path.Combine(Path.GetTempPath(), "soulmod-nope-" + Guid.NewGuid()), TestSupport.ExeName, TestSupport.DataWinName));
        Assert.Equal(ExecutionCopyFailureReason.InvalidInstallRoot, failed.Failure.Reason);
    }

    [Fact]
    public void PrepareCopy_refuses_unowned_existing_managed_root_and_does_not_delete_it()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir managedRoot = new();
        string userFile = Path.Combine(managedRoot.Path, "user-owned.txt");
        File.WriteAllText(userFile, "do not touch");

        var failed = Assert.IsType<PrepareOutcome.Failed>(
            new ExecutionCopyManager().PrepareCopy(install.AsInstall(), managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.StalePartialUnowned, failed.Failure.Reason);
        Assert.True(File.Exists(userFile), "unowned managed root preserved");
    }

    [Fact]
    public void PrepareCopy_idempotently_replaces_an_owned_managed_root()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir managedRoot = new();
        ExecutionCopyManager manager = new();

        PrepareOutcome first = manager.PrepareCopy(install.AsInstall(), managedRoot.Path);
        Assert.IsType<PrepareOutcome.Prepared>(first);
        string firstRunId = ((PrepareOutcome.Prepared)first).Copy.RunId;

        PrepareOutcome second = manager.PrepareCopy(install.AsInstall(), managedRoot.Path);
        var secondPrepared = Assert.IsType<PrepareOutcome.Prepared>(second);
        Assert.NotEqual(firstRunId, secondPrepared.Copy.RunId);
        Assert.True(File.Exists(secondPrepared.Copy.ExecutablePath));
    }

    [Fact]
    public void PrepareCopy_cancellation_removes_partial_staging_tree()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        using TempDir parent = new();
        string managedRoot = Path.Combine(parent.Path, "managed");

        using CancellationTokenSource cts = new();
        cts.Cancel();
        var failed = Assert.IsType<PrepareOutcome.Failed>(
            new ExecutionCopyManager().PrepareCopy(install.AsInstall(), managedRoot, cts.Token));
        Assert.Equal(ExecutionCopyFailureReason.CopyCancelled, failed.Failure.Reason);

        Assert.False(Directory.Exists(managedRoot), "managed root never created on cancellation");
        Assert.Empty(Directory.EnumerateDirectories(parent.Path, ".soulmod-staging-*"));
    }

    [Fact]
    public void CleanPartial_is_idempotent_and_removes_owned_root_plus_staging()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir managedRoot = new();
        ExecutionCopyManager manager = new();
        Assert.IsType<PrepareOutcome.Prepared>(manager.PrepareCopy(install.AsInstall(), managedRoot.Path));

        var first = Assert.IsType<CleanupOutcome.Cleaned>(manager.CleanPartial(managedRoot.Path));
        Assert.True(first.Receipt.RemovedManagedRoot);

        var second = Assert.IsType<CleanupOutcome.Cleaned>(manager.CleanPartial(managedRoot.Path));
        Assert.False(second.Receipt.RemovedManagedRoot);
        Assert.False(Directory.Exists(managedRoot.Path));
    }

    [Fact]
    public void CleanPartial_refuses_unowned_root()
    {
        using TempDir managedRoot = new();
        File.WriteAllText(Path.Combine(managedRoot.Path, "user.txt"), "mine");

        var failed = Assert.IsType<CleanupOutcome.Failed>(new ExecutionCopyManager().CleanPartial(managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.StalePartialUnowned, failed.Failure.Reason);
        Assert.True(File.Exists(Path.Combine(managedRoot.Path, "user.txt")));
    }

    [Fact]
    public void PrepareCopy_source_directory_manifest_is_byte_stable_across_runs()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        ExecutionCopyManager manager = new();
        var resolvedA = Assert.IsType<ResolveOutcome.Resolved>(manager.ResolveInstall(install.RootPath, TestSupport.ExeName, TestSupport.DataWinName));
        var resolvedB = Assert.IsType<ResolveOutcome.Resolved>(manager.ResolveInstall(install.RootPath, TestSupport.ExeName, TestSupport.DataWinName));
        Assert.Equal(resolvedA.Integrity.DirectoryManifestSha256, resolvedB.Integrity.DirectoryManifestSha256);
        Assert.Equal(resolvedA.Integrity.Executable.Sha256, resolvedB.Integrity.Executable.Sha256);
    }
}
