namespace SoulMod.Package;

public sealed record PackageDocument(Manifest Manifest, IReadOnlyList<Operation> Operations);
