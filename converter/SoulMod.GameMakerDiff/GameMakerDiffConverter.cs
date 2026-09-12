using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

// ─── GameMaker data.win Diff Converter POC Skeleton ─────────────────────────
// Compares base data.win with patched data.win and extracts changed resources

class GameMakerDiffConverter
{
    public struct StringDiffOp
    {
        public string TargetIndex;
        public string OriginalValue;
        public string PatchedValue;
    }

    static void Main(string[] args)
    {
        Console.WriteLine("==================================================");
        Console.WriteLine(" UTDT SoulMod: GameMaker data.win Diff Converter POC");
        Console.WriteLine("==================================================");

        string basePath = args.Length > 0 ? args[0] : @"C:\Program Files (x86)\Steam\steamapps\common\DELTARUNE\chapter1_windows\data.win";
        string patchedPath = args.Length > 1 ? args[1] : @"D:\UTDT-SoulMod\tools\ch1_patched.win";
        string outputPath = args.Length > 2 ? args[2] : @"D:\UTDT-SoulMod\mods\dtkr.ch1\extracted_ops.json";

        Console.WriteLine($"Base data.win:    {basePath}");
        Console.WriteLine($"Patched data.win: {patchedPath}");
        Console.WriteLine($"Output Spec:      {outputPath}");

        if (!File.Exists(basePath))
        {
            Console.WriteLine("[WARN] Base data.win not found. Running in Diff Spec Schema Validation Mode...");
            GenerateSampleOperationsSpec(outputPath);
            return;
        }

        if (!File.Exists(patchedPath))
        {
            Console.WriteLine("[INFO] Patched data.win not found. Converter ready for UTMT data stream diffing.");
            GenerateSampleOperationsSpec(outputPath);
            return;
        }

        Console.WriteLine("[SUCCESS] Diff Converter POC engine ready!");
    }

    static void GenerateSampleOperationsSpec(string outputPath)
    {
        string jsonSpec = @"[
  {
    ""id"": ""op_str_1"",
    ""modId"": ""dtkr.ch1"",
    ""target"": { ""type"": ""string"", ""name"": ""DEVICE_CONTACT_slash_Step_0_gml_6_0"" },
    ""operation"": ""string.replace"",
    ""value"": ""거기 계십니까?""
  },
  {
    ""id"": ""op_font_1"",
    ""modId"": ""dtkr.ch1"",
    ""target"": { ""type"": ""font"", ""name"": ""fnt_main"" },
    ""operation"": ""font.redirect"",
    ""value"": ""fnt_main_ko""
  }
]";
        Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
        File.WriteAllText(outputPath, jsonSpec, Encoding.UTF8);
        Console.WriteLine($"Generated operations spec: {outputPath}");
    }
}
