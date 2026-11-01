// X Scope installer for the FNIRSI 1014D (Windows, .NET Framework 4.x, C# 5)
//
// Installs or updates the X Scope firmware on the scope's internal SD card while the scope is
// connected by USB in its disk mode. The firmware image and the start screen are embedded.
//
// Card layout written by the installer:
//   sector 0            MBR with one FAT32 partition starting at sector 2048 (1 MiB)
//   sector 16           SD card image (loader, firmware at +0x8000), written at 8 KiB
//   sectors 2046/2047   X Scope calibration and settings (left alone, created by the firmware)
//
// A stock card has its FAT32 partition at sector 63, which overlaps the firmware area, so a first
// install repartitions the card (all files on it are erased; a full backup is offered first).
// An update only rewrites the image area and keeps the partition and its files.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Management;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace XScopeInstaller
{
  internal class ScopeDisk
  {
    public int Number;
    public string Model;
    public long Size;
    public long FirstPartitionOffset;   // -1 when there is no partition
    public string DriveLetter;          // null when not mounted

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

  internal class MainForm : Form
  {
    private const long MaxCardSize = 64L * 1024 * 1024 * 1024;
    private const int ImageOffset = 8192;

    private ComboBox diskList = new ComboBox();
    private Label layoutLabel = new Label();
    private CheckBox backupBox = new CheckBox();
    private TextBox backupPath = new TextBox();
    private Button backupBrowse = new Button();
    private CheckBox splashBox = new CheckBox();
    private Button refreshButton = new Button();
    private Button installButton = new Button();
    private ProgressBar progress = new ProgressBar();
    private TextBox log = new TextBox();

    private byte[] image;
    private byte[] splash;

    public MainForm()
    {
      Text = "X Scope installer - FNIRSI 1014D";
      ClientSize = new Size(640, 520);
      FormBorderStyle = FormBorderStyle.FixedDialog;
      MaximizeBox = false;
      Font = new Font("Segoe UI", 9f);

      Label intro = new Label();
      intro.Text = "1. Connect the scope to this PC with a USB cable.\r\n" +
                   "2. Open the USB disk mode on the scope:\r\n" +
                   "     original FNIRSI firmware: the USB connection option in its menu\r\n" +
                   "     X Scope: MENU, USB export\r\n" +
                   "3. Select the scope below and press Install.";
      intro.SetBounds(16, 12, 608, 84);
      Controls.Add(intro);

      diskList.DropDownStyle = ComboBoxStyle.DropDownList;
      diskList.SetBounds(16, 104, 500, 24);
      diskList.SelectedIndexChanged += delegate { UpdateLayout(); };
      Controls.Add(diskList);

      refreshButton.Text = "Refresh";
      refreshButton.SetBounds(528, 103, 96, 26);
      refreshButton.Click += delegate { FindDisks(); };
      Controls.Add(refreshButton);

      layoutLabel.SetBounds(16, 136, 608, 54);
      Controls.Add(layoutLabel);

      backupBox.Text = "Back up the whole card to a file first";
      backupBox.SetBounds(16, 194, 300, 22);
      backupBox.CheckedChanged += delegate { backupPath.Enabled = backupBrowse.Enabled = backupBox.Checked; };
      Controls.Add(backupBox);

      backupPath.SetBounds(16, 218, 500, 24);
      backupPath.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                                     "FNIRSI-1014D-card-backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".img");
      Controls.Add(backupPath);

      backupBrowse.Text = "Browse...";
      backupBrowse.SetBounds(528, 217, 96, 26);
      backupBrowse.Click += delegate
      {
        SaveFileDialog dialog = new SaveFileDialog();
        dialog.FileName = Path.GetFileName(backupPath.Text);
        dialog.Filter = "Card image (*.img)|*.img";
        if(dialog.ShowDialog(this) == DialogResult.OK)
          backupPath.Text = dialog.FileName;
      };
      Controls.Add(backupBrowse);

      splashBox.Text = "Copy the X Scope start screen (scope.bmp) to the card";
      splashBox.Checked = true;
      splashBox.SetBounds(16, 250, 400, 22);
      Controls.Add(splashBox);

      installButton.Text = "Install";
      installButton.SetBounds(16, 280, 120, 32);
      installButton.Click += delegate { StartInstall(); };
      Controls.Add(installButton);

      progress.SetBounds(148, 286, 476, 20);
      Controls.Add(progress);

      log.Multiline = true;
      log.ReadOnly = true;
      log.ScrollBars = ScrollBars.Vertical;
      log.Font = new Font("Consolas", 9f);
      log.SetBounds(16, 322, 608, 184);
      Controls.Add(log);

      image = ReadResource("x-scope-1014d.bin");
      splash = ReadResource("scope.bmp");
      Log("Firmware image " + image.Length + " bytes, SHA-256 " + Sha256(image));

      Shown += delegate { FindDisks(); };
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

      progress.Value = Math.Max(0, Math.Min(100, percent));
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

    private void FindDisks()
    {
      diskList.Items.Clear();

      try
      {
        foreach(ScopeDisk disk in ListDisks())
        {
          diskList.Items.Add(disk);

          string model = disk.Model.ToUpperInvariant();
          if(model.Contains("ADS1014D") || model.Contains("F1C100S"))
            diskList.SelectedItem = disk;
        }
      }
      catch(Exception e)
      {
        Log("Could not list the disks: " + e.Message);
      }

      if((diskList.SelectedIndex < 0) && (diskList.Items.Count > 0))
        diskList.SelectedIndex = 0;

      if(diskList.Items.Count == 0)
        Log("No USB disk found. Is the scope in its USB disk mode? Press Refresh to look again.");

      UpdateLayout();
    }

    private void UpdateLayout()
    {
      ScopeDisk disk = diskList.SelectedItem as ScopeDisk;

      if(disk == null)
      {
        layoutLabel.Text = "";
        installButton.Enabled = false;
        return;
      }

      installButton.Enabled = true;

      if(disk.NeedsRepartition)
      {
        layoutLabel.ForeColor = Color.DarkRed;
        layoutLabel.Text = "First install: the card's partition starts before 1 MiB (original layout). The card " +
                           "will be repartitioned and ALL FILES ON IT ARE ERASED. A backup is recommended.";
        backupBox.Checked = true;
        installButton.Text = "Install";
      }
      else
      {
        layoutLabel.ForeColor = Color.DarkGreen;
        layoutLabel.Text = "Update: the card already has the X Scope layout. Only the firmware area is rewritten, " +
                           "your files and settings are kept.";
        backupBox.Checked = false;
        installButton.Text = "Update";
      }

      backupPath.Enabled = backupBrowse.Enabled = backupBox.Checked;
    }

    //------------------------------------------------------------------------------------------------
    // Installation

    private void StartInstall()
    {
      ScopeDisk disk = diskList.SelectedItem as ScopeDisk;

      if(disk == null)
        return;

      string question = disk.NeedsRepartition
        ? "Install X Scope on\r\n\r\n" + disk + "\r\n\r\nALL FILES ON THIS DISK WILL BE ERASED. Continue?"
        : "Update X Scope on\r\n\r\n" + disk + "\r\n\r\nContinue?";

      if(MessageBox.Show(this, question, Text, MessageBoxButtons.YesNo, disk.NeedsRepartition ? MessageBoxIcon.Warning : MessageBoxIcon.Question) != DialogResult.Yes)
        return;

      bool backup = backupBox.Checked;
      string backupFile = backupPath.Text;
      bool copySplash = splashBox.Checked;

      SetBusy(true);

      Thread worker = new Thread(delegate()
      {
        try
        {
          Install(disk, backup, backupFile, copySplash);
          Log("DONE. Leave the USB mode on the scope (original firmware: switch the scope off with the power button), " +
              "then switch it off and on again.");
          Done(true);
        }
        catch(Exception e)
        {
          Log("FAILED: " + e.Message);
          Done(false);
        }
      });

      worker.IsBackground = true;
      worker.Start();
    }

    private void SetBusy(bool busy)
    {
      installButton.Enabled = refreshButton.Enabled = diskList.Enabled = !busy;
      backupBox.Enabled = splashBox.Enabled = !busy;
      UseWaitCursor = busy;
    }

    private void Done(bool ok)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<bool>(Done), ok);
        return;
      }

      SetBusy(false);
      MessageBox.Show(this, ok ? "X Scope was written and verified." : "The installation failed, see the log.",
                      Text, MessageBoxButtons.OK, ok ? MessageBoxIcon.Information : MessageBoxIcon.Error);
      FindDisks();
    }

    private void Install(ScopeDisk disk, bool backup, string backupFile, bool copySplash)
    {
      // Re-read the disk right before writing: it must still be the same card
      ScopeDisk current = null;

      foreach(ScopeDisk item in ListDisks())
      {
        if((item.Number == disk.Number) && (item.Size == disk.Size))
          current = item;
      }

      if(current == null)
        throw new Exception("The selected disk is no longer there.");

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

      if(copySplash)
      {
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
      }
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
      // Raw device writes must be whole sectors
      int length = (image.Length + 511) / 512 * 512;
      byte[] data = new byte[length];
      Array.Copy(image, data, image.Length);

      if((ImageOffset + length) > disk.FirstPartitionOffset)
        throw new Exception("The image would overlap the partition.");

      Log("Writing " + length + " bytes at 8 KiB");

      string path = @"\\.\PhysicalDrive" + disk.Number;

      using(FileStream stream = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite))
      {
        stream.Seek(ImageOffset, SeekOrigin.Begin);
        stream.Write(data, 0, data.Length);
        stream.Flush();
      }

      SetProgress(80);

      byte[] check = new byte[length];
      using(FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
      {
        stream.Seek(ImageOffset, SeekOrigin.Begin);

        int read = 0;
        while(read < length)
        {
          int got = stream.Read(check, read, length - read);
          if(got <= 0)
            break;
          read += got;
        }
      }

      for(int i = 0; i < length; i++)
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
    [STAThread]
    private static void Main()
    {
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);

      try
      {
        Application.Run(new MainForm());
      }
      catch(Exception e)
      {
        MessageBox.Show(e.Message, "X Scope installer", MessageBoxButtons.OK, MessageBoxIcon.Error);
      }
    }
  }
}
