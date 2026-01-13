using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.Win32;
using System;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;
using System.Threading.Tasks;
using Windows.Graphics;
using Windows.Media.Protection;
using Windows.System;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace WinFlow_Installer
{
    /// <summary>
    /// An empty window that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Load_Project_Installer();
        }
        public void Load_Project_Installer()
        {
            // Set the window title
            AppWindow.Title = "WinFlow Application Suite Installer";

            // Set the window size (including borders)
            AppWindow.Resize(new Windows.Graphics.SizeInt32(1024, 640));

            // Set the preferred theme for the title bar
            AppWindow.TitleBar.PreferredTheme = TitleBarTheme.UseDefaultAppMode;

            // Create an overlapped presenter to customize window options
            OverlappedPresenter presenter = OverlappedPresenter.Create();
            // Extend content into title bar area
            this.ExtendsContentIntoTitleBar = true;


            // Customize window options
            //Let the window be always on top
            presenter.IsAlwaysOnTop = true;
            // Disable maximize and resize
            presenter.IsMaximizable = false;
            // Allow minimize
            presenter.IsMinimizable = true;
            // Disable resize
            presenter.IsResizable = false;
            // Show border and title bar
            presenter.SetBorderAndTitleBar(true, true);

            // Apply the presenter to the window
            AppWindow.SetPresenter(presenter);

            //Center the window on the screen
            CenterWindow();

        }
        private void CenterWindow()
        {
            var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest)?.WorkArea;
            if (area == null) return;
            AppWindow.Move(new PointInt32((area.Value.Width - AppWindow.Size.Width) / 2, (area.Value.Height - AppWindow.Size.Height) / 2));
        }

        private void OK_Click(object sender, RoutedEventArgs e)
        {
            Ready.Visibility = Visibility.Collapsed;
            Agreement.Visibility = Visibility.Visible;
        }

        private void Accept_Click(object sender, RoutedEventArgs e)
        {
            Agreement.Visibility = Visibility.Collapsed;
            Components.Visibility = Visibility.Visible;
        }

        private void Exit_Click(object sender, RoutedEventArgs e)
        {
            Application.Current.Exit();
        }

        private void CheckBox_Checked(object sender, RoutedEventArgs e)
        {
            Continue_Button.IsEnabled = true;
        }

        private void CheckBox_Unchecked(object sender, RoutedEventArgs e)
        {
            Continue_Button.IsEnabled = false;
        }

        private async void Continue_Button_Click(object sender, RoutedEventArgs e)
        {
            Components.Visibility = Visibility.Collapsed;
            SignUp.Visibility = Visibility.Visible;
            var uri = new Uri("https://forms.gle/VaNVDx2UHZQJLh8U6");
            await Launcher.LaunchUriAsync(uri);
        }

        private void Signed_Up_Click(object sender, RoutedEventArgs e)
        {
            SignUp.Visibility = Visibility.Collapsed;
            Installation.Visibility = Visibility.Visible;
        }

        private async void Install_Click(object sender, RoutedEventArgs e)
        {
            Installation.Visibility = Visibility.Collapsed;
            Install_Page.Visibility = Visibility.Visible;
            AppWindow.SetPresenter(AppWindowPresenterKind.FullScreen);
            await Task.Run(() => InstallWinFlow());
        }
        private void InstallWinFlow()
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                Description_Install_Page.Text = $"Checking for Admin Permissions...";
            });
            // 0. Ensure Admin
            if (!new WindowsPrincipal(WindowsIdentity.GetCurrent())
                .IsInRole(WindowsBuiltInRole.Administrator))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = Process.GetCurrentProcess().MainModule!.FileName!,
                    UseShellExecute = true,
                    Verb = "runas"
                });
                Environment.Exit(0);
                return;
            }

            // 1. Check for .NET Desktop Runtime 10
            bool IsRuntimeInstalled()
            {
                using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
                foreach (var subkeyName in key!.GetSubKeyNames())
                {
                    using var subkey = key.OpenSubKey(subkeyName);
                    if (subkey!.GetValue("DisplayName")?.ToString().Contains("Microsoft .NET Desktop Runtime 10") == true)
                        return true;
                }
                return false;
            }

            // Install runtime if missing
            if (!IsRuntimeInstalled())
            {
                DispatcherQueue.TryEnqueue(() =>
                { Description_Install_Page.Text = "Installing .NET Desktop Runtime..."; });
                var runtimeInstaller = Path.Combine(AppContext.BaseDirectory, @"obc_bin\Runtime\windowsdesktop-runtime-10.0.1-win-x64.exe");
                var process = Process.Start(new ProcessStartInfo
                {
                    FileName = runtimeInstaller,
                    Arguments = "/quiet /norestart",
                    UseShellExecute = true
                });
                process!.WaitForExit();
            }

            DispatcherQueue.TryEnqueue(() =>
            { Description_Install_Page.Text = $"Registering Paths..."; });
            // Paths
            string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            string installRoot = Path.Combine(programFiles, "WinFlow");
            string winverFolder = Path.Combine(installRoot, "Winver");
            string uninstallFolder = Path.Combine(installRoot, "Uninstall");
            string winverExe = Path.Combine(winverFolder, "Winver.exe");
            string uninstallBat = Path.Combine(uninstallFolder, "uninstall.bat");

            // Source paths (your unpackaged app layout)
            string sourceRoot = AppContext.BaseDirectory;
            string sourceWinver = Path.Combine(sourceRoot, "obc_bin", "Winver");

            DispatcherQueue.TryEnqueue(() =>
            { Description_Install_Page.Text = $"Installing WinFlow Application Suite..."; });
            // 1. Create folders
            Directory.CreateDirectory(winverFolder);
            Directory.CreateDirectory(uninstallFolder);

            // 2. Copy Winver app files
            foreach (var file in Directory.GetFiles(sourceWinver, "*", SearchOption.AllDirectories))
            {
                string dest = file.Replace(sourceWinver, winverFolder);
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                File.Copy(file, dest, true);
            }

            // 3. Register in Settings → Installed apps
            using (var key = Registry.LocalMachine.CreateSubKey(
                @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\WinFlow"))
            {
                key!.SetValue("DisplayName", "WinFlow");
                key.SetValue("Publisher", "Advay-CMD");
                key.SetValue("DisplayIcon", winverExe);
                key.SetValue("InstallLocation", installRoot);
                key.SetValue("UninstallString", uninstallBat);
                key.SetValue("DisplayVersion", "0.0.1");
            }

            // 4. Register winver override (SAFE way)
            using (var key = Registry.LocalMachine.CreateSubKey(
                @"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\winver.exe"))
            {
                key!.SetValue("", winverExe);
                key.SetValue("Path", winverFolder);
            }

            // 5. Create uninstall.bat
            File.WriteAllText(uninstallBat, $"""
@echo off
echo Uninstalling WinFlow...

taskkill /f /im Winver.exe >nul 2>&1

reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\winver.exe" /f
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\WinFlow" /f

rmdir /s /q "{installRoot}"

echo WinFlow uninstalled.
pause
""");
            // 6. Finish && reboot
            DispatcherQueue.TryEnqueue(() =>
            { Description_Install_Page.Text = $"Installation Complete! The system will reboot in 3 seconds to apply changes."; });
            RebootSystem();

        }
        private void RebootSystem()
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "shutdown",
                Arguments = "/r /t 3",
                CreateNoWindow = true,
                UseShellExecute = false
            });
        }

    }
}
