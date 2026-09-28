using System.IO.Compression;
using WiiCompiled.Setup.Common;

namespace WiiCompiled.Setup.Windows;

internal static class RetroRewindDownload
{
    private const string Server = "https://update.rwfc.net/RetroRewind/";
    internal const long MaximumDownloadBytes = 4L * 1024 * 1024 * 1024;
    internal const long MaximumExtractedBytes = 8L * 1024 * 1024 * 1024;
    internal sealed record Update(Version Version, string Url);

    internal static List<Update> ParseUpdates(string manifest) => manifest.Split('\n')
        .Select(line => line.Trim().Split(' ', 4, StringSplitOptions.RemoveEmptyEntries))
        .Where(parts => parts.Length == 4 && Version.TryParse(parts[0], out _))
        .Select(parts => new Update(Version.Parse(parts[0]), parts[1].Replace(
            "http://update.rwfc.net:8000/", "https://update.rwfc.net/")))
        .OrderBy(update => update.Version).ToList();

    public static async Task<string> EnsureLatestAsync(string staging, string? existing,
        IInstallReporter reporter, CancellationToken cancellationToken)
    {
        using var client = CreateClient();
        reporter.Progress(InstallStages.Validate, "Checking the latest official Retro Rewind version...", 2);
        var updates = ParseUpdates(await client.GetStringAsync(Server + "RetroRewindVersion.txt", cancellationToken));
        if (updates.Count == 0) throw new InvalidDataException("The Retro Rewind version manifest is empty.");
        var versionFile = existing is null ? null : Path.Combine(existing, "version.txt");
        if (versionFile is not null && File.Exists(versionFile) &&
            Version.TryParse(File.ReadAllText(versionFile).Trim(), out var current) && current >= updates[^1].Version)
        {
            reporter.Diagnostic($"Retro Rewind {current} is up to date; no pack download needed.");
            return existing!;
        }
        var downloaded = await DownloadAsync(staging, reporter, cancellationToken);
        // Custom Wheel Wizard patches survive replacing an outdated pack.
        var oldPatches = existing is null ? null : Path.Combine(existing, "Patches");
        if (oldPatches is not null && Directory.Exists(oldPatches))
            foreach (var file in Directory.EnumerateFiles(oldPatches, "*", SearchOption.AllDirectories))
            {
                var destination = Path.Combine(downloaded, "Patches", Path.GetRelativePath(oldPatches, file));
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(file, destination, overwrite: true);
            }
        return downloaded;
    }

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("WheelWizard/2.5.4");
        return client;
    }

    public static async Task<string> DownloadAsync(string staging, IInstallReporter reporter,
        CancellationToken cancellationToken)
    {
        using var client = CreateClient();
        // The official service rejects clients without Wheel Wizard's user agent.
        reporter.Progress(InstallStages.Validate, "Finding the latest Retro Rewind release...", 2);
        var address = (await client.GetStringAsync(
            "https://update.rwfc.net/RetroRewind/RetroRewindInstall.txt", cancellationToken)).Trim();
        if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || uri.Scheme != "https")
            throw new InvalidDataException("The Retro Rewind service returned an invalid download URL.");
        var archive = Path.Combine(staging, "RetroRewind-download.zip");
        reporter.Progress(InstallStages.Validate, "Downloading Retro Rewind...", 2);
        using (var response = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
        {
            response.EnsureSuccessStatusCode();
            await SaveDownloadAsync(response, archive, cancellationToken);
        }
        reporter.Progress(InstallStages.Validate, "Extracting Retro Rewind...", 2);
        var extraction = Path.Combine(staging, "retro-download");
        await ExtractPackAsync(archive, extraction, cancellationToken);
        var source = RetroRewindSource.ResolveRetroRewind6(extraction);
        // The full ZIP can lag behind incremental releases. Apply every newer official patch
        // before taking compile-input snapshots, just as Wheel Wizard does.
        for (var attempt = 0; attempt < 3; attempt++)
        {
            var updates = ParseUpdates(await client.GetStringAsync(Server + "RetroRewindVersion.txt", cancellationToken));
            if (updates.Count == 0) throw new InvalidDataException("The Retro Rewind version manifest is empty.");
            var current = Version.Parse(File.ReadAllText(Path.Combine(source, "version.txt")).Trim());
            var pending = updates.Where(update => update.Version > current).ToList();
            if (pending.Count == 0) break;
            var deletions = await client.GetStringAsync(Server + "RetroRewindDelete.txt", cancellationToken);
            foreach (var update in pending)
            {
                reporter.Progress(InstallStages.Validate, $"Installing Retro Rewind update {update.Version}...", 2);
                foreach (var line in deletions.Split('\n'))
                {
                    var parts = line.Trim().Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length != 2 || !Version.TryParse(parts[0], out var version) || version <= current || version > update.Version) continue;
                    var target = Path.GetFullPath(Path.Combine(extraction, parts[1].TrimStart('/', '\\')));
                    if (!target.StartsWith(Path.GetFullPath(extraction) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Invalid path in the Retro Rewind deletion manifest.");
                    if (File.Exists(target)) File.Delete(target);
                    else if (Directory.Exists(target)) Directory.Delete(target, recursive: true);
                }
                if (!Uri.TryCreate(update.Url, UriKind.Absolute, out var patchUri) || patchUri.Scheme != "https")
                    throw new InvalidDataException("Invalid Retro Rewind update URL.");
                using var response = await client.GetAsync(patchUri, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
                response.EnsureSuccessStatusCode();
                await SaveDownloadAsync(response, archive, cancellationToken);
                await ExtractPackAsync(archive, extraction, cancellationToken);
                current = update.Version;
                File.WriteAllText(Path.Combine(source, "version.txt"), current.ToString());
            }
            if (attempt == 2) throw new IOException("Retro Rewind changed during installation. Retry to get the latest version.");
        }
        var destination = Path.Combine(staging, "RetroRewind6");
        // Both directories are staged on the destination volume.
        Directory.Move(source, destination);
        File.Delete(archive);
        return destination;
    }

    private static async Task SaveDownloadAsync(HttpResponseMessage response, string archive,
        CancellationToken cancellationToken)
    {
        var length = response.Content.Headers.ContentLength;
        if (length > MaximumDownloadBytes)
            throw new InvalidDataException("Retro Rewind download exceeds the 4 GiB safety limit.");
        InstallationDiskGuard.Check(archive, length ?? 0);
        await using var input = await response.Content.ReadAsStreamAsync(cancellationToken);
        await using var output = File.Create(archive);
        var buffer = new byte[81920];
        long written = 0;
        int count;
        while ((count = await input.ReadAsync(buffer, cancellationToken)) != 0)
        {
            written = checked(written + count);
            if (written > MaximumDownloadBytes)
                throw new InvalidDataException("Retro Rewind download exceeds the 4 GiB safety limit.");
            InstallationDiskGuard.Check(archive, count);
            await output.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
        }
    }

    internal static async Task ExtractPackAsync(string archive, string destination,
        CancellationToken cancellationToken)
    {
        using var zip = ZipFile.OpenRead(archive);
        long expanded = 0;
        foreach (var entry in zip.Entries)
        {
            expanded = checked(expanded + entry.Length);
            if (expanded > MaximumExtractedBytes)
                throw new InvalidDataException("Retro Rewind archive exceeds the 8 GiB extraction safety limit.");
        }
        InstallationDiskGuard.Check(destination, expanded);
        var root = Path.GetFullPath(destination).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        foreach (var entry in zip.Entries)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var path = Path.GetFullPath(Path.Combine(root, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
            if (!path.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Unsafe path in the Retro Rewind archive.");
            if (entry.FullName.EndsWith('/')) { Directory.CreateDirectory(path); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await using var input = entry.Open();
            await using var output = File.Create(path);
            var buffer = new byte[81920];
            long written = 0;
            int count;
            while ((count = await input.ReadAsync(buffer, cancellationToken)) != 0)
            {
                written = checked(written + count);
                if (written > entry.Length)
                    throw new InvalidDataException("Retro Rewind archive entry exceeds its advertised size.");
                InstallationDiskGuard.Check(path, count);
                await output.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
            }
        }
    }
}
