//----------------------------------------------------------------------------------------------------------------------------------
//
//remote.c hands every line that is not one of its own commands to scpi_execute from the main loop, so
//the commands run between acquisition and drawing like front panel input. A line can hold several
//commands separated by ';'. Every command answers with value lines (queries), "OK" (settings) or
//"#ERR <code>,<text>"; the line is closed with "#END". A rejected command changes nothing: values are
//checked against the same tables the menus use before anything is written.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "variables.h"
#include "statemachine.h"
#include "scope_functions.h"
#include "fpga_control.h"
#include "menu_1014d.h"
#include "ideal_1014d.h"
#include "menu_ideal.h"
#include "analysis_1014d.h"
#include "analysis_scope.h"
#include "remote.h"
#include "scpi.h"
#include "timer.h"

#if PORT_1014D

//----------------------------------------------------------------------------------------------------------------------------------

#define SCPI_MAX_NODES        5

typedef struct
{
  const char *text;
  uint32      length;
  int32       suffix;                     //Number after the keyword, -1 when there is none
} SCPINODE;

static SCPINODE    scpi_nodes[SCPI_MAX_NODES];
static uint32      scpi_nodecount;
static uint32      scpi_query;
static const char *scpi_args;

//Last error for SYSTem:ERRor?
static int32       scpi_error_code;
static const char *scpi_error_text = "No error";

//Set when a command sent its own #END (the binary and bulk data replies of remote.c)
static uint32      scpi_ended;

//Volts per division on the 1x probe row in mV, probe factors in tenths, seconds per division
static const uint32 scpi_mv_per_div[7]   = { 5000, 2500, 1000, 500, 200, 100, 50 };
static const uint32 scpi_probe_tenths[7] = { 5, 10, 100, 200, 500, 1000, 10000 };

static const double scpi_time_per_div[35] =
{
  50.0, 20.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02,
  0.2, 0.1, 0.05, 0.02, 0.01, 5e-3, 2e-3, 1e-3, 5e-4, 2e-4, 1e-4, 5e-5, 2e-5, 1e-5,
  5e-6, 2e-6, 1e-6, 5e-7, 2e-7, 1e-7, 5e-8, 2e-8, 1e-8, 5e-9
};

static const char *scpi_trace_names[IDEAL_TRACE_STYLES] = { "LINear", "DOTS", "PEAK", "PRO", "SINC" };
static const char *scpi_math_names[IDEAL_MATH_MODES]    = { "OFF", "ADD", "SUBtract", "MULtiply", "DIVide" };
static const char *scpi_source_names[IDEAL_MATH_SOURCES] = { "CHAN1", "CHAN2", "REF1", "REF2" };
static const char *scpi_mode_names[3]                    = { "AUTO", "SINGle", "NORMal" };
static const char *scpi_format_names[AS_UART_FORMATS]    = { "8N1", "8E1", "8O1" };
static const char *scpi_decode_names[AS_DECODE_MODES]    = { "OFF", "UART", "I2C" };

//----------------------------------------------------------------------------------------------------------------------------------
//Replies

static void scpi_error(int32 code, const char *text)
{
  char  line[24];
  char *ptr;

  scpi_error_code = code;
  scpi_error_text = text;

  //The texts are constants of any length, so they are sent in parts instead of being copied
  ptr = ideal_append_text(line, "#ERR ");
  ptr = ideal_append_int(ptr, code);
  ideal_append_text(ptr, ",");

  remote_text(line);
  remote_text(text);
  remote_text("\n");
}

static void scpi_ok(void)
{
  remote_text("OK\n");
}

static void scpi_reply(const char *text)
{
  char  line[64];

  *ideal_append_text(ideal_append_text(line, text), "\n") = 0;
  remote_text(line);
}

static void scpi_reply_number(double value)
{
  char line[32];

  *ideal_append_text(an_format_double(line, value, 6), "\n") = 0;
  remote_text(line);
}

static void scpi_reply_int(int32 value)
{
  char line[16];

  *ideal_append_text(ideal_append_int(line, value), "\n") = 0;
  remote_text(line);
}

static void scpi_reply_bool(uint32 value)
{
  remote_text(value ? "ON\n" : "OFF\n");
}

//The short form of a keyword pattern ("SINGle" gives "SING")
static char *scpi_append_short(char *buffer, const char *pattern)
{
  while(*pattern && (((*pattern >= 'A') && (*pattern <= 'Z')) || ((*pattern >= '0') && (*pattern <= '9'))))
  {
    *buffer++ = *pattern++;
  }

  *buffer = 0;

  return(buffer);
}

static void scpi_reply_keyword(const char *pattern)
{
  char line[24];

  *ideal_append_text(scpi_append_short(line, pattern), "\n") = 0;
  remote_text(line);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Parsing

static uint32 scpi_is_letter(char c)
{
  return(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '*') || (c == '_'));
}

static uint32 scpi_parse_header(const char *ptr)
{
  scpi_nodecount = 0;
  scpi_query = 0;

  if(*ptr == ':')
  {
    ptr++;
  }

  while(1)
  {
    const char *start = ptr;
    int32 suffix = -1;

    while(scpi_is_letter(*ptr))
    {
      ptr++;
    }

    if((ptr == start) || (scpi_nodecount >= SCPI_MAX_NODES))
    {
      return(0);
    }

    //The keyword without its number
    scpi_nodes[scpi_nodecount].length = (uint32)(ptr - start);

    if((*ptr >= '0') && (*ptr <= '9'))
    {
      suffix = 0;

      while((*ptr >= '0') && (*ptr <= '9') && (suffix < 1000))
      {
        suffix = (suffix * 10) + (*ptr++ - '0');
      }
    }

    scpi_nodes[scpi_nodecount].text = start;
    scpi_nodes[scpi_nodecount].suffix = suffix;
    scpi_nodecount++;

    if(*ptr != ':')
    {
      break;
    }

    ptr++;
  }

  if(*ptr == '?')
  {
    scpi_query = 1;
    ptr++;
  }

  if((*ptr != 0) && (*ptr != ' '))
  {
    return(0);
  }

  while(*ptr == ' ')
  {
    ptr++;
  }

  scpi_args = ptr;

  return(1);
}

static uint32 scpi_node(uint32 index, const char *pattern)
{
  return((index < scpi_nodecount) && an_mnemonic(pattern, scpi_nodes[index].text, scpi_nodes[index].length));
}

//Header is exactly these nodes
static uint32 scpi_header(uint32 count, const char *a, const char *b, const char *c)
{
  if(scpi_nodecount != count)
  {
    return(0);
  }

  return(scpi_node(0, a) && ((count < 2) || scpi_node(1, b)) && ((count < 3) || scpi_node(2, c)));
}

//The current argument word and its length (up to a comma, a space or the end)
static uint32 scpi_word_length(const char *text)
{
  uint32 length = 0;

  while(text[length] && (text[length] != ',') && (text[length] != ' '))
  {
    length++;
  }

  return(length);
}

//Only spaces may follow
static uint32 scpi_at_end(const char *text)
{
  while(*text == ' ')
  {
    text++;
  }

  return(*text == 0);
}

static uint32 scpi_word_is(const char *text, const char *pattern)
{
  return(an_mnemonic(pattern, text, scpi_word_length(text)));
}

//Index of the argument word in a pattern list, -1 when not found
static int32 scpi_choice(const char *text, const char **patterns, uint32 count)
{
  uint32 i;

  if(!scpi_at_end(text + scpi_word_length(text)))
  {
    return(-1);
  }

  for(i=0;i<count;i++)
  {
    if(scpi_word_is(text, patterns[i]))
    {
      return(i);
    }
  }

  return(-1);
}

static int32 scpi_bool(const char *text)
{
  static const char *words[4] = { "OFF", "ON", "0", "1" };
  int32 choice = scpi_choice(text, words, 4);

  return((choice < 0) ? -1 : (choice & 1));
}

//CHANnel1, CHAN2, CH1, 1, 2; returns 0 or 1, -1 when it is not a channel. The pointer moves past it.
static int32 scpi_channel_word(const char **text)
{
  const char *ptr = *text;
  uint32 length = 0;

  while(*ptr == ' ')
  {
    ptr++;
  }

  while(((ptr[length] >= 'a') && (ptr[length] <= 'z')) || ((ptr[length] >= 'A') && (ptr[length] <= 'Z')))
  {
    length++;
  }

  if(length && !an_mnemonic("CHANnel", ptr, length) && !an_mnemonic("CH", ptr, length))
  {
    return(-1);
  }

  ptr += length;

  if(((*ptr != '1') && (*ptr != '2')) || ((ptr[1] >= '0') && (ptr[1] <= '9')))
  {
    return(-1);
  }

  *text = ptr + 1;

  return(*ptr - '1');
}

static uint32 scpi_number(const char *text, double *value)
{
  if(!an_parse_double(&text, value))
  {
    return(0);
  }

  return(scpi_at_end(text));
}

//Within 2 % of the expected value (the tables hold exact 1-2-5 values)
static uint32 scpi_close(double value, double expected)
{
  double difference = value - expected;

  if(difference < 0.0)
  {
    difference = -difference;
  }

  if(expected < 0.0)
  {
    expected = -expected;
  }

  return(difference <= (expected * 0.02));
}

//----------------------------------------------------------------------------------------------------------------------------------
//Settings may only change on the live trace screen. Open menus are closed like their own key would;
//a saved picture or waveform on the screen blocks changes.

static uint32 scpi_ready(void)
{
  if(scopesettings.waveviewmode || (viewactive != VIEW_NOT_ACTIVE) ||
     (buttondialstate == BUTTON_DIAL_FILE_VIEW_HANDLING) || (buttondialstate == BUTTON_DIAL_PICTURE_VIEW_HANDLING) ||
     (buttondialstate == BUTTON_DIAL_WAVE_VIEW_HANDLING))
  {
    scpi_error(-221, "Settings conflict;a saved file is shown, close it first");
    return(0);
  }

  if((buttondialstate == BUTTON_DIAL_MENU_HANDLING) || (buttondialstate == BUTTON_DIAL_MEASUREMENTS_MENU_HANDLING) ||
     (buttondialstate == BUTTON_DIAL_CHANNEL_MENU_HANDLING))
  {
    sm_close_menu();
  }

  return(1);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Scales

static double scpi_volts_per_div(PCHANNELSETTINGS settings, uint32 index)
{
  return(((double)scpi_mv_per_div[index % 7] * scpi_probe_tenths[settings->magnification % 7]) / 10000.0);
}

static double scpi_channel_scale(PCHANNELSETTINGS settings)
{
  return(scpi_volts_per_div(settings, settings->displayvoltperdiv));
}

static PCHANNELSETTINGS scpi_channel(uint32 channel)
{
  return(channel ? &scopesettings.channel2 : &scopesettings.channel1);
}

static uint32 scpi_is_trigger_channel(PCHANNELSETTINGS settings)
{
  return(((settings == &scopesettings.channel1) && (scopesettings.triggerchannel == 0)) ||
         ((settings == &scopesettings.channel2) && (scopesettings.triggerchannel == 1)));
}

//Vertical position in divisions from the center (50 pixels per division)
static double scpi_position_div(PCHANNELSETTINGS settings)
{
  return(((double)settings->traceposition - VERTICAL_POINTER_CENTER) / 50.0);
}

static double scpi_trigger_level(void)
{
  PCHANNELSETTINGS settings = scpi_channel(scopesettings.triggerchannel);

  return((((double)scopesettings.triggerverticalposition - settings->traceposition) / 50.0) * scpi_channel_scale(settings));
}

static double scpi_time_position(void)
{
  return((((double)scopesettings.triggerhorizontalposition - TRACE_HORIZONTAL_CENTER) / 50.0) * scpi_time_per_div[scopesettings.timeperdiv % 35]);
}

static int32 scpi_round(double value)
{
  return((value < 0.0) ? (int32)(value - 0.5) : (int32)(value + 0.5));
}

//----------------------------------------------------------------------------------------------------------------------------------
//Setters with the side effects of the front panel paths

static void scpi_set_channel_position(PCHANNELSETTINGS settings, int32 position)
{
  int32 delta;

  if(position < VERTICAL_POINTER_POS_MIN)
  {
    position = VERTICAL_POINTER_POS_MIN;
  }
  else if(position > VERTICAL_POINTER_POS_MAX)
  {
    position = VERTICAL_POINTER_POS_MAX;
  }

  delta = position - (int32)settings->traceposition;
  settings->traceposition = position;

  ui_display_channel_position(settings);

  //The trigger level pointer moves with the trace of the trigger channel (sm_set_channel_position)
  if(delta && scpi_is_trigger_channel(settings))
  {
    scopesettings.triggerverticalposition += delta;
    ui_display_trigger_vertical_position();
  }
}

static void scpi_set_coupling(PCHANNELSETTINGS settings, uint32 ac)
{
  if(settings->coupling == ac)
  {
    return;
  }

  //Same side effects as the channel menu: AC drops the DC offset trim, then push it all
  settings->coupling = ac;

  if(settings->coupling)
  {
    settings->dcoffset = 0;
  }

  fpga_set_channel_coupling(settings);
  fpga_set_channel_offset(settings);

  if(scpi_is_trigger_channel(settings))
  {
    fpga_set_trigger_level();
  }

  ui_display_channel_settings(settings);
}

static void scpi_set_trigger_mode(uint32 mode)
{
  scopesettings.triggermode = mode;
  ui_display_trigger_mode();
  fpga_set_trigger_mode();
}

static void scpi_run(void)
{
  if(scopesettings.runstate != RUN_STATE_RUNNING)
  {
    scopesettings.runstate = RUN_STATE_RUNNING;
    sm_apply_settings_on_run();
    ui_display_run_stop_text();
  }
}

static void scpi_set_xy(uint32 on)
{
  if((scopesettings.xymodedisplay ? 1 : 0) == on)
  {
    return;
  }

  //sm_check_display_mode_change does the switch when it sees the X-Y menu item
  scopesettings.tracedisplaymode = on ? DISPLAY_MODE_X_Y : DISPLAY_MODE_NORMAL;
  menuitem = MAIN_MENU_XY_MODE;
  sm_check_display_mode_change();
}

//----------------------------------------------------------------------------------------------------------------------------------
//CHANnel<n>:...

static void scpi_channel_command(void)
{
  PCHANNELSETTINGS settings;
  int32  choice;
  double value;
  uint32 i;

  if((scpi_nodes[0].suffix != 1) && (scpi_nodes[0].suffix != 2))
  {
    scpi_error(-114, "Header suffix out of range;use CHANnel1 or CHANnel2");
    return;
  }

  settings = scpi_channel(scpi_nodes[0].suffix - 1);

  if(scpi_nodecount != 2)
  {
    scpi_error(-113, "Undefined header");
    return;
  }

  if(scpi_node(1, "DISPlay"))
  {
    if(scpi_query)
    {
      scpi_reply_bool(settings->enable);
    }
    else if((choice = scpi_bool(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use ON or OFF");
    }
    else if(scpi_ready())
    {
      if(settings->enable != (uint32)choice)
      {
        sm_toggle_channel_enable(settings);
      }

      scpi_ok();
    }
  }
  else if(scpi_node(1, "COUPling"))
  {
    static const char *words[2] = { "DC", "AC" };

    if(scpi_query)
    {
      scpi_reply(words[settings->coupling & 1]);
    }
    else if((choice = scpi_choice(scpi_args, words, 2)) < 0)
    {
      scpi_error(-104, "Data type error;use AC or DC");
    }
    else if(scpi_ready())
    {
      scpi_set_coupling(settings, choice);
      scpi_ok();
    }
  }
  else if(scpi_node(1, "PROBe"))
  {
    static const double ratios[7] = { 0.5, 1.0, 10.0, 20.0, 50.0, 100.0, 1000.0 };

    if(scpi_query)
    {
      scpi_reply_number(ratios[settings->magnification % 7]);
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;use 0.5, 1, 10, 20, 50, 100 or 1000");
      return;
    }

    for(i=0;(i<7) && !scpi_close(value, ratios[i]);i++);

    if(i == 7)
    {
      scpi_error(-222, "Data out of range;use 0.5, 1, 10, 20, 50, 100 or 1000");
    }
    else if(scpi_ready())
    {
      settings->magnification = i;
      ui_display_channel_settings(settings);

      if(scpi_is_trigger_channel(settings))
      {
        ui_display_trigger_vertical_position();
      }

      scpi_ok();
    }
  }
  else if(scpi_node(1, "SCALe"))
  {
    if(scpi_query)
    {
      scpi_reply_number(scpi_channel_scale(settings));
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;volts per division expected");
      return;
    }

    for(i=0;(i<7) && !scpi_close(value, scpi_volts_per_div(settings, i));i++);

    if(i == 7)
    {
      scpi_error(-222, "Data out of range;not a volts per division step of this probe");
    }
    else if(scpi_ready())
    {
      setvalue = (int8)((int32)i - (int32)settings->displayvoltperdiv);

      if(setvalue)
      {
        sm_set_channel_sensitivity(settings);
      }

      scpi_ok();
    }
  }
  else if(scpi_node(1, "POSition") || scpi_node(1, "OFFSet"))
  {
    uint32 offset = scpi_node(1, "OFFSet");
    double scale = offset ? scpi_channel_scale(settings) : 1.0;

    if(scpi_query)
    {
      scpi_reply_number(scpi_position_div(settings) * scale);
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, offset ? "Data type error;volts expected" : "Data type error;divisions expected");
      return;
    }

    value /= scale;

    if((value < -4.0) || (value > 4.0))
    {
      scpi_error(-222, "Data out of range;the position is -4 to 4 divisions");
    }
    else if(scpi_ready())
    {
      scpi_set_channel_position(settings, VERTICAL_POINTER_CENTER + scpi_round(value * 50.0));
      scpi_ok();
    }
  }
  else if(scpi_node(1, "UNIT"))
  {
    static const char *words[2] = { "VOLT", "AMPere" };
    static const char *letters[2] = { "V", "A" };

    if(scpi_query)
    {
      scpi_reply(letters[settings->V_A & 1]);
    }
    else if(((choice = scpi_choice(scpi_args, words, 2)) < 0) && ((choice = scpi_choice(scpi_args, letters, 2)) < 0))
    {
      scpi_error(-104, "Data type error;use V or A");
    }
    else if(scpi_ready())
    {
      settings->V_A = choice;
      ui_display_channel_settings(settings);
      scpi_ok();
    }
  }
  else if(scpi_node(1, "INVert") || scpi_node(1, "FFT"))
  {
    uint8 *flag = scpi_node(1, "FFT") ? &settings->fftenable : &settings->invert;

    if(scpi_query)
    {
      scpi_reply_bool(*flag);
    }
    else if((choice = scpi_bool(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use ON or OFF");
    }
    else if(scpi_ready())
    {
      *flag = choice;
      scpi_ok();
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//TIMebase:...

static void scpi_timebase_command(void)
{
  double value;
  int32  i;

  if(scpi_header(2, "TIMebase", "SCALe", 0))
  {
    if(scpi_query)
    {
      scpi_reply_number(scpi_time_per_div[scopesettings.timeperdiv % 35]);
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;seconds per division expected");
      return;
    }

    //200 ms to 5 ns in sweep mode, 500 ms to 50 s in roll mode (the front panel skips the roll copies
    //of 200 ms to 20 ms)
    for(i=11;(i<35) && !scpi_close(value, scpi_time_per_div[i]);i++);

    if(i == 35)
    {
      for(i=0;(i<7) && !scpi_close(value, scpi_time_per_div[i]);i++);

      if(i == 7)
      {
        scpi_error(-222, "Data out of range;use a 1-2-5 step from 5 ns to 50 s");
        return;
      }
    }

    if(scpi_ready())
    {
      setvalue = (int8)(i - (int32)scopesettings.timeperdiv);

      if(setvalue)
      {
        sm_set_time_base();
      }

      scpi_ok();
    }
  }
  else if(scpi_header(2, "TIMebase", "POSition", 0))
  {
    if(scpi_query)
    {
      scpi_reply_number(scpi_time_position());
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;seconds expected");
      return;
    }

    //Positions beyond the record are limited to it, like turning the knob further
    value = (value / scpi_time_per_div[scopesettings.timeperdiv % 35]) * 50.0;

    if(value < -16000.0)
    {
      value = -16000.0;
    }
    else if(value > 16000.0)
    {
      value = 16000.0;
    }

    if(scpi_ready())
    {
      //sm_set_trigger_position limits it to the record and shows it
      scopesettings.triggerhorizontalposition = TRACE_HORIZONTAL_CENTER + scpi_round(value);
      speedvalue = 0;
      sm_set_trigger_position();
      scpi_ok();
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//TRIGger:...

static void scpi_trigger_command(void)
{
  int32  choice;
  double value;

  if(scpi_header(2, "TRIGger", "MODE", 0))
  {
    if(scpi_query)
    {
      scpi_reply_keyword(scpi_mode_names[scopesettings.triggermode % 3]);
    }
    else if((choice = scpi_choice(scpi_args, scpi_mode_names, 3)) < 0)
    {
      scpi_error(-104, "Data type error;use AUTO, NORMal or SINGle");
    }
    else if(scpi_ready())
    {
      scpi_set_trigger_mode(choice);
      scpi_ok();
    }
  }
  else if(scpi_header(2, "TRIGger", "SLOPe", 0) || scpi_header(3, "TRIGger", "EDGE", "SLOPe") || scpi_header(2, "TRIGger", "EDGE", 0))
  {
    static const char *words[4] = { "RISing", "FALLing", "POSitive", "NEGative" };

    if(scpi_query)
    {
      scpi_reply_keyword(words[scopesettings.triggeredge & 1]);
    }
    else if((choice = scpi_choice(scpi_args, words, 4)) < 0)
    {
      scpi_error(-104, "Data type error;use RISing or FALLing");
    }
    else if(scpi_ready())
    {
      scopesettings.triggeredge = choice & 1;
      ui_display_trigger_edge();
      fpga_set_trigger_edge();
      scpi_ok();
    }
  }
  else if(scpi_header(2, "TRIGger", "SOURce", 0))
  {
    const char *ptr = scpi_args;

    if(scpi_query)
    {
      scpi_reply(scopesettings.triggerchannel ? "CHAN2" : "CHAN1");
    }
    else if(((choice = scpi_channel_word(&ptr)) < 0) || !scpi_at_end(ptr))
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
    }
    else if(scpi_ready())
    {
      if(scopesettings.triggerchannel != (uint32)choice)
      {
        //Same steps as the TRIG CH key
        scopesettings.triggerchannel = choice;
        ui_display_trigger_channel();
        fpga_set_trigger_channel();
        scope_calculate_trigger_vertical_position();
        fpga_set_trigger_level();
      }

      scpi_ok();
    }
  }
  else if(scpi_header(2, "TRIGger", "LEVel", 0))
  {
    PCHANNELSETTINGS settings = scpi_channel(scopesettings.triggerchannel);

    if(scpi_query)
    {
      scpi_reply_number(scpi_trigger_level());
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;volts expected");
      return;
    }

    value = (value / scpi_channel_scale(settings)) * 50.0;

    if((value < -1000.0) || (value > 1000.0))
    {
      scpi_error(-222, "Data out of range");
    }
    else if(scpi_ready())
    {
      //sm_set_trigger_level limits the level to the ADC range and sends it to the FPGA
      scopesettings.triggerverticalposition = settings->traceposition + scpi_round(value);
      speedvalue = 0;
      sm_set_trigger_level();
      scpi_ok();
    }
  }
  else if(scpi_header(2, "TRIGger", "FIFTy", 0) && !scpi_query)
  {
    if(scpi_ready())
    {
      scope_do_50_percent_trigger_setup();
      fpga_set_trigger_level();
      ui_display_trigger_vertical_position();
      scpi_ok();
    }
  }
  else if(scpi_header(2, "TRIGger", "HOLDoff", 0))
  {
    uint32 step;

    if(scpi_query)
    {
      scpi_reply_number(fpgasettings.en_holdoff_trigger ? (fpgasettings.value_holdoff_trigger / 1000.0) : 0.0);
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;seconds expected");
      return;
    }

    for(step=0;step<IDEAL_HOLDOFF_STEPS;step++)
    {
      if(((step == 0) && (value == 0.0)) || ((step != 0) && scpi_close(value * 1000.0, ideal_holdoff_ms(step))))
      {
        break;
      }
    }

    if(step == IDEAL_HOLDOFF_STEPS)
    {
      scpi_error(-222, "Data out of range;use 0, 10m, 20m, 50m, 100m, 200m, 500m, 1 or 2");
    }
    else if(scpi_ready())
    {
      fpgasettings.value_holdoff_trigger = ideal_holdoff_ms(step);
      fpgasettings.en_holdoff_trigger = (step != 0);
      scpi_ok();
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//DISPlay:..., ACQuire:..., MATH:...

static void scpi_flag_command(uint8 *flag)
{
  int32 choice;

  if(scpi_query)
  {
    scpi_reply_bool(*flag);
  }
  else if((choice = scpi_bool(scpi_args)) < 0)
  {
    scpi_error(-104, "Data type error;use ON or OFF");
  }
  else if(scpi_ready())
  {
    *flag = choice;
    scpi_ok();
  }
}

static void scpi_choice_command(uint8 *value, const char **names, uint32 count, const char *error)
{
  int32 choice;

  if(scpi_query)
  {
    scpi_reply_keyword(names[*value % count]);
  }
  else if((choice = scpi_choice(scpi_args, names, count)) < 0)
  {
    scpi_error(-104, error);
  }
  else if(scpi_ready())
  {
    *value = choice;
    scpi_ok();
  }
}

static void scpi_display_command(void)
{
  int32 choice;

  if(scpi_header(2, "DISPlay", "XY", 0))
  {
    if(scpi_query)
    {
      scpi_reply_bool(scopesettings.xymodedisplay);
    }
    else if((choice = scpi_bool(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use ON or OFF");
    }
    else if(scpi_ready())
    {
      scpi_set_xy(choice);
      scpi_ok();
    }
  }
  else if(scpi_header(2, "DISPlay", "GRID", 0))
  {
    scpi_flag_command(&scopesettings.gridenable);
  }
  else if(scpi_header(2, "DISPlay", "TRACe", 0))
  {
    scpi_choice_command(&scopesettings.ACQ_trace, scpi_trace_names, IDEAL_TRACE_STYLES, "Data type error;use LINear, DOTS, PEAK, PRO or SINC");
  }
  else if(scpi_header(2, "DISPlay", "PERSistence", 0))
  {
    scpi_error(-241, "Hardware missing;this firmware has no persistence display");
  }
  else if(scpi_header(2, "DISPlay", "DATA", 0) && scpi_query)
  {
    remote_screen_command(scpi_args);
    scpi_ended = 1;
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

static void scpi_math_command(void)
{
  if(scpi_header(2, "MATH", "MODE", 0))
  {
    scpi_choice_command(&mathmode, scpi_math_names, IDEAL_MATH_MODES, "Data type error;use OFF, ADD, SUBtract, MULtiply or DIVide");
  }
  else if((scpi_nodecount == 2) && scpi_node(1, "SOURce") && ((scpi_nodes[1].suffix == 1) || (scpi_nodes[1].suffix == 2)))
  {
    scpi_choice_command((scpi_nodes[1].suffix == 1) ? &channelA : &channelB, scpi_source_names, IDEAL_MATH_SOURCES, "Data type error;use CHAN1, CHAN2, REF1 or REF2");
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//MEASure:...

static void scpi_measure_value(uint32 channel, uint32 measurement)
{
  const ANMEASURE *m = &as_measure[((measurement == AN_PHASE) || (measurement == AN_DELAY)) ? 0 : channel];

  //9.91E+37 is the SCPI "not a number": nothing to measure in this capture
  if(m->valid & (1u << measurement))
  {
    scpi_reply_number(m->value[measurement]);
  }
  else
  {
    remote_text("9.91E+37\n");
  }
}

//Optional channel argument, channel 1 when absent
static int32 scpi_optional_channel(const char *text)
{
  const char *ptr = text;
  int32 channel;

  if(scpi_at_end(text))
  {
    return(0);
  }

  channel = scpi_channel_word(&ptr);

  return(((channel >= 0) && scpi_at_end(ptr)) ? channel : -1);
}

static void scpi_stat_line(const char *name, uint32 stat)
{
  const ANSTAT *s = &as_stats[stat];
  uint32 measurement = as_stat_measurement(stat);
  char   line[160];
  char  *ptr;

  ptr = ideal_append_text(line, name);
  ptr = ideal_append_text(ptr, ((measurement == AN_PHASE) || (measurement == AN_DELAY) || !as_stat_channel(stat)) ? ",CHAN1," : ",CHAN2,");
  ptr = ideal_append_text(ptr, an_measure_name(measurement));
  *ptr++ = ',';
  ptr = ideal_append_int(ptr, s->count);
  *ptr++ = ',';
  ptr = an_format_double(ptr, s->last, 6);
  *ptr++ = ',';
  ptr = an_format_double(ptr, an_stat_mean(s), 6);
  *ptr++ = ',';
  ptr = an_format_double(ptr, s->min, 6);
  *ptr++ = ',';
  ptr = an_format_double(ptr, s->max, 6);
  *ptr++ = ',';
  ptr = an_format_double(ptr, an_stat_stddev(s), 6);
  ideal_append_text(ptr, "\n");

  remote_text(line);
}

static void scpi_measure_command(void)
{
  int32  channel, measurement;
  double value;
  uint32 i;

  if(scpi_nodecount < 2)
  {
    scpi_error(-113, "Undefined header");
    return;
  }

  //MEASure:<name>? [CHANnel<n>]
  for(measurement=0;(measurement<AN_MEASUREMENTS) && !an_mnemonic(an_measure_name(measurement), scpi_nodes[1].text, scpi_nodes[1].length);measurement++);

  if((scpi_nodecount == 2) && scpi_query && (scpi_nodes[1].suffix < 0) && (measurement < AN_MEASUREMENTS))
  {
    if((channel = scpi_optional_channel(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
      return;
    }

    as_measure_now();
    scpi_measure_value(channel, measurement);
  }
  else if(scpi_header(2, "MEASure", "ALL", 0) && scpi_query)
  {
    char  line[64];
    char *ptr;

    if((channel = scpi_optional_channel(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
      return;
    }

    as_measure_now();

    for(i=0;i<AN_MEASUREMENTS;i++)
    {
      const ANMEASURE *m = &as_measure[((i == AN_PHASE) || (i == AN_DELAY)) ? 0 : channel];

      ptr = ideal_append_text(line, an_measure_name(i));
      *ptr++ = ',';

      if(m->valid & (1u << i))
      {
        ptr = an_format_double(ptr, m->value[i], 6);
      }
      else
      {
        ptr = ideal_append_text(ptr, "9.91E+37");
      }

      *ptr++ = ',';
      ptr = ideal_append_text(ptr, an_measure_unit(i));
      ideal_append_text(ptr, "\n");
      remote_text(line);
    }
  }
  else if((scpi_nodecount == 2) && scpi_node(1, "SLOT"))
  {
    const char *ptr = scpi_args;
    int32 slot = scpi_nodes[1].suffix - 1;

    if((slot < 0) || (slot > 5))
    {
      scpi_error(-114, "Header suffix out of range;use SLOT1 to SLOT6");
      return;
    }

    if(scpi_query)
    {
      char  line[32];
      char *end;

      end = ideal_append_text(line, scopesettings.measurementitems[slot].channel ? "CHAN2," : "CHAN1,");
      end = ideal_append_text(end, an_measure_name(scopesettings.measurementitems[slot].index % AN_SLOT_MEASUREMENTS));
      ideal_append_text(end, "\n");
      remote_text(line);
      return;
    }

    channel = scpi_channel_word(&ptr);

    while(*ptr == ' ')
    {
      ptr++;
    }

    if((channel < 0) || (*ptr != ',') || ((measurement = an_measure_lookup(ptr + 1)) < 0) ||
       (measurement >= AN_SLOT_MEASUREMENTS) || !scpi_at_end(ptr + 1 + scpi_word_length(ptr + 1)))
    {
      scpi_error(-104, "Data type error;use CHAN<n>,<VMAX|VMIN|VAVG|VRMS|VPP|VP|FREQ|PERIOD|PWIDTH|NWIDTH|PDUTY|NDUTY>");
    }
    else if(scpi_ready())
    {
      scopesettings.measurementitems[slot].channel = channel;
      scopesettings.measurementitems[slot].index = measurement;
      scopesettings.measurementitems[slot].channelsettings = scpi_channel(channel);
      ui_display_measurements();
      scpi_ok();
    }
  }
  else if(scpi_header(2, "MEASure", "EXTRa", 0))
  {
    static const char *words[3] = { "OFF", "CHAN1", "CHAN2" };
    const char *ptr = scpi_args;

    if(scpi_query)
    {
      scpi_reply(words[as_config.extra % AS_EXTRA_CHOICES]);
    }
    else if(scpi_word_is(ptr, "OFF") && scpi_at_end(ptr + 3))
    {
      as_config.extra = AS_EXTRA_OFF;
      scpi_ok();
    }
    else if(((channel = scpi_channel_word(&ptr)) < 0) || !scpi_at_end(ptr))
    {
      scpi_error(-104, "Data type error;use OFF, CHAN1 or CHAN2");
    }
    else
    {
      as_config.extra = channel + 1;
      scpi_ok();
    }
  }
  else if(scpi_header(2, "MEASure", "STATistics", 0))
  {
    int32 choice;

    if(scpi_query)
    {
      scpi_reply_bool(as_config.stats);
    }
    else if((choice = scpi_bool(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use ON or OFF");
    }
    else
    {
      if(as_config.stats != (uint32)choice)
      {
        as_config.stats = choice;
        as_reset_stats();
      }

      scpi_ok();
    }
  }
  else if(scpi_header(3, "MEASure", "STATistics", "COUNt"))
  {
    if(scpi_query)
    {
      scpi_reply_int(an_stat_counts[as_config.stats_n % AN_STAT_COUNTS]);
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;use 10, 20, 50, 100, 200, 500 or 1000");
      return;
    }

    for(i=0;(i<AN_STAT_COUNTS) && (value != an_stat_counts[i]);i++);

    if(i == AN_STAT_COUNTS)
    {
      scpi_error(-222, "Data out of range;use 10, 20, 50, 100, 200, 500 or 1000");
    }
    else
    {
      as_config.stats_n = i;
      as_reset_stats();
      scpi_ok();
    }
  }
  else if(scpi_header(3, "MEASure", "STATistics", "RESet") && !scpi_query)
  {
    as_reset_stats();
    scpi_ok();
  }
  else if(scpi_header(3, "MEASure", "STATistics", "DATA") && scpi_query)
  {
    char name[8];
    uint32 rows = 6 + ((as_config.extra != AS_EXTRA_OFF) ? AS_EXTRA_ITEMS : 0);

    //SLOT<n> or EXTRA,<channel>,<measurement>,<count>,<last>,<mean>,<min>,<max>,<std dev>
    for(i=0;i<rows;i++)
    {
      if(i < 6)
      {
        *ideal_append_int(ideal_append_text(name, "SLOT"), i + 1) = 0;
      }
      else
      {
        ideal_append_text(name, "EXTRA");
      }

      scpi_stat_line(name, i);
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//DECode:...

static void scpi_decode_command(void)
{
  int32  choice;
  double value;
  uint32 i;

  if(scpi_header(2, "DECode", "MODE", 0))
  {
    if(scpi_query)
    {
      scpi_reply_keyword(scpi_decode_names[as_config.decode % AS_DECODE_MODES]);
    }
    else if((choice = scpi_choice(scpi_args, scpi_decode_names, AS_DECODE_MODES)) < 0)
    {
      scpi_error(-104, "Data type error;use OFF, UART or I2C");
    }
    else
    {
      as_config.decode = choice;
      scpi_ok();
    }
  }
  else if(scpi_header(3, "DECode", "UART", "BAUD"))
  {
    if(scpi_query)
    {
      if(as_config.uart_baud == 0)
      {
        scpi_reply("AUTO");
      }
      else
      {
        scpi_reply_int(an_uart_bauds[(as_config.uart_baud - 1) % AN_UART_BAUDS]);
      }

      return;
    }

    if(scpi_word_is(scpi_args, "AUTO") && scpi_at_end(scpi_args + scpi_word_length(scpi_args)))
    {
      as_config.uart_baud = 0;
      scpi_ok();
      return;
    }

    if(!scpi_number(scpi_args, &value))
    {
      scpi_error(-104, "Data type error;use AUTO or a baud rate");
      return;
    }

    for(i=0;(i<AN_UART_BAUDS) && (value != an_uart_bauds[i]);i++);

    if(i == AN_UART_BAUDS)
    {
      scpi_error(-222, "Data out of range;use 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600 or 115200");
    }
    else
    {
      as_config.uart_baud = i + 1;
      scpi_ok();
    }
  }
  else if(scpi_header(3, "DECode", "UART", "FORMat"))
  {
    scpi_choice_command(&as_config.uart_format, scpi_format_names, AS_UART_FORMATS, "Data type error;use 8N1, 8E1 or 8O1");
  }
  else if(scpi_header(3, "DECode", "UART", "IDLE"))
  {
    static const char *words[2] = { "HIGH", "LOW" };

    scpi_choice_command(&as_config.uart_idle_low, words, 2, "Data type error;use HIGH or LOW");
  }
  else if(scpi_header(3, "DECode", "UART", "SOURce"))
  {
    const char *ptr = scpi_args;

    if(scpi_query)
    {
      scpi_reply(as_config.uart_source ? "CHAN2" : "CHAN1");
    }
    else if(((choice = scpi_channel_word(&ptr)) < 0) || !scpi_at_end(ptr))
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
    }
    else
    {
      as_config.uart_source = choice;
      scpi_ok();
    }
  }
  else if(scpi_header(2, "DECode", "LIST", 0))
  {
    int32 on;

    if(scpi_query)
    {
      scpi_reply_bool(as_config.event_list);
    }
    else if((on = scpi_bool(scpi_args)) < 0)
    {
      scpi_error(-104, "Data type error;use ON or OFF");
    }
    else
    {
      as_config.event_list = on;
      scpi_ok();
    }
  }
  else if(scpi_header(2, "DECode", "DATA", 0) && scpi_query)
  {
    static const char *types[5] = { "BYTE", "START", "STOP", "ADDRESS", "DATA" };
    double rate = (double)ideal_effective_sample_rate();
    char   line[96];
    char  *ptr;

    //#DECODE <mode> <status> <count> <baud>, then <type>,<start s>,<end s>,<value>,<flags> per event,
    //times relative to the trigger point
    ptr = ideal_append_text(line, "#DECODE ");
    ptr = scpi_append_short(ptr, scpi_decode_names[as_config.decode % AS_DECODE_MODES]);
    *ptr++ = ' ';
    ptr = ideal_append_int(ptr, as_decode.status);
    *ptr++ = ' ';
    ptr = ideal_append_int(ptr, as_decode.count);
    *ptr++ = ' ';
    ptr = ideal_append_int(ptr, as_decode.baud);
    ideal_append_text(ptr, "\n");
    remote_text(line);

    for(i=0;i<as_decode.count;i++)
    {
      const ANEVENT *event = &as_decode.events[i];

      ptr = ideal_append_text(line, types[event->type % 5]);
      *ptr++ = ',';
      ptr = an_format_double(ptr, ((double)event->start - (double)disp_trigger_index) / rate, 6);
      *ptr++ = ',';
      ptr = an_format_double(ptr, ((double)event->end - (double)disp_trigger_index) / rate, 6);
      *ptr++ = ',';
      ptr = ideal_append_int(ptr, event->value);
      *ptr++ = ',';
      ptr = ideal_append_text(ptr, (event->flags & AN_FLAG_PARITY) ? "P" : "");
      ptr = ideal_append_text(ptr, (event->flags & AN_FLAG_FRAMING) ? "F" : "");
      ptr = ideal_append_text(ptr, (event->flags & AN_FLAG_NAK) ? "N" : "");
      ptr = ideal_append_text(ptr, (event->flags & AN_FLAG_READ) ? "R" : "");
      ptr = ideal_append_text(ptr, (event->flags & AN_FLAG_PARTIAL) ? "X" : "");
      ideal_append_text(ptr, "\n");

      if(remote_text(line) != 0)
      {
        return;
      }
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//SYSTem:SETup? lists the commands that bring a scope into the current state, in an order that works:
//probe before scale, scale before position and level, time base before its position

static void scpi_setup_line(const char *a, const char *b)
{
  char line[80];

  *ideal_append_text(ideal_append_text(ideal_append_text(line, a), b), "\n") = 0;
  remote_text(line);
}

static void scpi_setup_number(const char *header, double value)
{
  char text[24];

  an_format_double(text, value, 6);
  scpi_setup_line(header, text);
}

static void scpi_setup_command(void)
{
  static const double ratios[7] = { 0.5, 1.0, 10.0, 20.0, 50.0, 100.0, 1000.0 };
  char   header[24];
  char   text[24];
  uint32 i;

  for(i=0;i<2;i++)
  {
    PCHANNELSETTINGS settings = scpi_channel(i);
    char *ptr = ideal_append_text(header, i ? ":CHAN2:" : ":CHAN1:");

    ideal_append_text(ptr, "DISP ");
    scpi_setup_line(header, settings->enable ? "ON" : "OFF");
    ideal_append_text(ptr, "COUP ");
    scpi_setup_line(header, settings->coupling ? "AC" : "DC");
    ideal_append_text(ptr, "PROB ");
    scpi_setup_number(header, ratios[settings->magnification % 7]);
    ideal_append_text(ptr, "SCAL ");
    scpi_setup_number(header, scpi_channel_scale(settings));
    ideal_append_text(ptr, "POS ");
    scpi_setup_number(header, scpi_position_div(settings));
    ideal_append_text(ptr, "UNIT ");
    scpi_setup_line(header, settings->V_A ? "A" : "V");
    ideal_append_text(ptr, "INV ");
    scpi_setup_line(header, settings->invert ? "ON" : "OFF");
    ideal_append_text(ptr, "FFT ");
    scpi_setup_line(header, settings->fftenable ? "ON" : "OFF");
  }

  scpi_setup_number(":TIM:SCAL ", scpi_time_per_div[scopesettings.timeperdiv % 35]);
  scpi_setup_number(":TIM:POS ", scpi_time_position());
  scpi_append_short(text, scpi_mode_names[scopesettings.triggermode % 3]);
  scpi_setup_line(":TRIG:MODE ", text);
  scpi_setup_line(":TRIG:SLOP ", scopesettings.triggeredge ? "FALL" : "RIS");
  scpi_setup_line(":TRIG:SOUR ", scopesettings.triggerchannel ? "CHAN2" : "CHAN1");
  scpi_setup_number(":TRIG:LEV ", scpi_trigger_level());
  scpi_setup_number(":TRIG:HOLD ", fpgasettings.en_holdoff_trigger ? (fpgasettings.value_holdoff_trigger / 1000.0) : 0.0);
  scpi_setup_line(":DISP:XY ", scopesettings.xymodedisplay ? "ON" : "OFF");
  scpi_setup_line(":DISP:GRID ", scopesettings.gridenable ? "ON" : "OFF");
  scpi_append_short(text, scpi_trace_names[scopesettings.ACQ_trace % IDEAL_TRACE_STYLES]);
  scpi_setup_line(":DISP:TRAC ", text);
  scpi_setup_line(":ACQ:AVER ", scopesettings.average_mode ? "ON" : "OFF");
  scpi_append_short(text, scpi_math_names[mathmode % IDEAL_MATH_MODES]);
  scpi_setup_line(":MATH:MODE ", text);
  scpi_setup_line(":MATH:SOUR1 ", scpi_source_names[channelA % IDEAL_MATH_SOURCES]);
  scpi_setup_line(":MATH:SOUR2 ", scpi_source_names[channelB % IDEAL_MATH_SOURCES]);

  for(i=0;i<6;i++)
  {
    char *ptr = ideal_append_text(header, ":MEAS:SLOT");

    ptr = ideal_append_int(ptr, i + 1);
    ideal_append_text(ptr, " ");
    ptr = ideal_append_text(text, scopesettings.measurementitems[i].channel ? "CHAN2," : "CHAN1,");
    ideal_append_text(ptr, an_measure_name(scopesettings.measurementitems[i].index % AN_SLOT_MEASUREMENTS));
    scpi_setup_line(header, text);
  }

  scpi_setup_line(":MEAS:EXTR ", (as_config.extra == 2) ? "CHAN2" : (as_config.extra == 1) ? "CHAN1" : "OFF");
  *ideal_append_int(text, an_stat_counts[as_config.stats_n % AN_STAT_COUNTS]) = 0;
  scpi_setup_line(":MEAS:STAT:COUN ", text);
  scpi_setup_line(":MEAS:STAT ", as_config.stats ? "ON" : "OFF");
  scpi_append_short(text, scpi_decode_names[as_config.decode % AS_DECODE_MODES]);
  scpi_setup_line(":DEC:MODE ", text);

  if(as_config.uart_baud)
  {
    *ideal_append_int(text, an_uart_bauds[(as_config.uart_baud - 1) % AN_UART_BAUDS]) = 0;
  }
  else
  {
    ideal_append_text(text, "AUTO");
  }

  scpi_setup_line(":DEC:UART:BAUD ", text);
  scpi_setup_line(":DEC:UART:FORM ", scpi_format_names[as_config.uart_format % AS_UART_FORMATS]);
  scpi_setup_line(":DEC:UART:IDLE ", as_config.uart_idle_low ? "LOW" : "HIGH");
  scpi_setup_line(":DEC:UART:SOUR ", as_config.uart_source ? "CHAN2" : "CHAN1");
  scpi_setup_line(":DEC:LIST ", as_config.event_list ? "ON" : "OFF");
}

//----------------------------------------------------------------------------------------------------------------------------------
//One command

static void scpi_command(const char *command)
{
  int32 choice;

  if(!scpi_parse_header(command))
  {
    scpi_error(-102, "Syntax error");
    return;
  }

  if(scpi_header(1, "*IDN", 0, 0) && scpi_query)
  {
    remote_text("X Scope,FNIRSI 1014D,0," VERSION_STRING "\n");
  }
  else if(scpi_header(1, "*OPC", 0, 0) && scpi_query)
  {
    remote_text("1\n");
  }
  else if((scpi_header(1, "*CLS", 0, 0) || scpi_header(1, "*WAI", 0, 0)) && !scpi_query)
  {
    scpi_error_code = 0;
    scpi_error_text = "No error";
    scpi_ok();
  }
  else if(scpi_header(2, "SYSTem", "ERRor", 0) && scpi_query)
  {
    char  line[80];
    char *ptr;

    ptr = ideal_append_int(line, scpi_error_code);
    ptr = ideal_append_text(ptr, ",\"");
    ptr = ideal_append_text(ptr, scpi_error_text);
    ideal_append_text(ptr, "\"\n");
    remote_text(line);

    scpi_error_code = 0;
    scpi_error_text = "No error";
  }
  else if(scpi_header(2, "SYSTem", "SETup", 0) && scpi_query)
  {
    scpi_setup_command();
  }
  else if(scpi_header(1, "RUN", 0, 0) && !scpi_query)
  {
    if(scpi_ready())
    {
      scpi_run();
      scpi_ok();
    }
  }
  else if(scpi_header(1, "STOP", 0, 0) && !scpi_query)
  {
    if(scpi_ready())
    {
      scopesettings.runstate = RUN_STATE_STOPPED;
      ui_display_run_stop_text();
      scpi_ok();
    }
  }
  else if(scpi_header(1, "SINGle", 0, 0) && !scpi_query)
  {
    if(scpi_ready())
    {
      if(scopesettings.triggermode != 1)
      {
        scpi_set_trigger_mode(1);
      }

      scpi_run();
      scpi_ok();
    }
  }
  else if(scpi_header(1, "RSTate", 0, 0) && scpi_query)
  {
    scpi_reply(scopesettings.runstate ? "RUN" : "STOP");
  }
  else if(scpi_header(1, "AUToscale", 0, 0) && !scpi_query)
  {
    //Runs as the AUTO key from the key handling, right after this reply
    if(scpi_ready())
    {
      remote_press_key(UIC_BUTTON_AUTO);
      scpi_ok();
    }
  }
  else if(scpi_node(0, "CHANnel") && (scpi_nodecount >= 1))
  {
    scpi_channel_command();
  }
  else if(scpi_node(0, "TIMebase"))
  {
    scpi_timebase_command();
  }
  else if(scpi_node(0, "TRIGger"))
  {
    scpi_trigger_command();
  }
  else if(scpi_node(0, "DISPlay"))
  {
    scpi_display_command();
  }
  else if(scpi_header(2, "ACQuire", "AVERage", 0))
  {
    scpi_flag_command(&scopesettings.average_mode);
  }
  else if(scpi_node(0, "MATH"))
  {
    scpi_math_command();
  }
  else if(scpi_node(0, "MEASure"))
  {
    scpi_measure_command();
  }
  else if(scpi_node(0, "DECode"))
  {
    scpi_decode_command();
  }
  else if(scpi_header(2, "WAVeform", "DATA", 0) && scpi_query)
  {
    const char *ptr = scpi_args;

    if(((choice = scpi_channel_word(&ptr)) < 0) || !scpi_at_end(ptr))
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
    }
    else
    {
      remote_wave_command(choice ? "2" : "1");
      scpi_ended = 1;
    }
  }
  else if(scpi_header(2, "WAVeform", "RAW", 0) && scpi_query)
  {
    const char *ptr = scpi_args;

    if(((choice = scpi_channel_word(&ptr)) < 0) || !scpi_at_end(ptr))
    {
      scpi_error(-104, "Data type error;use CHAN1 or CHAN2");
    }
    else
    {
      remote_waveb_command(choice ? "2" : "1");
      scpi_ended = 1;
    }
  }
  else
  {
    scpi_error(-113, "Undefined header");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//A line from the PC: commands separated by ';'. Data replies (WAVeform, DISPlay:DATA?) end the line.

void scpi_execute(const char *line)
{
  char   command[SCPI_LINE_SIZE];
  uint32 i = 0, length;

  scpi_ended = 0;

  while(line[i] && !scpi_ended)
  {
    length = 0;

    while(line[i] == ' ')
    {
      i++;
    }

    while(line[i] && (line[i] != ';') && (length < (SCPI_LINE_SIZE - 1)))
    {
      command[length++] = line[i++];
    }

    //Too long for one command: skip the rest of it
    while(line[i] && (line[i] != ';'))
    {
      i++;
      length = SCPI_LINE_SIZE;
    }

    if(line[i] == ';')
    {
      i++;
    }

    if(length == SCPI_LINE_SIZE)
    {
      scpi_error(-112, "Program mnemonic too long");
      continue;
    }

    //Trailing spaces
    while(length && (command[length - 1] == ' '))
    {
      length--;
    }

    command[length] = 0;

    if(length)
    {
      scpi_command(command);
    }
  }

  if(!scpi_ended)
  {
    remote_text("#END\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

#endif //PORT_1014D
