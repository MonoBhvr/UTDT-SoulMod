namespace SoulMod.ExecutionCopy;

/// <summary>Options for <see cref="ExecutionCopyManager"/>. Repository-relative and configurable.</summary>
public sealed class ExecutionCopyManagerOptions
{
    /// <summary>Prefix used for staging directories siblings to a requested managed root.</summary>
    public string StagingPrefix { get; init; } = ".soulmod-staging-";

    /// <summary>The integrity manifest written inside a managed root we created; checked to prove ownership before deletion.</summary>
    public string ManifestFileName { get; init; } = ".soulmod-copy-manifest.json";

    /// <summary>
    /// The ownership marker written into a staging directory at creation, bound to the intended
    /// managed target. Staging cleanup only deletes a sibling whose marker resolves to the target
    /// being cleaned, so arbitrary user-created prefix-matching directories are preserved.
    /// </summary>
    public string OwnershipMarkerFileName { get; init; } = ".soulmod-ownership.json";
}

/// <summary>
/// The reusable execution-copy boundary. Source install is read-only input; the managed
/// execution root is the only mutable target handed to downstream Aurie deploy/patch calls.
/// Interruptions clean partial staging trees; pre/post SHA-256 proves the install is untouched;
/// rollback-safe promotion preserves a prior owned root until the new one is durably in place.
/// No API on this type accepts a source install path as a patch target.
/// </summary>
public sealed class ExecutionCopyManager
{
    private readonly ExecutionCopyManagerOptions _options;
    private readonly Action<string, string> _promote;

    public ExecutionCopyManager() : this(new ExecutionCopyManagerOptions()) { }

    public ExecutionCopyManager(ExecutionCopyManagerOptions options) : this(options, Directory.Move) { }

    /// <summary>Test seam: inject the promotion move to simulate promotion failure deterministically.</summary>
    internal ExecutionCopyManager(ExecutionCopyManagerOptions options, Action<string, string> promote)
    {
        _options = options;
        _promote = promote;
    }

    /// <summary>Root-relative SoulMod metadata names excluded from directory-content manifests.</summary>
    private IReadOnlySet<string> MetadataRelativePaths => new HashSet<string>
    {
        _options.ManifestFileName,
        _options.OwnershipMarkerFileName,
    };

    /// <summary>
    /// Parses and validates a source install at the boundary via the single validation funnel
    /// shared with <see cref="PrepareCopy"/>. Returns the typed, normalized install plus a
    /// baseline integrity snapshot, or a typed failure. Never mutates the source.
    /// </summary>
    public ResolveOutcome ResolveInstall(
        string rootPath,
        string executableRelativePath,
        string dataWinRelativePath)
        => ValidateInstall(new GameInstall(rootPath, executableRelativePath, dataWinRelativePath));

    /// <summary>
    /// The single validation funnel. Re-normalizes the root, asserts every child path is contained
    /// (rejecting <c>..</c>/absolute escapes) and present and not a symlink, then snapshots. Used by
    /// both <see cref="ResolveInstall"/> and <see cref="PrepareCopy"/> so a forged public
    /// <see cref="GameInstall"/> cannot bypass child containment.
    /// </summary>
    private ResolveOutcome ValidateInstall(GameInstall install)
    {
        (string normalizedRoot, ExecutionCopyFailureReason? rootReason) =
            PathContainment.NormalizeRoot(install.RootPath);
        if (rootReason is { } r)
            return Failed(r, $"Install root is not a usable directory: {install.RootPath}");

        if (!Directory.Exists(normalizedRoot))
            return Failed(ExecutionCopyFailureReason.InvalidInstallRoot, $"Install root does not exist: {normalizedRoot}");

        foreach ((string relativePath, string label, ExecutionCopyFailureReason missingReason) in
            new[] {
                (install.ExecutableRelativePath, "executable", ExecutionCopyFailureReason.SourceExecutableMissing),
                (install.DataWinRelativePath, "data.win", ExecutionCopyFailureReason.SourceDataWinMissing),
            })
        {
            (string absolute, ExecutionCopyFailureReason? childReason) =
                PathContainment.NormalizeChild(normalizedRoot, relativePath);
            if (childReason is { } cr)
                return Failed(cr, $"Install {label} path is not usable: {relativePath}");
            if (!File.Exists(absolute))
                return Failed(missingReason, $"Install {label} missing: {absolute}");
        }

        GameInstall normalized = new(normalizedRoot, install.ExecutableRelativePath, install.DataWinRelativePath);
        try
        {
            return new ResolveOutcome.Resolved(normalized, IntegrityHashing.Snapshot(normalized, MetadataRelativePaths));
        }
        catch (SymlinkInSourceException symlink)
        {
            return Failed(ExecutionCopyFailureReason.SymlinkRejected, $"Symlink inside install rejected: {symlink.Path}");
        }
        catch (IOException io)
        {
            return Failed(ExecutionCopyFailureReason.CopyFailed, $"Could not read source install: {io.Message}");
        }
    }

    /// <summary>
    /// Prepares a managed execution copy. Validates the (possibly forged) install via the funnel,
    /// copies into a sibling staging tree carrying an ownership marker bound to the target, verifies
    /// source immutability and copy faithfulness, promotes with rollback so a prior owned root is
    /// preserved if promotion fails, and proves the recorded directory hash equals the persisted root.
    /// </summary>
    public PrepareOutcome PrepareCopy(
        GameInstall install,
        string managedRootPath,
        CancellationToken cancellationToken = default)
    {
        ResolveOutcome validation = ValidateInstall(install);
        if (validation is ResolveOutcome.Failed failedValidation)
            return new PrepareOutcome.Failed(failedValidation.Failure);
        var resolved = (ResolveOutcome.Resolved)validation;
        GameInstall validatedInstall = resolved.Install;
        InstallIntegrity before = resolved.Integrity;

        (string normalizedTarget, ExecutionCopyFailureReason? targetReason) =
            PathContainment.NormalizeRoot(managedRootPath);
        if (targetReason is { } tr)
            return PrepareFailed(tr, $"Managed root is not a usable directory: {managedRootPath}");

        if (PathContainment.CheckOverlap(validatedInstall.RootPath, normalizedTarget) is { } overlap)
            return PrepareFailed(overlap, $"Source root and managed root overlap: {validatedInstall.RootPath} <-> {normalizedTarget}");

        if (Directory.Exists(normalizedTarget) && !ManagedRootIO.OwnsManifest(normalizedTarget, _options.ManifestFileName))
            return PrepareFailed(ExecutionCopyFailureReason.StalePartialUnowned,
                $"Managed root already exists and is not owned by SoulMod: {normalizedTarget}");

        // Sweep only staging siblings whose marker is bound to THIS target; preserve all others.
        ManagedRootIO.RemoveSiblingStagingTrees(normalizedTarget, _options.StagingPrefix, _options.OwnershipMarkerFileName);

        string runId = ManagedRootIO.GenerateRunId();
        string parent = Directory.GetParent(normalizedTarget)?.FullName
            ?? throw new IOException($"Managed root has no parent directory: {normalizedTarget}");
        string stagingRoot = Path.Combine(parent, _options.StagingPrefix + runId);

        try
        {
            Directory.CreateDirectory(stagingRoot);
            ManagedRootIO.WriteStagingMarker(stagingRoot, _options.OwnershipMarkerFileName, normalizedTarget, runId);
            ManagedRootIO.CopyTree(validatedInstall.RootPath, stagingRoot, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();

            InstallIntegrity after;
            try { after = IntegrityHashing.Snapshot(validatedInstall, MetadataRelativePaths); }
            catch (SymlinkInSourceException symlink)
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.SymlinkRejected, $"Symlink inside install rejected: {symlink.Path}");
            }
            catch (IOException io)
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.CopyFailed, $"Could not re-snapshot source after copy: {io.Message}");
            }

            if (!after.Equals(before))
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.SourceMutatedDuringCopy,
                    $"Source install changed during copy (before dir manifest {before.DirectoryManifestSha256[..8]}, after {after.DirectoryManifestSha256[..8]}).");
            }

            InstallIntegrity copyIntegrity;
            try
            {
                FileIntegrity copyExeHash = IntegrityHashing.HashFile(Path.Combine(stagingRoot, validatedInstall.ExecutableRelativePath));
                FileIntegrity copyDataWinHash = IntegrityHashing.HashFile(Path.Combine(stagingRoot, validatedInstall.DataWinRelativePath));
                copyIntegrity = new InstallIntegrity(
                    copyExeHash, copyDataWinHash, IntegrityHashing.DirectoryManifestSha256(stagingRoot, MetadataRelativePaths));
            }
            catch (SymlinkInSourceException symlink)
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.SymlinkRejected, $"Symlink inside copy rejected: {symlink.Path}");
            }
            catch (IOException io)
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.CopyFailed, $"Could not hash the copy: {io.Message}");
            }

            if (!copyIntegrity.Executable.Sha256.Equals(before.Executable.Sha256, StringComparison.OrdinalIgnoreCase) ||
                !copyIntegrity.DataWin.Sha256.Equals(before.DataWin.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                ManagedRootIO.TryDelete(stagingRoot);
                return PrepareFailed(ExecutionCopyFailureReason.CopyFailed,
                    "Copy EXE/data.win hashes do not match the source; the copy is not faithful.");
            }

            ManagedRootIO.WriteManifest(stagingRoot, _options.ManifestFileName, validatedInstall, before, after, copyIntegrity, copyIntegrity.DirectoryManifestSha256, runId);

            // Rollback-safe promotion: prior owned root is moved aside, restored if promotion fails.
            ManagedRootIO.PromoteWithRollback(stagingRoot, normalizedTarget, runId, _promote);

            // Persisted-hash proof: the recorded manifest must equal the actual managed root on disk.
            string persistedManifest = IntegrityHashing.DirectoryManifestSha256(normalizedTarget, MetadataRelativePaths);
            if (!persistedManifest.Equals(copyIntegrity.DirectoryManifestSha256, StringComparison.OrdinalIgnoreCase))
            {
                return PrepareFailed(ExecutionCopyFailureReason.CopyFailed,
                    $"Persisted managed root manifest {persistedManifest[..8]} != recorded {copyIntegrity.DirectoryManifestSha256[..8]}.");
            }

            return new PrepareOutcome.Prepared(new ExecutionCopy(
                normalizedTarget,
                Path.Combine(normalizedTarget, validatedInstall.ExecutableRelativePath),
                Path.Combine(normalizedTarget, validatedInstall.DataWinRelativePath),
                before, after, copyIntegrity, persistedManifest, runId));
        }
        catch (OperationCanceledException)
        {
            ManagedRootIO.TryDelete(stagingRoot);
            return PrepareFailed(ExecutionCopyFailureReason.CopyCancelled,
                $"Copy cancelled; partial staging tree removed: {stagingRoot}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            ManagedRootIO.TryDelete(stagingRoot);
            return PrepareFailed(ExecutionCopyFailureReason.CopyFailed, $"Copy failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Idempotently cleans a partial/interrupted copy. Removes staging siblings whose marker is
    /// bound to this target, plus an owned managed root. An unowned managed root and unbound
    /// staging directories are refused, never deleted.
    /// </summary>
    public CleanupOutcome CleanPartial(string managedRootPath)
    {
        (string normalized, ExecutionCopyFailureReason? reason) = PathContainment.NormalizeRoot(managedRootPath);
        if (reason is { } r)
            return CleanupFailed(r, $"Managed root is not a usable directory: {managedRootPath}");

        int removedStaging = ManagedRootIO.RemoveSiblingStagingTrees(normalized, _options.StagingPrefix, _options.OwnershipMarkerFileName);

        if (!Directory.Exists(normalized))
            return new CleanupOutcome.Cleaned(new CleanupReceipt(normalized, false, removedStaging, ManagedRootIO.GenerateRunId()));

        if (!ManagedRootIO.OwnsManifest(normalized, _options.ManifestFileName))
            return CleanupFailed(ExecutionCopyFailureReason.StalePartialUnowned,
                $"Managed root exists without a SoulMod ownership manifest; refusing to delete user data: {normalized}");

        try { Directory.Delete(normalized, recursive: true); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return CleanupFailed(ExecutionCopyFailureReason.CopyFailed, $"Could not remove owned managed root: {ex.Message}");
        }

        return new CleanupOutcome.Cleaned(new CleanupReceipt(normalized, true, removedStaging, ManagedRootIO.GenerateRunId()));
    }

    private static ResolveOutcome.Failed Failed(ExecutionCopyFailureReason reason, string message) =>
        new(new ExecutionCopyFailure(reason, message));

    private static PrepareOutcome.Failed PrepareFailed(ExecutionCopyFailureReason reason, string message) =>
        new(new ExecutionCopyFailure(reason, message));

    private static CleanupOutcome.Failed CleanupFailed(ExecutionCopyFailureReason reason, string message) =>
        new(new ExecutionCopyFailure(reason, message));
}
