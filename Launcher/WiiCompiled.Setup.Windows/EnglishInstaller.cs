namespace WiiCompiled.Setup.Windows;

using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using System.Windows.Forms;

internal static class EnglishInstaller
{
    public static int Run()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new InstallerForm());
        return 0;
    }

    private sealed class InstallerForm : Form
    {
        private readonly TextBox _romPath = new() { Dock = DockStyle.Fill, ReadOnly = true };
        private readonly TextBox _retroPath = new() { Dock = DockStyle.Fill, ReadOnly = true };
        private readonly TextBox _destination = new() { Dock = DockStyle.Fill };
        private readonly CheckBox _downloadRetro = new()
        {
            Text = "Download and install Retro Rewind automatically (recommended)",
            Checked = true,
            AutoSize = true
        };
        private Button _retroBrowse = null!;
        private readonly CheckBox _portable = new() { Text = "Create a portable installation", AutoSize = true };
        private readonly Button _install = new() { Text = "Install", AutoSize = true, Padding = new Padding(18, 5, 18, 5) };
        private readonly ProgressBar _progress = new() { Dock = DockStyle.Fill, Minimum = 0, Maximum = 100 };
        private readonly Label _status = new() { Text = "Choose your Mario Kart Wii PAL disc image to begin.", AutoSize = true };
        private readonly TextBox _details = new()
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            BackColor = System.Drawing.SystemColors.Window
        };
        private bool _destinationWasEdited;

        private static string StandardDestination => ProductInfo.DefaultInstallDirectory;
        private static string PortableDestination => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "WiiCompiled VR Portable");

        public InstallerForm()
        {
            Text = $"WiiCompiled VR Setup {ProductInfo.Version}";
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = true;
            ClientSize = new System.Drawing.Size(720, 610);
            Font = new System.Drawing.Font("Segoe UI", 10F);

            var title = new Label
            {
                Text = "Install Mario Kart Wii VR",
                Font = new System.Drawing.Font("Segoe UI Semibold", 18F),
                AutoSize = true
            };
            var explanation = new Label
            {
                Text = "Choose your own clean PAL RMCP01 disc image. By default, setup also downloads the latest Retro Rewind pack from the official Wheel Wizard service. Your ROM stays on this PC.",
                Dock = DockStyle.Fill,
                AutoSize = true,
                MaximumSize = new System.Drawing.Size(680, 0)
            };

            var layout = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                Padding = new Padding(20),
                ColumnCount = 3,
                RowCount = 11
            };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 145));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 95));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 55));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            layout.Controls.Add(title, 0, 0);
            layout.SetColumnSpan(title, 3);
            layout.Controls.Add(explanation, 0, 1);
            layout.SetColumnSpan(explanation, 3);
            AddPathRow(layout, 2, "PAL disc image", _romPath, "Browse...", BrowseRom);
            AddPathRow(layout, 3, "Install location", _destination, "Browse...", BrowseDestination);
            layout.Controls.Add(_downloadRetro, 1, 4);
            layout.SetColumnSpan(_downloadRetro, 2);
            _retroBrowse = AddPathRow(layout, 5, "Existing RR folder", _retroPath, "Browse...", BrowseRetro);
            _retroPath.Enabled = false;
            _retroBrowse.Enabled = false;
            _downloadRetro.CheckedChanged += (_, _) =>
            {
                _retroPath.Enabled = !_downloadRetro.Checked;
                _retroBrowse.Enabled = !_downloadRetro.Checked;
            };
            _destination.Text = StandardDestination;
            _destination.TextChanged += (_, _) => _destinationWasEdited = true;
            _portable.CheckedChanged += (_, _) =>
            {
                if (_destinationWasEdited) return;
                _destination.Text = _portable.Checked ? PortableDestination : StandardDestination;
                _destinationWasEdited = false;
            };
            layout.Controls.Add(_portable, 1, 6);
            layout.SetColumnSpan(_portable, 2);
            layout.Controls.Add(_progress, 0, 7);
            layout.SetColumnSpan(_progress, 3);
            layout.Controls.Add(_status, 0, 8);
            layout.SetColumnSpan(_status, 3);
            layout.Controls.Add(_details, 0, 9);
            layout.SetColumnSpan(_details, 3);

            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, AutoSize = true };
            var close = new Button { Text = "Close", AutoSize = true, Padding = new Padding(12, 5, 12, 5) };
            close.Click += (_, _) => Close();
            _install.Click += async (_, _) => await InstallAsync();
            buttons.Controls.Add(close);
            buttons.Controls.Add(_install);
            layout.Controls.Add(buttons, 0, 10);
            layout.SetColumnSpan(buttons, 3);
            Controls.Add(layout);
            AcceptButton = _install;

            if (File.Exists(Path.Combine(AppContext.BaseDirectory, "WheelWizard", "vr-local.txt")))
            {
                _portable.Checked = true;
                _destination.Text = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
                _destinationWasEdited = false;
            }
        }

        private static Button AddPathRow(TableLayoutPanel layout, int row, string label, Control input,
            string buttonText, EventHandler browse)
        {
            var caption = new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left };
            var button = new Button { Text = buttonText, AutoSize = true, Anchor = AnchorStyles.Right };
            button.Click += browse;
            input.Margin = new Padding(3, 10, 3, 10);
            layout.Controls.Add(caption, 0, row);
            layout.Controls.Add(input, 1, row);
            layout.Controls.Add(button, 2, row);
            return button;
        }

        private void BrowseRom(object? sender, EventArgs e)
        {
            using var dialog = new OpenFileDialog
            {
                Title = "Choose your clean PAL Mario Kart Wii disc image",
                Filter = "Wii disc images|*.iso;*.gcm;*.gcz;*.ciso;*.wbfs;*.wia;*.rvz|All files|*.*",
                CheckFileExists = true
            };
            if (dialog.ShowDialog(this) == DialogResult.OK) _romPath.Text = dialog.FileName;
        }

        private void BrowseDestination(object? sender, EventArgs e)
        {
            using var dialog = new FolderBrowserDialog
            {
                Description = "Choose where WiiCompiled VR will be installed",
                UseDescriptionForTitle = true,
                SelectedPath = Directory.Exists(_destination.Text) ? _destination.Text : string.Empty,
                ShowNewFolderButton = true
            };
            if (dialog.ShowDialog(this) == DialogResult.OK) _destination.Text = dialog.SelectedPath;
        }

        private void BrowseRetro(object? sender, EventArgs e)
        {
            using var dialog = new FolderBrowserDialog
            {
                Description = "Choose an existing RetroRewind6 folder, or enable automatic download",
                UseDescriptionForTitle = true,
                ShowNewFolderButton = false
            };
            if (dialog.ShowDialog(this) == DialogResult.OK) _retroPath.Text = dialog.SelectedPath;
        }

        private async Task InstallAsync()
        {
            if (!File.Exists(_romPath.Text))
            {
                MessageBox.Show(this, "Choose a valid Mario Kart Wii disc image first.", Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (string.IsNullOrWhiteSpace(_destination.Text))
            {
                MessageBox.Show(this, "Choose an installation folder first.", Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            var executable = Environment.ProcessPath;
            if (string.IsNullOrWhiteSpace(executable) ||
                Path.GetFileName(executable).Equals("dotnet.exe", StringComparison.OrdinalIgnoreCase))
            {
                MessageBox.Show(this, "Run the published WiiCompiled-VR-Setup.exe to install the game.", Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            SetBusy(true);
            _progress.Value = 0;
            _details.Clear();
            _status.Text = "Starting setup...";
            var installDirectory = _portable.Checked
                ? Path.Combine(Path.GetFullPath(_destination.Text), "Install")
                : Path.GetFullPath(_destination.Text);
            try
            {
                var retroPath = _retroPath.Text;
                if (_downloadRetro.Checked)
                {
                    var retroParent = _portable.Checked
                        ? Path.GetFullPath(_destination.Text)
                        : installDirectory;
                    retroPath = await DownloadLatestRetroRewindAsync(retroParent);
                }

                var start = new ProcessStartInfo(executable)
                {
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    WorkingDirectory = Path.GetDirectoryName(executable)!
                };
                start.ArgumentList.Add("--silent");
                start.ArgumentList.Add("--game");
                start.ArgumentList.Add(_romPath.Text);
                start.ArgumentList.Add("--install-dir");
                start.ArgumentList.Add(installDirectory);
                start.ArgumentList.Add("--progress-json");
                if (_portable.Checked) start.ArgumentList.Add("--portable");
                if (!string.IsNullOrWhiteSpace(retroPath))
                {
                    start.ArgumentList.Add("--retro-dir");
                    start.ArgumentList.Add(retroPath);
                    start.ArgumentList.Add("--download-retro-wfc-payload");
                }

                using var process = Process.Start(start) ?? throw new InvalidOperationException("Setup could not be started.");
                var stderrTask = process.StandardError.ReadToEndAsync();
                string? resultError = null;
                while (await process.StandardOutput.ReadLineAsync() is { } line)
                {
                    try
                    {
                        using var document = JsonDocument.Parse(line);
                        var root = document.RootElement;
                        var type = root.GetProperty("type").GetString();
                        if (type == "progress")
                        {
                            var message = root.GetProperty("message").GetString() ?? "Working...";
                            var percent = root.GetProperty("percent").GetInt32();
                            _status.Text = message;
                            _progress.Value = string.IsNullOrWhiteSpace(retroPath)
                                ? Math.Clamp(percent, 0, 100)
                                : Math.Clamp(40 + percent * 60 / 100, 40, 100);
                            _details.AppendText(message + Environment.NewLine);
                        }
                        else if (type == "result" && !root.GetProperty("success").GetBoolean())
                        {
                            resultError = root.TryGetProperty("error", out var error) ? error.GetString() : "Installation failed.";
                        }
                    }
                    catch (JsonException)
                    {
                        _details.AppendText(line + Environment.NewLine);
                    }
                }
                await process.WaitForExitAsync();
                var diagnostics = await stderrTask;
                if (process.ExitCode != 0 || resultError is not null)
                    throw new InvalidOperationException(resultError ?? LastUsefulLine(diagnostics) ?? $"Setup exited with code {process.ExitCode}.");

                _progress.Value = 100;
                _status.Text = "Installation complete.";
                MessageBox.Show(this,
                    _portable.Checked
                        ? "Portable installation complete. You can move the entire selected folder to another location."
                        : "WiiCompiled VR was installed successfully.",
                    Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception exception)
            {
                _status.Text = "Installation failed.";
                _details.AppendText(exception.Message + Environment.NewLine);
                MessageBox.Show(this, exception.Message + Environment.NewLine + Environment.NewLine +
                    "More details are available in %TEMP%\\WiiCompiled-setup.log.", Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                SetBusy(false);
            }
        }

        private void SetBusy(bool busy)
        {
            _install.Enabled = !busy;
            _portable.Enabled = !busy;
            _downloadRetro.Enabled = !busy;
            _retroPath.Enabled = !busy && !_downloadRetro.Checked;
            _retroBrowse.Enabled = !busy && !_downloadRetro.Checked;
            UseWaitCursor = busy;
        }

        private async Task<string> DownloadLatestRetroRewindAsync(string destinationParent)
        {
            const string officialEndpoint = "https://update.rwfc.net/RetroRewind/RetroRewindInstall.txt";
            // Keep staging on the destination volume because Directory.Move cannot cross drives.
            destinationParent = Path.GetFullPath(destinationParent);
            Directory.CreateDirectory(destinationParent);
            var temporaryRoot = Path.Combine(destinationParent,
                ".wiicompiled-retro-" + Guid.NewGuid().ToString("N"));
            var archivePath = Path.Combine(temporaryRoot, "RetroRewind.zip");
            var extractionPath = Path.Combine(temporaryRoot, "Extracted");
            Directory.CreateDirectory(temporaryRoot);
            try
            {
                _status.Text = "Finding the latest Retro Rewind release...";
                _details.AppendText(_status.Text + Environment.NewLine);
                using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
                client.DefaultRequestHeaders.UserAgent.ParseAdd("WiiCompiled-VR-Setup/0.4.3");
                var downloadText = (await client.GetStringAsync(officialEndpoint)).Trim();
                if (!Uri.TryCreate(downloadText, UriKind.Absolute, out var downloadUri) ||
                    downloadUri.Scheme != Uri.UriSchemeHttps ||
                    !(downloadUri.Host.Equals("update.rwfc.net", StringComparison.OrdinalIgnoreCase) ||
                      downloadUri.Host.EndsWith(".update.rwfc.net", StringComparison.OrdinalIgnoreCase)))
                    throw new InvalidDataException("The official Retro Rewind service returned an invalid download address.");

                _status.Text = "Downloading Retro Rewind...";
                _details.AppendText(_status.Text + Environment.NewLine);
                using var response = await client.GetAsync(downloadUri, HttpCompletionOption.ResponseHeadersRead);
                response.EnsureSuccessStatusCode();
                var total = response.Content.Headers.ContentLength;
                await using (var input = await response.Content.ReadAsStreamAsync())
                await using (var output = new FileStream(archivePath, FileMode.Create, FileAccess.Write, FileShare.None))
                {
                    var buffer = new byte[1024 * 1024];
                    long received = 0;
                    int read;
                    while ((read = await input.ReadAsync(buffer)) > 0)
                    {
                        await output.WriteAsync(buffer.AsMemory(0, read));
                        received += read;
                        if (total is > 0)
                            _progress.Value = Math.Clamp(5 + (int)(received * 30 / total.Value), 5, 35);
                    }
                }

                _status.Text = "Extracting Retro Rewind...";
                _details.AppendText(_status.Text + Environment.NewLine);
                _progress.Value = 36;
                await Task.Run(() => ExtractArchiveSafely(archivePath, extractionPath));
                var source = Path.Combine(extractionPath, "RetroRewind6");
                if (!File.Exists(Path.Combine(source, "Binaries", "Code.pul")))
                    throw new InvalidDataException("The official archive does not contain RetroRewind6\\Binaries\\Code.pul.");

                var destination = Path.Combine(destinationParent, "RetroRewind6");
                if (Directory.Exists(destination)) Directory.Delete(destination, recursive: true);
                Directory.Move(source, destination);
                _progress.Value = 40;
                _details.AppendText("Retro Rewind is ready. Starting VR compilation..." + Environment.NewLine);
                return destination;
            }
            finally
            {
                try
                {
                    if (Directory.Exists(temporaryRoot)) Directory.Delete(temporaryRoot, recursive: true);
                }
                catch
                {
                    // A temporary file held briefly by antivirus software is harmless and can be
                    // removed later by Windows temporary-file cleanup.
                }
            }
        }

        private static void ExtractArchiveSafely(string archivePath, string destination)
        {
            var root = Path.GetFullPath(destination).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            Directory.CreateDirectory(root);
            using var archive = ZipFile.OpenRead(archivePath);
            foreach (var entry in archive.Entries)
            {
                var outputPath = Path.GetFullPath(Path.Combine(root,
                    entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
                if (!outputPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The Retro Rewind archive contains an unsafe path.");
                if (string.IsNullOrEmpty(entry.Name))
                {
                    Directory.CreateDirectory(outputPath);
                    continue;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
                entry.ExtractToFile(outputPath, overwrite: true);
            }
        }

        private static string? LastUsefulLine(string text) => text.Split(new[] { '\r', '\n' },
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).LastOrDefault();
    }
}
