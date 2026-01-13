using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Win32;
using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Management;
using System.Threading.Tasks;
using Windows.ApplicationModel.DataTransfer;
using Windows.System.Diagnostics;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace Winver
{
    /// <summary>
    /// An empty window that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainWindow : Window
    {
        //List variable for Drives, cpu, gpu, ram
        public ObservableCollection<DriveInfo> Drives { get; } = new();
        private DispatcherTimer? _ramTimer;
        private double _ramUsageRatio;
        private DispatcherQueueTimer? _cpuTimer;
        private DispatcherQueueTimer? _gpuTimer;
        private PerformanceCounter[]? _gpuCounters;

        public MainWindow()
        {
            InitializeComponent();
            //Load the project
            LoadProject_Name_Winver();
        }

        private void LoadProject_Name_Winver()
        {
            //Load the drives
            LoadDrivesAsync();
            //Display Windows Version
            DisplayWindowsVersion();
            //Find and display if Windows is activated
            IsntWindowsActivated();
            //Display Windows Edition
            WindowsProductEdition();
            //Display Windows Install Date
            GetWindowsInstallDate();
            //Display CPU, GPU, RAM Names
            DisplayCPU();
            DisplayGPU();
            DisplayRAM();
            // Start monitoring resources
            StartRamMonitor();
            StartCpuMonitor();
            StartGpuMonitor();
            // Customize the window
            // Use the prefered theme of the app for the title bar
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

            // Set the window size (including borders)
            AppWindow.Resize(new Windows.Graphics.SizeInt32(726, 685));

            // Set the window position on screen
            AppWindow.Move(new Windows.Graphics.PointInt32(50, 50));
        }

        private void SelectorBar1_SelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
        {
            // Check if an item was actually selected AND if it has a Tag, and store the Tag as a string
            if (sender.SelectedItem is SelectorBarItem selectedItem && selectedItem.Tag is string selectedTag)
            {
                // Hide all content grids first
                Version.Visibility = Visibility.Collapsed;
                Resources.Visibility = Visibility.Collapsed;
                About.Visibility = Visibility.Collapsed;
                Storage.Visibility = Visibility.Collapsed;

                // Show the corresponding grid based on the safe 'selectedTag'
                switch (selectedTag)
                {
                    case "Version":
                        Version.Visibility = Visibility.Visible;
                        // Set window size for Version tab
                        AppWindow.Resize(new Windows.Graphics.SizeInt32(726, 685));
                        break;
                    case "Resources":
                        Resources.Visibility = Visibility.Visible;
                        // Set window size for Resources tab
                        AppWindow.Resize(new Windows.Graphics.SizeInt32(726, 740));
                        break;
                    case "About":
                        About.Visibility = Visibility.Visible;
                        // Set window size for About tab
                        AppWindow.Resize(new Windows.Graphics.SizeInt32(726, 685));
                        break;
                    case "Storage":
                        Storage.Visibility = Visibility.Visible;
                        // Set window size for Storage tab
                        AppWindow.Resize(new Windows.Graphics.SizeInt32(726, 740));
                        break;
                }
            }
        }

        private void Expander_Loaded(object sender, RoutedEventArgs e)
        {
            // Get the DriveInfo from the Expander's DataContext
            var expander = (Expander)sender;
            var drive = (DriveInfo)expander.DataContext;

            // Find the TextBlocks inside this Expander
            var usedTextBlock = expander.FindName("SpaceRemaining") as TextBlock;
            var totalTextBlock = expander.FindName("SpaceTotal") as TextBlock;
            var freeTextBlock = expander.FindName("SpaceLeft") as TextBlock;

            //Set TextBlocks according to DriveInfo. Set TextBlocks: Used, Total, Free
            if (usedTextBlock != null)
                usedTextBlock.Text = FormatBytes(drive.TotalSize - drive.TotalFreeSpace);

            if (totalTextBlock != null)
                totalTextBlock.Text = FormatBytes(drive.TotalSize);

            if (freeTextBlock != null)
                freeTextBlock.Text = FormatBytes(drive.TotalFreeSpace);

            //Progress Bar Value
            //Calculation
            double percentUsed =
                (double)(drive.TotalSize - drive.TotalFreeSpace) / drive.TotalSize * 100;
            //Find Progress Ring
            var ProgressRingSpace = expander.FindName("ProgressRingSpace") as ProgressRing;
            //Display on the Progress ring
            ProgressRingSpace.Value = percentUsed;

            //Mounted at Text
            var MountedAtTextBlock = expander.FindName("MountedLetter") as TextBlock;
            if (MountedAtTextBlock != null)
                MountedAtTextBlock.Text = $"{drive.RootDirectory.FullName}";

            //Find Glyph
            var DriveGlyph = expander.FindName("DriveIcon") as FontIcon;

            switch (drive.DriveType)
            {
                case DriveType.Fixed:
                    DriveGlyph.Glyph = "\uEDA2"; // Hard Drive icon
                    break;

                case DriveType.Removable:
                    DriveGlyph.Glyph = "\uE88E"; // USB icon
                    break;

                case DriveType.Network:
                    DriveGlyph.Glyph = "\xE8CE"; // Map Drive icon
                    break;

                case DriveType.CDRom:
                    DriveGlyph.Glyph = "\uE958"; // CD Drive icon
                    break;

                default:
                    DriveGlyph.Glyph = "\uE897"; // Default to ?
                    break;
            }

        }

        public string FormatBytes(long bytes)
        {
            // Convert bytes to human-readable format
            // e.g., 1024 -> 1 KB, 1048576 -> 1 MB, etc.
            // Using binary prefixes (1 KB = 1024 bytes)

            // Define size units
            const long KB = 1024;
            const long MB = KB * 1024;
            const long GB = MB * 1024;
            const long TB = GB * 1024;

            // Determine the appropriate unit
            if (bytes >= TB)
                return (bytes / (double)TB).ToString("F2") + " TB";
            if (bytes >= GB)
                return (bytes / (double)GB).ToString("F2") + " GB";
            if (bytes >= MB)
                return (bytes / (double)MB).ToString("F1") + " MB";
            if (bytes >= KB)
                return (bytes / (double)KB).ToString("F0") + " KB";

            return bytes + " B";
        }

        private async void LoadDrivesAsync()
        {
            // Clear existing drives
            Drives.Clear();
            // Load drives asynchronously
            await Task.Run(() =>
            {
                foreach (var d in DriveInfo.GetDrives())
                {
                    try
                    {
                        if (d.IsReady)
                        {
                            // UI thread update
                            DispatcherQueue.TryEnqueue(() => Drives.Add(d));
                        }
                    }
                    catch
                    {
                        // ignore drives that block or throw
                    }
                }
            });
        }

        private void DisplayWindowsVersion()
        {
            // --- 1. Get Core Build Information ---
            var version = Windows.System.Profile.AnalyticsInfo.VersionInfo.DeviceFamilyVersion;
            ulong versionNumeric = ulong.Parse(version);
            ulong build = (versionNumeric & 0x00000000FFFF0000L) >> 16;
            ulong revision = (versionNumeric & 0x000000000000FFFFL);

            // --- 2. Get the Marketing Name (e.g., 25H2) from the Registry ---
            string featureUpdateVersion = string.Empty;

            // The key path where Windows stores its release and version names
            const string regKeyPath = @"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion";

            try
            {
                // Read the "DisplayVersion" value (e.g., "24H2") directly. This is the cleanest method.
                var displayVersion = Registry.GetValue(regKeyPath, "DisplayVersion", string.Empty);

                if (displayVersion is string displayVersionString && !string.IsNullOrWhiteSpace(displayVersionString))
                {
                    featureUpdateVersion = displayVersionString;
                }
                else
                {
                    // Fallback for older formats: sometimes 'ReleaseId' is used.
                    var releaseId = Registry.GetValue(regKeyPath, "ReleaseId", string.Empty);
                    if (releaseId is string releaseIdString)
                    {
                        // Simple logic: if ReleaseId is "2402", we map it to 24H1 or 24H2 based on month/build, 
                        // but relying on DisplayVersion is much safer and simpler.
                        // I will skip complex mapping and just use the ID if we can't find the name.
                        featureUpdateVersion = $"Build {build}";
                    }
                }
            }
            catch (Exception ex)
            {
                // Handle security exceptions if registry access is denied (common in sandboxed apps)
                System.Diagnostics.Debug.WriteLine($"Error reading registry: {ex.Message}");
                featureUpdateVersion = $"Build {build}"; // Fallback to just the build number
            }


            // --- 3. Determine Final Display Strings ---
            string windowsName = build >= 22000 ? "Windows 11" : "Windows 10";

            // Combine the feature version and the OS Build number
            string finalBuildInfo = string.IsNullOrWhiteSpace(featureUpdateVersion)
                ? $"OS Build {build}.{revision}" // Fallback if registry read failed
                : $"Version {featureUpdateVersion} (OS Build {build}.{revision})"; // e.g., Version 25H2 (OS Build 26220.7271)

            // --- 4. Update the TextBlocks ---
            if (WinverProductName != null)
            {
                WinverProductName.Text = $"Product: Microsoft {windowsName}";
            }
            if (WinverBuildInfo != null)
            {
                WinverBuildInfo.Text = $"Build: {finalBuildInfo}";
            }
        }

        private void InitGpuCounters()
        {
            // Clean up existing counters if any
            DisposeGpuCounters();

            // Create a PerformanceCounterCategory for GPU Engine
            var category = new PerformanceCounterCategory("GPU Engine");

            // Get only 3D engines (like Task Manager)
            var counterNames = category.GetInstanceNames()
                .Where(name => name.EndsWith("engtype_3D"));

            // Create PerformanceCounter objects for each 3D engine
            _gpuCounters = counterNames
                .Select(name => new PerformanceCounter(
                    "GPU Engine",
                    "Utilization Percentage",
                    name,
                    readOnly: true))
                .ToArray();

            // Warm-up: first NextValue() call always returns 0
            if (_gpuCounters != null)
            {
                foreach (var c in _gpuCounters)
                {
                    try
                    {
                        c.NextValue();
                    }
                    catch
                    {
                        // Ignore individual failures during warm-up
                    }
                }
            }
        }

        private void DisposeGpuCounters()
        {
            // Dispose existing GPU counters
            if (_gpuCounters == null) return;

            foreach (var c in _gpuCounters)
            {
                try
                {
                    c.Dispose();
                }
                catch
                {
                    // ignore
                }
            }

            // Clear the array
            _gpuCounters = null;
        }

        bool IsWindowsActivated()
        {
            try
            {
                // Query WMI for SoftwareLicensingProduct
                var searcher = new ManagementObjectSearcher(
                    "SELECT Name, LicenseStatus FROM SoftwareLicensingProduct");

                // Iterate through the results
                foreach (ManagementObject o in searcher.Get())
                {
                    string name = o["Name"]?.ToString() ?? "";
                    uint status = (uint)(o["LicenseStatus"] ?? 0);

                    if (name.Contains("Windows") && status == 1)
                    {
                        return true; // FOUND activated Windows
                    }
                }
            }
            catch
            {
            }

            return false; // none were activated
        }

        string GetWindowsEditionShort()
        {
            try
            {
                // Query WMI for Win32_OperatingSystem's Caption
                foreach (ManagementObject o in
                    new ManagementObjectSearcher("SELECT Caption FROM Win32_OperatingSystem").Get())
                {
                    string caption = o["Caption"]?.ToString() ?? "";
                    caption = caption.Replace("Microsoft ", ""); // remove "Microsoft "
                    return caption; // e.g., "Windows 11 Pro"
                }
            }
            catch
            {
            }

            return "Windows";
        }


        string GetWindowsInstallDateS()
        {
            try
            {
                // Query WMI for Win32_OperatingSystem's InstallDate
                foreach (ManagementObject o in
                    new ManagementObjectSearcher("SELECT InstallDate FROM Win32_OperatingSystem").Get())
                {
                    string? rawDate = o["InstallDate"]?.ToString(); //? is annoying!!
                    if (rawDate != null)
                    {
                        return ManagementDateTimeConverter
                            .ToDateTime(rawDate)
                            .ToString("dd MMM yyyy");
                    }
                }
            }
            catch
            {
            }

            return "Just a question! Do you know?";
        }


        private string GetCPUName()
        {
            try
            {
                // Query WMI for Win32_Processor's Name
                using var searcher =
                    new ManagementObjectSearcher("SELECT Name FROM Win32_Processor");

                foreach (ManagementObject cpu in searcher.Get())
                {
                    return cpu["Name"]?.ToString()?.Trim() ?? "Unknown CPU";
                }
            }
            catch
            {
            }

            return "Unknown CPU! Go buy a good CPU man.";
        }
        private string GetGpuName()
        {
            try
            {
                // Query WMI for Win32_VideoController's Name
                using var searcher =
                    new ManagementObjectSearcher("SELECT Name FROM Win32_VideoController");

                foreach (ManagementObject gpu in searcher.Get())
                {
                    string name = gpu["Name"]?.ToString()?.Trim() ?? "";

                    if (!string.IsNullOrEmpty(name))
                        return name;
                }
            }
            catch
            {
            }

            return "Unknown GPU! Get your GPU a life man!";
        }

        private string GetRamName()
        {
            try
            {
                // Query WMI for Win32_PhysicalMemory's Manufacturer and PartNumber
                using var searcher =
                    new ManagementObjectSearcher(
                        "SELECT Manufacturer, PartNumber FROM Win32_PhysicalMemory");

                foreach (ManagementObject ram in searcher.Get())
                {
                    string manufacturer = ram["Manufacturer"]?.ToString()?.Trim() ?? "";
                    string partNumber = ram["PartNumber"]?.ToString()?.Trim() ?? "";

                    if (!string.IsNullOrEmpty(manufacturer) || !string.IsNullOrEmpty(partNumber))
                    {
                        return $"{manufacturer} {partNumber}".Trim();
                    }
                }
            }
            catch
            {
            }

            return "Why do you have a cheap ram?";
        }

        private void StartRamMonitor()
        {
            // Create a DispatcherTimer to update RAM usage every 0.5 seconds
            _ramTimer = new DispatcherTimer();
            _ramTimer.Interval = TimeSpan.FromSeconds(0.5);

            // Update RAM usage on each tick
            _ramTimer.Tick += (s, e) =>
            {
                // Get memory usage report
                var memReport = SystemDiagnosticInfo.GetForCurrentSystem()
                                                    .MemoryUsage
                                                    .GetReport();

                // Calculate used and total RAM in GB
                double totalGB = memReport.TotalPhysicalSizeInBytes / 1024.0 / 1024 / 1024;
                double usedGB = (memReport.TotalPhysicalSizeInBytes -
                                 memReport.AvailableSizeInBytes) / 1024.0 / 1024 / 1024;

                // Calculate percentage used
                double percent = (usedGB / totalGB) * 100;

                // RAM_Info.Text = $"{usedGB:F1} GB / {totalGB:F1} GB ({percent:F0}%)";
                _ramUsageRatio = (usedGB / totalGB) * 100;
                ProgressBar_RAM.Value = _ramUsageRatio;
                RAM_Resources_Usage.Text = $"Used: {usedGB:F1} GB";
                RAM_Resources_Total.Text = $"Total: {totalGB:F1} GB";
            };

            // Start the timer
            _ramTimer.Start();
        }

        private void StartCpuMonitor()
        {
            // Create a DispatcherQueueTimer to update CPU usage every second
            var dispatcherQueue = DispatcherQueue.GetForCurrentThread();

            // Create the timer
            _cpuTimer = dispatcherQueue.CreateTimer();
            _cpuTimer.Interval = TimeSpan.FromSeconds(1);

            // Store previous values for CPU usage calculation
            TimeSpan? prevIdleTime = null;
            TimeSpan? prevKernelTime = null;
            TimeSpan? prevUserTime = null;

            _cpuTimer.Tick += (s, e) =>
            {
                // Get CPU usage report
                var cpuReport = SystemDiagnosticInfo
                    .GetForCurrentSystem()
                    .CpuUsage
                    .GetReport();

                // Calculate CPU usage based on deltas
                if (prevIdleTime.HasValue && prevKernelTime.HasValue && prevUserTime.HasValue)
                {
                    // Current times
                    var idleTime = cpuReport.IdleTime;
                    var kernelTime = cpuReport.KernelTime;
                    var userTime = cpuReport.UserTime;

                    // Calculate deltas
                    var idleDelta = (idleTime - prevIdleTime.Value).TotalMilliseconds;
                    var kernelDelta = (kernelTime - prevKernelTime.Value).TotalMilliseconds;
                    var userDelta = (userTime - prevUserTime.Value).TotalMilliseconds;

                    //Logic
                    var totalDelta = kernelDelta + userDelta;

                    double cpuPercent = 0;
                    if (totalDelta > 0)
                    {
                        cpuPercent = ((totalDelta - idleDelta) / totalDelta) * 100.0;
                    }

                    // Update UI
                    ProgressBar_CPU.Value = cpuPercent;
                    CPU_Resources_Usage.Text = $"Usage: {cpuPercent:F2}%";
                    int logicalCores = Environment.ProcessorCount;
                    CPU_Resources_Cores.Text = $"Cores: {logicalCores} Logical";
                }

                // Update previous times
                prevIdleTime = cpuReport.IdleTime;
                prevKernelTime = cpuReport.KernelTime;
                prevUserTime = cpuReport.UserTime;
            };

            // Start the timer
            _cpuTimer.Start();
        }

        private void StartGpuMonitor()
        {
            // Initialize GPU counters
            InitGpuCounters();

            //
            var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
            _gpuTimer = dispatcherQueue.CreateTimer();
            _gpuTimer.Interval = TimeSpan.FromSeconds(1);

            _gpuTimer.Tick += (s, e) =>
            {
                if (_gpuCounters == null) return;

                float totalUsage = 0f;

                try
                {
                    foreach (var counter in _gpuCounters)
                        totalUsage += counter.NextValue();
                }
                catch (InvalidOperationException)
                {
                    // Instance disappeared — recreate counters and skip this tick
                    InitGpuCounters();
                    return;
                }

                // Clamp to 100
                totalUsage = Math.Min(totalUsage, 100f);

                GPU_Resources_Usage.Text = $"Usage: {totalUsage:F2}%";
                ProgressBar_GPU.Value = totalUsage;
            };

            _gpuTimer.Start();
        }

        public double GetUsedPercent(DriveInfo d)
        {
            //Get the used Percentage of the disk
            if (d.TotalSize == 0) return 0;
            return (double)(d.TotalSize - d.TotalFreeSpace) / d.TotalSize * 100;
        }

        private void ErrorChecker(DriveInfo d)
        {
            var psi = new ProcessStartInfo
            {
                FileName = "cmd.exe",
                Arguments = $"/c \"echo Unavailible && pause\"",
                UseShellExecute = false
            };
            Process.Start(psi);
        }

        private void CPU_Click(object sender, RoutedEventArgs e)
        {
            CopyButton_Click(GetCPUName());
        }

        private void GPU_Click(object sender, RoutedEventArgs e)
        {
            CopyButton_Click(GetGpuName());
        }

        private void RAM_Click(object sender, RoutedEventArgs e)
        {
            CopyButton_Click(GetRamName());
        }

        private void DisplayCPU()
        {
            CPU_Info.Text = GetCPUName();
        }

        private void DisplayGPU()
        {
            GPU_Info.Text = GetGpuName();
        }

        private void DisplayRAM()
        {
            RAM_Info.Text = GetRamName();
        }

        private void WindowsProductEdition()
        {
            ProductName.Text = "Edition: " + GetWindowsEditionShort();
        }

        private void IsntWindowsActivated()
        {
            if (IsWindowsActivated())
            {
                ActivationStatus.Text = "Activation Status: Activated";
                ActivationStatus.SelectionHighlightColor = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 0, 128, 0)); //Green
            }
            else
            {
                ActivationStatus.Text = "Activation Status: Unactivated";
                ActivationStatus.SelectionHighlightColor = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 0, 255, 0)); //Red
            }
        }

        private void GetWindowsInstallDate()
        {
            InstalledOn.Text = "Installed On: " + GetWindowsInstallDateS();
        }

        private void CopyButton_Click(string textocopy)
        {
            if (!string.IsNullOrEmpty(textocopy))
            {
                var dataPackage = new DataPackage();
                dataPackage.SetText(textocopy);
                Clipboard.SetContent(dataPackage);
            }
        }

        private void OK_Click(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        private void ErrorChecker_Click(object? sender, RoutedEventArgs e)
        {
            // Try to obtain the DriveInfo from common places (Button.DataContext, sender.DataContext, or parent Expander)
            DriveInfo? drive = null;

            if (sender is FrameworkElement fe && fe.DataContext is DriveInfo d0)
            {
                drive = d0;
            }
            else if (sender is DependencyObject dep)
            {
                var btn = dep as Button;
                if (btn?.DataContext is DriveInfo d1)
                {
                    drive = d1;
                }
                else
                {
                    var exp = FindParent<Expander>(dep);
                    if (exp?.DataContext is DriveInfo d2)
                        drive = d2;
                }
            }

            if (drive != null)
            {
                ErrorChecker(drive);
            }
            else
            {
                // Optional: fallback behavior or log for debugging
                Debug.WriteLine("ErrorChecker_Click: Could not determine DriveInfo from sender.");
            }
        }

        private static T? FindParent<T>(DependencyObject? child) where T : DependencyObject
        {
            if (child == null) return null;
            DependencyObject? current = child;
            while (current != null)
            {
                current = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(current);
                if (current is T t) return t;
            }
            return null;
        }
    }

}
