//----------------------------------------------------------------------------------------------------------------------------------
//Remote control over the USB serial (CDC) mode (Utilities > USB mode > Serial)
//
//The PC sends text lines ending in CR or LF. Lines that start with an upper case letter are remote
//commands; the USB interrupt only queues them and the main loop executes them here, so they run
//next to acquisition and drawing exactly like front panel input. Replies are text lines; binary
//blocks are announced with a header line that gives their size.
//
//  IDN?                 X Scope,FNIRSI 1014D,<version>
//  KEYS?                the key names KEY accepts, one per line
//  KEY <name>           act as if that front panel key was pressed or that knob turned one step
//  STATUS?              run state, time base, sample rate, channel and trigger settings
//  SCREEN?              #SCREEN 800 480 RGB565 768000, then the screen as little endian RGB565
//  SCREEN? <row> <n>    #SCREEN <row> <n> <bytes>, then n rows (n at most 16) starting at <row>
//  WAVE? <1|2>          #WAVE <ch> <count> <sample rate> <trigger index>, then one value in mV per line
//  WAVEB? <1|2>         raw 8 bit samples with their scaling (see remote_waveb_command), for live display
//
//Every reply ends with a line "#END"; errors are "#ERR <reason>".
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "arm32.h"
#include "variables.h"
#include "statemachine.h"
#include "usb_interface.h"
#include "ideal_1014d.h"
#include "menu_ideal.h"
#include "remote.h"
#include "timer.h"

#if PORT_1014D

//----------------------------------------------------------------------------------------------------------------------------------

#define REMOTE_LINE_SIZE      128
#define REMOTE_PACKET         512
#define REMOTE_TX_TIMEOUT_MS  2000        //Give up when the host takes no packet for this long

static char           remote_line[REMOTE_LINE_SIZE];
static volatile uint8 remote_pending;
static volatile uint8 remote_key;

typedef struct
{
  const char *name;
  uint8       code;
} REMOTEKEY;

//The power off code is left out on purpose: it saves the settings and stops the firmware
static const REMOTEKEY remote_keys[] =
{
  { "RUN_STOP",        UIC_BUTTON_RUN_STOP },
  { "AUTO",            UIC_BUTTON_AUTO },
  { "MENU",            UIC_BUTTON_MENU },
  { "SAVE_PICTURE",    UIC_BUTTON_SAVE_PICTURE },
  { "SAVE_WAVE",       UIC_BUTTON_SAVE_WAVE },
  { "H_CUR",           UIC_BUTTON_H_CUR },
  { "V_CUR",           UIC_BUTTON_V_CUR },
  { "RIGHT",           UIC_BUTTON_NAV_RIGHT },
  { "UP",              UIC_BUTTON_NAV_UP },
  { "OK",              UIC_BUTTON_NAV_OK },
  { "DOWN",            UIC_BUTTON_NAV_DOWN },
  { "LEFT",            UIC_BUTTON_NAV_LEFT },
  { "MOVE_SPEED",      UIC_BUTTON_MOVE_SPEED },
  { "CH1",             UIC_BUTTON_CH1_ENABLE },
  { "CH1_CONF",        UIC_BUTTON_CH1_CONF },
  { "CH2",             UIC_BUTTON_CH2_ENABLE },
  { "CH2_CONF",        UIC_BUTTON_CH2_CONF },
  { "TRIG_ORIGIN",     UIC_BUTTON_TRIG_ORIG },
  { "TRIG_MODE",       UIC_BUTTON_TRIG_MODE },
  { "TRIG_EDGE",       UIC_BUTTON_TRIG_EDGE },
  { "TRIG_CH",         UIC_BUTTON_TRIG_CHX },
  { "TRIG_50",         UIC_BUTTON_TRIG_50_PERCENT },
  { "F1",              UIC_BUTTON_F1 },
  { "F2",              UIC_BUTTON_F2 },
  { "F3",              UIC_BUTTON_F3 },
  { "F4",              UIC_BUTTON_F4 },
  { "F5",              UIC_BUTTON_F5 },
  { "F6",              UIC_BUTTON_F6 },
  { "GEN",             UIC_BUTTON_GEN },
  { "NEXT",            UIC_BUTTON_NEXT },
  { "PREVIOUS",        UIC_BUTTON_PREVIOUS },
  { "DELETE",          UIC_BUTTON_DELETE },
  { "SELECT_ALL",      UIC_BUTTON_SELECT_ALL },
  { "SELECT",          UIC_BUTTON_SELECT },
  { "SEL_ADD",         UIC_ROTARY_SEL_ADD },
  { "SEL_SUB",         UIC_ROTARY_SEL_SUB },
  { "CH1_POS_ADD",     UIC_ROTARY_CH1_POS_ADD },
  { "CH1_POS_SUB",     UIC_ROTARY_CH1_POS_SUB },
  { "CH2_POS_ADD",     UIC_ROTARY_CH2_POS_ADD },
  { "CH2_POS_SUB",     UIC_ROTARY_CH2_POS_SUB },
  { "TRIG_POS_ADD",    UIC_ROTARY_TRIG_POS_ADD },
  { "TRIG_POS_SUB",    UIC_ROTARY_TRIG_POS_SUB },
  { "TRIG_LEVEL_ADD",  UIC_ROTARY_TRIG_LEVEL_ADD },
  { "TRIG_LEVEL_SUB",  UIC_ROTARY_TRIG_LEVEL_SUB },
  { "CH1_SCALE_ADD",   UIC_ROTARY_SCALE_CH1_ADD },
  { "CH1_SCALE_SUB",   UIC_ROTARY_SCALE_CH1_SUB },
  { "CH2_SCALE_ADD",   UIC_ROTARY_SCALE_CH2_ADD },
  { "CH2_SCALE_SUB",   UIC_ROTARY_SCALE_CH2_SUB },
  { "TIME_ADD",        UIC_ROTARY_TIME_ADD },
  { "TIME_SUB",        UIC_ROTARY_TIME_SUB },
};

#define REMOTE_KEY_COUNT  (sizeof(remote_keys) / sizeof(REMOTEKEY))

//----------------------------------------------------------------------------------------------------------------------------------
//Called from the USB interrupt with a complete line. Only one command is held; the PC waits for the
//#END of a reply before sending the next one, so a second line while one is pending is answered busy

uint32 remote_queue_line(const char *line)
{
  uint32 i;

  if(remote_pending)
  {
    return(0);
  }

  for(i=0;(i<(REMOTE_LINE_SIZE - 1)) && line[i];i++)
  {
    remote_line[i] = line[i];
  }

  remote_line[i] = 0;
  remote_pending = 1;

  return(1);
}

//----------------------------------------------------------------------------------------------------------------------------------
//The key handling takes a remote key when no front panel key is waiting

uint8 remote_take_key(void)
{
  uint8 key = remote_key;

  remote_key = 0;

  return(key);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Send bytes straight to the CDC data endpoint, one packet at a time. Interrupts are off while the
//endpoint registers are used, because the USB interrupt selects other endpoints with the same index
//register. Gives up when the host stops reading.

static int32 remote_send(const void *data, uint32 length)
{
  const uint8 *ptr = (const uint8 *)data;
  uint32 count, start;

  while(length)
  {
    count = (length > REMOTE_PACKET) ? REMOTE_PACKET : length;
    start = timer0_get_ticks();

    //Wait for the previous packet to be taken with interrupts on (the timer keeps counting and the
    //host may pause for a while when the PC program is busy); check and fill the endpoint with
    //interrupts off
    while(1)
    {
      arm32_interrupt_disable();

      *USBC_REG_EPIND = 2;

      if((*USBC_REG_TXCSR & USBC_BP_TXCSR_D_TX_READY) == 0)
      {
        break;
      }

      arm32_interrupt_enable();

      if((timer0_get_ticks() - start) > REMOTE_TX_TIMEOUT_MS)
      {
        return(-1);
      }
    }

    usb_write_to_fifo((void *)USBC_REG_EPFIFO2, (void *)ptr, count);

    *USBC_REG_TXCSR &= ~USBC_BP_TXCSR_D_UNDER_RUN;
    *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_TX_READY;

    arm32_interrupt_enable();

    ptr += count;
    length -= count;
  }

  return(0);
}

//----------------------------------------------------------------------------------------------------------------------------------

static int32 remote_text(const char *text)
{
  uint32 length = 0;

  while(text[length])
  {
    length++;
  }

  return(remote_send(text, length));
}

//----------------------------------------------------------------------------------------------------------------------------------

static uint32 remote_starts(const char *line, const char *command)
{
  while(*command)
  {
    if(*line++ != *command++)
    {
      return(0);
    }
  }

  //The command must end here or be followed by an argument
  return((*line == 0) || (*line == ' '));
}

//----------------------------------------------------------------------------------------------------------------------------------

static const char *remote_argument(const char *line)
{
  while(*line && (*line != ' '))
  {
    line++;
  }

  while(*line == ' ')
  {
    line++;
  }

  return(line);
}

//----------------------------------------------------------------------------------------------------------------------------------

static uint32 remote_same(const char *a, const char *b)
{
  while(*a && *b && (*a == *b))
  {
    a++;
    b++;
  }

  return((*a == 0) && ((*b == 0) || (*b == ' ')));
}

//----------------------------------------------------------------------------------------------------------------------------------

static void remote_key_command(const char *name)
{
  uint32 i;

  for(i=0;i<REMOTE_KEY_COUNT;i++)
  {
    if(remote_same(remote_keys[i].name, name))
    {
      remote_key = remote_keys[i].code;
      remote_text("OK\n#END\n");
      return;
    }
  }

  remote_text("#ERR unknown key, see KEYS?\n#END\n");
}

//----------------------------------------------------------------------------------------------------------------------------------

static void remote_keys_command(void)
{
  uint32 i;
  char   line[32];

  for(i=0;i<REMOTE_KEY_COUNT;i++)
  {
    *ideal_append_text(ideal_append_text(line, remote_keys[i].name), "\n") = 0;
    remote_text(line);
  }

  remote_text("#END\n");
}

//----------------------------------------------------------------------------------------------------------------------------------

static char *remote_channel_status(char *ptr, PCHANNELSETTINGS settings, const char *name)
{
  ptr = ideal_append_text(ptr, name);
  ptr = ideal_append_text(ptr, settings->enable ? " on " : " off ");
  ptr = ideal_append_text(ptr, volt_div_texts_short[settings->magnification % 7][settings->displayvoltperdiv % 7]);
  ptr = ideal_append_text(ptr, settings->V_A ? "/div A " : "/div V ");
  ptr = ideal_append_text(ptr, settings->coupling ? "AC" : "DC");
  ptr = ideal_append_text(ptr, settings->invert ? " inverted" : "");
  ptr = ideal_append_text(ptr, settings->fftenable ? " FFT\n" : "\n");

  return(ptr);
}

static void remote_status_command(void)
{
  static const char *modes[3] = { "auto", "single", "normal" };
  char  text[320];
  char *ptr = text;

  ptr = ideal_append_text(ptr, scopesettings.runstate ? "RUN\n" : "STOP\n");
  ptr = ideal_append_text(ptr, "TIMEBASE ");
  ptr = ideal_append_text(ptr, (const char *)time_div_texts[scopesettings.timeperdiv % 35]);
  ptr = ideal_append_text(ptr, "\nSAMPLERATE ");
  ptr = ideal_append_int(ptr, ideal_effective_sample_rate());
  ptr = ideal_append_text(ptr, "\n");
  ptr = remote_channel_status(ptr, &scopesettings.channel1, "CH1");
  ptr = remote_channel_status(ptr, &scopesettings.channel2, "CH2");
  ptr = ideal_append_text(ptr, "TRIGGER ");
  ptr = ideal_append_text(ptr, modes[scopesettings.triggermode % 3]);
  ptr = ideal_append_text(ptr, scopesettings.triggeredge ? " falling CH" : " rising CH");
  ptr = ideal_append_int(ptr, scopesettings.triggerchannel + 1);
  ptr = ideal_append_text(ptr, "\n#END\n");

  remote_text(text);
}

//----------------------------------------------------------------------------------------------------------------------------------

//SCREEN? sends the whole screen; SCREEN? <first row> <rows> sends a band of at most 16 rows. The
//Windows serial driver drops data when the PC program does not keep up with a large burst, so PC
//programs fetch the screen in bands and check each one

static uint32 remote_number(const char **text)
{
  uint32 value = 0;

  while(**text == ' ')
  {
    (*text)++;
  }

  while((**text >= '0') && (**text <= '9'))
  {
    value = (value * 10) + (**text - '0');
    (*text)++;
  }

  return(value);
}

static void remote_screen_command(const char *argument)
{
  uint32 first = 0;
  uint32 rows = 480;
  char   line[64];
  char  *ptr;

  if(*argument)
  {
    first = remote_number(&argument);
    rows = remote_number(&argument);

    if((first >= 480) || (rows == 0) || (rows > 16) || ((first + rows) > 480))
    {
      remote_text("#ERR use SCREEN? <first row 0..479> <rows 1..16>\n#END\n");
      return;
    }
  }

  ptr = ideal_append_text(line, "#SCREEN ");

  if(rows == 480)
  {
    ptr = ideal_append_text(ptr, "800 480 RGB565 768000\n");
  }
  else
  {
    ptr = ideal_append_int(ptr, first);
    *ptr++ = ' ';
    ptr = ideal_append_int(ptr, rows);
    *ptr++ = ' ';
    ptr = ideal_append_int(ptr, rows * 1600);
    ptr = ideal_append_text(ptr, "\n");
  }

  remote_text(line);

  if(remote_send((const uint8 *)maindisplaybuffer + (first * 1600), rows * 1600) == 0)
  {
    remote_text("\n#END\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

static void remote_wave_command(const char *argument)
{
  PCHANNELSETTINGS settings;
  uint32 count = scopesettings.samplecount;
  uint32 index;
  char   line[96];
  char  *ptr;

  if(argument[0] == '1')
  {
    settings = &scopesettings.channel1;
  }
  else if(argument[0] == '2')
  {
    settings = &scopesettings.channel2;
  }
  else
  {
    remote_text("#ERR use WAVE? 1 or WAVE? 2\n#END\n");
    return;
  }

  if((count == 0) || (count > MAX_SAMPLE_BUFFER_SIZE))
  {
    count = 3000;
  }

  ptr = ideal_append_text(line, "#WAVE ");
  ptr = ideal_append_int(ptr, (argument[0] == '1') ? 1 : 2);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, count);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, ideal_effective_sample_rate());
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, disp_trigger_index);
  ideal_append_text(ptr, "\n");
  remote_text(line);

  //Ten values per line keeps the number of USB packets low
  for(index=0;index<count;index++)
  {
    if((index % 10) == 0)
    {
      ptr = line;
    }

    ptr = ideal_append_int(ptr, ideal_channel_mv(settings, index));

    if(((index % 10) == 9) || (index == (count - 1)))
    {
      ideal_append_text(ptr, "\n");

      if(remote_text(line) != 0)
      {
        return;
      }
    }
    else
    {
      *ptr++ = ',';
    }
  }

  remote_text("#END\n");
}

//----------------------------------------------------------------------------------------------------------------------------------
//WAVEB? <1|2>: the raw 8 bit samples of a channel for fast live display on the PC, after one header
//line with everything needed to scale them:
//  #WAVEB <ch> <enabled> <count> <sample rate> <trigger index> <cal> <mV/div sample> <offset uV>
//         <position px> <mV/div display> <unit V|A>
//  millivolts = (sample - 128) * cal * mV/div sample / (50 * 2^21) + offset uV / 1000
//  position: screen row of 0 V above the bottom of the 400 px trace window (50 px per division)

static const uint32 remote_mv_per_div[7]   = { 5000, 2500, 1000, 500, 200, 100, 50 };
static const uint32 remote_probe_tenths[7] = { 5, 10, 100, 200, 500, 1000, 10000 };

static void remote_waveb_command(const char *argument)
{
  PCHANNELSETTINGS settings;
  uint32 count = scopesettings.samplecount;
  uint32 range, display, mvdiv, displaymvdiv;
  int64  offset_uv = 0;
  char   line[160];
  char  *ptr;

  if(argument[0] == '1')
  {
    settings = &scopesettings.channel1;
  }
  else if(argument[0] == '2')
  {
    settings = &scopesettings.channel2;
  }
  else
  {
    remote_text("#ERR use WAVEB? 1 or WAVEB? 2\n#END\n");
    return;
  }

  if((count == 0) || (count > MAX_SAMPLE_BUFFER_SIZE))
  {
    count = 3000;
  }

  range = (settings->samplevoltperdiv < 7) ? settings->samplevoltperdiv : 6;
  display = (settings->displayvoltperdiv < 7) ? settings->displayvoltperdiv : 6;
  mvdiv = (remote_mv_per_div[range] * remote_probe_tenths[settings->magnification % 7]) / 10;
  displaymvdiv = (remote_mv_per_div[display] * remote_probe_tenths[settings->magnification % 7]) / 10;

  //The display adds the FPGA offset trim back in display pixels (see scope_get_y_sample)
  if(settings->dc_shift_size)
  {
    int32 pixels = (settings->dcoffset * 100) / settings->dc_shift_size;

    if(display == 6)
    {
      pixels *= 2;
    }

    offset_uv = ((int64)pixels * displaymvdiv * 1000) / 50;
  }

  ptr = ideal_append_text(line, "#WAVEB ");
  ptr = ideal_append_int(ptr, (argument[0] == '1') ? 1 : 2);
  ptr = ideal_append_text(ptr, settings->enable ? " 1 " : " 0 ");
  ptr = ideal_append_int(ptr, count);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, ideal_effective_sample_rate());
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, disp_trigger_index);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, settings->input_calibration[range]);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, mvdiv);
  *ptr++ = ' ';
  ptr = ideal_append_int64(ptr, offset_uv);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, settings->traceposition);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, displaymvdiv);
  ptr = ideal_append_text(ptr, settings->V_A ? " A\n" : " V\n");
  remote_text(line);

  if(remote_send(settings->tracebuffer, count) == 0)
  {
    remote_text("\n#END\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Main loop: execute a queued command

void remote_poll(void)
{
  char line[REMOTE_LINE_SIZE];
  uint32 i;

  if(remote_pending == 0)
  {
    return;
  }

  for(i=0;i<REMOTE_LINE_SIZE;i++)
  {
    line[i] = remote_line[i];
  }

  remote_pending = 0;

  if(remote_starts(line, "IDN?"))
  {
    remote_text("X Scope,FNIRSI 1014D," VERSION_STRING "\n#END\n");
  }
  else if(remote_starts(line, "KEYS?"))
  {
    remote_keys_command();
  }
  else if(remote_starts(line, "KEY"))
  {
    remote_key_command(remote_argument(line));
  }
  else if(remote_starts(line, "STATUS?"))
  {
    remote_status_command();
  }
  else if(remote_starts(line, "SCREEN?"))
  {
    remote_screen_command(remote_argument(line));
  }
  else if(remote_starts(line, "WAVE?"))
  {
    remote_wave_command(remote_argument(line));
  }
  else if(remote_starts(line, "WAVEB?"))
  {
    remote_waveb_command(remote_argument(line));
  }
  else
  {
    remote_text("#ERR unknown command\n#END\n");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

#endif //PORT_1014D
