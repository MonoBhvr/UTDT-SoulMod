using SoulMod.ExecutionCopy;

namespace SoulMod.Injector;

/// <summary>
/// Boundary seam between the injector/launcher layer and the execution-copy manager. The
/// installed game is immutable input; the managed execution root handed to Aurie deploy/launch
/// is the ONLY writable target. Nothing in this type accepts a source install path as a patch
/// target, only as read-only copy input.
/// </summary>
public static class ExecutionCopyLauncher
{
    /// <summary>
    /// Resolves and prepares a managed execution copy of the source install. Downstream Aurie
    /// deploy/launch calls must consume <see cref="ExecutionCopy.ManagedRootPath"/> only; the
    /// source root is read-only and is proven unchanged by the returned pre/post integrity.
    /// </summary>
    /// <param name="sourceRootPath">Absolute path to the immutable installed game directory.</param>
    /// <param name="executableRelativePath">EXE name relative to the install root (e.g. "DELTARUNE.exe").</param>
    /// <param name="dataWinRelativePath">data.win name relative to the install root.</param>
    /// <param name="managedRootPath">Absolute path to the managed execution root to create/replace.</param>
    /// <param name="cancellationToken">Cancellation token for the copy; aborts clean the staging tree.</param>
    public static PrepareOutcome PrepareExecutionCopy(
        string sourceRootPath,
        string executableRelativePath,
        string dataWinRelativePath,
        string managedRootPath,
        CancellationToken cancellationToken = default)
    {
        ExecutionCopyManager manager = new();
        ResolveOutcome resolve = manager.ResolveInstall(sourceRootPath, executableRelativePath, dataWinRelativePath);
        return resolve switch
        {
            ResolveOutcome.Failed failed => new PrepareOutcome.Failed(failed.Failure),
            ResolveOutcome.Resolved resolved => manager.PrepareCopy(resolved.Install, managedRootPath, cancellationToken),
            _ => throw new InvalidOperationException("unreachable ResolveOutcome"),
        };
    }

    /// <summary>
    /// Idempotently removes a managed execution copy this run owns, plus any partial staging
    /// trees. A managed root without a SoulMod ownership manifest is refused, never deleted.
    /// </summary>
    public static CleanupOutcome RemoveExecutionCopy(string managedRootPath)
        => new ExecutionCopyManager().CleanPartial(managedRootPath);
}
