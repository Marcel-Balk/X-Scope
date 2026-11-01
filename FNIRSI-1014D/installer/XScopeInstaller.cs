// X Scope Setup for the FNIRSI 1014D (Windows, .NET Framework 4.x, C# 5)
//
// One program with everything embedded:
//   - X Scope Remote: installed in Program Files with a Start menu entry, a desktop shortcut and an
//     entry in Windows' installed apps list (uninstall removes all of it)
//   - X Scope firmware: written to the scope's internal SD card while the scope is connected by USB in
//     its disk mode. Setup watches for the scope and installs automatically when it appears.
//
// Card layout written for the firmware:
//   sector 0            MBR with one FAT32 partition starting at sector 2048 (1 MiB)
//   sector 16           SD card image (loader, firmware at +0x8000), written at 8 KiB
//   sectors 2046/2047   X Scope calibration and settings (left alone, created by the firmware)
//
// A stock card has its FAT32 partition at sector 63, which overlaps the firmware area, so a first
// install repartitions the card (all files on it are erased; a full backup is offered first and that
// step always asks). An update only rewrites the image area and keeps the partition and its files.
//
//   X-Scope-Setup.exe               setup window
//   X-Scope-Setup.exe --uninstall   remove X Scope Remote (used by Windows' installed apps list)

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Management;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace XScopeInstaller
{
  //------------------------------------------------------------------------------------------------
  // Look shared with X Scope Remote

  internal static class Theme
  {
    public static readonly Color Window = Color.FromArgb(18, 20, 24);
    public static readonly Color Card = Color.FromArgb(30, 33, 40);
    public static readonly Color CardEdge = Color.FromArgb(48, 52, 62);
    public static readonly Color Button = Color.FromArgb(44, 48, 58);
    public static readonly Color Hover = Color.FromArgb(58, 63, 76);
    public static readonly Color Text = Color.FromArgb(228, 231, 236);
    public static readonly Color Dim = Color.FromArgb(150, 156, 168);
    public static readonly Color Accent = Color.FromArgb(232, 36, 44);
    public static readonly Color Good = Color.FromArgb(70, 190, 110);
    public static readonly Color Warn = Color.FromArgb(240, 170, 60);
    public static readonly Font Normal = new Font("Segoe UI", 9.5f);
    public static readonly Font Bold = new Font("Segoe UI Semibold", 10.5f);
    public static readonly Font Title = new Font("Segoe UI", 20f, FontStyle.Bold);
    public static readonly Font Mono = new Font("Consolas", 9f);

    public static GraphicsPath Rounded(Rectangle r, int radius)
    {
      GraphicsPath path = new GraphicsPath();
      int d = radius * 2;
      path.AddArc(r.X, r.Y, d, d, 180, 90);
      path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
      path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
      path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
      path.CloseFigure();
      return path;
    }
  }

  internal class FlatButton : Control
  {
    public Color Fill = Theme.Button;
    private bool hover;

    public FlatButton(string text)
    {
      Text = text;
      Font = Theme.Normal;
      ForeColor = Theme.Text;
      Cursor = Cursors.Hand;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
      BackColor = Color.Transparent;
    }

    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      Color fill = !Enabled ? Theme.Button : (hover ? ControlPaint.Light(Fill, 0.25f) : Fill);

      using(GraphicsPath path = Theme.Rounded(new Rectangle(0, 0, Width - 1, Height - 1), 6))
      using(SolidBrush brush = new SolidBrush(fill))
        e.Graphics.FillPath(brush, path);

      TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Enabled ? ForeColor : Theme.Dim,
                            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
    }
  }

  internal class Toggle : Control
  {
    private bool on;
    public event EventHandler Changed;

    public bool Checked
    {
      get { return on; }
      set { on = value; Invalidate(); if(Changed != null) Changed(this, EventArgs.Empty); }
    }

    public Toggle(string text, bool initial)
    {
      Text = text;
      on = initial;
      Font = Theme.Normal;
      ForeColor = Theme.Text;
      Cursor = Cursors.Hand;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.SupportsTransparentBackColor, true);
      BackColor = Color.Transparent;
    }

    protected override void OnClick(EventArgs e)
    {
      if(Enabled)
        Checked = !on;
      base.OnClick(e);
    }

    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      int h = 18, w = 34, y = (Height - h) / 2;
      Color track = !Enabled ? Theme.Button : (on ? Theme.Accent : Theme.Hover);

      using(GraphicsPath path = Theme.Rounded(new Rectangle(0, y, w, h), h / 2))
      using(SolidBrush brush = new SolidBrush(track))
        e.Graphics.FillPath(brush, path);

      using(SolidBrush knob = new SolidBrush(Enabled ? Color.White : Theme.Dim))
        e.Graphics.FillEllipse(knob, on ? w - h + 3 : 3, y + 3, h - 6, h - 6);

      TextRenderer.DrawText(e.Graphics, Text, Font, new Rectangle(w + 10, 0, Width - w - 10, Height),
                            Enabled ? ForeColor : Theme.Dim, TextFormatFlags.VerticalCenter | TextFormatFlags.Left);
    }
  }

  internal class Card : Panel
  {
    public string Title;
    public string Badge;
    public Color BadgeColor = Theme.Dim;

    public Card(string title)
    {
      Title = title;
      BackColor = Theme.Window;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.ResizeRedraw, true);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);

      using(GraphicsPath path = Theme.Rounded(r, 10))
      {
        using(SolidBrush brush = new SolidBrush(Theme.Card))
          e.Graphics.FillPath(brush, path);
        using(Pen pen = new Pen(Theme.CardEdge))
          e.Graphics.DrawPath(pen, path);
      }

      using(SolidBrush bar = new SolidBrush(Theme.Accent))
        e.Graphics.FillRectangle(bar, 16, 18, 3, 16);

      TextRenderer.DrawText(e.Graphics, Title, Theme.Bold, new Point(26, 14), Theme.Text);

      if(Badge != null)
      {
        Size size = TextRenderer.MeasureText(Badge, Theme.Normal);
        TextRenderer.DrawText(e.Graphics, Badge, Theme.Normal, new Point(Width - size.Width - 18, 16), BadgeColor);
      }
    }
  }

  internal class TextLine : Control
  {
    public Color Color = Theme.Dim;

    public TextLine(string text)
    {
      Text = text;
      Font = Theme.Normal;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.SupportsTransparentBackColor | ControlStyles.ResizeRedraw, true);
      BackColor = Color.Transparent;
    }

    protected override void OnTextChanged(EventArgs e) { Invalidate(); base.OnTextChanged(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
      TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Color, TextFormatFlags.WordBreak | TextFormatFlags.Left);
    }
  }

  internal class Bar : Control
  {
    private int value;

    public int Value
    {
      get { return value; }
      set { this.value = Math.Max(0, Math.Min(100, value)); Invalidate(); }
    }

    public Bar()
    {
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.SupportsTransparentBackColor, true);
      BackColor = Color.Transparent;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;

      using(GraphicsPath path = Theme.Rounded(new Rectangle(0, 0, Width - 1, Height - 1), Height / 2 - 1))
      using(SolidBrush brush = new SolidBrush(Theme.Button))
        e.Graphics.FillPath(brush, path);

      int w = (Width - 1) * value / 100;
      if(w > Height)
      {
        using(GraphicsPath path = Theme.Rounded(new Rectangle(0, 0, w, Height - 1), Height / 2 - 1))
        using(SolidBrush brush = new SolidBrush(Theme.Accent))
          e.Graphics.FillPath(brush, path);
      }
    }
  }

  //------------------------------------------------------------------------------------------------

  internal class ScopeDisk
  {
    public int Number;
    public string Model;
    public long Size;
    public long FirstPartitionOffset;   // -1 when there is no partition
    public string DriveLetter;          // null when not mounted

    public bool IsScope
    {
      get
      {
        string model = Model.ToUpperInvariant();
        return model.Contains("ADS1014D") || model.Contains("F1C100S");
      }
    }

    public bool NeedsRepartition
    {
      get { return (FirstPartitionOffset < 1048576); }
    }

    public override string ToString()
    {
      return string.Format("Disk {0}: {1} ({2:0.0} GB){3}", Number, Model.Trim(), Size / 1e9,
                           DriveLetter != null ? "  " + DriveLetter + ":" : "");
    }
  }

  //------------------------------------------------------------------------------------------------
  // X Scope Remote installation

  internal static class AppInstall
  {
    public const string Name = "X Scope Remote";
    public const string ExeName = "X-Scope-Remote.exe";
    private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\XScopeRemote";

    public static string Folder
    {
      get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "X Scope"); }
    }

    public static string StartMenuLink
    {
      get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), Name + ".lnk"); }
    }

    public static string DesktopLink
    {
      get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory), Name + ".lnk"); }
    }

    public static bool Installed
    {
      get { return File.Exists(Path.Combine(Folder, ExeName)); }
    }

    public static void Install(byte[] exe, bool desktop, Action<string> log)
    {
      StopRunning();
      Directory.CreateDirectory(Folder);

      string target = Path.Combine(Folder, ExeName);
      File.WriteAllBytes(target, exe);
      log("Installed " + target);

      // A copy of this setup program serves as the uninstaller
      string uninstaller = Path.Combine(Folder, "X-Scope-Setup.exe");
      string self = Assembly.GetExecutingAssembly().Location;
      if(!string.Equals(Path.GetFullPath(self), Path.GetFullPath(uninstaller), StringComparison.OrdinalIgnoreCase))
        File.Copy(self, uninstaller, true);

      CreateShortcut(StartMenuLink, target);
      log("Start menu entry created");

      if(desktop)
      {
        CreateShortcut(DesktopLink, target);
        log("Desktop shortcut created");
      }
      else if(File.Exists(DesktopLink))
      {
        File.Delete(DesktopLink);
      }

      using(RegistryKey key = Registry.LocalMachine.CreateSubKey(UninstallKey))
      {
        key.SetValue("DisplayName", Name);
        key.SetValue("DisplayVersion", Program.Version);
        key.SetValue("Publisher", "X Scope");
        key.SetValue("DisplayIcon", target);
        key.SetValue("InstallLocation", Folder);
        key.SetValue("UninstallString", "\"" + uninstaller + "\" --uninstall");
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("EstimatedSize", (exe.Length + new FileInfo(uninstaller).Length) / 1024, RegistryValueKind.DWord);
      }
      log("Added to the installed apps list");
    }

    public static void Uninstall()
    {
      StopRunning();

      foreach(string link in new string[] { StartMenuLink, DesktopLink })
        if(File.Exists(link))
          File.Delete(link);

      Registry.LocalMachine.DeleteSubKeyTree(UninstallKey, false);

      string exe = Path.Combine(Folder, ExeName);
      if(File.Exists(exe))
        File.Delete(exe);

      // The running uninstaller cannot delete itself: a short command removes it and the folder after exit
      string folder = Folder;
      ProcessStartInfo info = new ProcessStartInfo("cmd.exe",
        "/c timeout /t 2 /nobreak > nul & del /q \"" + Path.Combine(folder, "X-Scope-Setup.exe") + "\" & rmdir \"" + folder + "\"");
      info.CreateNoWindow = true;
      info.UseShellExecute = false;
      Process.Start(info);
    }

    private static void StopRunning()
    {
      foreach(Process process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(ExeName)))
      {
        try
        {
          process.CloseMainWindow();
          if(!process.WaitForExit(3000))
            process.Kill();
        }
        catch(Exception)
        {
        }
      }
    }

    private static void CreateShortcut(string link, string target)
    {
      Type shellType = Type.GetTypeFromProgID("WScript.Shell");
      object shell = Activator.CreateInstance(shellType);
      object shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
      Type type = shortcut.GetType();
      type.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
      type.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { Path.GetDirectoryName(target) });
      type.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, new object[] { "Live view and control of the FNIRSI 1014D over USB" });
      type.InvokeMember("IconLocation", BindingFlags.SetProperty, null, shortcut, new object[] { target + ",0" });
      type.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
      Marshal.FinalReleaseComObject(shortcut);
      Marshal.FinalReleaseComObject(shell);
    }
  }

  //------------------------------------------------------------------------------------------------

  internal class MainForm : Form
  {
    private const long MaxCardSize = 64L * 1024 * 1024 * 1024;
    private const int ImageOffset = 8192;

    private Card appCard = new Card("X Scope Remote");
    private Toggle desktopToggle = new Toggle("Desktop shortcut", true);
    private Toggle launchToggle = new Toggle("Start X Scope Remote when finished", true);
    private FlatButton appButton = new FlatButton("Install X Scope Remote");

    private Card firmwareCard = new Card("Firmware on the scope");
    private Toggle autoToggle = new Toggle("Install automatically as soon as the scope is connected", true);
    private Toggle backupToggle = new Toggle("Back up the whole card before a first install", true);
    private TextLine scopeLine = new TextLine("");
    private FlatButton firmwareButton = new FlatButton("Install firmware");
    private Bar bar = new Bar();

    private TextBox log = new TextBox();

    private byte[] image;
    private byte[] splash;
    private byte[] remote;

    private ScopeDisk shownDisk;
    private int handledDisk = -1;      // disk number already processed while it stays connected
    private volatile bool busy;
    private System.Windows.Forms.Timer watch = new System.Windows.Forms.Timer();

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    public MainForm()
    {
      Text = "X Scope Setup";
      BackColor = Theme.Window;
      ForeColor = Theme.Text;
      Font = Theme.Normal;
      ClientSize = new Size(720, 700);
      FormBorderStyle = FormBorderStyle.FixedSingle;
      MaximizeBox = false;
      StartPosition = FormStartPosition.CenterScreen;
      DoubleBuffered = true;

      try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
      catch(Exception) { }

      image = ReadResource("x-scope-1014d.bin");
      splash = ReadResource("scope.bmp");
      remote = ReadResource("X-Scope-Remote.exe");

      // Header
      TextLine subtitle = new TextLine("Version " + Program.Version + "  |  FNIRSI 1014D firmware and the X Scope Remote application");
      subtitle.SetBounds(24, 64, 672, 20);
      Controls.Add(subtitle);

      // Application card
      appCard.Badge = AppInstall.Installed ? "installed, will be updated" : "not installed";
      appCard.SetBounds(16, 96, 688, 168);
      Controls.Add(appCard);

      TextLine appText = new TextLine("Live view of both channels, every front panel key, measurements, CSV export and " +
                                      "screenshots over the USB cable. Installed in " + AppInstall.Folder + ".");
      appText.SetBounds(26, 44, 640, 38);
      appCard.Controls.Add(appText);

      desktopToggle.SetBounds(26, 86, 300, 26);
      appCard.Controls.Add(desktopToggle);
      launchToggle.SetBounds(26, 116, 330, 26);
      appCard.Controls.Add(launchToggle);

      appButton.Fill = Theme.Accent;
      appButton.Text = AppInstall.Installed ? "Update X Scope Remote" : "Install X Scope Remote";
      appButton.SetBounds(470, 112, 196, 34);
      appButton.Click += delegate { InstallApp(); };
      appCard.Controls.Add(appButton);

      // Firmware card
      firmwareCard.SetBounds(16, 276, 688, 248);
      Controls.Add(firmwareCard);

      TextLine steps = new TextLine("Connect the scope with a USB cable and open its USB disk mode:\r\n" +
                                    "   original FNIRSI firmware: the USB connection option in its menu\r\n" +
                                    "   X Scope: Utilities, USB mode, Mass storage, then MENU, USB export");
      steps.SetBounds(26, 44, 640, 58);
      firmwareCard.Controls.Add(steps);

      autoToggle.SetBounds(26, 104, 460, 26);
      autoToggle.Changed += delegate { handledDisk = -1; Scan(); };
      firmwareCard.Controls.Add(autoToggle);
      backupToggle.SetBounds(26, 134, 460, 26);
      firmwareCard.Controls.Add(backupToggle);

      scopeLine.SetBounds(26, 168, 430, 40);
      firmwareCard.Controls.Add(scopeLine);

      firmwareButton.Fill = Theme.Accent;
      firmwareButton.SetBounds(470, 172, 196, 34);
      firmwareButton.Click += delegate { if(shownDisk != null) StartFirmware(shownDisk, false); };
      firmwareCard.Controls.Add(firmwareButton);

      bar.SetBounds(26, 218, 640, 8);
      firmwareCard.Controls.Add(bar);

      // Log
      log.Multiline = true;
      log.ReadOnly = true;
      log.BorderStyle = BorderStyle.None;
      log.ScrollBars = ScrollBars.None;
      log.BackColor = Theme.Card;
      log.ForeColor = Theme.Dim;
      log.Font = Theme.Mono;
      log.SetBounds(28, 548, 664, 136);
      Controls.Add(log);

      Log("X Scope " + Program.Version + " firmware, " + image.Length + " bytes, SHA-256 " + Sha256(image).Substring(0, 16) + "...");

      watch.Interval = 1500;
      watch.Tick += delegate { Scan(); };

      Shown += delegate
      {
        Scan();
        watch.Start();
      };
    }

    protected override void OnHandleCreated(EventArgs e)
    {
      base.OnHandleCreated(e);

      int on = 1;
      if(DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0)
        DwmSetWindowAttribute(Handle, 19, ref on, 4);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      base.OnPaint(e);
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;

      TextRenderer.DrawText(e.Graphics, "X", Theme.Title, new Point(20, 18), Theme.Accent);
      TextRenderer.DrawText(e.Graphics, "SCOPE  SETUP", Theme.Title, new Point(44, 18), Theme.Text);

      Rectangle frame = new Rectangle(16, 536, 688, 156);
      using(GraphicsPath path = Theme.Rounded(frame, 10))
      {
        using(SolidBrush brush = new SolidBrush(Theme.Card))
          e.Graphics.FillPath(brush, path);
        using(Pen pen = new Pen(Theme.CardEdge))
          e.Graphics.DrawPath(pen, path);
      }
    }

    private static byte[] ReadResource(string name)
    {
      using(Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
      {
        if(stream == null)
          throw new Exception("Missing embedded file " + name);

        MemoryStream memory = new MemoryStream();
        stream.CopyTo(memory);
        return memory.ToArray();
      }
    }

    private static string Sha256(byte[] data)
    {
      using(System.Security.Cryptography.SHA256 sha = System.Security.Cryptography.SHA256.Create())
      {
        return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", "").ToLowerInvariant();
      }
    }

    private void Log(string text)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<string>(Log), text);
        return;
      }

      log.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + text + "\r\n");
    }

    private void SetProgress(int percent)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<int>(SetProgress), percent);
        return;
      }

      bar.Value = percent;
    }

    private void SetScopeLine(string text, Color color)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<string, Color>(SetScopeLine), text, color);
        return;
      }

      scopeLine.Text = text;
      scopeLine.Color = color;
      scopeLine.Invalidate();
    }

    //------------------------------------------------------------------------------------------------
    // Application

    private void InstallApp()
    {
      appButton.Enabled = false;

      try
      {
        AppInstall.Install(remote, desktopToggle.Checked, Log);
        appCard.Badge = "installed";
        appCard.BadgeColor = Theme.Good;
        appButton.Text = "Installed";
        appCard.Invalidate();

        if(launchToggle.Checked)
        {
          ProcessStartInfo start = new ProcessStartInfo(Path.Combine(AppInstall.Folder, AppInstall.ExeName));
          start.UseShellExecute = true;
          start.WorkingDirectory = AppInstall.Folder;
          Process.Start(start);
          Log("X Scope Remote started");
        }
      }
      catch(Exception e)
      {
        Log("Installing X Scope Remote failed: " + e.Message);
        appCard.Badge = "failed";
        appCard.BadgeColor = Theme.Accent;
        appCard.Invalidate();
      }

      appButton.Enabled = true;
    }

    //------------------------------------------------------------------------------------------------
    // Disk discovery through the Windows storage WMI classes

    private static List<ScopeDisk> ListDisks()
    {
      List<ScopeDisk> disks = new List<ScopeDisk>();
      ManagementScope scope = new ManagementScope(@"\\.\root\Microsoft\Windows\Storage");

      using(ManagementObjectSearcher searcher = new ManagementObjectSearcher(scope, new ObjectQuery("SELECT * FROM MSFT_Disk")))
      {
        foreach(ManagementObject disk in searcher.Get())
        {
          // BusType 7 is USB; never offer boot or system disks or anything large
          ushort bus = Convert.ToUInt16(disk["BusType"]);
          bool boot = (bool)disk["IsBoot"];
          bool system = (bool)disk["IsSystem"];
          long size = Convert.ToInt64(disk["Size"]);

          if((bus != 7) || boot || system || (size == 0) || (size > MaxCardSize))
            continue;

          ScopeDisk item = new ScopeDisk();
          item.Number = Convert.ToInt32(disk["Number"]);
          item.Model = Convert.ToString(disk["FriendlyName"]);
          item.Size = size;
          item.FirstPartitionOffset = -1;

          string query = "SELECT * FROM MSFT_Partition WHERE DiskNumber = " + item.Number;
          using(ManagementObjectSearcher parts = new ManagementObjectSearcher(scope, new ObjectQuery(query)))
          {
            foreach(ManagementObject part in parts.Get())
            {
              long offset = Convert.ToInt64(part["Offset"]);
              if((item.FirstPartitionOffset < 0) || (offset < item.FirstPartitionOffset))
                item.FirstPartitionOffset = offset;

              char letter = Convert.ToChar(part["DriveLetter"]);
              if(letter != '\0')
                item.DriveLetter = letter.ToString();
            }
          }

          disks.Add(item);
        }
      }

      return disks;
    }

    // Called every 1.5 s: shows the connected scope and starts the automatic install once per connection
    private void Scan()
    {
      if(busy)
        return;

      ScopeDisk found = null;

      try
      {
        foreach(ScopeDisk disk in ListDisks())
        {
          if(disk.IsScope)
            found = disk;
        }
      }
      catch(Exception e)
      {
        SetScopeLine("Could not list the disks: " + e.Message, Theme.Accent);
        return;
      }

      shownDisk = found;

      if(found == null)
      {
        handledDisk = -1;
        firmwareCard.Badge = "waiting for the scope";
        firmwareCard.BadgeColor = Theme.Dim;
        firmwareCard.Invalidate();
        firmwareButton.Enabled = false;
        SetScopeLine("No scope in USB disk mode found yet. Setup keeps looking.", Theme.Dim);
        return;
      }

      firmwareButton.Enabled = true;
      firmwareButton.Text = found.NeedsRepartition ? "Install firmware" : "Update firmware";
      firmwareCard.Badge = "scope connected";
      firmwareCard.BadgeColor = Theme.Good;
      firmwareCard.Invalidate();

      if(handledDisk == found.Number)
        return;

      SetScopeLine(found + "\r\n" + (found.NeedsRepartition
        ? "First install: the card is repartitioned, all files on it are erased."
        : "Update: only the firmware area is rewritten, files and settings are kept."),
        found.NeedsRepartition ? Theme.Warn : Theme.Text);

      if(autoToggle.Checked)
      {
        handledDisk = found.Number;
        StartFirmware(found, true);
      }
    }

    //------------------------------------------------------------------------------------------------
    // Firmware installation

    private void StartFirmware(ScopeDisk disk, bool automatic)
    {
      if(busy)
        return;

      if(disk.NeedsRepartition || !automatic)
      {
        string question = disk.NeedsRepartition
          ? "Install X Scope on\r\n\r\n" + disk + "\r\n\r\nThe card is repartitioned and ALL FILES ON IT ARE ERASED." +
            (backupToggle.Checked ? " A full backup is saved to your Documents folder first." : "") + "\r\n\r\nContinue?"
          : "Update X Scope on\r\n\r\n" + disk + "\r\n\r\nContinue?";

        if(MessageBox.Show(this, question, Text, MessageBoxButtons.YesNo,
                           disk.NeedsRepartition ? MessageBoxIcon.Warning : MessageBoxIcon.Question) != DialogResult.Yes)
        {
          Log("Firmware installation cancelled");
          return;
        }
      }

      bool backup = disk.NeedsRepartition && backupToggle.Checked;
      string backupFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                                       "FNIRSI-1014D-card-backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".img");

      SetBusy(true);

      Thread worker = new Thread(delegate()
      {
        try
        {
          bool written = Install(disk, backup, backupFile);
          SetScopeLine(written
            ? "Done. Leave the USB mode on the scope (original firmware: switch it off with the power button), then switch it off and on."
            : "The scope already has this firmware. Nothing was written.", Theme.Good);
          Done(true, written);
        }
        catch(Exception e)
        {
          Log("FAILED: " + e.Message);
          SetScopeLine("The installation failed, see the log below.", Theme.Accent);
          Done(false, false);
        }
      });

      worker.IsBackground = true;
      worker.Start();
    }

    private void SetBusy(bool on)
    {
      busy = on;
      firmwareButton.Enabled = !on;
      autoToggle.Enabled = backupToggle.Enabled = !on;
      UseWaitCursor = on;

      if(on)
      {
        firmwareCard.Badge = "installing";
        firmwareCard.BadgeColor = Theme.Warn;
        firmwareCard.Invalidate();
      }
    }

    private void Done(bool ok, bool written)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<bool, bool>(Done), ok, written);
        return;
      }

      SetBusy(false);
      firmwareCard.Badge = ok ? (written ? "installed and verified" : "up to date") : "failed";
      firmwareCard.BadgeColor = ok ? Theme.Good : Theme.Accent;
      firmwareCard.Invalidate();
    }

    // Returns false when the card already holds this image
    private bool Install(ScopeDisk disk, bool backup, string backupFile)
    {
      // Re-read the disk right before writing: it must still be the same card
      ScopeDisk current = null;

      foreach(ScopeDisk item in ListDisks())
      {
        if((item.Number == disk.Number) && (item.Size == disk.Size) && item.IsScope)
          current = item;
      }

      if(current == null)
        throw new Exception("The scope is no longer connected.");

      Log("Scope found: " + current);

      if(!current.NeedsRepartition && AlreadyInstalled(current))
      {
        Log("The card already holds this firmware");
        SetProgress(100);
        return false;
      }

      if(backup)
        Backup(current, backupFile);

      if(current.NeedsRepartition)
      {
        Log("Repartitioning: one FAT32 partition at 1 MiB");
        RunPowerShell(
          "$ErrorActionPreference = 'Stop'; $n = " + current.Number + ";" +
          "if((Get-Disk -Number $n).NumberOfPartitions -gt 0) { Clear-Disk -Number $n -RemoveData -RemoveOEM -Confirm:$false };" +
          "if((Get-Disk -Number $n).PartitionStyle -eq 'RAW') { Initialize-Disk -Number $n -PartitionStyle MBR };" +
          "if((Get-Disk -Number $n).PartitionStyle -ne 'MBR') { throw 'disk is not MBR' };" +
          "$p = New-Partition -DiskNumber $n -Offset 1MB -UseMaximumSize -MbrType FAT32 -AssignDriveLetter;" +
          "Format-Volume -Partition $p -FileSystem FAT32 -NewFileSystemLabel 'SCOPE' -Confirm:$false | Out-Null");

        current = null;
        foreach(ScopeDisk item in ListDisks())
        {
          if(item.Number == disk.Number)
            current = item;
        }

        if((current == null) || (current.FirstPartitionOffset != 1048576))
          throw new Exception("The new partition does not start at 1 MiB.");
      }

      WriteImage(current);

      if(current.DriveLetter == null)
      {
        Log("The card has no drive letter, start screen not copied (the firmware then shows a text start screen).");
      }
      else
      {
        string target = current.DriveLetter + ":\\scope.bmp";
        File.WriteAllBytes(target, splash);
        Log("Start screen copied to " + target);
      }

      return true;
    }

    private byte[] PaddedImage()
    {
      // Raw device access must be whole sectors
      int length = (image.Length + 511) / 512 * 512;
      byte[] data = new byte[length];
      Array.Copy(image, data, image.Length);
      return data;
    }

    private byte[] ReadAt(ScopeDisk disk, long offset, int length)
    {
      byte[] data = new byte[length];

      using(FileStream stream = new FileStream(@"\\.\PhysicalDrive" + disk.Number, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
      {
        stream.Seek(offset, SeekOrigin.Begin);

        int read = 0;
        while(read < length)
        {
          int got = stream.Read(data, read, length - read);
          if(got <= 0)
            break;
          read += got;
        }
      }

      return data;
    }

    private bool AlreadyInstalled(ScopeDisk disk)
    {
      byte[] data = PaddedImage();
      byte[] card = ReadAt(disk, ImageOffset, data.Length);

      for(int i = 0; i < data.Length; i++)
      {
        if(card[i] != data[i])
          return false;
      }

      return true;
    }

    private void Backup(ScopeDisk disk, string file)
    {
      Log("Backing up " + disk.Size + " bytes to " + file);

      byte[] buffer = new byte[1024 * 1024];
      long done = 0;

      using(FileStream source = new FileStream(@"\\.\PhysicalDrive" + disk.Number, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
      using(FileStream target = new FileStream(file, FileMode.CreateNew, FileAccess.Write))
      {
        while(done < disk.Size)
        {
          int want = (int)Math.Min(buffer.Length, disk.Size - done);
          int got = source.Read(buffer, 0, want);

          if(got <= 0)
            throw new Exception("Read error during the backup at byte " + done);

          target.Write(buffer, 0, got);
          done += got;
          SetProgress((int)(done * 50 / disk.Size));
        }
      }

      Log("Backup complete");
    }

    private void WriteImage(ScopeDisk disk)
    {
      byte[] data = PaddedImage();

      if((ImageOffset + data.Length) > disk.FirstPartitionOffset)
        throw new Exception("The image would overlap the partition.");

      Log("Writing " + data.Length + " bytes at 8 KiB");

      using(FileStream stream = new FileStream(@"\\.\PhysicalDrive" + disk.Number, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite))
      {
        stream.Seek(ImageOffset, SeekOrigin.Begin);
        stream.Write(data, 0, data.Length);
        stream.Flush();
      }

      SetProgress(80);

      byte[] check = ReadAt(disk, ImageOffset, data.Length);
      for(int i = 0; i < data.Length; i++)
      {
        if(check[i] != data[i])
          throw new Exception("Verification failed at byte " + i);
      }

      SetProgress(100);
      Log("Firmware written and verified");
    }

    private void RunPowerShell(string command)
    {
      ProcessStartInfo info = new ProcessStartInfo("powershell.exe",
        "-NoProfile -ExecutionPolicy Bypass -Command \"" + command.Replace("\"", "\\\"") + "\"");
      info.UseShellExecute = false;
      info.CreateNoWindow = true;
      info.RedirectStandardError = true;
      info.RedirectStandardOutput = true;

      using(Process process = Process.Start(info))
      {
        string output = process.StandardOutput.ReadToEnd();
        string error = process.StandardError.ReadToEnd();
        process.WaitForExit();

        if(process.ExitCode != 0)
          throw new Exception("Partitioning failed: " + error.Trim() + output.Trim());
      }
    }
  }

  internal static class Program
  {
    public static string Version
    {
      get
      {
        using(Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("version.txt"))
        {
          if(stream == null)
            return "dev";
          return new StreamReader(stream).ReadToEnd().Trim();
        }
      }
    }

    [STAThread]
    private static void Main(string[] args)
    {
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);

      if((args.Length == 1) && (args[0] == "--uninstall"))
      {
        if(MessageBox.Show("Remove X Scope Remote from this PC?\r\n\r\nThe firmware on the scope is not changed.",
                           "X Scope", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
        {
          try
          {
            AppInstall.Uninstall();
          }
          catch(Exception e)
          {
            MessageBox.Show("Removing X Scope Remote failed: " + e.Message, "X Scope", MessageBoxButtons.OK, MessageBoxIcon.Error);
          }
        }
        return;
      }

      // --preview <file.png>: save the window as an image (documentation; build without the manifest)
      if((args.Length == 2) && (args[0] == "--preview"))
      {
        MainForm form = new MainForm();
        form.Opacity = 0.01;
        form.Shown += delegate
        {
          System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
          timer.Interval = 2500;
          timer.Tick += delegate
          {
            timer.Stop();
            using(Bitmap bitmap = new Bitmap(form.Width, form.Height))
            {
              form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
              bitmap.Save(args[1], System.Drawing.Imaging.ImageFormat.Png);
            }
            form.Close();
          };
          timer.Start();
        };
        Application.Run(form);
        return;
      }

      try
      {
        Application.Run(new MainForm());
      }
      catch(Exception e)
      {
        MessageBox.Show(e.Message, "X Scope Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
      }
    }
  }
}
