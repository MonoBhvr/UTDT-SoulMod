using System.Reflection;
using SoulMod.ExecutionCopy;
using Xunit;

namespace SoulMod.ExecutionCopy.Tests;

/// <summary>
/// Baseline characterization of the existing injector prototype before the execution-copy
/// boundary replaced any patch-in-place assumptions. Locks current constants (hardcoded dev
/// DLL path, Steam app id, inject-once monitor) so a launcher migration is visible, and locks
/// the new boundary wiring so downstream deploy consumes managed paths only.
/// </summary>
public sealed class InjectorCharacterizationTests
{
    private static readonly string s_repoRoot = FindRepositoryRoot();

    [Fact]
    public void Injector_project_references_the_execution_copy_boundary()
    {
        string csproj = File.ReadAllText(Path.Combine(s_repoRoot, "converter", "SoulMod.Injector", "SoulMod.Injector.csproj"));
        Assert.Contains("SoulMod.ExecutionCopy.csproj", csproj);
    }

    [Fact]
    public void ExecutionCopyLauncher_exposes_prepare_and_remove_seams_typed_by_the_manager()
    {
        Type launcher = typeof(SoulMod.Injector.ExecutionCopyLauncher);
        Assert.NotNull(launcher.GetMethod(nameof(SoulMod.Injector.ExecutionCopyLauncher.PrepareExecutionCopy)));
        Assert.NotNull(launcher.GetMethod(nameof(SoulMod.Injector.ExecutionCopyLauncher.RemoveExecutionCopy)));

        // The prepare seam returns the manager's discriminated outcome, not a raw path, so callers
        // cannot accidentally treat a source install as a patch target.
        MethodInfo prepare = launcher.GetMethod(nameof(SoulMod.Injector.ExecutionCopyLauncher.PrepareExecutionCopy))!;
        Assert.Equal(typeof(PrepareOutcome), prepare.ReturnType);
    }

    [Fact]
    public void Legacy_injector_prototype_keeps_characterized_constants_and_monitor_loop()
    {
        string program = File.ReadAllText(Path.Combine(s_repoRoot, "converter", "SoulMod.Injector", "Program.cs"));
        Assert.Contains("SoulMod.Runtime.dll", program);
        Assert.Contains("steam://run/1671210", program);
        Assert.Contains("DELTARUNE", program);
        Assert.Contains("alreadyInjected", program);
        Assert.Contains("InjectIntoProcess", program);
    }

    [Fact]
    public void Legacy_injector_prototype_injects_runtime_dll_into_process_not_files()
    {
        string program = File.ReadAllText(Path.Combine(s_repoRoot, "converter", "SoulMod.Injector", "Program.cs"));
        Assert.Contains("LoadLibraryW", program);
        Assert.Contains("CreateRemoteThread", program);
        Assert.DoesNotContain("File.Copy", program, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("data.win", program);
    }

    private static string FindRepositoryRoot()
    {
        for (DirectoryInfo? current = new(AppContext.BaseDirectory); current is not null; current = current.Parent)
        {
            if (Directory.Exists(Path.Combine(current.FullName, "converter")) &&
                File.Exists(Path.Combine(current.FullName, "README.md")))
            {
                return current.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate the repository root from the test output directory.");
    }
}
