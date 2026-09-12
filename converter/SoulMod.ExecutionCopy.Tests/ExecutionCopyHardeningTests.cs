using SoulMod.ExecutionCopy;
using Xunit;

namespace SoulMod.ExecutionCopy.Tests;

/// <summary>
/// Failing-first regressions for the Wave 2 hardening: forged/escaped children, rollback-safe
/// promotion, ownership-bound staging cleanup, persisted-hash equality, directory symlinks.
/// Each would fail against the pre-hardening manager.
/// </summary>
public sealed class ExecutionCopyHardeningTests
{
    [Fact]
    public void PrepareCopy_rejects_forged_install_with_escaped_child()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir managedRoot = new();
        string outside = Path.Combine(Directory.GetParent(install.RootPath)!.FullName, "outside.exe");
        File.WriteAllText(outside, "outside");
        GameInstall forged = new(install.RootPath, "../outside.exe", TestSupport.DataWinName);

        var failed = Assert.IsType<PrepareOutcome.Failed>(new ExecutionCopyManager().PrepareCopy(forged, managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.InvalidPath, failed.Failure.Reason);
        Assert.False(Directory.Exists(managedRoot.Path), "managed root created from forged install");
    }

    [Fact]
    public void PrepareCopy_rejects_forged_install_with_absolute_child()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName);
        using TempDir managedRoot = new();
        GameInstall forged = new(install.RootPath, Path.Combine(install.RootPath, TestSupport.ExeName), TestSupport.DataWinName);

        var failed = Assert.IsType<PrepareOutcome.Failed>(new ExecutionCopyManager().PrepareCopy(forged, managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.InvalidPath, failed.Failure.Reason);
    }

    [Fact]
    public void PrepareCopy_promotion_failure_preserves_prior_owned_root()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        using TempDir managedRoot = new();
        ExecutionCopyManager manager = new();
        Assert.IsType<PrepareOutcome.Prepared>(manager.PrepareCopy(install.AsInstall(), managedRoot.Path));
        string sentinel = Path.Combine(managedRoot.Path, "SENTinel-from-first-run.txt");
        File.WriteAllText(sentinel, "must survive a failed promotion");

        ExecutionCopyManager failing = new(new ExecutionCopyManagerOptions(),
            (_, _) => throw new IOException("injected promotion failure"));
        var second = Assert.IsType<PrepareOutcome.Failed>(failing.PrepareCopy(install.AsInstall(), managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.CopyFailed, second.Failure.Reason);

        Assert.True(Directory.Exists(managedRoot.Path), "prior owned managed root lost on promotion failure");
        Assert.True(File.Exists(sentinel), "prior owned root sentinel lost on promotion failure");
        string parent = Directory.GetParent(managedRoot.Path)!.FullName;
        Assert.Empty(Directory.EnumerateDirectories(parent, ".soulmod-staging-*"));
        Assert.Empty(Directory.EnumerateDirectories(parent, "*soulmod-rollback-*"));
    }

    [Fact]
    public void CleanPartial_preserves_unowned_prefix_matching_staging_sibling()
    {
        using TempDir parent = new();
        string managed = Path.Combine(parent.Path, "managed");
        Directory.CreateDirectory(managed);
        string userStaging = Path.Combine(parent.Path, ".soulmod-staging-user-owned");
        Directory.CreateDirectory(userStaging);
        File.WriteAllText(Path.Combine(userStaging, "keep.txt"), "user data");

        var failed = Assert.IsType<CleanupOutcome.Failed>(new ExecutionCopyManager().CleanPartial(managed));
        Assert.Equal(ExecutionCopyFailureReason.StalePartialUnowned, failed.Failure.Reason);
        Assert.True(Directory.Exists(userStaging), "user-created prefix-matching staging directory was deleted");
        Assert.True(File.Exists(Path.Combine(userStaging, "keep.txt")), "user staging content lost");
    }

    [Fact]
    public void PrepareCopy_recorded_directory_manifest_equals_persisted_root()
    {
        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        using TempDir managedRoot = new();
        var prepared = Assert.IsType<PrepareOutcome.Prepared>(new ExecutionCopyManager().PrepareCopy(install.AsInstall(), managedRoot.Path));

        Assert.Equal(prepared.Copy.CopyIntegrity.DirectoryManifestSha256, prepared.Copy.PersistedDirectoryManifestSha256);
        Assert.True(File.Exists(Path.Combine(prepared.Copy.ManagedRootPath, ".soulmod-copy-manifest.json")));
    }

    [Fact]
    public void PrepareCopy_rejects_directory_symlink_in_install()
    {
        if (!OperatingSystem.IsWindows() && !OperatingSystem.IsLinux() && !OperatingSystem.IsMacOS())
            return;

        using FakeInstall install = FakeInstall.Create(TestSupport.ExeName, TestSupport.DataWinName, withExtraFiles: true);
        using TempDir managedRoot = new();
        string outside = Path.Combine(Directory.GetParent(install.RootPath)!.FullName, "outside-dir");
        Directory.CreateDirectory(outside);
        try
        {
            Directory.CreateSymbolicLink(Path.Combine(install.RootPath, "linked_dir"), outside);
        }
        catch (PlatformNotSupportedException)
        {
            return;
        }
        catch (UnauthorizedAccessException)
        {
            return;
        }
        catch (IOException)
        {
            return;
        }

        var failed = Assert.IsType<PrepareOutcome.Failed>(new ExecutionCopyManager().PrepareCopy(install.AsInstall(), managedRoot.Path));
        Assert.Equal(ExecutionCopyFailureReason.SymlinkRejected, failed.Failure.Reason);
        Assert.False(Directory.Exists(managedRoot.Path), "managed root created despite directory symlink");
    }
}
