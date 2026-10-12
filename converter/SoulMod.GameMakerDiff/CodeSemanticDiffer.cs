using System.Security.Cryptography;
using System.Text;

namespace SoulMod.GameMakerDiff;

public sealed record CodeFingerprint(string Name, string BytecodeSha256, int InstructionCount)
{
    public bool IsValid => !string.IsNullOrWhiteSpace(Name) && InstructionCount >= 0 &&
        BytecodeSha256.Length == 64 && BytecodeSha256.All(char.IsAsciiHexDigit);

    public string GraphSha256 => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
        $"{Name}\n{BytecodeSha256.ToLowerInvariant()}\n{InstructionCount}"))).ToLowerInvariant();
}

public enum CodeDiffReportKind
{
    ManualReviewRequired
}

public sealed record CodeDiffReport(CodeDiffReportKind Kind, string Reason, string? Name);

public sealed record CodeReplacementCandidate(CodeFingerprint Base, CodeFingerprint Patched);

public sealed record CodeSemanticDiffResult(
    List<CodeReplacementCandidate> Candidates,
    List<CodeDiffReport> Reports);

public static class CodeSemanticDiffer
{
    public static CodeSemanticDiffResult Compare(
        IEnumerable<CodeFingerprint> baseCodes,
        IEnumerable<CodeFingerprint> patchedCodes)
    {
        CodeFingerprint[] baseSnapshots = baseCodes.OrderBy(snapshot => snapshot.Name, StringComparer.Ordinal)
            .ThenBy(snapshot => snapshot.BytecodeSha256, StringComparer.Ordinal).ToArray();
        CodeFingerprint[] patchedSnapshots = patchedCodes.OrderBy(snapshot => snapshot.Name, StringComparer.Ordinal)
            .ThenBy(snapshot => snapshot.BytecodeSha256, StringComparer.Ordinal).ToArray();
        var candidates = new List<CodeReplacementCandidate>();
        var reports = new List<CodeDiffReport>();

        foreach (CodeFingerprint snapshot in baseSnapshots.Concat(patchedSnapshots).Where(snapshot => !snapshot.IsValid)
                     .Distinct())
        {
            reports.Add(new(CodeDiffReportKind.ManualReviewRequired, "MalformedCodeFingerprint", snapshot.Name));
        }

        HashSet<string> invalidNames = baseSnapshots.Concat(patchedSnapshots).Where(snapshot => !snapshot.IsValid)
            .Select(snapshot => snapshot.Name).ToHashSet(StringComparer.Ordinal);
        foreach (string name in baseSnapshots.Select(snapshot => snapshot.Name).Concat(patchedSnapshots.Select(snapshot => snapshot.Name))
                     .Distinct(StringComparer.Ordinal).OrderBy(name => name, StringComparer.Ordinal))
        {
            if (invalidNames.Contains(name)) continue;
            CodeFingerprint[] baseMatches = baseSnapshots.Where(snapshot => snapshot.Name == name).ToArray();
            CodeFingerprint[] patchedMatches = patchedSnapshots.Where(snapshot => snapshot.Name == name).ToArray();
            if (baseMatches.Length > 1 || patchedMatches.Length > 1)
            {
                reports.Add(new(CodeDiffReportKind.ManualReviewRequired, "AmbiguousCodeFingerprint", name));
                continue;
            }

            if (baseMatches.Length == 1 && patchedMatches.Length == 1)
            {
                if (baseMatches[0] != patchedMatches[0]) candidates.Add(new(baseMatches[0], patchedMatches[0]));
                continue;
            }

            reports.Add(new(CodeDiffReportKind.ManualReviewRequired,
                baseMatches.Length == 1 ? "RenamedOrRemovedCode" : "RenamedOrAddedCode", name));
        }

        return new(candidates, reports);
    }
}
