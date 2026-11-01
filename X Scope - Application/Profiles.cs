// X Scope Remote: measurement profiles (Windows, .NET Framework 4.x, C# 5)
//
// A profile is a JSON file with the scope settings as remote commands (the same lines the scope
// lists for SYSTem:SETup?), the measurements to show, a note on how to connect the probes and an
// optional warning that must be confirmed before the profile is applied. Applying sends every
// command, reads every setting back and shows the differences and the failures.
//
// User profiles live in %APPDATA%\X Scope\profiles; the built-in set is part of the program.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace XScopeRemote
{
  //------------------------------------------------------------------------------------------------
  // One profile

  internal class Profile
  {
    public string Name = "";
    public string Description = "";
    public string Connect = "";
    public string Warning = "";
    public int UartBaud;                                  // UART profiles: decoder baud rate and time base
    public List<string> Settings = new List<string>();
    public List<string> Measurements = new List<string>(); // "CHAN1:VRMS", "PHASE", ...
    public bool BuiltIn;
    public string File;

    public Profile Copy()
    {
      Profile p = (Profile)MemberwiseClone();
      p.Settings = new List<string>(Settings);
      p.Measurements = new List<string>(Measurements);
      p.BuiltIn = false;
      p.File = null;
      return p;
    }

    //----------------------------------------------------------------------------------------------
    // JSON

    public string ToJson()
    {
      StringBuilder s = new StringBuilder();
      s.Append("{\n");
      s.Append("  \"format\": \"x-scope-profile-1\",\n");
      s.Append("  \"name\": ").Append(Quote(Name)).Append(",\n");
      s.Append("  \"description\": ").Append(Quote(Description)).Append(",\n");
      s.Append("  \"connect\": ").Append(Quote(Connect)).Append(",\n");
      s.Append("  \"warning\": ").Append(Quote(Warning)).Append(",\n");
      s.Append("  \"uart_baud\": ").Append(UartBaud.ToString(CultureInfo.InvariantCulture)).Append(",\n");
      s.Append("  \"measurements\": ").Append(QuoteList(Measurements, "    ")).Append(",\n");
      s.Append("  \"settings\": ").Append(QuoteList(Settings, "    ")).Append("\n");
      s.Append("}\n");
      return s.ToString();
    }

    private static string QuoteList(List<string> items, string indent)
    {
      if(items.Count == 0)
        return "[]";

      StringBuilder s = new StringBuilder("[\n");
      for(int i = 0; i < items.Count; i++)
        s.Append(indent).Append(Quote(items[i])).Append((i + 1 < items.Count) ? ",\n" : "\n");
      s.Append(indent.Substring(2)).Append("]");
      return s.ToString();
    }

    private static string Quote(string text)
    {
      StringBuilder s = new StringBuilder("\"");
      foreach(char c in text ?? "")
      {
        switch(c)
        {
          case '"':  s.Append("\\\""); break;
          case '\\': s.Append("\\\\"); break;
          case '\n': s.Append("\\n"); break;
          case '\r': break;
          case '\t': s.Append("\\t"); break;
          default:
            if(c < ' ')
              s.Append("\\u").Append(((int)c).ToString("x4"));
            else
              s.Append(c);
            break;
        }
      }
      return s.Append("\"").ToString();
    }

    public static Profile FromJson(string json)
    {
      Dictionary<string, object> d = new JavaScriptSerializer().DeserializeObject(json) as Dictionary<string, object>;
      if(d == null)
        throw new FormatException("not a profile (expected a JSON object)");

      Profile p = new Profile();
      p.Name = Text(d, "name").Trim();
      p.Description = Text(d, "description");
      p.Connect = Text(d, "connect");
      p.Warning = Text(d, "warning");

      object baud;
      if(d.TryGetValue("uart_baud", out baud) && (baud != null))
        p.UartBaud = Convert.ToInt32(baud, CultureInfo.InvariantCulture);

      p.Settings = List(d, "settings");
      p.Measurements = List(d, "measurements");

      if(p.Name.Length == 0)
        throw new FormatException("the profile has no name");

      foreach(string line in p.Settings)
        if((line.Length > 120) || (line.IndexOf('\n') >= 0))
          throw new FormatException("setting too long: " + line);

      return p;
    }

    private static string Text(Dictionary<string, object> d, string key)
    {
      object value;
      return (d.TryGetValue(key, out value) && (value != null)) ? value.ToString() : "";
    }

    private static List<string> List(Dictionary<string, object> d, string key)
    {
      List<string> list = new List<string>();
      object value;

      if(d.TryGetValue(key, out value) && (value is object[]))
        foreach(object item in (object[])value)
          if((item != null) && (item.ToString().Trim().Length > 0))
            list.Add(item.ToString().Trim());

      return list;
    }

    //----------------------------------------------------------------------------------------------
    // The commands sent to apply this profile: the settings, the UART baud rate with a time base that
    // gives about ten samples per bit, and the measurements in the F1 to F6 slots and the extra panel

    private static readonly string[] SlotNames = { "VMAX", "VMIN", "VAVG", "VRMS", "VPP", "VP", "FREQ", "PERIOD", "PWIDTH", "NWIDTH", "PDUTY", "NDUTY" };

    public List<string> Commands()
    {
      List<string> commands = new List<string>(Settings);

      if(UartBaud > 0)
      {
        commands.Add(":DEC:UART:BAUD " + UartBaud.ToString(CultureInfo.InvariantCulture));
        commands.Add(":TIM:SCAL " + TimebaseForBaud(UartBaud).ToString("0.###E+0", CultureInfo.InvariantCulture));
      }

      int slot = 1;
      string extra = null;

      foreach(string m in Measurements)
      {
        string channel, name;
        SplitMeasurement(m, out channel, out name);

        if((Array.IndexOf(SlotNames, name) >= 0) && (slot <= 6))
          commands.Add(":MEAS:SLOT" + (slot++) + " " + channel + "," + name);
        else if((Array.IndexOf(SlotNames, name) < 0) && (extra == null))
          extra = ((name == "PHASE") || (name == "DELAY")) ? "CHAN1" : channel;
      }

      if(Measurements.Count > 0)
        commands.Add(":MEAS:EXTR " + (extra ?? "OFF"));

      return commands;
    }

    public static void SplitMeasurement(string text, out string channel, out string name)
    {
      string t = text.Trim().ToUpperInvariant();
      int colon = t.IndexOf(':');

      channel = (colon > 0) ? t.Substring(0, colon) : "CHAN1";
      name = (colon > 0) ? t.Substring(colon + 1) : t;

      if((channel == "CH1") || (channel == "1")) channel = "CHAN1";
      if((channel == "CH2") || (channel == "2")) channel = "CHAN2";
    }

    // The sweep time bases record 100 samples per division; aim for at least ten samples per bit
    public static double TimebaseForBaud(int baud)
    {
      double wanted = 10.0 / baud;
      double[] steps = { 5e-6, 1e-5, 2e-5, 5e-5, 1e-4, 2e-4, 5e-4, 1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 1e-1, 2e-1 };
      double best = steps[0];

      foreach(double step in steps)
        if(step <= wanted * 1.0001)
          best = step;

      return best;
    }
  }

  //------------------------------------------------------------------------------------------------
  // The profile files and the built-in set

  internal static class ProfileStore
  {
    public static string Folder
    {
      get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "X Scope", "profiles"); }
    }

    public static List<Profile> Load(List<string> problems)
    {
      List<Profile> profiles = BuiltIn();

      try
      {
        if(Directory.Exists(Folder))
        {
          string[] files = Directory.GetFiles(Folder, "*.json");
          Array.Sort(files, StringComparer.OrdinalIgnoreCase);

          foreach(string file in files)
          {
            try
            {
              Profile p = Profile.FromJson(System.IO.File.ReadAllText(file, Encoding.UTF8));
              p.File = file;
              profiles.Add(p);
            }
            catch(Exception e)
            {
              problems.Add(Path.GetFileName(file) + ": " + e.Message);
            }
          }
        }
      }
      catch(Exception e)
      {
        problems.Add(e.Message);
      }

      return profiles;
    }

    public static void Save(Profile p)
    {
      Directory.CreateDirectory(Folder);

      if(p.File == null)
        p.File = UniqueFile(p.Name);

      System.IO.File.WriteAllText(p.File, p.ToJson(), new UTF8Encoding(false));
    }

    public static void Delete(Profile p)
    {
      if((p.File != null) && System.IO.File.Exists(p.File))
        System.IO.File.Delete(p.File);
    }

    public static string UniqueFile(string name)
    {
      StringBuilder s = new StringBuilder();
      foreach(char c in name)
        s.Append((char.IsLetterOrDigit(c) || (c == '-') || (c == '_')) ? char.ToLowerInvariant(c) : '-');

      string stem = s.ToString().Trim('-');
      if(stem.Length == 0)
        stem = "profile";
      if(stem.Length > 40)
        stem = stem.Substring(0, 40);

      string file = Path.Combine(Folder, stem + ".json");
      for(int i = 2; System.IO.File.Exists(file); i++)
        file = Path.Combine(Folder, stem + "-" + i + ".json");

      return file;
    }

    private static Profile Make(string name, string description, string connect, string[] measurements, params string[] settings)
    {
      Profile p = new Profile();
      p.Name = name;
      p.Description = description;
      p.Connect = connect;
      p.Measurements.AddRange(measurements);
      p.Settings.AddRange(settings);
      p.BuiltIn = true;
      return p;
    }

    public static List<Profile> BuiltIn()
    {
      List<Profile> list = new List<Profile>();
      Profile p;

      list.Add(Make("Audio / music",
        "Line level audio: the waveform on CH1 and its spectrum on CH2 (FFT in dB, 0 to 25 kHz at 2 ms/div, linear frequency axis). AC coupled.",
        "Feed the same signal to CH1 and CH2 (two probes on the same point, or a Y cable). Probe switch on x1. Ground clip on the audio ground.",
        new string[] { "CHAN1:VRMS", "CHAN1:FREQ", "CHAN1:VPP" },
        ":CHAN1:DISP ON", ":CHAN1:COUP AC", ":CHAN1:PROB 1", ":CHAN1:SCAL 0.5", ":CHAN1:POS 1.5", ":CHAN1:FFT OFF",
        ":CHAN2:DISP ON", ":CHAN2:COUP AC", ":CHAN2:PROB 1", ":CHAN2:SCAL 0.5", ":CHAN2:POS 0", ":CHAN2:FFT ON",
        ":TIM:SCAL 2m", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 0",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      p = Make("Mains 50 Hz",
        "Mains voltage through an isolated or differential probe: 5 ms/div shows 2.5 periods; Vrms and frequency. The x10 setting at 50 V/div covers 400 V peak to peak; with a 1:100 differential probe change the probe setting to 100.",
        "Only with a differential probe or behind an isolation transformer. Set the probe ratio here to the ratio of the differential probe.",
        new string[] { "CHAN1:VRMS", "CHAN1:FREQ", "CHAN1:VPP" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 50", ":CHAN1:POS 0", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 5m", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 0",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF");
      p.Warning = "Never measure mains against the scope ground while the scope is connected to the PC by USB. " +
                  "The probe ground clip is connected to the USB ground and through it to the PC: touching mains with it causes a short circuit, " +
                  "destroys the scope and the PC and can kill. Use only an isolated or differential probe.";
      list.Add(p);

      list.Add(Make("PWM / motor",
        "PWM drive signals up to about 20 V: duty cycle, frequency and rise time. 50 us/div suits PWM of 1 to 50 kHz.",
        "Probe on the PWM signal (gate drive or motor terminal against ground), probe switch on x10, ground clip on the driver ground.",
        new string[] { "CHAN1:PDUTY", "CHAN1:FREQ", "CHAN1:RISE", "CHAN1:VMAX" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 5", ":CHAN1:POS -3", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 50u", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 2",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      p = Make("UART (9600 baud)",
        "3.3 V or 5 V UART, 8N1, one capture on the falling edge of the start bit, bytes decoded on the scope. The time base follows the baud rate (about ten samples per bit); edit the baud rate in a copy of this profile for other rates.",
        "Probe on the TX line (CH1), probe switch on x10, ground clip on the circuit ground. Press SINGLE (or RUN) before the data is sent.",
        new string[] { "CHAN1:VMAX", "CHAN1:VMIN" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 2", ":CHAN1:POS -2", ":CHAN1:INV OFF", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:POS 0", ":TRIG:SOUR CHAN1", ":TRIG:SLOP FALL", ":TRIG:LEV 1.5", ":TRIG:MODE SING",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF",
        ":DEC:MODE UART", ":DEC:UART:FORM 8N1", ":DEC:UART:IDLE HIGH", ":DEC:UART:SOUR CHAN1", ":DEC:LIST ON");
      p.UartBaud = 9600;
      list.Add(p);

      list.Add(Make("I2C (100 kHz)",
        "I2C bus at 100 kHz: start, address with read or write, data and acknowledge decoded on the scope. One capture on the start condition. For 400 kHz use 20 us/div.",
        "SDA on CH1 and SCL on CH2 (this order is fixed), both probe switches on x10, ground clips on the circuit ground.",
        new string[] { "CHAN2:FREQ" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 2", ":CHAN1:POS 0.5", ":CHAN1:INV OFF", ":CHAN1:FFT OFF",
        ":CHAN2:DISP ON", ":CHAN2:COUP DC", ":CHAN2:PROB 10", ":CHAN2:SCAL 2", ":CHAN2:POS -3", ":CHAN2:INV OFF", ":CHAN2:FFT OFF",
        ":TIM:SCAL 50u", ":TIM:POS 0", ":TRIG:SOUR CHAN1", ":TRIG:SLOP FALL", ":TRIG:LEV 1.5", ":TRIG:MODE SING",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE I2C", ":DEC:LIST ON"));

      list.Add(Make("PSU ripple",
        "Ripple on a DC supply: AC coupling and the most sensitive range (50 mV/div with x1). 2 ms/div shows mains ripple; use a faster time base for switching ripple. This firmware has no persistence mode; use statistics on Vpp for the spread.",
        "Probe switch on x1, ground clip close to the measuring point (short ground lead).",
        new string[] { "CHAN1:VPP", "CHAN1:FREQ", "CHAN1:VRMSAC" },
        ":CHAN1:DISP ON", ":CHAN1:COUP AC", ":CHAN1:PROB 1", ":CHAN1:SCAL 50m", ":CHAN1:POS 0", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 2m", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 0",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF", ":MEAS:STAT:COUN 100", ":MEAS:STAT ON"));

      list.Add(Make("Switching PSU / buck",
        "Switch node of a buck converter up to about 30 V: frequency, duty cycle, rise time and overshoot at 1 us/div (100 kHz to 1 MHz).",
        "Probe switch on x10, ground clip (or better a short ground spring) on the power ground next to the switch node.",
        new string[] { "CHAN1:FREQ", "CHAN1:PDUTY", "CHAN1:RISE", "CHAN1:OVERSHOOT" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 5", ":CHAN1:POS -3", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 1u", ":TIM:POS 0", ":TRIG:MODE NORM", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 5",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      list.Add(Make("Battery / slow drift",
        "Slow changes such as a battery under load: roll mode at 5 s/div (70 s across the screen). Save a waveform on the scope (timestamped roll file) or use Export CSV here for a trend. The measurement values of this program need a sweep time base and show - in roll mode; read them on the scope.",
        "Probe switch on x1 (x10 above 30 V), ground clip on the battery minus.",
        new string[] { "CHAN1:VAVG" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 1", ":CHAN1:SCAL 1", ":CHAN1:POS -3", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 5", ":TRIG:MODE AUTO", ":TRIG:SOUR CHAN1",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      list.Add(Make("Automotive 12 V",
        "12 V vehicle supply: battery level, alternator ripple and dips while cranking, up to about 35 V on screen. DC coupled.",
        "Probe switch on x10, ground clip on the battery minus or the chassis. Keep the scope on its own power when the PC is connected to the car.",
        new string[] { "CHAN1:VAVG", "CHAN1:VMAX", "CHAN1:VMIN", "CHAN1:VPP" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 10", ":CHAN1:SCAL 5", ":CHAN1:POS -3", ":CHAN1:FFT OFF",
        ":CHAN2:DISP OFF", ":TIM:SCAL 10m", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 12",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      list.Add(Make("Phase between 2 signals",
        "Phase and delay from CH1 to CH2 (positive: CH2 lags) with both waveforms on screen, plus the frequency. 200 us/div suits about 1 to 10 kHz.",
        "Reference signal on CH1, the other on CH2, the same probe ratio on both, ground clips on the same ground.",
        new string[] { "PHASE", "DELAY", "CHAN1:FREQ" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 1", ":CHAN1:SCAL 1", ":CHAN1:POS 1.5", ":CHAN1:FFT OFF",
        ":CHAN2:DISP ON", ":CHAN2:COUP DC", ":CHAN2:PROB 1", ":CHAN2:SCAL 1", ":CHAN2:POS -1.5", ":CHAN2:FFT OFF",
        ":TIM:SCAL 200u", ":TIM:POS 0", ":TRIG:MODE AUTO", ":TRIG:SLOP RIS", ":TRIG:SOUR CHAN1", ":TRIG:LEV 0",
        ":DISP:XY OFF", ":ACQ:AVER OFF", ":MATH:MODE OFF", ":DEC:MODE OFF"));

      list.Add(Make("Component test / Lissajous",
        "X-Y display: CH1 horizontal, CH2 vertical. With a small AC source a resistor shows a line, a capacitor or coil an ellipse, a diode a bent line; two sines show their frequency ratio and phase.",
        "Component tester: AC source (a few volts, for example a transformer) through a series resistor of about 1 kOhm to the component. CH1 across the component, CH2 across the series resistor (current), ground clips on the joint between the two.",
        new string[] { "PHASE", "CHAN1:FREQ" },
        ":CHAN1:DISP ON", ":CHAN1:COUP DC", ":CHAN1:PROB 1", ":CHAN1:SCAL 1", ":CHAN1:POS 0", ":CHAN1:FFT OFF",
        ":CHAN2:DISP ON", ":CHAN2:COUP DC", ":CHAN2:PROB 1", ":CHAN2:SCAL 1", ":CHAN2:POS 0", ":CHAN2:FFT OFF",
        ":TIM:SCAL 1m", ":TRIG:MODE AUTO", ":TRIG:SOUR CHAN1", ":MATH:MODE OFF", ":DEC:MODE OFF", ":DISP:XY ON"));

      return list;
    }
  }

  //------------------------------------------------------------------------------------------------
  // Applying a profile: send, read back, compare

  internal class ApplyResult
  {
    public string Command;
    public string Wanted;
    public string Actual;
    public string Problem;            // null when the setting is as wanted
  }

  internal static class ProfileApplier
  {
    public static List<ApplyResult> Apply(ScopeLink link, Profile profile)
    {
      List<ApplyResult> results = new List<ApplyResult>();

      foreach(string command in profile.Commands())
      {
        ApplyResult r = new ApplyResult();
        r.Command = command;

        int space = command.IndexOf(' ');
        string header = (space > 0) ? command.Substring(0, space) : command;
        r.Wanted = (space > 0) ? command.Substring(space + 1).Trim() : "";

        List<string> reply = link.CommandRaw(command);

        if((reply.Count == 0) || (reply[0] != "OK"))
        {
          r.Problem = (reply.Count > 0) ? reply[0] : "no reply";
          r.Actual = "";
          results.Add(r);
          continue;
        }

        // Read the setting back (commands without a value, such as RUN, have nothing to read)
        if(space > 0)
        {
          List<string> back = link.CommandRaw(header + "?");
          r.Actual = (back.Count > 0) ? back[0] : "";

          if(r.Actual.StartsWith("#ERR"))
            r.Problem = "read back failed: " + r.Actual;
          else if(!Same(r.Wanted, r.Actual))
            r.Problem = "the scope reports another value";
        }

        results.Add(r);
      }

      return results;
    }

    // Numbers within 3 % (levels and positions move in steps of 1/50 division on the scope); words
    // when one is the short form of the other
    public static bool Same(string wanted, string actual)
    {
      double a, b;

      if(ParseNumber(wanted, out a) && ParseNumber(actual, out b))
        return Math.Abs(a - b) <= Math.Max(Math.Abs(a), Math.Abs(b)) * 0.03 + 1e-12;

      string w = Normal(wanted), x = Normal(actual);
      return (w == x) || (w.Length > 0 && x.Length > 0 && (w.StartsWith(x) || x.StartsWith(w)));
    }

    private static string Normal(string text)
    {
      string t = text.Trim().ToUpperInvariant().Replace(" ", "");
      if(t == "1") return "ON";
      if(t == "0") return "OFF";
      if(t == "VOLT") return "V";
      if(t == "AMPERE" || t == "AMP") return "A";
      if(t == "POSITIVE" || t == "POS") return "RIS";
      if(t == "NEGATIVE" || t == "NEG") return "FALL";
      if(t == "CH1" || t == "CHANNEL1") return "CHAN1";
      if(t == "CH2" || t == "CHANNEL2") return "CHAN2";
      return t;
    }

    // Number with an optional SI multiplier as the scope accepts it (M is milli)
    public static bool ParseNumber(string text, out double value)
    {
      value = 0;
      string t = text.Trim();
      int end = 0;

      while((end < t.Length) && ("+-.0123456789".IndexOf(t[end]) >= 0 ||
            (((t[end] == 'e') || (t[end] == 'E')) && (end + 1 < t.Length) && ("+-0123456789".IndexOf(t[end + 1]) >= 0))))
      {
        if((t[end] == 'e') || (t[end] == 'E'))
          end++;
        end++;
      }

      if((end == 0) || !double.TryParse(t.Substring(0, end), NumberStyles.Float, CultureInfo.InvariantCulture, out value))
        return false;

      string rest = t.Substring(end);
      if(rest.Length > 0)
      {
        switch(rest[0])
        {
          case 'n': case 'N': value *= 1e-9; rest = rest.Substring(1); break;
          case 'u': case 'U': value *= 1e-6; rest = rest.Substring(1); break;
          case 'm': case 'M': value *= 1e-3; rest = rest.Substring(1); break;
          case 'k': case 'K': value *= 1e3; rest = rest.Substring(1); break;
        }
      }

      foreach(char c in rest)
        if(!char.IsLetter(c) && (c != '%'))
          return false;

      return true;
    }
  }

  //------------------------------------------------------------------------------------------------
  // Small dark dialogs

  internal static class Dark
  {
    public static TextBox Box(bool multiline)
    {
      TextBox t = new TextBox();
      t.BackColor = Theme.Card;
      t.ForeColor = Theme.Text;
      t.BorderStyle = BorderStyle.FixedSingle;
      t.Font = multiline ? Theme.Mono : Theme.Normal;
      t.Multiline = multiline;
      if(multiline)
      {
        t.ScrollBars = ScrollBars.Vertical;
        t.AcceptsReturn = true;
        t.WordWrap = false;
      }
      return t;
    }

    // Text of a few lines that wraps, without line breaks of its own
    public static TextBox Paragraph()
    {
      TextBox t = Box(false);
      t.Multiline = true;
      t.WordWrap = true;
      t.AcceptsReturn = false;
      t.ScrollBars = ScrollBars.Vertical;
      return t;
    }

    public static Label Caption(string text)
    {
      Label l = new Label();
      l.Text = text;
      l.ForeColor = Theme.Dim;
      l.BackColor = Color.Transparent;
      l.Font = Theme.Small;
      l.AutoSize = true;
      return l;
    }

    public static Form Window(string title, int width, int height)
    {
      Form f = new Form();
      f.Text = title;
      f.BackColor = Theme.Window;
      f.ForeColor = Theme.Text;
      f.Font = Theme.Normal;
      f.ClientSize = new Size(width, height);
      f.StartPosition = FormStartPosition.CenterParent;
      f.FormBorderStyle = FormBorderStyle.FixedDialog;
      f.MinimizeBox = false;
      f.MaximizeBox = false;
      f.ShowInTaskbar = false;
      f.HandleCreated += delegate { MainForm.DarkTitle(f.Handle); };
      return f;
    }

    public static string Ask(IWin32Window owner, string title, string question, string initial)
    {
      using(Form f = Window(title, 420, 130))
      {
        Label l = Caption(question);
        l.Location = new Point(16, 14);
        f.Controls.Add(l);

        TextBox t = Box(false);
        t.Text = initial;
        t.SetBounds(16, 38, 388, 26);
        f.Controls.Add(t);

        FlatButton ok = new FlatButton("OK");
        ok.Fill = Theme.Accent;
        ok.SetBounds(220, 82, 88, 32);
        ok.Click += delegate { f.DialogResult = DialogResult.OK; };
        f.Controls.Add(ok);

        FlatButton cancel = new FlatButton("Cancel");
        cancel.SetBounds(316, 82, 88, 32);
        cancel.Click += delegate { f.DialogResult = DialogResult.Cancel; };
        f.Controls.Add(cancel);

        f.AcceptButton = null;
        t.KeyDown += delegate(object s, KeyEventArgs e) { if(e.KeyCode == Keys.Enter) f.DialogResult = DialogResult.OK; };

        return (f.ShowDialog(owner) == DialogResult.OK) ? t.Text.Trim() : null;
      }
    }

    // A warning that has to be confirmed by ticking a box first
    public static bool ConfirmWarning(IWin32Window owner, string profile, string warning)
    {
      using(Form f = Window("Safety warning", 520, 300))
      {
        Panel stripe = new Panel();
        stripe.BackColor = Theme.Accent;
        stripe.SetBounds(0, 0, 520, 6);
        f.Controls.Add(stripe);

        Label title = new Label();
        title.Text = "⚠  " + profile;
        title.Font = Theme.Title;
        title.ForeColor = Theme.Accent;
        title.AutoSize = true;
        title.Location = new Point(16, 18);
        f.Controls.Add(title);

        Label text = new Label();
        text.Text = warning;
        text.ForeColor = Theme.Text;
        text.SetBounds(16, 58, 488, 140);
        f.Controls.Add(text);

        CheckBox agree = new CheckBox();
        agree.Text = "I use an isolated or differential probe";
        agree.ForeColor = Theme.Text;
        agree.AutoSize = true;
        agree.Location = new Point(16, 204);
        f.Controls.Add(agree);

        FlatButton ok = new FlatButton("Apply profile");
        ok.Fill = Theme.Accent;
        ok.Enabled = false;
        ok.SetBounds(256, 248, 136, 34);
        ok.Click += delegate { if(agree.Checked) f.DialogResult = DialogResult.OK; };
        f.Controls.Add(ok);
        agree.CheckedChanged += delegate { ok.Enabled = agree.Checked; ok.Invalidate(); };

        FlatButton cancel = new FlatButton("Cancel");
        cancel.SetBounds(400, 248, 104, 34);
        cancel.Click += delegate { f.DialogResult = DialogResult.Cancel; };
        f.Controls.Add(cancel);

        return f.ShowDialog(owner) == DialogResult.OK;
      }
    }

    // The result of applying a profile: every setting with what the scope reports
    public static void ShowResults(IWin32Window owner, Profile profile, List<ApplyResult> results)
    {
      int problems = 0;
      foreach(ApplyResult r in results)
        if(r.Problem != null)
          problems++;

      using(Form f = Window("Profile applied: " + profile.Name, 760, 560))
      {
        Label head = new Label();
        head.Font = Theme.Bold;
        head.AutoSize = true;
        head.Location = new Point(16, 14);
        head.ForeColor = (problems == 0) ? Theme.Good : Theme.Accent;
        head.Text = (problems == 0) ? "All " + results.Count + " settings are on the scope as wanted."
                                    : problems + " of " + results.Count + " settings are not as wanted (red).";
        f.Controls.Add(head);

        ListView list = new ListView();
        list.View = View.Details;
        list.FullRowSelect = true;
        list.HeaderStyle = ColumnHeaderStyle.Nonclickable;
        list.BackColor = Theme.Card;
        list.ForeColor = Theme.Text;
        list.BorderStyle = BorderStyle.None;
        list.Font = Theme.Mono;
        list.SetBounds(16, 44, 728, 400);
        list.Columns.Add("Setting", 240);
        list.Columns.Add("Wanted", 120);
        list.Columns.Add("On the scope", 120);
        list.Columns.Add("Result", 230);

        foreach(ApplyResult r in results)
        {
          int space = r.Command.IndexOf(' ');
          ListViewItem item = new ListViewItem((space > 0) ? r.Command.Substring(0, space) : r.Command);
          item.SubItems.Add(r.Wanted);
          item.SubItems.Add(r.Actual ?? "");
          item.SubItems.Add(r.Problem ?? "OK");
          item.ForeColor = (r.Problem == null) ? Theme.Text : Theme.Accent;
          list.Items.Add(item);
        }

        f.Controls.Add(list);

        Label connect = new Label();
        connect.Text = "How to connect: " + profile.Connect;
        connect.ForeColor = Theme.Dim;
        connect.SetBounds(16, 452, 728, 54);
        f.Controls.Add(connect);

        FlatButton close = new FlatButton("Close");
        close.SetBounds(640, 512, 104, 34);
        close.Click += delegate { f.DialogResult = DialogResult.OK; };
        f.Controls.Add(close);

        f.ShowDialog(owner);
      }
    }
  }

  //------------------------------------------------------------------------------------------------
  // The profile manager

  internal class ProfileDialog : Form
  {
    private readonly MainForm main;
    private readonly ScopeLink link;
    private List<Profile> profiles;
    private ListBox list = new ListBox();
    private TextBox name = Dark.Box(false);
    private TextBox description = Dark.Paragraph();
    private TextBox connect = Dark.Paragraph();
    private TextBox warning = Dark.Paragraph();
    private TextBox baud = Dark.Box(false);
    private TextBox measurements = Dark.Box(false);
    private TextBox settings = Dark.Box(true);
    private Label state = Dark.Caption("");
    private FlatButton applyButton = new FlatButton("Apply to scope");
    private FlatButton saveButton = new FlatButton("Save changes");
    private FlatButton deleteButton = new FlatButton("Delete");
    private FlatButton renameButton = new FlatButton("Rename");
    private bool loading;
    private bool busy;

    public ProfileDialog(MainForm main, ScopeLink link)
    {
      this.main = main;
      this.link = link;

      Text = "Profiles";
      BackColor = Theme.Window;
      ForeColor = Theme.Text;
      Font = Theme.Normal;
      ClientSize = new Size(1060, 700);
      StartPosition = FormStartPosition.CenterParent;
      FormBorderStyle = FormBorderStyle.FixedDialog;
      MinimizeBox = false;
      MaximizeBox = false;
      ShowInTaskbar = false;
      HandleCreated += delegate { MainForm.DarkTitle(Handle); };

      list.BackColor = Theme.Card;
      list.ForeColor = Theme.Text;
      list.BorderStyle = BorderStyle.None;
      list.DrawMode = DrawMode.OwnerDrawFixed;
      list.ItemHeight = 30;
      list.SetBounds(16, 16, 280, 586);
      list.DrawItem += DrawProfile;
      list.SelectedIndexChanged += delegate { ShowSelected(); };
      Controls.Add(list);

      int x = 316, w = 728, y = 16;
      Field("Name", name, x, ref y, w, 26);
      Field("Description", description, x, ref y, w, 48);
      Field("How to connect", connect, x, ref y, w, 48);
      Field("Warning shown before applying (empty for none)", warning, x, ref y, w, 48);
      Field("UART baud rate (0 when not a UART profile; sets the decoder and a matching time base)", baud, x, ref y, w, 26);
      Field("Measurements to show, comma separated (CHAN1:VRMS, CHAN2:FREQ, PHASE, ...; see COMMANDS.md)", measurements, x, ref y, w, 26);
      Field("Settings: remote commands, one per line, sent in this order", settings, x, ref y, w, 602 - y - 18);

      foreach(TextBox box in new TextBox[] { name, description, connect, warning, baud, measurements, settings })
        box.TextChanged += delegate { if(!loading) UpdateButtons(); };

      state.SetBounds(316, 612, 728, 18);
      state.AutoSize = false;
      Controls.Add(state);

      int bx = 16, by = 648;
      applyButton.Fill = Theme.Accent;
      Button(applyButton, ref bx, by, 132, delegate { ApplySelected(); });
      Button(new FlatButton("Save scope settings as profile"), ref bx, by, 222, delegate { SaveFromScope(); });
      Button(new FlatButton("Copy"), ref bx, by, 70, delegate { CopySelected(); });
      Button(saveButton, ref bx, by, 116, delegate { SaveSelected(); });
      Button(renameButton, ref bx, by, 84, delegate { RenameSelected(); });
      Button(deleteButton, ref bx, by, 80, delegate { DeleteSelected(); });
      Button(new FlatButton("Import"), ref bx, by, 76, delegate { ImportProfile(); });
      Button(new FlatButton("Export"), ref bx, by, 76, delegate { ExportSelected(); });
      Button(new FlatButton("Close"), ref bx, by, 70, delegate { Close(); });

      Reload(null);
    }

    private void Field(string caption, TextBox box, int x, ref int y, int width, int height)
    {
      Label l = Dark.Caption(caption);
      l.Location = new Point(x, y);
      Controls.Add(l);
      box.SetBounds(x, y + 18, width, height);
      Controls.Add(box);
      y += height + 26;
    }

    private void Button(FlatButton b, ref int x, int y, int width, EventHandler click)
    {
      b.SetBounds(x, y, width, 36);
      b.Click += click;
      Controls.Add(b);
      x += width + 8;
    }

    private void DrawProfile(object sender, DrawItemEventArgs e)
    {
      if(e.Index < 0)
        return;

      Profile p = profiles[e.Index];
      bool selected = (e.State & DrawItemState.Selected) != 0;

      using(SolidBrush back = new SolidBrush(selected ? Theme.Hover : Theme.Card))
        e.Graphics.FillRectangle(back, e.Bounds);

      TextRenderer.DrawText(e.Graphics, p.Name, Theme.Normal, new Point(e.Bounds.X + 10, e.Bounds.Y + 6), Theme.Text, TextFormatFlags.NoPrefix);

      if(p.BuiltIn || (p.Warning.Length > 0))
        TextRenderer.DrawText(e.Graphics, (p.Warning.Length > 0) ? "⚠" : "built in", Theme.Small,
                              new Rectangle(e.Bounds.X, e.Bounds.Y, e.Bounds.Width - 8, e.Bounds.Height),
                              (p.Warning.Length > 0) ? Theme.Accent : Theme.Dim,
                              TextFormatFlags.Right | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
    }

    private Profile Selected
    {
      get { return ((list.SelectedIndex >= 0) && (list.SelectedIndex < profiles.Count)) ? profiles[list.SelectedIndex] : null; }
    }

    private void Reload(Profile select)
    {
      List<string> problems = new List<string>();
      profiles = ProfileStore.Load(problems);

      list.Items.Clear();
      int index = 0;
      for(int i = 0; i < profiles.Count; i++)
      {
        list.Items.Add(profiles[i].Name);
        if((select != null) && (profiles[i].File != null) && (profiles[i].File == select.File))
          index = i;
      }

      list.SelectedIndex = (profiles.Count > 0) ? index : -1;
      state.Text = (problems.Count > 0) ? "Not loaded: " + string.Join("; ", problems.ToArray()) : "User profiles: " + ProfileStore.Folder;
    }

    private void ShowSelected()
    {
      Profile p = Selected;
      if(p == null)
        return;

      loading = true;
      name.Text = p.Name;
      description.Text = p.Description;
      connect.Text = p.Connect;
      warning.Text = p.Warning;
      baud.Text = p.UartBaud.ToString(CultureInfo.InvariantCulture);
      measurements.Text = string.Join(", ", p.Measurements.ToArray());
      settings.Text = string.Join("\r\n", p.Settings.ToArray());

      foreach(TextBox box in new TextBox[] { name, description, connect, warning, baud, measurements, settings })
        box.ReadOnly = p.BuiltIn;

      loading = false;
      UpdateButtons();
    }

    private void UpdateButtons()
    {
      Profile p = Selected;
      bool user = (p != null) && !p.BuiltIn;

      saveButton.Enabled = user && !loading;
      renameButton.Enabled = user;
      deleteButton.Enabled = user;
      applyButton.Enabled = (p != null) && !busy;

      foreach(Control c in new Control[] { saveButton, renameButton, deleteButton, applyButton })
        c.Invalidate();
    }

    // The profile as shown in the fields (edits included)
    private Profile FromFields(Profile basis)
    {
      Profile p = basis.BuiltIn ? basis : basis.Copy();
      if(basis.BuiltIn)
        return p;

      p.File = basis.File;
      p.Name = name.Text.Trim();
      p.Description = description.Text.Trim();
      p.Connect = connect.Text.Trim();
      p.Warning = warning.Text.Trim();

      int b;
      if(!int.TryParse(baud.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out b) || (b < 0))
        throw new FormatException("the baud rate is not a number");
      p.UartBaud = b;

      p.Measurements.Clear();
      foreach(string m in measurements.Text.Split(new char[] { ',', ';', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        if(m.Trim().Length > 0)
          p.Measurements.Add(m.Trim());

      p.Settings.Clear();
      foreach(string line in settings.Text.Split(new char[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        if(line.Trim().Length > 0)
          p.Settings.Add(line.Trim());

      if(p.Name.Length == 0)
        throw new FormatException("the profile needs a name");

      return p;
    }

    private void Fail(string what, Exception e)
    {
      state.Text = what + ": " + e.Message;
      state.ForeColor = Theme.Accent;
    }

    private void Note(string text)
    {
      state.Text = text;
      state.ForeColor = Theme.Dim;
    }

    //----------------------------------------------------------------------------------------------

    private void ApplySelected()
    {
      Profile p;

      try
      {
        if(Selected == null)
          return;
        p = FromFields(Selected);
      }
      catch(Exception e)
      {
        Fail("Not applied", e);
        return;
      }

      if(!link.IsOpen)
      {
        Note("Connect to the scope first.");
        return;
      }

      if((p.Warning.Length > 0) && !Dark.ConfirmWarning(this, p.Name, p.Warning))
      {
        Note("Not applied.");
        return;
      }

      busy = true;
      UpdateButtons();
      Note("Applying " + p.Name + " ...");
      Cursor = Cursors.WaitCursor;

      Thread thread = new Thread(delegate()
      {
        List<ApplyResult> results = null;
        Exception error = null;

        try { results = ProfileApplier.Apply(link, p); }
        catch(Exception e) { error = e; }

        BeginInvoke(new Action(delegate
        {
          busy = false;
          Cursor = Cursors.Default;
          UpdateButtons();

          if(error != null)
          {
            Fail("Applying stopped", error);
            return;
          }

          Note("Applied " + p.Name + ".");
          main.SetActiveProfile(p);
          Dark.ShowResults(this, p, results);
        }));
      });
      thread.IsBackground = true;
      thread.Start();
    }

    private void SaveFromScope()
    {
      if(!link.IsOpen)
      {
        Note("Connect to the scope first.");
        return;
      }

      string title = Dark.Ask(this, "Save scope settings", "Name of the new profile", "My setup " + DateTime.Now.ToString("yyyy-MM-dd HH.mm"));
      if(string.IsNullOrEmpty(title))
        return;

      try
      {
        List<string> lines = link.CommandRaw("SYST:SET?");
        if((lines.Count == 0) || lines[0].StartsWith("#ERR"))
          throw new IOException((lines.Count > 0) ? lines[0] : "no reply (does the scope run the current X Scope firmware?)");

        Profile p = new Profile();
        p.Name = title;
        p.Description = "Saved from the scope on " + DateTime.Now.ToString("yyyy-MM-dd HH:mm");
        p.Settings.AddRange(lines);

        // The F1 to F6 slots are part of the settings; show them in X Scope Remote too
        foreach(string line in lines)
          if(line.StartsWith(":MEAS:SLOT"))
          {
            string[] parts = line.Substring(line.IndexOf(' ') + 1).Split(',');
            if(parts.Length == 2)
              p.Measurements.Add(parts[0] + ":" + parts[1]);
          }

        ProfileStore.Save(p);
        Reload(p);
        Note("Saved " + Path.GetFileName(p.File) + ".");
      }
      catch(Exception e)
      {
        Fail("Not saved", e);
      }
    }

    private void CopySelected()
    {
      if(Selected == null)
        return;

      try
      {
        Profile p = Selected.BuiltIn ? Selected.Copy() : FromFields(Selected).Copy();
        p.Name = p.Name + " (copy)";
        ProfileStore.Save(p);
        Reload(p);
        Note("Copied to " + Path.GetFileName(p.File) + "; edit it and press Save changes.");
      }
      catch(Exception e)
      {
        Fail("Not copied", e);
      }
    }

    private void SaveSelected()
    {
      if((Selected == null) || Selected.BuiltIn)
        return;

      try
      {
        Profile p = FromFields(Selected);
        ProfileStore.Save(p);
        Reload(p);
        Note("Saved.");
      }
      catch(Exception e)
      {
        Fail("Not saved", e);
      }
    }

    private void RenameSelected()
    {
      if((Selected == null) || Selected.BuiltIn)
        return;

      string title = Dark.Ask(this, "Rename profile", "New name", Selected.Name);
      if(string.IsNullOrEmpty(title))
        return;

      try
      {
        Profile p = FromFields(Selected);
        string old = p.File;
        p.Name = title;
        p.File = null;
        ProfileStore.Save(p);

        if((old != null) && (old != p.File) && System.IO.File.Exists(old))
          System.IO.File.Delete(old);

        Reload(p);
        Note("Renamed.");
      }
      catch(Exception e)
      {
        Fail("Not renamed", e);
      }
    }

    private void DeleteSelected()
    {
      Profile p = Selected;
      if((p == null) || p.BuiltIn)
        return;

      if(MessageBox.Show(this, "Delete the profile \"" + p.Name + "\"?", "Delete profile", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
        return;

      try
      {
        ProfileStore.Delete(p);
        Reload(null);
        Note("Deleted.");
      }
      catch(Exception e)
      {
        Fail("Not deleted", e);
      }
    }

    private void ImportProfile()
    {
      OpenFileDialog dialog = new OpenFileDialog();
      dialog.Filter = "X Scope profile (*.json)|*.json";
      dialog.Multiselect = true;

      if(dialog.ShowDialog(this) != DialogResult.OK)
        return;

      Profile last = null;
      List<string> problems = new List<string>();

      foreach(string file in dialog.FileNames)
      {
        try
        {
          Profile p = Profile.FromJson(System.IO.File.ReadAllText(file, Encoding.UTF8));
          ProfileStore.Save(p);
          last = p;
        }
        catch(Exception e)
        {
          problems.Add(Path.GetFileName(file) + ": " + e.Message);
        }
      }

      Reload(last);

      if(problems.Count > 0)
        Fail("Not imported", new Exception(string.Join("; ", problems.ToArray())));
      else
        Note("Imported " + dialog.FileNames.Length + " profile(s).");
    }

    private void ExportSelected()
    {
      if(Selected == null)
        return;

      try
      {
        Profile p = FromFields(Selected);
        SaveFileDialog dialog = new SaveFileDialog();
        dialog.Filter = "X Scope profile (*.json)|*.json";
        dialog.FileName = Path.GetFileName(ProfileStore.UniqueFile(p.Name));

        if(dialog.ShowDialog(this) != DialogResult.OK)
          return;

        System.IO.File.WriteAllText(dialog.FileName, p.ToJson(), new UTF8Encoding(false));
        Note("Exported " + Path.GetFileName(dialog.FileName) + ".");
      }
      catch(Exception e)
      {
        Fail("Not exported", e);
      }
    }
  }
}
