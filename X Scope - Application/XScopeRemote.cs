// X Scope Remote for the FNIRSI 1014D (Windows, .NET Framework 4.x, C# 5)
//
// Live view and control of the scope over its USB serial mode (the scope starts in it; Utilities >
// USB mode switches to mass storage for a session). The traces are read as raw samples and drawn here, the measurements are computed here,
// and every front panel key is available. Mouse wheel over the graph zooms the time axis, dragging
// pans, double click shows the whole record.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace XScopeRemote
{
  //------------------------------------------------------------------------------------------------
  // Colors and fonts

  internal static class Theme
  {
    public static readonly Color Window   = Color.FromArgb(22, 24, 29);
    public static readonly Color Card     = Color.FromArgb(32, 35, 42);
    public static readonly Color CardEdge = Color.FromArgb(48, 52, 62);
    public static readonly Color Button   = Color.FromArgb(46, 50, 60);
    public static readonly Color Hover    = Color.FromArgb(62, 67, 80);
    public static readonly Color Pressed  = Color.FromArgb(78, 84, 100);
    public static readonly Color Text     = Color.FromArgb(226, 229, 236);
    public static readonly Color Dim      = Color.FromArgb(140, 146, 160);
    public static readonly Color Accent   = Color.FromArgb(232, 36, 44);
    public static readonly Color Plot     = Color.FromArgb(10, 12, 15);
    public static readonly Color Grid     = Color.FromArgb(38, 42, 50);
    public static readonly Color Axis     = Color.FromArgb(70, 76, 90);
    public static readonly Color Ch1      = Color.FromArgb(255, 212, 0);
    public static readonly Color Ch2      = Color.FromArgb(0, 200, 255);
    public static readonly Color Good     = Color.FromArgb(70, 200, 110);

    public static readonly Font Normal = new Font("Segoe UI", 9.5f);
    public static readonly Font Small  = new Font("Segoe UI", 8.5f);
    public static readonly Font Bold   = new Font("Segoe UI Semibold", 10f);
    public static readonly Font Title  = new Font("Segoe UI Semibold", 14f);
    public static readonly Font Value  = new Font("Segoe UI Semibold", 12f);
    public static readonly Font Mono   = new Font("Consolas", 9f);

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

  //------------------------------------------------------------------------------------------------
  // Flat rounded button without any white system chrome

  internal class FlatButton : Control
  {
    private bool hover;
    private bool pressed;
    public Color Fill = Theme.Button;
    public Color Edge = Color.Empty;

    public FlatButton(string text)
    {
      Text = text;
      Font = Theme.Normal;
      ForeColor = Theme.Text;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
      BackColor = Color.Transparent;
      Cursor = Cursors.Hand;
      TabStop = false;
    }

    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; pressed = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e) { pressed = true; Invalidate(); base.OnMouseDown(e); }
    protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
      Graphics g = e.Graphics;
      g.SmoothingMode = SmoothingMode.AntiAlias;
      Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);
      Color fill = !Enabled ? Theme.Card : pressed ? Theme.Pressed : hover ? Theme.Hover : Fill;

      using(GraphicsPath path = Theme.Rounded(r, 6))
      using(SolidBrush brush = new SolidBrush(fill))
      {
        g.FillPath(brush, path);

        if(Edge != Color.Empty)
        {
          using(Pen pen = new Pen(Edge, 1.5f))
            g.DrawPath(pen, path);
        }
      }

      TextRenderer.DrawText(g, Text, Font, r, Enabled ? ForeColor : Theme.Dim,
                            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
    }
  }

  //------------------------------------------------------------------------------------------------
  // Card: a rounded panel with a title and an accent stripe

  internal class Card : Panel
  {
    public Color Accent = Theme.CardEdge;
    public string Title;

    public Card(string title)
    {
      Title = title;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
      BackColor = Theme.Window;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      Graphics g = e.Graphics;
      g.SmoothingMode = SmoothingMode.AntiAlias;
      Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);

      using(GraphicsPath path = Theme.Rounded(r, 8))
      using(SolidBrush brush = new SolidBrush(Theme.Card))
      using(Pen edge = new Pen(Theme.CardEdge))
      {
        g.FillPath(brush, path);
        g.DrawPath(edge, path);
      }

      using(SolidBrush stripe = new SolidBrush(Accent))
        g.FillRectangle(stripe, 12, 12, 3, 16);

      TextRenderer.DrawText(g, Title, Theme.Bold, new Point(20, 10), Theme.Text, TextFormatFlags.NoPrefix);
    }
  }

  //------------------------------------------------------------------------------------------------
  // Value label drawn on the card background

  internal class ValueLabel : Control
  {
    public Color Color = Theme.Text;
    public ContentAlignment Align = ContentAlignment.MiddleLeft;

    public ValueLabel(Font font)
    {
      Font = font;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.SupportsTransparentBackColor, true);
      BackColor = Color.Transparent;
    }

    public void SetText(string text)
    {
      if(Text != text)
      {
        Text = text;
        Invalidate();
      }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      TextFormatFlags flags = TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix | TextFormatFlags.EndEllipsis;
      if(Align == ContentAlignment.MiddleRight)
        flags |= TextFormatFlags.Right;
      else if(Align == ContentAlignment.MiddleCenter)
        flags |= TextFormatFlags.HorizontalCenter;

      TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, Color, flags);
    }
  }

  //------------------------------------------------------------------------------------------------
  // One channel of samples with its scaling

  internal class Wave
  {
    public int Channel;
    public bool Enabled;
    public int Count;
    public double Rate;
    public int Trigger;
    public double[] Volts;
    public double VoltsPerDiv;     // display scale
    public double ZeroDiv;         // divisions of the 0 V line above the screen center
    public string Unit = "V";

    public double Max, Min, Mean, Rms, Frequency;
  }

  //------------------------------------------------------------------------------------------------
  // Serial link with the X Scope remote protocol

  internal class ScopeLink
  {
    private SerialPort port;
    private readonly object sync = new object();

    public bool IsOpen
    {
      get { return (port != null) && port.IsOpen; }
    }

    public string Open(string name)
    {
      port = new SerialPort(name, 115200);
      port.ReadBufferSize = 1 << 20;
      port.ReadTimeout = 2000;
      port.WriteTimeout = 2000;
      port.DtrEnable = true;
      port.Open();
      Thread.Sleep(200);
      port.DiscardInBuffer();

      try
      {
        List<string> reply = Command("IDN?");
        if((reply.Count > 0) && reply[0].StartsWith("X Scope"))
          return reply[0];
      }
      catch(Exception)
      {
      }

      Close();
      return null;
    }

    public void Close()
    {
      try
      {
        if(port != null)
          port.Close();
      }
      catch(Exception)
      {
      }

      port = null;
    }

    private string ReadLine()
    {
      StringBuilder line = new StringBuilder();

      while(true)
      {
        int b = port.ReadByte();

        if(b == '\n')
          return line.ToString();

        if(b != '\r')
          line.Append((char)b);
      }
    }

    private byte[] ReadExact(int count)
    {
      byte[] data = new byte[count];
      int got = 0;

      while(got < count)
        got += port.Read(data, got, count - got);

      return data;
    }

    private void Resync()
    {
      Thread.Sleep(300);
      port.DiscardInBuffer();
    }

    public List<string> Command(string command)
    {
      lock(sync)
      {
        port.Write(command + "\n");

        List<string> lines = new List<string>();
        while(true)
        {
          string line = ReadLine();

          if(line == "#END")
            break;

          lines.Add(line);
        }

        if((lines.Count > 0) && lines[0].StartsWith("#ERR"))
          throw new IOException(lines[0].Substring(1));

        return lines;
      }
    }

    // Raw samples of one channel (WAVEB?), converted to volts
    public Wave Wave(int channel)
    {
      lock(sync)
      {
        try
        {
          port.Write("WAVEB? " + channel + "\n");

          // #WAVEB ch enabled count rate trigger cal mvdiv offset_uv position display_mvdiv unit
          string[] f = ReadLine().Split(' ');
          if((f.Length < 12) || (f[0] != "#WAVEB"))
            throw new IOException("unexpected reply");

          Wave w = new Wave();
          w.Channel = channel;
          w.Enabled = (f[2] == "1");
          w.Count = int.Parse(f[3]);
          w.Rate = double.Parse(f[4], CultureInfo.InvariantCulture);
          w.Trigger = int.Parse(f[5]);
          double cal = double.Parse(f[6], CultureInfo.InvariantCulture);
          double mvdiv = double.Parse(f[7], CultureInfo.InvariantCulture);
          double offset = double.Parse(f[8], CultureInfo.InvariantCulture) / 1e6;
          int position = int.Parse(f[9]);
          w.VoltsPerDiv = double.Parse(f[10], CultureInfo.InvariantCulture) / 1000.0;
          w.Unit = f[11];
          w.ZeroDiv = (position - 200) / 50.0;

          byte[] raw = ReadExact(w.Count);
          if((ReadLine() != "") || (ReadLine() != "#END"))
            throw new IOException("incomplete data");

          double scale = cal * mvdiv / (50.0 * 2097152.0) / 1000.0;
          w.Volts = new double[w.Count];
          for(int i = 0; i < w.Count; i++)
            w.Volts[i] = (raw[i] - 128) * scale + offset;

          Measure(w);
          return w;
        }
        catch(Exception)
        {
          Resync();
          throw;
        }
      }
    }

    // The scope's own screen, fetched in bands of 16 rows
    public byte[] Screen()
    {
      byte[] screen = new byte[800 * 480 * 2];

      for(int row = 0; row < 480; row += 16)
      {
        for(int attempt = 0; ; attempt++)
        {
          lock(sync)
          {
            try
            {
              port.Write("SCREEN? " + row + " 16\n");

              if(ReadLine() != "#SCREEN " + row + " 16 25600")
                throw new IOException("unexpected reply");

              byte[] band = ReadExact(25600);

              if((ReadLine() != "") || (ReadLine() != "#END"))
                throw new IOException("band not complete");

              Array.Copy(band, 0, screen, row * 1600, band.Length);
              break;
            }
            catch(Exception)
            {
              Resync();

              if(attempt >= 2)
                throw;
            }
          }
        }
      }

      return screen;
    }

    private static void Measure(Wave w)
    {
      double max = double.MinValue, min = double.MaxValue, sum = 0, squares = 0;

      foreach(double v in w.Volts)
      {
        if(v > max) max = v;
        if(v < min) min = v;
        sum += v;
        squares += v * v;
      }

      w.Max = max;
      w.Min = min;
      w.Mean = sum / w.Count;
      w.Rms = Math.Sqrt(squares / w.Count);

      // Rising crossings of the middle level with 10 % hysteresis, interpolated
      double middle = (max + min) / 2, band = (max - min) * 0.1;
      int crossings = 0;
      double first = 0, last = 0;
      bool low = w.Volts[0] < middle;

      if(band > 0)
      {
        for(int i = 1; i < w.Count; i++)
        {
          if(low && (w.Volts[i] > middle + band))
          {
            low = false;
            double at = i - 1 + (middle - w.Volts[i - 1]) / Math.Max(1e-12, w.Volts[i] - w.Volts[i - 1]);
            if(crossings == 0) first = at;
            last = at;
            crossings++;
          }
          else if(!low && (w.Volts[i] < middle - band))
          {
            low = true;
          }
        }
      }

      w.Frequency = (crossings >= 2) ? w.Rate * (crossings - 1) / (last - first) : 0;
    }
  }

  //------------------------------------------------------------------------------------------------
  // The graph: grid, traces, zero and trigger markers, zoom and pan

  internal class PlotView : Control
  {
    public Wave[] Waves = new Wave[2];
    private double viewStart;      // first sample shown
    private double viewSpan = -1;  // samples shown, -1 = whole record
    private int dragX = -1;
    private double dragStart;

    public PlotView()
    {
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
               ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
      BackColor = Theme.Plot;
      Cursor = Cursors.SizeWE;
    }

    private int RecordLength()
    {
      foreach(Wave w in Waves)
        if(w != null) return w.Count;
      return 0;
    }

    private Rectangle Area()
    {
      return new Rectangle(52, 14, Width - 66, Height - 46);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
      int length = RecordLength();
      if(length == 0) return;

      Rectangle a = Area();
      if(viewSpan < 0) { viewStart = 0; viewSpan = length; }

      double at = viewStart + (e.X - a.X) * viewSpan / a.Width;
      double span = viewSpan * ((e.Delta > 0) ? 0.8 : 1.25);
      span = Math.Max(20, Math.Min(length, span));
      viewStart = at - (at - viewStart) * span / viewSpan;
      viewSpan = span;
      Clamp(length);
      Invalidate();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
      Focus();
      dragX = e.X;
      dragStart = viewStart;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
      if((dragX < 0) || (viewSpan < 0)) return;
      Rectangle a = Area();
      viewStart = dragStart - (e.X - dragX) * viewSpan / a.Width;
      Clamp(RecordLength());
      Invalidate();
    }

    protected override void OnMouseUp(MouseEventArgs e) { dragX = -1; }

    protected override void OnDoubleClick(EventArgs e)
    {
      viewSpan = -1;
      Invalidate();
    }

    private void Clamp(int length)
    {
      if(viewStart < 0) viewStart = 0;
      if(viewStart + viewSpan > length) viewStart = length - viewSpan;
      if(viewSpan >= length) { viewStart = 0; viewSpan = length; }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      Graphics g = e.Graphics;
      g.Clear(Theme.Plot);
      Rectangle a = Area();

      // Grid: 10 x 8 divisions with small ticks on the center lines
      using(Pen grid = new Pen(Theme.Grid))
      using(Pen axis = new Pen(Theme.Axis))
      {
        for(int i = 0; i <= 10; i++)
        {
          int x = a.X + i * a.Width / 10;
          g.DrawLine(i == 5 ? axis : grid, x, a.Top, x, a.Bottom);
        }

        for(int i = 0; i <= 8; i++)
        {
          int y = a.Y + i * a.Height / 8;
          g.DrawLine(i == 4 ? axis : grid, a.Left, y, a.Right, y);
        }

        for(int i = 0; i <= 50; i++)
        {
          int x = a.X + i * a.Width / 50;
          g.DrawLine(axis, x, a.Y + a.Height / 2 - 3, x, a.Y + a.Height / 2 + 3);
        }

        for(int i = 0; i <= 40; i++)
        {
          int y = a.Y + i * a.Height / 40;
          g.DrawLine(axis, a.X + a.Width / 2 - 3, y, a.X + a.Width / 2 + 3, y);
        }
      }

      int length = RecordLength();
      if(length == 0)
      {
        TextRenderer.DrawText(g, "Not connected", Theme.Title, a, Theme.Dim,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        return;
      }

      double start = (viewSpan < 0) ? 0 : viewStart;
      double span = (viewSpan < 0) ? length : viewSpan;

      g.SmoothingMode = SmoothingMode.AntiAlias;
      g.SetClip(a);

      foreach(Wave w in Waves)
      {
        if((w == null) || !w.Enabled || (w.VoltsPerDiv <= 0))
          continue;

        Color color = (w.Channel == 1) ? Theme.Ch1 : Theme.Ch2;
        double divHeight = a.Height / 8.0;
        double centerY = a.Y + a.Height / 2.0;

        // One point per pixel column: min and max of the samples in it (no aliasing when zoomed out)
        List<PointF> points = new List<PointF>();
        int first = Math.Max(0, (int)Math.Floor(start));
        int end = Math.Min(w.Count, (int)Math.Ceiling(start + span) + 1);
        double perPixel = span / a.Width;

        if(perPixel <= 1)
        {
          for(int i = first; i < end; i++)
            points.Add(new PointF((float)(a.X + (i - start) / perPixel), (float)ToY(w, w.Volts[i], centerY, divHeight)));
        }
        else
        {
          for(int px = 0; px < a.Width; px++)
          {
            int s0 = (int)(start + px * perPixel), s1 = (int)(start + (px + 1) * perPixel);
            s1 = Math.Min(Math.Max(s1, s0 + 1), w.Count);
            if(s0 >= w.Count) break;
            double lo = double.MaxValue, hi = double.MinValue;
            for(int i = s0; i < s1; i++) { if(w.Volts[i] < lo) lo = w.Volts[i]; if(w.Volts[i] > hi) hi = w.Volts[i]; }
            points.Add(new PointF(a.X + px, (float)ToY(w, hi, centerY, divHeight)));
            points.Add(new PointF(a.X + px, (float)ToY(w, lo, centerY, divHeight)));
          }
        }

        if(points.Count > 1)
        {
          using(Pen glow = new Pen(Color.FromArgb(60, color), 4f))
          using(Pen pen = new Pen(color, 1.5f))
          {
            glow.LineJoin = pen.LineJoin = LineJoin.Round;
            g.DrawLines(glow, points.ToArray());
            g.DrawLines(pen, points.ToArray());
          }
        }
      }

      g.ResetClip();

      // Zero level markers on the left
      foreach(Wave w in Waves)
      {
        if((w == null) || !w.Enabled) continue;
        Color color = (w.Channel == 1) ? Theme.Ch1 : Theme.Ch2;
        float y = (float)(a.Y + a.Height / 2.0 - w.ZeroDiv * a.Height / 8.0);
        y = Math.Max(a.Top, Math.Min(a.Bottom, y));
        using(SolidBrush b = new SolidBrush(color))
        {
          g.FillPolygon(b, new PointF[] { new PointF(a.X - 22, y - 8), new PointF(a.X - 6, y - 8), new PointF(a.X, y), new PointF(a.X - 6, y + 8), new PointF(a.X - 22, y + 8) });
        }
        TextRenderer.DrawText(g, w.Channel.ToString(), Theme.Small, new Rectangle(a.X - 22, (int)y - 8, 16, 16), Theme.Plot,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
      }

      // Trigger point on top
      Wave reference = Waves[0] ?? Waves[1];
      double tx = a.X + (reference.Trigger - start) * a.Width / span;
      if((tx >= a.Left) && (tx <= a.Right))
      {
        using(SolidBrush b = new SolidBrush(Theme.Accent))
          g.FillPolygon(b, new PointF[] { new PointF((float)tx - 7, a.Y - 12), new PointF((float)tx + 7, a.Y - 12), new PointF((float)tx, a.Y) });
        using(Pen p = new Pen(Color.FromArgb(90, Theme.Accent)) { DashStyle = DashStyle.Dot })
          g.DrawLine(p, (float)tx, a.Top, (float)tx, a.Bottom);
      }

      // Horizontal scale under the graph
      double secondsPerDiv = span / reference.Rate / 10.0;
      double left = (start - reference.Trigger) / reference.Rate;
      string scale = Format(secondsPerDiv, "s") + "/div    " + Format(left, "s") + " ... " + Format(left + span / reference.Rate, "s") +
                     "    " + Format(reference.Rate, "Sa/s") + ((viewSpan < 0) ? "    whole record" : "    zoomed (double click: whole record)");
      TextRenderer.DrawText(g, scale, Theme.Small, new Point(a.X, a.Bottom + 8), Theme.Dim);
    }

    private static double ToY(Wave w, double volts, double centerY, double divHeight)
    {
      return centerY - (volts / w.VoltsPerDiv + w.ZeroDiv) * divHeight;
    }

    public static string Format(double value, string unit)
    {
      double a = Math.Abs(value);
      string[] prefixes = { "p", "n", "u", "m", "", "k", "M", "G" };
      int index = 4;

      if(a > 0)
      {
        while((a < 1) && (index > 0)) { a *= 1000; value *= 1000; index--; }
        while((a >= 1000) && (index < 7)) { a /= 1000; value /= 1000; index++; }
      }

      string number = (Math.Abs(value) >= 100) ? value.ToString("0", CultureInfo.InvariantCulture) :
                      (Math.Abs(value) >= 10) ? value.ToString("0.0", CultureInfo.InvariantCulture) :
                      value.ToString("0.00", CultureInfo.InvariantCulture);
      return number + " " + prefixes[index].Replace("u", "µ") + unit;
    }
  }

  //------------------------------------------------------------------------------------------------
  // Dark drop down menus

  internal class DarkMenuRenderer : ToolStripProfessionalRenderer
  {
    public DarkMenuRenderer() : base(new DarkColors()) { }

    protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
    {
      e.TextColor = e.Item.Enabled ? e.Item.ForeColor : Theme.Dim;
      base.OnRenderItemText(e);
    }
  }

  internal class DarkColors : ProfessionalColorTable
  {
    public override Color MenuItemSelected { get { return Theme.Hover; } }
    public override Color MenuItemBorder { get { return Theme.Hover; } }
    public override Color MenuBorder { get { return Theme.CardEdge; } }
    public override Color ToolStripDropDownBackground { get { return Theme.Card; } }
    public override Color MenuItemSelectedGradientBegin { get { return Theme.Hover; } }
    public override Color MenuItemSelectedGradientEnd { get { return Theme.Hover; } }
  }

  //------------------------------------------------------------------------------------------------
  // Main window

  internal class MainForm : Form
  {
    private ScopeLink link = new ScopeLink();
    private PlotView plot = new PlotView();
    private FlatButton portButton = new FlatButton("Port \u25BE");
    private string portName;
    private FlatButton connectButton = new FlatButton("Connect");
    private FlatButton runButton = new FlatButton("RUN / STOP");
    private ValueLabel statusLabel = new ValueLabel(Theme.Small);
    private ValueLabel timebaseLabel = new ValueLabel(Theme.Value);
    private ValueLabel triggerLabel = new ValueLabel(Theme.Normal);
    private ValueLabel[] scaleLabels = new ValueLabel[3];
    private ValueLabel[] couplingLabels = new ValueLabel[3];
    private ValueLabel[] measureLabels = new ValueLabel[3];

    private Thread worker;
    private volatile bool running;
    private readonly Queue<string> pendingKeys = new Queue<string>();
    private bool scopeRunning = true;

    public MainForm()
    {
      Text = "X Scope Remote";

      // Window and taskbar icon: the one compiled into the executable (/win32icon)
      try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
      catch(Exception) { }

      BackColor = Theme.Window;
      ForeColor = Theme.Text;
      Font = Theme.Normal;
      ClientSize = new Size(1260, 760);
      MinimumSize = new Size(1100, 700);
      StartPosition = FormStartPosition.CenterScreen;
      KeyPreview = true;
      SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);

      BuildTopBar();
      BuildSidebar();

      plot.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      plot.SetBounds(16, 72, ClientSize.Width - 16 - 340, ClientSize.Height - 72 - 16);
      Controls.Add(plot);

      KeyDown += OnKeyDown;
      FormClosing += delegate { Disconnect(); };
      Shown += delegate { ListPorts(); };
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    protected override void OnHandleCreated(EventArgs e)
    {
      base.OnHandleCreated(e);

      // Immersive dark mode title bar (attribute 20, 19 on older Windows 10 builds)
      int on = 1;
      if(DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0)
        DwmSetWindowAttribute(Handle, 19, ref on, 4);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
      base.OnPaint(e);
      Graphics g = e.Graphics;
      g.SmoothingMode = SmoothingMode.AntiAlias;

      // Product name with the red X
      TextRenderer.DrawText(g, "X", new Font("Segoe UI Black", 20f), new Point(14, 12), Theme.Accent);
      TextRenderer.DrawText(g, "SCOPE", new Font("Segoe UI Black", 20f), new Point(40, 12), Theme.Text);
      TextRenderer.DrawText(g, "Remote  |  FNIRSI 1014D", Theme.Small, new Point(150, 25), Theme.Dim);
    }

    private void BuildTopBar()
    {
      portButton.SetBounds(330, 14, 100, 32);
      portButton.Click += delegate { ShowPortMenu(); };
      Controls.Add(portButton);

      connectButton.Fill = Theme.Accent;
      connectButton.SetBounds(438, 14, 110, 32);
      connectButton.Click += delegate { ToggleConnection(); };
      Controls.Add(connectButton);

      statusLabel.Color = Theme.Dim;
      statusLabel.SetBounds(330, 48, 520, 20);
      statusLabel.SetText("Scope: Utilities > USB mode > Serial, then Connect");
      Controls.Add(statusLabel);

      FlatButton shot = new FlatButton("Scope screenshot");
      shot.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      shot.SetBounds(ClientSize.Width - 16 - 340 - 290, 14, 140, 32);
      shot.Click += delegate { SaveScreenshot(); };
      Controls.Add(shot);

      FlatButton csv = new FlatButton("Export CSV");
      csv.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      csv.SetBounds(ClientSize.Width - 16 - 340 - 142, 14, 126, 32);
      csv.Click += delegate { SaveCsv(); };
      Controls.Add(csv);
    }

    private FlatButton Key(Control parent, string text, string key, int x, int y, int w, int h)
    {
      FlatButton b = new FlatButton(text);
      b.SetBounds(x, y, w, h);
      b.Click += delegate { QueueKey(key); };
      parent.Controls.Add(b);
      return b;
    }

    private void BuildSidebar()
    {
      int x = ClientSize.Width - 340, y = 16;
      AnchorStyles side = AnchorStyles.Top | AnchorStyles.Right;

      // Acquisition
      Card acq = new Card("Acquisition");
      acq.Anchor = side;
      acq.SetBounds(x, y, 324, 132);
      runButton.SetBounds(16, 40, 140, 36);
      runButton.Fill = Theme.Good;
      runButton.Click += delegate { QueueKey("RUN_STOP"); };
      acq.Controls.Add(runButton);
      Key(acq, "AUTO", "AUTO", 164, 40, 70, 36);
      Key(acq, "MENU", "MENU", 240, 40, 68, 36);
      Key(acq, "◀", "TIME_SUB", 16, 86, 40, 32);
      timebaseLabel.Align = ContentAlignment.MiddleCenter;
      timebaseLabel.SetBounds(60, 86, 116, 32);
      acq.Controls.Add(timebaseLabel);
      Key(acq, "▶", "TIME_ADD", 180, 86, 40, 32);
      Key(acq, "SAVE", "SAVE_WAVE", 228, 86, 80, 32);
      Controls.Add(acq);
      y += 144;

      // Channels
      for(int ch = 1; ch <= 2; ch++)
      {
        Card card = new Card("Channel " + ch);
        card.Accent = (ch == 1) ? Theme.Ch1 : Theme.Ch2;
        card.Anchor = side;
        card.SetBounds(x, y, 324, 156);

        string p = "CH" + ch;
        Key(card, "On / off", p, 16, 40, 90, 30);
        Key(card, "Settings", p + "_CONF", 112, 40, 90, 30);
        couplingLabels[ch] = new ValueLabel(Theme.Normal);
        couplingLabels[ch].Color = Theme.Dim;
        couplingLabels[ch].Align = ContentAlignment.MiddleRight;
        couplingLabels[ch].SetBounds(206, 40, 102, 30);
        card.Controls.Add(couplingLabels[ch]);

        Key(card, "−", p + "_SCALE_SUB", 16, 78, 36, 30);
        scaleLabels[ch] = new ValueLabel(Theme.Value);
        scaleLabels[ch].Color = card.Accent;
        scaleLabels[ch].Align = ContentAlignment.MiddleCenter;
        scaleLabels[ch].SetBounds(54, 78, 110, 30);
        card.Controls.Add(scaleLabels[ch]);
        Key(card, "+", p + "_SCALE_ADD", 166, 78, 36, 30);
        Key(card, "▲", p + "_POS_ADD", 222, 78, 40, 30);
        Key(card, "▼", p + "_POS_SUB", 268, 78, 40, 30);

        measureLabels[ch] = new ValueLabel(Theme.Mono);
        measureLabels[ch].Color = Theme.Text;
        measureLabels[ch].SetBounds(16, 114, 296, 36);
        card.Controls.Add(measureLabels[ch]);

        Controls.Add(card);
        y += 168;
      }

      // Trigger
      Card trig = new Card("Trigger");
      trig.Accent = Theme.Accent;
      trig.Anchor = side;
      trig.SetBounds(x, y, 324, 118);
      Key(trig, "Mode", "TRIG_MODE", 16, 40, 68, 30);
      Key(trig, "Edge", "TRIG_EDGE", 88, 40, 68, 30);
      Key(trig, "Source", "TRIG_CH", 160, 40, 72, 30);
      Key(trig, "50 %", "TRIG_50", 236, 40, 72, 30);
      Key(trig, "Level ▲", "TRIG_LEVEL_ADD", 16, 78, 90, 30);
      Key(trig, "Level ▼", "TRIG_LEVEL_SUB", 110, 78, 90, 30);
      triggerLabel.Color = Theme.Dim;
      triggerLabel.Align = ContentAlignment.MiddleRight;
      triggerLabel.SetBounds(204, 78, 104, 30);
      trig.Controls.Add(triggerLabel);
      Controls.Add(trig);
      y += 130;

      // Scope menus
      Card nav = new Card("Scope menus");
      nav.Anchor = side;
      nav.SetBounds(x, y, 324, 118);
      Key(nav, "◀", "LEFT", 16, 58, 44, 30);
      Key(nav, "▲", "UP", 64, 40, 44, 30);
      Key(nav, "OK", "OK", 64, 76, 44, 30);
      Key(nav, "▼", "DOWN", 112, 58, 44, 30);
      Key(nav, "▶", "RIGHT", 160, 58, 44, 30);
      for(int i = 0; i < 6; i++)
        Key(nav, "F" + (i + 1), "F" + (i + 1), 214 + (i % 2) * 48, 40 + (i / 2) * 24, 44, 22);
      Controls.Add(nav);
    }

    //----------------------------------------------------------------------------------------------

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
      string key = null;

      switch(e.KeyCode)
      {
        case Keys.Up:     key = "UP"; break;
        case Keys.Down:   key = "DOWN"; break;
        case Keys.Left:   key = "LEFT"; break;
        case Keys.Right:  key = "RIGHT"; break;
        case Keys.Enter:  key = "OK"; break;
        case Keys.Escape: key = "MENU"; break;
        case Keys.Space:  key = "RUN_STOP"; break;
      }

      if(key != null)
      {
        QueueKey(key);
        e.Handled = e.SuppressKeyPress = true;
      }
    }

    protected override bool ProcessDialogKey(Keys keyData)
    {
      if((keyData == Keys.Up) || (keyData == Keys.Down) || (keyData == Keys.Left) || (keyData == Keys.Right))
      {
        OnKeyDown(this, new KeyEventArgs(keyData));
        return true;
      }

      return base.ProcessDialogKey(keyData);
    }

    private void QueueKey(string name)
    {
      lock(pendingKeys)
        pendingKeys.Enqueue(name);
    }

    private void SetStatus(string text)
    {
      if(InvokeRequired)
      {
        BeginInvoke(new Action<string>(SetStatus), text);
        return;
      }

      statusLabel.SetText(text);
    }

    private void ListPorts()
    {
      string[] names = SerialPort.GetPortNames();
      Array.Sort(names);

      if(((portName == null) || (Array.IndexOf(names, portName) < 0)) && (names.Length > 0))
        portName = names[names.Length - 1];

      portButton.Text = (portName ?? "Port") + " \u25BE";
      portButton.Invalidate();
    }

    private void ShowPortMenu()
    {
      ContextMenuStrip menu = new ContextMenuStrip();
      menu.Renderer = new DarkMenuRenderer();
      menu.BackColor = Theme.Card;
      menu.ForeColor = Theme.Text;
      menu.Font = Theme.Normal;
      menu.ShowImageMargin = false;

      string[] names = SerialPort.GetPortNames();
      Array.Sort(names);

      foreach(string name in names)
      {
        string chosen = name;
        ToolStripMenuItem item = new ToolStripMenuItem(name);
        item.ForeColor = (name == portName) ? Theme.Accent : Theme.Text;
        item.Click += delegate { portName = chosen; ListPorts(); };
        menu.Items.Add(item);
      }

      if(names.Length == 0)
        menu.Items.Add(new ToolStripMenuItem("No serial ports") { Enabled = false });

      menu.Show(portButton, new Point(0, portButton.Height + 2));
    }

    private void ToggleConnection()
    {
      if(running)
      {
        Disconnect();
        return;
      }

      string name = portName;
      if(name == null)
        return;

      string identity = null;

      try
      {
        identity = link.Open(name);
      }
      catch(Exception e)
      {
        SetStatus("Could not open " + name + ": " + e.Message);
        return;
      }

      if(identity == null)
      {
        SetStatus(name + " is not an X Scope in serial mode");
        return;
      }

      connectButton.Text = "Disconnect";
      connectButton.Fill = Theme.Button;
      connectButton.Invalidate();
      SetStatus("Connected to " + identity);

      running = true;
      worker = new Thread(Work);
      worker.IsBackground = true;
      worker.Start();
    }

    private void Disconnect()
    {
      running = false;

      if(worker != null)
        worker.Join(3000);

      link.Close();
      connectButton.Text = "Connect";
      connectButton.Fill = Theme.Accent;
      connectButton.Invalidate();
    }

    // Background loop: keys first, then both channels, the settings twice a second
    private void Work()
    {
      DateTime lastStatus = DateTime.MinValue;
      int frames = 0;
      DateTime rateStart = DateTime.Now;

      while(running)
      {
        try
        {
          string key = null;

          lock(pendingKeys)
            if(pendingKeys.Count > 0)
              key = pendingKeys.Dequeue();

          if(key != null)
          {
            link.Command("KEY " + key);
            lastStatus = DateTime.MinValue;
            continue;
          }

          Wave w1 = link.Wave(1);
          Wave w2 = link.Wave(2);
          List<string> status = null;

          if((DateTime.Now - lastStatus).TotalMilliseconds > 400)
          {
            status = link.Command("STATUS?");
            lastStatus = DateTime.Now;
          }

          frames++;
          double seconds = (DateTime.Now - rateStart).TotalSeconds;
          if(seconds > 2)
          {
            SetStatus("Connected, " + (frames / seconds).ToString("0.0") + " updates per second");
            frames = 0;
            rateStart = DateTime.Now;
          }

          BeginInvoke(new Action(delegate { Show(w1, w2, status); }));
        }
        catch(Exception e)
        {
          SetStatus("Connection problem: " + e.Message);
          Thread.Sleep(300);
        }
      }
    }

    private void Show(Wave w1, Wave w2, List<string> status)
    {
      plot.Waves[0] = w1;
      plot.Waves[1] = w2;
      plot.Invalidate();

      Wave[] waves = { null, w1, w2 };
      for(int ch = 1; ch <= 2; ch++)
      {
        Wave w = waves[ch];
        scaleLabels[ch].SetText(PlotView.Format(w.VoltsPerDiv, w.Unit) + "/div");

        if(!w.Enabled)
        {
          measureLabels[ch].SetText("channel off");
          continue;
        }

        measureLabels[ch].SetText(
          "Vpp " + Pad(PlotView.Format(w.Max - w.Min, w.Unit)) + "  Freq " + (w.Frequency > 0 ? PlotView.Format(w.Frequency, "Hz") : "-") + "\n" +
          "Vavg " + Pad(PlotView.Format(w.Mean, w.Unit)) + " Vrms " + PlotView.Format(w.Rms, w.Unit));
      }

      if(status == null)
        return;

      foreach(string line in status)
      {
        if(line == "RUN" || line == "STOP")
        {
          scopeRunning = (line == "RUN");
          runButton.Text = scopeRunning ? "RUNNING" : "STOPPED";
          runButton.Fill = scopeRunning ? Theme.Good : Theme.Accent;
          runButton.Invalidate();
        }
        else if(line.StartsWith("TIMEBASE "))
        {
          timebaseLabel.SetText(line.Substring(9).Replace("us", " µs").Replace("ns", " ns").Replace("ms", " ms") + "/div");
        }
        else if(line.StartsWith("TRIGGER "))
        {
          triggerLabel.SetText(line.Substring(8));
        }
        else if(line.StartsWith("CH1 ") || line.StartsWith("CH2 "))
        {
          int ch = line[2] - '0';
          string[] parts = line.Split(' ');
          if(parts.Length > 4)
            couplingLabels[ch].SetText(parts[4] + ((line.Contains("inverted")) ? " inv" : "") + ((line.Contains("FFT")) ? " FFT" : ""));
        }
      }
    }

    private static string Pad(string text)
    {
      return text.PadRight(10);
    }

    // Connect, run for a few seconds and save the window as an image (for documentation)
    public void CaptureLive(string port, string file)
    {
      portName = port;
      ToggleConnection();

      System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
      timer.Interval = 4000;
      timer.Tick += delegate
      {
        timer.Stop();

        using(Bitmap bitmap = new Bitmap(Width, Height))
        {
          DrawToBitmap(bitmap, new Rectangle(0, 0, Width, Height));
          bitmap.Save(file, ImageFormat.Png);
        }

        if(running)
          Disconnect();

        Close();
      };
      timer.Start();
    }

    // Synthetic signals for a design preview without a scope
    public void ShowPreview(string file)
    {
      Wave[] waves = new Wave[3];

      for(int ch = 1; ch <= 2; ch++)
      {
        Wave w = new Wave();
        w.Channel = ch;
        w.Enabled = true;
        w.Count = 3000;
        w.Rate = 15e6;
        w.Trigger = 1500;
        w.VoltsPerDiv = (ch == 1) ? 0.5 : 0.2;
        w.ZeroDiv = (ch == 1) ? 1.5 : -1.5;
        w.Volts = new double[3000];
        Random random = new Random(ch);

        for(int i = 0; i < 3000; i++)
        {
          double t = (i - 1500) / 15e6;
          w.Volts[i] = (ch == 1) ? ((Math.Sin(2 * Math.PI * 30e3 * t) > 0) ? 1.0 : -1.0) + (random.NextDouble() - 0.5) * 0.04
                                 : 0.5 * Math.Sin(2 * Math.PI * 60e3 * t) + (random.NextDouble() - 0.5) * 0.02;
        }

        waves[ch] = w;
        typeof(ScopeLink).GetMethod("Measure", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static).Invoke(null, new object[] { w });
      }

      Show(waves[1], waves[2], new List<string>(new string[] { "RUN", "TIMEBASE 20us", "CH1 on 500mV/div V DC", "CH2 on 200mV/div V AC", "TRIGGER auto rising CH1" }));
      SetStatus("Connected, 18.5 updates per second");
      connectButton.Text = "Disconnect";
      Refresh();
      Application.DoEvents();

      using(Bitmap bitmap = new Bitmap(Width, Height))
      {
        DrawToBitmap(bitmap, new Rectangle(0, 0, Width, Height));
        bitmap.Save(file, ImageFormat.Png);
      }
    }

    //----------------------------------------------------------------------------------------------

    private void SaveScreenshot()
    {
      if(!link.IsOpen)
        return;

      SaveFileDialog dialog = new SaveFileDialog();
      dialog.Filter = "PNG image (*.png)|*.png";
      dialog.FileName = "x-scope-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".png";

      if(dialog.ShowDialog(this) != DialogResult.OK)
        return;

      try
      {
        byte[] pixels = link.Screen();

        using(Bitmap bitmap = new Bitmap(800, 480, PixelFormat.Format16bppRgb565))
        {
          BitmapData data = bitmap.LockBits(new Rectangle(0, 0, 800, 480), ImageLockMode.WriteOnly, bitmap.PixelFormat);
          for(int y = 0; y < 480; y++)
            Marshal.Copy(pixels, y * 1600, new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), 1600);
          bitmap.UnlockBits(data);
          bitmap.Save(dialog.FileName, ImageFormat.Png);
        }

        SetStatus("Saved " + Path.GetFileName(dialog.FileName));
      }
      catch(Exception e)
      {
        SetStatus("Screenshot failed: " + e.Message);
      }
    }

    private void SaveCsv()
    {
      Wave w1 = plot.Waves[0], w2 = plot.Waves[1];
      if((w1 == null) || (w2 == null))
        return;

      SaveFileDialog dialog = new SaveFileDialog();
      dialog.Filter = "CSV file (*.csv)|*.csv";
      dialog.FileName = "x-scope-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".csv";

      if(dialog.ShowDialog(this) != DialogResult.OK)
        return;

      CultureInfo c = CultureInfo.InvariantCulture;
      using(StreamWriter csv = new StreamWriter(dialog.FileName))
      {
        csv.WriteLine("index,time_s,ch1_" + w1.Unit + ",ch2_" + w2.Unit);

        int count = Math.Min(w1.Count, w2.Count);
        for(int i = 0; i < count; i++)
        {
          double t = (i - w1.Trigger) / w1.Rate;
          csv.WriteLine(i + "," + t.ToString("0.#########", c) + "," + w1.Volts[i].ToString("0.#####", c) + "," + w2.Volts[i].ToString("0.#####", c));
        }
      }

      SetStatus("Saved " + Path.GetFileName(dialog.FileName));
    }
  }

  internal static class Program
  {
    [STAThread]
    private static void Main(string[] args)
    {
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);

      // --preview <file.png>: draw the window with synthetic signals into an image and quit
      if((args.Length == 2) && (args[0] == "--preview"))
      {
        MainForm form = new MainForm();
        form.Opacity = 0.01;
        form.ShowInTaskbar = false;
        form.Shown += delegate
        {
          Application.DoEvents();
          form.ShowPreview(args[1]);
          form.Close();
        };
        Application.Run(form);
        return;
      }

      // --capture <port> <file.png>: connect to the scope, show it live for a few seconds, save the window
      if((args.Length == 3) && (args[0] == "--capture"))
      {
        MainForm form = new MainForm();
        form.Shown += delegate { form.CaptureLive(args[1], args[2]); };
        Application.Run(form);
        return;
      }

      Application.Run(new MainForm());
    }
  }
}
