using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

class InjectorMain
{
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, uint s, uint t, uint prot);
    [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, uint s, out UIntPtr w);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr m, string n);
    [DllImport("kernel32.dll", CharSet = CharSet.Auto)] public static extern IntPtr GetModuleHandle(string? m);
    [DllImport("kernel32.dll")] public static extern IntPtr CreateRemoteThread(IntPtr p, IntPtr a, uint s, IntPtr fn, IntPtr arg, uint f, out IntPtr tid);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    const uint PROCESS_ALL_ACCESS = 0x1F0FFF;
    const uint MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, PAGE_READWRITE = 0x04;

    static readonly HashSet<int> alreadyInjected = new();

    static bool InjectIntoProcess(int pid, string dllPath) {
        if (alreadyInjected.Contains(pid)) return false;

        IntPtr hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, (uint)pid);
        if (hProcess == IntPtr.Zero) return false;

        byte[] dllBytes = Encoding.Unicode.GetBytes(dllPath + "\0");
        IntPtr mem = VirtualAllocEx(hProcess, IntPtr.Zero, (uint)dllBytes.Length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (mem == IntPtr.Zero) { CloseHandle(hProcess); return false; }

        WriteProcessMemory(hProcess, mem, dllBytes, (uint)dllBytes.Length, out _);
        IntPtr loadLib = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
        IntPtr hThread = CreateRemoteThread(hProcess, IntPtr.Zero, 0, loadLib, mem, 0, out _);
        
        if (hThread != IntPtr.Zero) {
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
            CloseHandle(hProcess);
            alreadyInjected.Add(pid);
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] [INJECTED] PID={pid}");
            return true;
        }
        CloseHandle(hProcess);
        return false;
    }

    static void Main(string[] args)
    {
        string dllPath = Path.GetFullPath(@"D:\UTDT-SoulMod\runtime\build\SoulMod.Runtime\Release\SoulMod.Runtime.dll");

        Console.WriteLine("==================================================");
        Console.WriteLine(" UTDT SoulMod Auto-Monitor Injector v0.2.0");
        Console.WriteLine("==================================================");
        Console.WriteLine($"DLL: {dllPath}");
        Console.WriteLine("Monitoring for DELTARUNE processes... (Ctrl+C to stop)");
        Console.WriteLine();

        // Launch via Steam
        Process.Start(new ProcessStartInfo("steam://run/1671210") { UseShellExecute = true });
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Steam launch triggered. Waiting for processes...");

        // Poll every 500ms for new DELTARUNE processes and inject
        while (true) {
            var procs = Process.GetProcessesByName("DELTARUNE");
            foreach (var p in procs) {
                if (!alreadyInjected.Contains(p.Id)) {
                    Thread.Sleep(3000); // Wait for game window to fully appear before injecting
                    bool ok = InjectIntoProcess(p.Id, dllPath);
                    if (ok) {
                        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] SoulMod injected. Game: {p.ProcessName} PID={p.Id}");
                        Console.WriteLine("  --> Check C:\\Users\\Public\\SoulMod.log for runtime status.");
                    }
                }
            }
            Thread.Sleep(500);
        }
    }
}
