namespace SoulMod.ExecutionCopy;

/// <summary>
/// Typed failure reasons for the execution-copy boundary. Callers switch on the
/// reason to produce actionable launcher output; the manager never throws for
/// contract failures, only for genuinely unrecoverable environment faults.
/// </summary>
public enum ExecutionCopyFailureReason
{
    /// <summary>The supplied install root does not exist or is not a directory.</summary>
    InvalidInstallRoot,
    /// <summary>The required source executable is missing inside the install root.</summary>
    SourceExecutableMissing,
    /// <summary>The required source data.win is missing inside the install root.</summary>
    SourceDataWinMissing,
    /// <summary>The requested managed execution root is not a usable absolute path.</summary>
    InvalidManagedRoot,
    /// <summary>Source root and managed root overlap (equal, or one contains the other).</summary>
    SourceTargetOverlap,
    /// <summary>A symlink was encountered in a source path or root; rejected for boundary safety.</summary>
    SymlinkRejected,
    /// <summary>An existing managed root has no SoulMod ownership manifest; refusing to delete user data.</summary>
    StalePartialUnowned,
    /// <summary>The copy was cancelled; the partial staging tree was removed.</summary>
    CopyCancelled,
    /// <summary>An OS-level copy error occurred; the partial staging tree was removed.</summary>
    CopyFailed,
    /// <summary>The source install changed during the copy (pre/post hashes differ).</summary>
    SourceMutatedDuringCopy,
    /// <summary>A path supplied to the boundary could not be normalized (escape, invalid chars).</summary>
    InvalidPath,
}

/// <summary>
/// A typed actionable failure carrying a machine reason and a human-readable message
/// with the offending path. Never mutated after construction.
/// </summary>
public sealed record ExecutionCopyFailure(ExecutionCopyFailureReason Reason, string Message);

/// <summary>
/// Immutable descriptor of the source game install: the read-only input. Every path is
/// normalized to an absolute form at the boundary; downstream code receives typed values.
/// </summary>
public sealed record GameInstall(
    string RootPath,
    string ExecutableRelativePath,
    string DataWinRelativePath)
{
    public string ExecutablePath => Path.Combine(RootPath, ExecutableRelativePath);
    public string DataWinPath => Path.Combine(RootPath, DataWinRelativePath);
}

/// <summary>SHA-256 of a single file plus its normalized absolute path.</summary>
public sealed record FileIntegrity(string Path, string Sha256);

/// <summary>
/// Integrity snapshot of a game install: hashes of the executable and data.win plus a
/// deterministic manifest hash over the whole install directory (sorted relative paths).
/// Used both as the immutability proof (before vs. after) and the copy-correctness proof.
/// </summary>
public sealed record InstallIntegrity(
    FileIntegrity Executable,
    FileIntegrity DataWin,
    string DirectoryManifestSha256);

/// <summary>
/// The prepared execution copy: the ONLY target downstream patch/deploy calls may write to.
/// Source before/after integrity proves the install was not mutated; copy integrity proves
/// the copy matches the source; <see cref="PersistedDirectoryManifestSha256"/> proves the
/// recorded directory hash equals the actual managed root on disk after promotion.
/// </summary>
public sealed record ExecutionCopy(
    string ManagedRootPath,
    string ExecutablePath,
    string DataWinPath,
    InstallIntegrity SourceIntegrityBefore,
    InstallIntegrity SourceIntegrityAfter,
    InstallIntegrity CopyIntegrity,
    string PersistedDirectoryManifestSha256,
    string RunId);

/// <summary>Receipt returned by partial-copy cleanup, for evidence and idempotent re-runs.</summary>
public sealed record CleanupReceipt(string ManagedRootPath, bool RemovedManagedRoot, int RemovedStagingTrees, string RunId);

/// <summary>
/// Discriminated outcome of <see cref="ExecutionCopyManager.PrepareCopy"/>. The hierarchy is
/// sealed so callers can exhaustively match the two cases without a default arms race.
/// </summary>
public abstract record PrepareOutcome
{
    private PrepareOutcome() { }
    public sealed record Prepared(ExecutionCopy Copy) : PrepareOutcome;
    public sealed record Failed(ExecutionCopyFailure Failure) : PrepareOutcome;

    public TResult Match<TResult>(Func<Prepared, TResult> prepared, Func<Failed, TResult> failed)
        => this switch
        {
            Prepared p => prepared(p),
            Failed f => failed(f),
            _ => throw new InvalidOperationException("unreachable PrepareOutcome")
        };
}

/// <summary>Discriminated outcome of <see cref="ExecutionCopyManager.CleanPartial"/>.</summary>
public abstract record CleanupOutcome
{
    private CleanupOutcome() { }
    public sealed record Cleaned(CleanupReceipt Receipt) : CleanupOutcome;
    public sealed record Failed(ExecutionCopyFailure Failure) : CleanupOutcome;

    public TResult Match<TResult>(Func<Cleaned, TResult> cleaned, Func<Failed, TResult> failed)
        => this switch
        {
            Cleaned c => cleaned(c),
            Failed f => failed(f),
            _ => throw new InvalidOperationException("unreachable CleanupOutcome")
        };
}

/// <summary>Discriminated outcome of <see cref="ExecutionCopyManager.ResolveInstall"/>.</summary>
public abstract record ResolveOutcome
{
    private ResolveOutcome() { }
    public sealed record Resolved(GameInstall Install, InstallIntegrity Integrity) : ResolveOutcome;
    public sealed record Failed(ExecutionCopyFailure Failure) : ResolveOutcome;
}
