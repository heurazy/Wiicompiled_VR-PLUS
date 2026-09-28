using System.Runtime.InteropServices;

namespace WiiCompiled.Setup.Windows;

/// <summary>Protects both the destination and the Windows paging/temp volume throughout an operation.</summary>
internal static class InstallationDiskGuard
{
    internal const long ReserveBytes = 8L * 1024 * 1024 * 1024;
    internal const long MaximumGrowthBytes = 24L * 1024 * 1024 * 1024;

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhysical, AvailablePhysical, TotalCommit, AvailableCommit,
            TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);

    private static void CheckMemory()
    {
        var status = new MemoryStatus { Length = (uint)Marshal.SizeOf<MemoryStatus>() };
        if (GlobalMemoryStatusEx(ref status) && status.AvailableCommit < 2UL * 1024 * 1024 * 1024)
            throw new IOException("Installation stopped because Windows is running out of virtual memory. " +
                "Check Task Manager for an application consuming excessive memory before retrying.");
    }

    internal static void ValidateBudget(string drive, long freeBytes, long initialFreeBytes)
    {
        if (freeBytes < ReserveBytes || initialFreeBytes - freeBytes > MaximumGrowthBytes)
            throw new IOException($"Installation stopped to protect drive {drive}. " +
                "Disk use grew beyond the 24 GiB working-space limit or less than 8 GiB remains free. " +
                "Temporary installation files will be cleaned up; check the setup log before retrying.");
    }

    internal static void Check(string path, long additionalBytes = 0)
    {
        var drive = new DriveInfo(Path.GetPathRoot(Path.GetFullPath(path))!);
        if (drive.AvailableFreeSpace < checked(ReserveBytes + additionalBytes))
            throw new IOException($"Installation stopped to avoid filling drive {drive.Name}. " +
                $"Keep at least 8 GiB free in addition to the installation files " +
                $"({(ReserveBytes + additionalBytes) / (1024.0 * 1024 * 1024):F1} GiB needed for this step).");
    }

    internal static async Task RunAsync(string destination, CancellationToken cancellationToken,
        Func<CancellationToken, Task> operation)
    {
        var paths = new[] { destination, Environment.SystemDirectory, Path.GetTempPath() }
            .DistinctBy(path => Path.GetPathRoot(Path.GetFullPath(path)), StringComparer.OrdinalIgnoreCase)
            .ToArray();
        foreach (var path in paths) Check(path);
        CheckMemory();
        var drives = paths.Select(path => new DriveInfo(Path.GetPathRoot(Path.GetFullPath(path))!))
            .Select(drive => (Drive: drive, InitialFree: drive.AvailableFreeSpace)).ToArray();
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        using var stopMonitor = new CancellationTokenSource();
        IOException? failure = null;
        var monitor = Task.Run(async () =>
        {
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
            try
            {
                while (await timer.WaitForNextTickAsync(stopMonitor.Token))
                {
                    CheckMemory();
                    foreach (var (drive, initialFree) in drives)
                        ValidateBudget(drive.Name, drive.AvailableFreeSpace, initialFree);
                }
            }
            catch (IOException ex)
            {
                Interlocked.Exchange(ref failure, ex);
                linked.Cancel(); // ProcessRunner terminates the compiler/extractor process tree.
            }
            catch (OperationCanceledException) when (stopMonitor.IsCancellationRequested) { }
        });
        try
        {
            await operation(linked.Token);
            if (failure is not null) throw failure;
        }
        catch (OperationCanceledException) when (failure is not null)
        {
            throw failure;
        }
        finally
        {
            stopMonitor.Cancel();
            await monitor;
        }
    }
}
