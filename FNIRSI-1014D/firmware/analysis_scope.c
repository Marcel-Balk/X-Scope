//----------------------------------------------------------------------------------------------------------------------------------
//1014D glue for the signal analysis (see analysis_scope.h). Measurements, statistics and decoding run
//once per new capture from the acquisition; the panels are drawn into the trace frame.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "variables.h"
#include "fnirsi_101xd_scope.h"
#include "display_lib.h"
#include "scope_functions.h"
#include "ideal_1014d.h"
#include "analysis_1014d.h"
#include "analysis_scope.h"

#if PORT_1014D

//----------------------------------------------------------------------------------------------------------------------------------

ASCONFIG  as_config = { AS_DECODE_OFF, 0, 0, 0, 0, 1, 0, 3, AS_EXTRA_OFF };
ANMEASURE as_measure[2];
uint32    as_measure_captures;
ANDECODE  as_decode;
ANSTAT    as_stats[AS_STATS];

const uint8 as_extra_items[AS_EXTRA_ITEMS] =
{
  AN_RISE, AN_FALL, AN_OVERSHOOT, AN_UNDERSHOOT, AN_VRMS_AC, AN_VRMS, AN_PHASE, AN_DELAY
};

//What each statistic tracks (channel * 32 + measurement); a change resets that statistic
static uint32 as_stat_keys[AS_STATS];

//Screen row of the highest sample of the decoded line, for the labels above the trace
static uint32 as_decode_top_index;
static uint32 as_decode_count;

static const char *as_short_names[AN_MEASUREMENTS] =
{
  "Vmax", "Vmin", "Vavg", "Vrms", "Vpp", "Vp", "Freq", "Period", "+Width", "-Width", "+Duty", "-Duty",
  "Rise", "Fall", "Over", "Under", "VrmsAC", "Vtop", "Vbase", "Vamp", "Phase", "Delay"
};

//Volts per division on the 1x probe row in mV and the probe factor in tenths
static const uint32 as_mv_per_div[7]   = { 5000, 2500, 1000, 500, 200, 100, 50 };
static const uint32 as_probe_tenths[7] = { 5, 10, 100, 200, 500, 1000, 10000 };

//----------------------------------------------------------------------------------------------------------------------------------
//Settings

void as_sanitize(void)
{
  if(as_config.decode >= AS_DECODE_MODES)
  {
    as_config.decode = AS_DECODE_OFF;
  }

  if(as_config.uart_baud > AN_UART_BAUDS)
  {
    as_config.uart_baud = 0;
  }

  if(as_config.uart_format >= AS_UART_FORMATS)
  {
    as_config.uart_format = 0;
  }

  if(as_config.extra >= AS_EXTRA_CHOICES)
  {
    as_config.extra = AS_EXTRA_OFF;
  }

  if(as_config.stats_n >= AN_STAT_COUNTS)
  {
    as_config.stats_n = 3;
  }

  as_config.uart_idle_low &= 1;
  as_config.uart_source &= 1;
  as_config.event_list &= 1;
  as_config.stats &= 1;
}

//Three words: marker 1 with decode mode, format, idle, source, list and statistics on; marker 2 with
//the baud index and the statistics count index; marker 3 with the extra panel channel
uint16 as_settings_word(uint32 which)
{
  if(which == 0)
  {
    return(AS_SETTINGS_MARKER_1 | as_config.decode | (as_config.uart_format << 2) | (as_config.uart_idle_low << 4) |
           (as_config.uart_source << 5) | (as_config.event_list << 6) | (as_config.stats << 7));
  }

  if(which == 1)
  {
    return(AS_SETTINGS_MARKER_2 | (as_config.uart_baud & 0x0F) | ((as_config.stats_n & 0x07) << 4));
  }

  return(AS_SETTINGS_MARKER_3 | (as_config.extra & 0x03));
}

void as_settings_load(uint16 word1, uint16 word2, uint16 word3)
{
  if((word1 & 0xFF00) == AS_SETTINGS_MARKER_1)
  {
    as_config.decode        = word1 & 3;
    as_config.uart_format   = (word1 >> 2) & 3;
    as_config.uart_idle_low = (word1 >> 4) & 1;
    as_config.uart_source   = (word1 >> 5) & 1;
    as_config.event_list    = (word1 >> 6) & 1;
    as_config.stats         = (word1 >> 7) & 1;
  }

  if((word2 & 0xFF00) == AS_SETTINGS_MARKER_2)
  {
    as_config.uart_baud = word2 & 0x0F;
    as_config.stats_n   = (word2 >> 4) & 0x07;
  }

  if((word3 & 0xFF00) == AS_SETTINGS_MARKER_3)
  {
    as_config.extra = word3 & 0x03;
  }

  as_sanitize();
  as_reset_stats();
}

//----------------------------------------------------------------------------------------------------------------------------------
//Statistics

uint32 as_stat_channel(uint32 stat)
{
  if(stat < 6)
  {
    return(scopesettings.measurementitems[stat].channel & 1);
  }

  return((as_config.extra == 2) ? 1 : 0);
}

uint32 as_stat_measurement(uint32 stat)
{
  if(stat < 6)
  {
    uint32 index = scopesettings.measurementitems[stat].index;

    return((index < AN_SLOT_MEASUREMENTS) ? index : AN_VPP);
  }

  return(as_extra_items[(stat - 6) % AS_EXTRA_ITEMS]);
}

void as_reset_stats(void)
{
  uint32 i;

  as_sanitize();

  for(i=0;i<AS_STATS;i++)
  {
    an_stat_reset(&as_stats[i], an_stat_counts[as_config.stats_n]);
    as_stat_keys[i] = (as_stat_channel(i) * 32) + as_stat_measurement(i);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Code to volt mapping of the captured samples, the same as ideal_channel_mv: the capture range scales
//the samples and the FPGA offset trim is added back with the display range

void as_channel_scale(PCHANNELSETTINGS settings, double *volts_per_code, double *volts_offset)
{
  uint32 range = (settings->samplevoltperdiv < 7) ? settings->samplevoltperdiv : 6;
  uint32 mvdiv = (as_mv_per_div[range] * as_probe_tenths[settings->magnification % 7]) / 10;

  *volts_per_code = ((double)settings->input_calibration[range] * (double)mvdiv) / ((double)(50 << VOLTAGE_SHIFTER) * 1000.0);
  *volts_offset = 0.0;

  if(settings->dc_shift_size)
  {
    uint32 display = (settings->displayvoltperdiv < 7) ? settings->displayvoltperdiv : 6;
    uint32 displaymvdiv = (as_mv_per_div[display] * as_probe_tenths[settings->magnification % 7]) / 10;
    int32  pixels = (settings->dcoffset * 100) / settings->dc_shift_size;

    if(display == 6)
    {
      pixels *= 2;
    }

    *volts_offset = ((double)pixels * displaymvdiv) / 50000.0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

static uint32 as_sample_count(void)
{
  uint32 count = scopesettings.samplecount;

  if((count == 0) || (count > MAX_SAMPLE_BUFFER_SIZE))
  {
    count = 3000;
  }

  return(count);
}

static void as_measure_channel(PCHANNELSETTINGS settings, ANMEASURE *result, double rate)
{
  ANCHANNEL channel;

  if(settings->enable == 0)
  {
    result->valid = 0;
    result->nrising = 0;
    result->nfalling = 0;
    return;
  }

  channel.samples = settings->tracebuffer;
  channel.count = as_sample_count();
  as_channel_scale(settings, &channel.volts_per_code, &channel.volts_offset);

  an_measure(&channel, rate, result);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Measurements of the buffers as they are now, for the command channel (a stopped capture after a
//channel was switched on or off); statistics and decoding are left alone

void as_measure_now(void)
{
  double rate = (double)ideal_effective_sample_rate();

  if(scopesettings.long_mode)
  {
    as_measure[0].valid = 0;
    as_measure[1].valid = 0;
    return;
  }

  as_measure_channel(&scopesettings.channel1, &as_measure[0], rate);
  as_measure_channel(&scopesettings.channel2, &as_measure[1], rate);
  an_measure_pair(&as_measure[0], &as_measure[1], rate);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Called by the acquisition after a complete short time base capture

void as_capture_done(void)
{
  double rate = (double)ideal_effective_sample_rate();
  uint32 count = as_sample_count();
  uint32 i;

  //Statistics windows are set up by the settings load; a sector without them leaves them empty
  if(as_stats[0].window == 0)
  {
    as_reset_stats();
  }

  as_measure_channel(&scopesettings.channel1, &as_measure[0], rate);
  as_measure_channel(&scopesettings.channel2, &as_measure[1], rate);
  an_measure_pair(&as_measure[0], &as_measure[1], rate);
  as_measure_captures++;

  if(as_config.stats)
  {
    for(i=0;i<AS_STATS;i++)
    {
      uint32 channel = as_stat_channel(i);
      uint32 measurement = as_stat_measurement(i);
      uint32 key = (channel * 32) + measurement;

      if(key != as_stat_keys[i])
      {
        an_stat_reset(&as_stats[i], an_stat_counts[as_config.stats_n]);
        as_stat_keys[i] = key;
      }

      //Phase and delay are pair measurements, kept with channel 1
      if((measurement == AN_PHASE) || (measurement == AN_DELAY))
      {
        channel = 0;
      }

      if(as_measure[channel].valid & (1u << measurement))
      {
        an_stat_add(&as_stats[i], as_measure[channel].value[measurement]);
      }
    }
  }

  as_decode.count = 0;
  as_decode.truncated = 0;
  as_decode.status = AN_DECODE_NO_SIGNAL;
  as_decode_count = count;

  if(as_config.decode == AS_DECODE_UART)
  {
    PCHANNELSETTINGS settings = as_config.uart_source ? &scopesettings.channel2 : &scopesettings.channel1;

    if(settings->enable)
    {
      ANUARTCONFIG config;

      config.baud = as_config.uart_baud ? an_uart_bauds[as_config.uart_baud - 1] : 0;
      config.parity = as_config.uart_format;
      config.idle_high = as_config.uart_idle_low ? 0 : 1;

      an_decode_uart(settings->tracebuffer, count, rate, &config, &as_decode);
    }
  }
  else if(as_config.decode == AS_DECODE_I2C)
  {
    if(scopesettings.channel1.enable && scopesettings.channel2.enable)
    {
      an_decode_i2c(scopesettings.channel1.tracebuffer, scopesettings.channel2.tracebuffer, count, &as_decode);
    }
  }

  //The labels go above the highest point of the decoded line (SDA for I2C)
  if(as_config.decode != AS_DECODE_OFF)
  {
    PCHANNELSETTINGS settings = ((as_config.decode == AS_DECODE_UART) && as_config.uart_source) ? &scopesettings.channel2 : &scopesettings.channel1;
    uint8 highest = 0;

    as_decode_top_index = 0;

    for(i=0;i<count;i++)
    {
      if(settings->tracebuffer[i] > highest)
      {
        highest = settings->tracebuffer[i];
        as_decode_top_index = i;
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Drawing
//----------------------------------------------------------------------------------------------------------------------------------

#define AS_TRACE_LEFT     6
#define AS_TRACE_RIGHT  703
#define AS_TRACE_TOP     60
#define AS_TRACE_BOTTOM 456
#define AS_LINE          14

static char *as_hex(char *buffer, uint32 value, uint32 digits)
{
  while(digits--)
  {
    uint32 nibble = (value >> (digits * 4)) & 0x0F;

    *buffer++ = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
  }

  *buffer = 0;

  return(buffer);
}

static int32 as_sample_x(uint32 index)
{
  return((int32)(disp_xstart + (((double)index - disp_first_sample) * disp_xpos_per_sample)));
}

static void as_panel(uint32 x, uint32 y, uint32 width, uint32 lines)
{
  display_set_fg_color(COLOR_BLACK);
  display_fill_rect(x, y, width, (lines * AS_LINE) + 4);
  display_set_fg_color(COLOR_DARK_GREY_4);
  display_draw_rect(x, y, width, (lines * AS_LINE) + 4);
}

//Event label text: UART "41 A", I2C "S", "P", "50W", "12", a trailing ! for a NAK
static void as_event_text(const ANEVENT *event, char *text)
{
  char *ptr = text;

  switch(event->type)
  {
    case AN_EVENT_START:
      *ptr++ = 'S';
      break;

    case AN_EVENT_STOP:
      *ptr++ = 'P';
      break;

    case AN_EVENT_ADDRESS:
      ptr = as_hex(ptr, event->value, 2);
      *ptr++ = (event->flags & AN_FLAG_READ) ? 'R' : 'W';
      break;

    default:
      ptr = as_hex(ptr, event->value, 2);

      if((event->type == AN_EVENT_BYTE) && (event->value >= 0x20) && (event->value < 0x7F))
      {
        *ptr++ = ' ';
        *ptr++ = (char)event->value;
      }
      break;
  }

  if(event->flags & AN_FLAG_NAK)
  {
    *ptr++ = '!';
  }

  *ptr = 0;
}

static uint32 as_event_color(const ANEVENT *event)
{
  if(event->flags & (AN_FLAG_PARITY | AN_FLAG_FRAMING | AN_FLAG_PARTIAL))
  {
    return(COLOR_RED);
  }

  switch(event->type)
  {
    case AN_EVENT_START:   return(COLOR_GREEN);
    case AN_EVENT_STOP:    return(COLOR_RED);
    case AN_EVENT_ADDRESS: return(COLOR_YELLOW);
    default:               return(COLOR_WHITE);
  }
}

static const char *as_decode_status_text(void)
{
  switch(as_decode.status)
  {
    case AN_DECODE_NO_SIGNAL: return((as_config.decode == AS_DECODE_I2C) ? "no signal (CH1 SDA, CH2 SCL)" : "no signal");
    case AN_DECODE_TOO_SLOW:  return("too few samples: faster time base");
    case AN_DECODE_NO_BAUD:   return("no baud rate found");
  }

  return(0);
}

static void as_draw_labels(void)
{
  PCHANNELSETTINGS settings = ((as_config.decode == AS_DECODE_UART) && as_config.uart_source) ? &scopesettings.channel2 : &scopesettings.channel1;
  int32  y, x1, x2;
  uint32 i, width;
  char   text[12];

  if((as_decode.count == 0) || scopesettings.xymodedisplay || (as_decode_top_index >= as_decode_count))
  {
    return;
  }

  y = scope_get_y_sample(settings, as_decode_top_index) - 18;

  if(y < AS_TRACE_TOP)
  {
    y = AS_TRACE_TOP;
  }
  else if(y > (AS_TRACE_BOTTOM - 16))
  {
    y = AS_TRACE_BOTTOM - 16;
  }

  display_set_font(&font_1);

  for(i=0;i<as_decode.count;i++)
  {
    const ANEVENT *event = &as_decode.events[i];

    x1 = as_sample_x(event->start);
    x2 = as_sample_x(event->end);

    if((x2 < AS_TRACE_LEFT) || (x1 > AS_TRACE_RIGHT))
    {
      continue;
    }

    if(x1 < AS_TRACE_LEFT)
    {
      x1 = AS_TRACE_LEFT;
    }

    if(x2 > AS_TRACE_RIGHT)
    {
      x2 = AS_TRACE_RIGHT;
    }

    as_event_text(event, text);
    display_set_fg_color(as_event_color(event));

    if((event->type == AN_EVENT_START) || (event->type == AN_EVENT_STOP))
    {
      display_draw_vert_line(x1, y, y + 14);
      display_text(x1 + 2, y + 1, text);
      continue;
    }

    //Bus style box over the byte, with its value when it fits
    if(x2 > (x1 + 2))
    {
      display_draw_rect(x1, y, x2 - x1, 15);
    }

    width = calc_vw_string_width(text);

    if((uint32)(x2 - x1) >= (width + 4))
    {
      display_text(x1 + ((x2 - x1 - width) / 2), y + 1, text);
    }
  }
}

//Event list in the right side of the trace area, starting at y
static void as_draw_event_list(uint32 y)
{
  static const char *formats[AS_UART_FORMATS] = { " 8N1", " 8E1", " 8O1" };
  const char *status = as_decode_status_text();
  double rate = (double)ideal_effective_sample_rate();
  uint32 lines = (AS_TRACE_BOTTOM - y - 8) / AS_LINE;
  uint32 shown, i;
  char   text[48];
  char  *ptr;

  if(lines < 2)
  {
    return;
  }

  shown = as_decode.count;

  if(shown > (lines - 1))
  {
    shown = lines - 1;
  }

  as_panel(500, y, 200, (status ? 2 : shown + 1));

  display_set_font(&font_1);
  display_set_fg_color(COLOR_CYAN);

  if(as_config.decode == AS_DECODE_UART)
  {
    ptr = ideal_append_text(text, "UART ");
    ptr = ideal_append_int(ptr, as_decode.baud ? as_decode.baud : (as_config.uart_baud ? an_uart_bauds[as_config.uart_baud - 1] : 0));
    ptr = ideal_append_text(ptr, formats[as_config.uart_format % AS_UART_FORMATS]);
    ptr = ideal_append_text(ptr, as_config.uart_source ? " CH2" : " CH1");
  }
  else
  {
    ptr = ideal_append_text(text, "I2C  SDA CH1, SCL CH2");
  }

  if(as_decode.truncated)
  {
    ideal_append_text(ptr, " +");
  }

  display_text(504, y + 3, text);

  if(status)
  {
    display_set_fg_color(COLOR_RED);
    display_text(504, y + 3 + AS_LINE, status);
    return;
  }

  for(i=0;i<shown;i++)
  {
    const ANEVENT *event = &as_decode.events[i];
    char value[12];

    ptr = an_format_si(text, ((double)event->start - (double)disp_trigger_index) / rate, "s");

    while((ptr - text) < 10)
    {
      *ptr++ = ' ';
    }

    as_event_text(event, value);
    ptr = ideal_append_text(ptr, (event->type == AN_EVENT_ADDRESS) ? "addr " : (event->type == AN_EVENT_START) ? "start" : (event->type == AN_EVENT_STOP) ? "stop" : "");
    ptr = ideal_append_text(ptr, " ");
    ptr = ideal_append_text(ptr, value);

    if(event->flags & AN_FLAG_PARITY)
    {
      ptr = ideal_append_text(ptr, " parity");
    }

    if(event->flags & AN_FLAG_FRAMING)
    {
      ideal_append_text(ptr, " framing");
    }

    display_set_fg_color(as_event_color(event));
    display_text(504, y + 3 + ((i + 1) * AS_LINE), text);
  }
}

//Unit of a measurement for a channel shown in ampere
static const char *as_unit(uint32 measurement, uint32 channel)
{
  const char *unit = an_measure_unit(measurement);
  PCHANNELSETTINGS settings = channel ? &scopesettings.channel2 : &scopesettings.channel1;

  if((unit[0] == 'V') && settings->V_A)
  {
    return("A");
  }

  return(unit);
}

static char *as_value_text(char *buffer, uint32 valid, double value, uint32 measurement, uint32 channel)
{
  if(!valid)
  {
    return(ideal_append_text(buffer, "- - -"));
  }

  return(an_format_si(buffer, value, as_unit(measurement, channel)));
}

//Extra measurements of one channel in the top right corner; returns the y below the panel
static uint32 as_draw_extra(void)
{
  uint32 channel = (as_config.extra == 2) ? 1 : 0;
  const ANMEASURE *m = &as_measure[channel];
  uint32 i, y = 62;
  char   text[40];
  char  *ptr;

  as_panel(560, y, 140, AS_EXTRA_ITEMS + 1);

  display_set_font(&font_1);
  display_set_fg_color(channel ? CHANNEL2_COLOR : CHANNEL1_COLOR);
  display_text(564, y + 3, channel ? "CH2 measurements" : "CH1 measurements");

  for(i=0;i<AS_EXTRA_ITEMS;i++)
  {
    uint32 measurement = as_extra_items[i];
    const ANMEASURE *source = ((measurement == AN_PHASE) || (measurement == AN_DELAY)) ? &as_measure[0] : m;

    display_set_fg_color(COLOR_GREY);
    display_text(564, y + 3 + ((i + 1) * AS_LINE), (measurement == AN_PHASE) ? "Phase 1-2" : (measurement == AN_DELAY) ? "Delay 1-2" : as_short_names[measurement]);

    ptr = as_value_text(text, (source->valid >> measurement) & 1, source->value[measurement], measurement, channel);
    *ptr = 0;

    display_set_fg_color(COLOR_WHITE);
    display_right_aligned_text(696, y + 3 + ((i + 1) * AS_LINE), text);
  }

  return(y + ((AS_EXTRA_ITEMS + 1) * AS_LINE) + 8);
}

//Statistics table in the bottom left corner
static void as_draw_stats(void)
{
  static const char *headers[5] = { "last", "mean", "min", "max", "std dev" };
  uint32 rows = 6 + ((as_config.extra != AS_EXTRA_OFF) ? AS_EXTRA_ITEMS : 0);
  uint32 y = 436 - ((rows + 1) * AS_LINE) - 4;
  uint32 i, c;
  char   text[40];
  char  *ptr;

  as_panel(8, y, 440, rows + 1);

  display_set_font(&font_1);
  display_set_fg_color(COLOR_CYAN);

  ptr = ideal_append_text(text, "Stats n=");
  ptr = ideal_append_int(ptr, as_stats[0].count);
  ptr = ideal_append_text(ptr, "/");
  *ideal_append_int(ptr, an_stat_counts[as_config.stats_n]) = 0;
  display_text(12, y + 3, text);

  for(c=0;c<5;c++)
  {
    display_right_aligned_text(150 + (c * 72), y + 3, headers[c]);
  }

  for(i=0;i<rows;i++)
  {
    const ANSTAT *stat = &as_stats[i];
    uint32 channel = as_stat_channel(i);
    uint32 measurement = as_stat_measurement(i);
    uint32 row = y + 3 + ((i + 1) * AS_LINE);
    double values[5];

    if((measurement == AN_PHASE) || (measurement == AN_DELAY))
    {
      display_set_fg_color(COLOR_WHITE);
      display_text(12, row, (measurement == AN_PHASE) ? "Phase 1-2" : "Delay 1-2");
    }
    else
    {
      display_set_fg_color(channel ? CHANNEL2_COLOR : CHANNEL1_COLOR);
      ptr = ideal_append_text(text, channel ? "2 " : "1 ");
      ideal_append_text(ptr, as_short_names[measurement]);
      display_text(12, row, text);
    }

    values[0] = stat->last;
    values[1] = an_stat_mean(stat);
    values[2] = stat->min;
    values[3] = stat->max;
    values[4] = an_stat_stddev(stat);

    display_set_fg_color(COLOR_WHITE);

    for(c=0;c<5;c++)
    {
      ptr = as_value_text(text, stat->count != 0, values[c], measurement, channel);
      *ptr = 0;
      display_right_aligned_text(150 + (c * 72), row, text);
    }
  }
}

//Called from the trace frame drawing, into the trace buffer
void as_draw(void)
{
  uint32 y = 62;

  if(scopesettings.long_mode)
  {
    return;
  }

  if(as_config.decode != AS_DECODE_OFF)
  {
    as_draw_labels();
  }

  if(as_config.extra != AS_EXTRA_OFF)
  {
    y = as_draw_extra();
  }

  if((as_config.decode != AS_DECODE_OFF) && as_config.event_list)
  {
    as_draw_event_list(y);
  }

  if(as_config.stats)
  {
    as_draw_stats();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

#endif //PORT_1014D
