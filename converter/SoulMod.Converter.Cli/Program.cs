using System;
using System.IO;
using SoulMod.Package;
using SoulMod.Importer.XDelta;

namespace SoulMod.Converter.Cli;

class Program
{
    static int Main(string[] args)
    {
        Console.WriteLine("==================================================");
        Console.WriteLine(" UTDT SoulMod Converter Toolchain v0.1.0");
        Console.WriteLine("==================================================");

        if (args.Length == 0)
        {
            PrintHelp();
            return 0;
        }

        string command = args[0].ToLowerInvariant();
        try
        {
            switch (command)
            {
                case "inspect-exe":
                    if (args.Length < 2)
                    {
                        Console.WriteLine("Usage: soulmod inspect-exe <DELTARUNE.exe>");
                        return 1;
                    }
                    InspectExecutableSymbols(args[1]);
                    return 0;

                case "convert-xdelta":
                    if (args.Length < 4)
                    {
                        Console.WriteLine("Usage: soulmod convert-xdelta --xdelta-exe <xdelta3> --base <data.win> --patch <patch.xdelta> --output <out.soulmod>");
                        return 1;
                    }
                    return RunConvertXDelta(args);

                case "pack":
                    if (args.Length < 3)
                    {
                        Console.WriteLine("Usage: soulmod pack <source-dir> <output.soulmod>");
                        return 1;
                    }
                    PackageBuilder.PackDirectory(args[1], args[2]);
                    Console.WriteLine($"[Success] Packed directory '{args[1]}' into '{args[2]}'.");
                    return 0;

                case "unpack":
                    if (args.Length < 3)
                    {
                        Console.WriteLine("Usage: soulmod unpack <package.soulmod> <target-dir>");
                        return 1;
                    }
                    PackageBuilder.UnpackPackage(args[1], args[2]);
                    Console.WriteLine($"[Success] Unpacked '{args[1]}' into directory '{args[2]}'.");
                    return 0;

                case "inspect":
                    if (args.Length < 2)
                    {
                        Console.WriteLine("Usage: soulmod inspect <package.soulmod>");
                        return 1;
                    }
                    InspectPackage(args[1]);
                    return 0;

                case "validate":
                    if (args.Length < 2)
                    {
                        Console.WriteLine("Usage: soulmod validate <package.soulmod>");
                        return 1;
                    }
                    ValidatePackage(args[1]);
                    return 0;

                default:
                    Console.WriteLine($"[Error] Command '{command}' not implemented or invalid.");
                    PrintHelp();
                    return 1;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CRITICAL ERROR] {ex.Message}");
            return 1;
        }
    }

    static void InspectExecutableSymbols(string exePath)
    {
        if (!File.Exists(exePath))
        {
            Console.WriteLine($"[Error] Executable file '{exePath}' not found.");
            return;
        }

        byte[] data = File.ReadAllBytes(exePath);
        Console.WriteLine($"[Inspect Executable] {exePath} ({data.Length:N0} bytes)");

        string text = System.Text.Encoding.ASCII.GetString(data);
        var matches = System.Text.RegularExpressions.Regex.Matches(text, @"(draw_text[a-zA-Z0-9_]*|draw_set_[a-zA-Z0-9_]*|font_[a-zA-Z0-9_]*|gml_[a-zA-Z0-9_]+)");

        var seen = new System.Collections.Generic.HashSet<string>();
        int count = 0;
        foreach (System.Text.RegularExpressions.Match m in matches)
        {
            if (seen.Add(m.Value))
            {
                count++;
                if (count <= 40)
                {
                    Console.WriteLine($"  - {m.Value}");
                }
            }
        }
        Console.WriteLine($"Total distinct GameMaker symbols found in binary: {seen.Count}");
    }

    static int RunConvertXDelta(string[] args)
    {
        string? basePath = null;
        string? patchPath = null;
        string? outputPath = null;
        string? xdeltaExecutablePath = null;

        for (int i = 1; i < args.Length - 1; i++)
        {
            if (args[i] == "--base") basePath = args[i + 1];
            if (args[i] == "--patch") patchPath = args[i + 1];
            if (args[i] == "--output") outputPath = args[i + 1];
            if (args[i] == "--xdelta-exe") xdeltaExecutablePath = args[i + 1];
        }

        if (string.IsNullOrEmpty(xdeltaExecutablePath) || string.IsNullOrEmpty(basePath) || string.IsNullOrEmpty(patchPath) || string.IsNullOrEmpty(outputPath))
        {
            Console.WriteLine("[Error] Missing required arguments for convert-xdelta.");
            Console.WriteLine("Usage: soulmod convert-xdelta --xdelta-exe <xdelta3> --base <data.win> --patch <patch.xdelta> --output <out.soulmod>");
            return 1;
        }

        var opts = new XDeltaImporterOptions
        {
            XDeltaExecutablePath = xdeltaExecutablePath,
            BaseDataWinPath = basePath,
            PatchFilePath = patchPath,
            OutputSoulModPath = outputPath
        };

        bool success = XDeltaImporter.ImportXDeltaPatch(opts);
        if (success)
        {
            Console.WriteLine($"[Success] Converted xdelta patch into '{outputPath}'.");
            return 0;
        }
        else
        {
            Console.WriteLine("[FAIL] xdelta conversion failed.");
            return 1;
        }
    }

    static void InspectPackage(string packagePath)
    {
        if (!File.Exists(packagePath))
        {
            Console.WriteLine($"[Error] Package file '{packagePath}' not found.");
            return;
        }

        PackageDocument package = PackageBuilder.ReadPackageFromZip(packagePath);
        Manifest manifest = package.Manifest;
        IReadOnlyList<Operation> operations = package.Operations;

        Console.WriteLine($"Package ID:        {manifest.Id}");
        Console.WriteLine($"Name:              {manifest.Name}");
        Console.WriteLine($"Version:           {manifest.Version}");
        Console.WriteLine($"Conflict Policy:   {manifest.ConflictPolicy}");
        Console.WriteLine($"Operations File:   {manifest.OperationsFile}");
        Console.WriteLine($"Operations Count:  {operations.Count}");
        Console.WriteLine("--------------------------------------------------");
        foreach (var op in operations)
        {
            Console.WriteLine($"  - [{op.Id}] Op: {op.OperationType} | Target: {op.Target.Type}:{op.Target.Name} | Priority: {op.Priority}");
        }
    }

    static void ValidatePackage(string packagePath)
    {
        if (!File.Exists(packagePath))
        {
            Console.WriteLine($"[Error] Package file '{packagePath}' not found.");
            return;
        }

        PackageDocument package = PackageBuilder.ReadPackageFromZip(packagePath);
        Console.WriteLine($"[PASS] Package '{package.Manifest.Id}' and {package.Operations.Count} operations are contract-valid.");
    }

    static void PrintHelp()
    {
        Console.WriteLine(@"
Usage: soulmod <command> [options]

Available Commands:
  inspect-exe <DELTARUNE.exe>           Inspect GameMaker function symbols inside runner executable
  convert-xdelta --xdelta-exe <xdelta3> --base <data.win> --patch <patch.xdelta> --output <out.soulmod>
                                        Convert an xdelta patch file to a .soulmod package
  pack <source-dir> <output.soulmod>    Pack a mod directory into a .soulmod container
  unpack <package.soulmod> <target-dir> Extract a .soulmod package to a directory
  inspect <package.soulmod>             Display metadata and operations of a .soulmod package
  validate <package.soulmod>            Validate package structure and JSON schema rules
");
    }
}
