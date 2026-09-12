using System.Diagnostics;

namespace SoulMod.Importer.XDelta;

internal static class XDeltaDecoder
{
    private static readonly TimeSpan DecodeTimeout = TimeSpan.FromSeconds(30);

    public static void Decode(string executablePath, string baseFile, string patchFile, string outputFile)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executablePath,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("-d");
        startInfo.ArgumentList.Add("-s");
        startInfo.ArgumentList.Add(baseFile);
        startInfo.ArgumentList.Add(patchFile);
        startInfo.ArgumentList.Add(outputFile);

        using Process process = Process.Start(startInfo)
            ?? throw new InvalidOperationException("Failed to start the xdelta decoder.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();

        if (!process.WaitForExit(DecodeTimeout))
        {
            process.Kill(true);
            process.WaitForExit();
            throw new TimeoutException($"xdelta decoding exceeded {DecodeTimeout.TotalSeconds} seconds.");
        }

        string output = standardOutput.GetAwaiter().GetResult();
        string error = standardError.GetAwaiter().GetResult();
        if (process.ExitCode != 0)
        {
            string detail = string.IsNullOrWhiteSpace(error) ? output : error;
            throw new InvalidOperationException($"xdelta decoding failed with exit code {process.ExitCode}: {detail.Trim()}");
        }

        if (!File.Exists(outputFile))
        {
            throw new InvalidOperationException("xdelta reported success without producing a decoded file.");
        }
    }
}
