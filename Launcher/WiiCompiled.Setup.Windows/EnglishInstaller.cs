namespace WiiCompiled.Setup.Windows;

using System.Diagnostics;
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
            ClientSize = new System.Drawing.Size(720, 570);
            Font = new System.Drawing.Font("Segoe UI", 10F);

            var title = new Label
            {
                Text = "Install Mario Kart Wii VR",
                Font = new System.Drawing.Font("Segoe UI Semibold", 18F),
                AutoSize = true
            };
            var explanation = new Label
            {
                Text = "Choose your own clean PAL RMCP01 disc image. ISO, GCM, GCZ, CISO, WBFS, WIA, and RVZ are supported. You may also select an existing RetroRewind6 folder. Your files stay on this PC.",
                Dock = DockStyle.Fill,
                AutoSize = true,
                MaximumSize = new System.Drawing.Size(680, 0)
            };

            var layout = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                Padding = new Padding(20),
                ColumnCount = 3,
                RowCount = 10
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
            AddPathRow(layout, 4, "Retro Rewind folder", _retroPath, "Optional...", BrowseRetro);
            _destination.Text = StandardDestination;
            _destination.TextChanged += (_, _) => _destinationWasEdited = true;
            _portable.CheckedChanged += (_, _) =>
            {
                if (_destinationWasEdited) return;
                _destination.Text = _portable.Checked ? PortableDestination : StandardDestination;
                _destinationWasEdited = false;
            };
            layout.Controls.Add(_portable, 1, 5);
            layout.SetColumnSpan(_portable, 2);
            layout.Controls.Add(_progress, 0, 6);
            layout.SetColumnSpan(_progress, 3);
            layout.Controls.Add(_status, 0, 7);
            layout.SetColumnSpan(_status, 3);
            layout.Controls.Add(_details, 0, 8);
            layout.SetColumnSpan(_details, 3);

            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, AutoSize = true };
            var close = new Button { Text = "Close", AutoSize = true, Padding = new Padding(12, 5, 12, 5) };
            close.Click += (_, _) => Close();
            _install.Click += async (_, _) => await InstallAsync();
            buttons.Controls.Add(close);
            buttons.Controls.Add(_install);
            layout.Controls.Add(buttons, 0, 9);
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

        private static void AddPathRow(TableLayoutPanel layout, int row, string label, Control input,
            string buttonText, EventHandler browse)
        {
            var caption = new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left };
            var button = new Button { Text = buttonText, AutoSize = true, Anchor = AnchorStyles.Right };
            button.Click += browse;
            input.Margin = new Padding(3, 10, 3, 10);
            layout.Controls.Add(caption, 0, row);
            layout.Controls.Add(input, 1, row);
            layout.Controls.Add(button, 2, row);
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
                Description = "Optionally choose an existing RetroRewind6 folder",
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
                if (!string.IsNullOrWhiteSpace(_retroPath.Text))
                {
                    start.ArgumentList.Add("--retro-dir");
                    start.ArgumentList.Add(_retroPath.Text);
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
                            _progress.Value = Math.Clamp(percent, 0, 100);
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
            UseWaitCursor = busy;
        }

        private static string? LastUsefulLine(string text) => text.Split(new[] { '\r', '\n' },
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).LastOrDefault();
    }
}


