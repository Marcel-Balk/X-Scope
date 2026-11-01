//----------------------------------------------------------------------------------------------------------------------------------
//
//A small table driven menu engine: every row has a label, an optional value text, an optional change
//handler for the left / right keys and an optional OK action. The menus are overlays: they are
//redrawn into the trace buffer on every display frame (ui_redraw_active_menu), so the traces keep
//updating beneath them, and closing one only needs a state change.
//
//Keys while a list menu is open:
//  up / down / select dial   move the highlight
//  left / right              change the value of the row (rows without a value: left = back)
//  OK                        run the row action, or step the value forward
//  any other key             close all menus (same as the stock menus)
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "display_lib.h"
#include "timer.h"
#include "variables.h"
#include "uart.h"
#include "usb_interface.h"
#include "menu_1014d.h"
#include "statemachine.h"
#include "scope_functions.h"
#include "fpga_control.h"
#include "ref_and_math.h"
#include "sd_card_layout.h"
#include "ff.h"
#include "ideal_1014d.h"
#include "menu_ideal.h"
#include "analysis_1014d.h"
#include "analysis_scope.h"

#include <string.h>

#if PORT_1014D

//----------------------------------------------------------------------------------------------------------------------------------
//Engine state

static const IDEALMENU *ideal_menu_current;
static uint32           ideal_menu_item;
static uint32           ideal_menu_from_main;

static const IDEALMENU *ideal_menu_stack[IDEAL_MENU_STACK_DEPTH];
static uint32           ideal_menu_stack_item[IDEAL_MENU_STACK_DEPTH];
static uint32           ideal_menu_depth;

//Result of the last explicit save from these menus, shown on the diagnostics page
static int32            ideal_last_save = 1;

HIGHLIGHTRECTDATA ideal_menu_highlight_box =
{
  IDEAL_MENU_WIDTH - 8,
  IDEAL_MENU_ROW_HEIGHT - 2,
  { COLOR_GREEN, COLOR_ISLAMIC_GREEN, COLOR_PHARMACY_GREEN, COLOR_DARK_GREEN },
  COLOR_BLACK
};

//----------------------------------------------------------------------------------------------------------------------------------
//Engine
//----------------------------------------------------------------------------------------------------------------------------------

void ideal_menu_open(const IDEALMENU *menu, uint32 frommainmenu)
{
  ideal_menu_current   = menu;
  ideal_menu_item      = 0;
  ideal_menu_from_main = frommainmenu;
  ideal_menu_depth     = 0;

  navigationstate = NAV_LIST_MENU_HANDLING;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_menu_push(const IDEALMENU *menu)
{
  if(ideal_menu_depth < IDEAL_MENU_STACK_DEPTH)
  {
    ideal_menu_stack[ideal_menu_depth]      = ideal_menu_current;
    ideal_menu_stack_item[ideal_menu_depth] = ideal_menu_item;
    ideal_menu_depth++;

    ideal_menu_current = menu;
    ideal_menu_item    = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_menu_back(void)
{
  if(ideal_menu_depth)
  {
    //Back to the parent list menu
    ideal_menu_depth--;
    ideal_menu_current = ideal_menu_stack[ideal_menu_depth];
    ideal_menu_item    = ideal_menu_stack_item[ideal_menu_depth];
  }
  else if(ideal_menu_from_main)
  {
    //Back to the main menu, which is still drawn underneath
    ideal_menu_current = 0;
    navigationstate = NAV_MAIN_MENU_HANDLING;
  }
  else
  {
    //Opened with a front panel key, so back means close
    ideal_menu_current = 0;
    sm_close_menu();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32 ideal_menu_layout(void)
{
  return((uint32)ideal_menu_current + (ideal_menu_depth * 4) + ideal_menu_from_main);
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32 ideal_menu_active(void)
{
  return((navigationstate == NAV_LIST_MENU_HANDLING) && (ideal_menu_current != 0));
}

//----------------------------------------------------------------------------------------------------------------------------------

void ideal_menu_handle_navigation(void)
{
  const IDEALMENUITEM *item;
  int32 newitem;

  if(ideal_menu_current == 0)
  {
    sm_close_menu();
    return;
  }

  //The last row is the implicit back row
  item = (ideal_menu_item < ideal_menu_current->count) ? &ideal_menu_current->items[ideal_menu_item] : 0;

  switch(toprocesscommand)
  {
    case UIC_BUTTON_NAV_UP:
    case UIC_BUTTON_NAV_DOWN:
    case UIC_ROTARY_SEL_ADD:
    case UIC_ROTARY_SEL_SUB:
      //Same direction convention as the main menu: up / dial add move to the previous row
      newitem = (int32)ideal_menu_item - setvalue;

      if(newitem < 0)
      {
        newitem = ideal_menu_current->count;
      }
      else if(newitem > (int32)ideal_menu_current->count)
      {
        newitem = 0;
      }

      ideal_menu_item = newitem;
      break;

    case UIC_BUTTON_NAV_LEFT:
    case UIC_BUTTON_NAV_RIGHT:
      if(item == 0)
      {
        ideal_menu_back();
      }
      else if(item->change)
      {
        item->change((toprocesscommand == UIC_BUTTON_NAV_RIGHT) ? 1 : -1);
      }
      else if((toprocesscommand == UIC_BUTTON_NAV_RIGHT) && item->action)
      {
        item->action();
      }
      else if(toprocesscommand == UIC_BUTTON_NAV_LEFT)
      {
        ideal_menu_back();
      }
      break;

    case UIC_BUTTON_NAV_OK:
      if(item == 0)
      {
        ideal_menu_back();
      }
      else if(item->action)
      {
        item->action();
      }
      else if(item->change)
      {
        item->change(1);
      }
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void ideal_menu_draw(void)
{
  const IDEALMENU *menu = ideal_menu_current;
  uint32 xpos = ideal_menu_from_main ? IDEAL_MENU_XPOS_MAIN : IDEAL_MENU_XPOS_KEY;
  uint32 ypos = IDEAL_MENU_YPOS;
  uint32 height;
  uint32 i, y;
  char   text[40];

  if(menu == 0)
  {
    return;
  }

  //A menu opened from the main menu sits next to it, so draw that first
  if(ideal_menu_from_main)
  {
    ui_display_main_menu();
  }

  //Title, the rows and the back row
  height = ((menu->count + 2) * IDEAL_MENU_ROW_HEIGHT) + 6;

  display_set_fg_color(COLOR_DARK_GREY_3);
  display_draw_rect(xpos, ypos, IDEAL_MENU_WIDTH, height);

  display_set_fg_color(COLOR_DARK_GREY_1);
  display_fill_rect(xpos + 1, ypos + 1, IDEAL_MENU_WIDTH - 2, height - 2);

  display_set_font(&font_1);

  //Title in yellow
  display_set_fg_color(COLOR_YELLOW);
  display_text(xpos + 10, ypos + 5, menu->title);

  //Highlight the selected row
  y = ypos + IDEAL_MENU_ROW_HEIGHT + 3;
  display_draw_highlight_rect(xpos + 4, y + (ideal_menu_item * IDEAL_MENU_ROW_HEIGHT), &ideal_menu_highlight_box);

  for(i=0;i<=menu->count;i++,y+=IDEAL_MENU_ROW_HEIGHT)
  {
    display_set_fg_color(COLOR_WHITE);

    if(i == menu->count)
    {
      display_text(xpos + 12, y + 4, "< Back");
      break;
    }

    display_text(xpos + 12, y + 4, menu->items[i].label);

    if(menu->items[i].value)
    {
      text[0] = 0;
      menu->items[i].value(text);

      //Editable values in cyan, read only ones in grey
      display_set_fg_color(menu->items[i].change ? COLOR_CYAN : COLOR_GREY);
      display_right_aligned_text(xpos + IDEAL_MENU_WIDTH - 12, y + 4, text);
    }
    else if(menu->items[i].action)
    {
      display_set_fg_color(COLOR_GREY);
      display_right_aligned_text(xpos + IDEAL_MENU_WIDTH - 12, y + 4, ">");
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Small helpers
//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_on_off_text(char *text, uint32 state)
{
  ideal_append_text(text, state ? "On" : "Off");
}

//Step a value through 0..count-1 with wrap around
static uint32 ideal_cycle(uint32 value, int32 direction, uint32 count)
{
  int32 newvalue = (int32)value + direction;

  if(newvalue < 0)
  {
    newvalue = count - 1;
  }
  else if(newvalue >= (int32)count)
  {
    newvalue = 0;
  }

  return(newvalue);
}

//Busy message box on the visible screen for the blocking actions
static void ideal_message(const char *line1, const char *line2, const char *line3)
{
  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  display_set_fg_color(COLOR_DARK_GREY_3);
  display_draw_rect(180, 170, 440, 100);
  display_set_fg_color(COLOR_DARK_GREY_1);
  display_fill_rect(181, 171, 438, 98);

  display_set_font(&font_1);
  display_set_fg_color(COLOR_WHITE);

  if(line1)
    display_text(194, 182, line1);

  if(line2)
    display_text(194, 208, line2);

  if(line3)
  {
    display_set_fg_color(COLOR_YELLOW);
    display_text(194, 238, line3);
  }
}

//After a blocking action the whole screen is rebuilt, like the clock search does
static void ideal_finish_blocking_action(void)
{
  sm_close_menu();
  ui_setup_main_screen();
}

//The trigger level is derived from the trigger channel settings
static uint32 ideal_is_trigger_channel(PCHANNELSETTINGS settings)
{
  return(((settings == &scopesettings.channel1) && (scopesettings.triggerchannel == 0)) ||
         ((settings == &scopesettings.channel2) && (scopesettings.triggerchannel == 1)));
}

//----------------------------------------------------------------------------------------------------------------------------------

//Main menu: items 2 and 3 were the stock "Output browsing" and "Capture output" icons with no action
//behind them; they are now text entries for the new menus

void ideal_draw_main_menu_label(uint32 item, uint32 xpos, uint32 ypos)
{
  //Pictogram and label in the style of the stock entries (main_menu_ideal_icons.c)
  if((item == 2) || (item == 3))
  {
    display_copy_icon_fg_color(main_menu_ideal_icons[item - 2], xpos, ypos, 166, 18);
  }
  else
  {
    display_copy_icon_fg_color(main_menu_icons[item], xpos, ypos, 166, 18);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Channel menu (CH1 / CH2 CONF keys), works on currentsettings
//----------------------------------------------------------------------------------------------------------------------------------

static const char *ideal_probe_texts[7] = { "x0.5", "x1", "x10", "x20", "x50", "x100", "x1000" };

static void ideal_channel_refresh(void)
{
  ui_display_channel_settings(currentsettings);

  if(ideal_is_trigger_channel(currentsettings))
  {
    ui_display_trigger_vertical_position();
  }
}

static void ideal_probe_value(char *text)
{
  ideal_append_text(text, ideal_probe_texts[currentsettings->magnification % 7]);
}

static void ideal_probe_change(int32 direction)
{
  currentsettings->magnification = ideal_cycle(currentsettings->magnification, direction, 7);
  ideal_channel_refresh();
}

static void ideal_coupling_value(char *text)
{
  ideal_append_text(text, currentsettings->coupling ? "AC" : "DC");
}

static void ideal_coupling_change(int32 direction)
{
  currentsettings->coupling ^= 1;

  //Same side effects as the original channel menu: AC drops the DC offset trim, push it all
  if(currentsettings->coupling)
  {
    currentsettings->dcoffset = 0;
  }

  fpga_set_channel_coupling(currentsettings);
  fpga_set_channel_offset(currentsettings);

  if(ideal_is_trigger_channel(currentsettings))
  {
    fpga_set_trigger_level();
  }

  ideal_channel_refresh();
}

static void ideal_unit_value(char *text)
{
  ideal_append_text(text, currentsettings->V_A ? "Ampere (A)" : "Volt (V)");
}

static void ideal_unit_change(int32 direction)
{
  currentsettings->V_A ^= 1;
  ideal_channel_refresh();
}

static void ideal_invert_value(char *text)
{
  ideal_on_off_text(text, currentsettings->invert);
}

static void ideal_invert_change(int32 direction)
{
  //Applied when the samples are read from the FPGA, so it shows from the next capture on
  currentsettings->invert ^= 1;
}

static void ideal_fft_value(char *text)
{
  ideal_on_off_text(text, currentsettings->fftenable);
}

static void ideal_fft_change(int32 direction)
{
  currentsettings->fftenable ^= 1;
}

static const IDEALMENUITEM ideal_channel_items[] =
{
  { "Probe",        ideal_probe_value,    ideal_probe_change,    0 },
  { "Coupling",     ideal_coupling_value, ideal_coupling_change, 0 },
  { "Unit",         ideal_unit_value,     ideal_unit_change,     0 },
  { "Invert",       ideal_invert_value,   ideal_invert_change,   0 },
  { "FFT spectrum", ideal_fft_value,      ideal_fft_change,      0 },
};

const IDEALMENU ideal_channel_menu =
{
  "Channel settings",
  ideal_channel_items,
  sizeof(ideal_channel_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

static uint32 ideal_ref_slot = 1;

static PCHANNELSETTINGS ideal_ref_settings(uint32 slot)
{
  switch(slot)
  {
    case 1:  return(&scopesettings.ch_ref1);
    case 2:  return(&scopesettings.ch_ref2);
    case 3:  return(&scopesettings.ch_ref3);
    case 4:  return(&scopesettings.ch_ref4);
    case 5:  return(&scopesettings.ch_ref5);
    case 6:  return(&scopesettings.ch_ref6);
    case 7:  return(&scopesettings.ch_ref7);
    default: return(&scopesettings.ch_ref8);
  }
}

static uint8 *ideal_ref_flag(uint32 slot)
{
  switch(slot)
  {
    case 1:  return(&scopesettings.ref1);
    case 2:  return(&scopesettings.ref2);
    case 3:  return(&scopesettings.ref3);
    case 4:  return(&scopesettings.ref4);
    case 5:  return(&scopesettings.ref5);
    case 6:  return(&scopesettings.ref6);
    case 7:  return(&scopesettings.ref7);
    default: return(&scopesettings.ref8);
  }
}

//trace point buffer, color and 1014D chrome pointers; only settings and samples are taken over
static void ideal_store_reference(uint32 slot, PCHANNELSETTINGS source)
{
  PCHANNELSETTINGS dest = ideal_ref_settings(slot);
  CHANNELSETTINGS  keep = *dest;

  *dest = *source;

  dest->tracebuffer      = keep.tracebuffer;
  dest->buffer           = keep.buffer;
  dest->tmp_tracebuffer  = keep.tmp_tracebuffer;
  dest->tracepoints      = keep.tracepoints;
  dest->noftracepoints   = 0;
  dest->color            = keep.color;
  dest->buttontext       = keep.buttontext;
  dest->highlightboxdata = keep.highlightboxdata;
  dest->boxdata          = keep.boxdata;
  dest->boxtext          = keep.boxtext;

  memcpy(dest->tracebuffer, source->tracebuffer, SAMPLE_COUNT);

  //The reference shows where the channel was
  dest->enable = 1;
  *ideal_ref_flag(slot) = 1;
}

//A slot holds data once a channel was stored in it or a file was loaded: both set a range scale
static uint32 ideal_ref_has_data(uint32 slot)
{
  PCHANNELSETTINGS settings = ideal_ref_settings(slot);

  return((settings->dc_shift_size != 0) && (settings->input_calibration[settings->samplevoltperdiv % 7] != 0));
}

static void ideal_ref_slot_value(char *text)
{
  *ideal_append_int(ideal_append_text(text, "REF"), ideal_ref_slot) = 0;
}

static void ideal_ref_slot_change(int32 direction)
{
  ideal_ref_slot = ideal_cycle(ideal_ref_slot - 1, direction, 8) + 1;
}

static void ideal_ref_show_value(char *text)
{
  ideal_on_off_text(text, *ideal_ref_flag(ideal_ref_slot));
}

static void ideal_ref_show_change(int32 direction)
{
  //Showing an empty slot would draw a flat line with a zero scale
  if(*ideal_ref_flag(ideal_ref_slot) || ideal_ref_has_data(ideal_ref_slot))
  {
    *ideal_ref_flag(ideal_ref_slot) ^= 1;
  }
}

static void ideal_ref_store_ch1(void)
{
  ideal_store_reference(ideal_ref_slot, &scopesettings.channel1);
}

static void ideal_ref_store_ch2(void)
{
  ideal_store_reference(ideal_ref_slot, &scopesettings.channel2);
}

static void ideal_ref_save(void)
{
  //result message itself
  scope_save_REFx_item_file(ideal_ref_slot);
}

static void ideal_ref_load(void)
{
  scope_load_REFx_item_file(ideal_ref_slot);

  //Only show the slot when the load produced usable settings (the file code shows its own errors)
  *ideal_ref_flag(ideal_ref_slot) = ideal_ref_has_data(ideal_ref_slot) ? 1 : 0;
}

static void ideal_ref_startup_value(char *text)
{
  ideal_on_off_text(text, scopesettings.ref_on_startup);
}

static void ideal_ref_startup_change(int32 direction)
{
  scopesettings.ref_on_startup ^= 1;
}

static void ideal_ref_hide_all(void)
{
  uint32 slot;

  for(slot=1;slot<=8;slot++)
  {
    *ideal_ref_flag(slot) = 0;
  }
}

static const IDEALMENUITEM ideal_reference_items[] =
{
  { "Slot",              ideal_ref_slot_value,    ideal_ref_slot_change,    0 },
  { "Show slot",         ideal_ref_show_value,    ideal_ref_show_change,    0 },
  { "Store CH1 in slot", 0,                       0,                        ideal_ref_store_ch1 },
  { "Store CH2 in slot", 0,                       0,                        ideal_ref_store_ch2 },
  { "Save slot to SD",   0,                       0,                        ideal_ref_save },
  { "Load slot from SD", 0,                       0,                        ideal_ref_load },
  { "Load all at start", ideal_ref_startup_value, ideal_ref_startup_change, 0 },
  { "Hide all",          0,                       0,                        ideal_ref_hide_all },
};

static const IDEALMENU ideal_reference_menu =
{
  "Reference waveforms",
  ideal_reference_items,
  sizeof(ideal_reference_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//Display & analysis menu
//----------------------------------------------------------------------------------------------------------------------------------

static const char *ideal_trace_texts[5]  = { "Linear", "Dots", "Peak", "Pro", "Sin(x)/x" };
static const char *ideal_math_texts[5]   = { "Off", "A + B", "A - B", "A x B", "A / B" };
static const char *ideal_source_texts[4] = { "CH1", "CH2", "REF1", "REF2" };

static void ideal_trace_value(char *text)
{
  ideal_append_text(text, ideal_trace_texts[scopesettings.ACQ_trace % 5]);
}

static void ideal_trace_change(int32 direction)
{
  scopesettings.ACQ_trace = ideal_cycle(scopesettings.ACQ_trace, direction, 5);
}

static void ideal_average_value(char *text)
{
  ideal_on_off_text(text, scopesettings.average_mode);
}

static void ideal_average_change(int32 direction)
{
  scopesettings.average_mode ^= 1;
}

static void ideal_grid_value(char *text)
{
  ideal_on_off_text(text, scopesettings.gridenable);
}

static void ideal_grid_change(int32 direction)
{
  scopesettings.gridenable ^= 1;
}

static void ideal_lock_value(char *text)
{
  ideal_on_off_text(text, scopesettings.lockcursors);
}

static void ideal_lock_change(int32 direction)
{
  scopesettings.lockcursors ^= 1;
}

static void ideal_fft1_value(char *text)
{
  ideal_on_off_text(text, scopesettings.channel1.fftenable);
}

static void ideal_fft1_change(int32 direction)
{
  scopesettings.channel1.fftenable ^= 1;
}

static void ideal_fft2_value(char *text)
{
  ideal_on_off_text(text, scopesettings.channel2.fftenable);
}

static void ideal_fft2_change(int32 direction)
{
  scopesettings.channel2.fftenable ^= 1;
}

static void ideal_math_value(char *text)
{
  ideal_append_text(text, ideal_math_texts[mathmode % IDEAL_MATH_MODES]);
}

static void ideal_math_change(int32 direction)
{
  mathmode = ideal_cycle(mathmode, direction, IDEAL_MATH_MODES);
}

static void ideal_math_a_value(char *text)
{
  ideal_append_text(text, ideal_source_texts[channelA % IDEAL_MATH_SOURCES]);
}

static void ideal_math_a_change(int32 direction)
{
  channelA = ideal_cycle(channelA, direction, IDEAL_MATH_SOURCES);
}

static void ideal_math_b_value(char *text)
{
  ideal_append_text(text, ideal_source_texts[channelB % IDEAL_MATH_SOURCES]);
}

static void ideal_math_b_change(int32 direction)
{
  channelB = ideal_cycle(channelB, direction, IDEAL_MATH_SOURCES);
}

static uint32 ideal_holdoff_step(void)
{
  uint32 step;

  if(fpgasettings.en_holdoff_trigger == 0)
  {
    return(0);
  }

  for(step=IDEAL_HOLDOFF_STEPS-1;step>0;step--)
  {
    if(ideal_holdoff_ms(step) <= fpgasettings.value_holdoff_trigger)
    {
      break;
    }
  }

  return(step);
}

static void ideal_holdoff_value(char *text)
{
  uint32 ms = ideal_holdoff_ms(ideal_holdoff_step());

  if(ms == 0)
  {
    ideal_append_text(text, "Off");
  }
  else if(ms >= 1000)
  {
    ideal_append_text(ideal_append_int(text, ms / 1000), " s");
  }
  else
  {
    ideal_append_text(ideal_append_int(text, ms), " ms");
  }
}

static void ideal_holdoff_change(int32 direction)
{
  uint32 step = ideal_cycle(ideal_holdoff_step(), direction, IDEAL_HOLDOFF_STEPS);

  fpgasettings.value_holdoff_trigger = ideal_holdoff_ms(step);
  fpgasettings.en_holdoff_trigger    = (step != 0);
}

static void ideal_open_reference_menu(void)
{
  ideal_menu_push(&ideal_reference_menu);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Measure & decode: extra measurements, statistics over captures and serial decoding (analysis_scope.c)

static const char *ideal_extra_texts[AS_EXTRA_CHOICES]  = { "Off", "CH1", "CH2" };
static const char *ideal_decode_texts[AS_DECODE_MODES]  = { "Off", "UART", "I2C" };
static const char *ideal_format_texts[AS_UART_FORMATS]  = { "8N1", "8E1", "8O1" };

static void ideal_extra_value(char *text)
{
  ideal_append_text(text, ideal_extra_texts[as_config.extra % AS_EXTRA_CHOICES]);
}

static void ideal_extra_change(int32 direction)
{
  as_config.extra = ideal_cycle(as_config.extra, direction, AS_EXTRA_CHOICES);
}

static void ideal_stats_value(char *text)
{
  ideal_on_off_text(text, as_config.stats);
}

static void ideal_stats_change(int32 direction)
{
  as_config.stats ^= 1;
  as_reset_stats();
}

static void ideal_stats_n_value(char *text)
{
  *ideal_append_int(text, an_stat_counts[as_config.stats_n % AN_STAT_COUNTS]) = 0;
}

static void ideal_stats_n_change(int32 direction)
{
  as_config.stats_n = ideal_cycle(as_config.stats_n, direction, AN_STAT_COUNTS);
  as_reset_stats();
}

static void ideal_stats_reset(void)
{
  as_reset_stats();
}

static void ideal_decode_value(char *text)
{
  ideal_append_text(text, ideal_decode_texts[as_config.decode % AS_DECODE_MODES]);
}

static void ideal_decode_change(int32 direction)
{
  as_config.decode = ideal_cycle(as_config.decode, direction, AS_DECODE_MODES);
}

static void ideal_baud_value(char *text)
{
  if(as_config.uart_baud == 0)
  {
    ideal_append_text(text, "Auto");
  }
  else
  {
    *ideal_append_int(text, an_uart_bauds[(as_config.uart_baud - 1) % AN_UART_BAUDS]) = 0;
  }
}

static void ideal_baud_change(int32 direction)
{
  as_config.uart_baud = ideal_cycle(as_config.uart_baud, direction, AN_UART_BAUDS + 1);
}

static void ideal_format_value(char *text)
{
  ideal_append_text(text, ideal_format_texts[as_config.uart_format % AS_UART_FORMATS]);
}

static void ideal_format_change(int32 direction)
{
  as_config.uart_format = ideal_cycle(as_config.uart_format, direction, AS_UART_FORMATS);
}

static void ideal_idle_value(char *text)
{
  ideal_append_text(text, as_config.uart_idle_low ? "Low (inverted)" : "High");
}

static void ideal_idle_change(int32 direction)
{
  as_config.uart_idle_low ^= 1;
}

static void ideal_uart_source_value(char *text)
{
  ideal_append_text(text, as_config.uart_source ? "CH2" : "CH1");
}

static void ideal_uart_source_change(int32 direction)
{
  as_config.uart_source ^= 1;
}

static void ideal_event_list_value(char *text)
{
  ideal_on_off_text(text, as_config.event_list);
}

static void ideal_event_list_change(int32 direction)
{
  as_config.event_list ^= 1;
}

static const IDEALMENUITEM ideal_analysis_items[] =
{
  { "Extra measurements", ideal_extra_value,       ideal_extra_change,       0 },
  { "Statistics",         ideal_stats_value,       ideal_stats_change,       0 },
  { "Statistics over N",  ideal_stats_n_value,     ideal_stats_n_change,     0 },
  { "Reset statistics",   0,                       0,                        ideal_stats_reset },
  { "Decode",             ideal_decode_value,      ideal_decode_change,      0 },
  { "UART baud",          ideal_baud_value,        ideal_baud_change,        0 },
  { "UART format",        ideal_format_value,      ideal_format_change,      0 },
  { "UART idle level",    ideal_idle_value,        ideal_idle_change,        0 },
  { "UART source",        ideal_uart_source_value, ideal_uart_source_change, 0 },
  { "Event list",         ideal_event_list_value,  ideal_event_list_change,  0 },
};

static const IDEALMENU ideal_analysis_menu =
{
  "Measure & decode (I2C: CH1 SDA, CH2 SCL)",
  ideal_analysis_items,
  sizeof(ideal_analysis_items) / sizeof(IDEALMENUITEM)
};

static void ideal_open_analysis_menu(void)
{
  ideal_menu_push(&ideal_analysis_menu);
}

static const IDEALMENUITEM ideal_display_items[] =
{
  { "Trace style",      ideal_trace_value,   ideal_trace_change,   0 },
  { "Averaging",        ideal_average_value, ideal_average_change, 0 },
  { "Grid",             ideal_grid_value,    ideal_grid_change,    0 },
  { "Cursor lock",      ideal_lock_value,    ideal_lock_change,    0 },
  { "FFT CH1",          ideal_fft1_value,    ideal_fft1_change,    0 },
  { "FFT CH2",          ideal_fft2_value,    ideal_fft2_change,    0 },
  { "Math",             ideal_math_value,    ideal_math_change,    0 },
  { "Math source A",    ideal_math_a_value,  ideal_math_a_change,  0 },
  { "Math source B",    ideal_math_b_value,  ideal_math_b_change,  0 },
  { "Trigger hold off", ideal_holdoff_value, ideal_holdoff_change, 0 },
  { "Reference waves",  0,                   0,                    ideal_open_reference_menu },
  { "Measure & decode", 0,                   0,                    ideal_open_analysis_menu },
};

const IDEALMENU ideal_display_menu =
{
  "Display & analysis",
  ideal_display_items,
  sizeof(ideal_display_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//Signal generator (GEN key, or Utilities)
//
//FPGA. The stock 1014D FPGA (0x1432) tolerates them but its own AWG protocol is not reverse engineered,
//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_gen_apply(void)
{
  fpga_on_off_generator();
}

static void ideal_gen_output_value(char *text)
{
  ideal_on_off_text(text, fpgasettings.gen_enable);
}

static void ideal_gen_output_change(int32 direction)
{
  fpgasettings.gen_enable ^= 1;
  ideal_gen_apply();
}

static void ideal_gen_frequency_value(char *text)
{
  ideal_append_frequency(text, fpgasettings.gen_freq);
}

static void ideal_gen_frequency_change(int32 direction)
{
  fpgasettings.gen_freq = ideal_frequency_step(fpgasettings.gen_freq, direction);
  ideal_gen_apply();
}

static void ideal_gen_duty_value(char *text)
{
  ideal_append_text(ideal_append_int(text, fpgasettings.gen_duty), " %");
}

static void ideal_gen_duty_change(int32 direction)
{
  int32 duty = (int32)fpgasettings.gen_duty + (direction * 5);

  if(duty < 0)
  {
    duty = 0;
  }
  else if(duty > 100)
  {
    duty = 100;
  }

  fpgasettings.gen_duty = duty;
  ideal_gen_apply();
}

static void ideal_gen_support_value(char *text)
{
  ideal_append_text(text, (fpgasettings.fw_FPGA > 1) ? "custom FPGA: yes" : "stock FPGA: no output");
}

static const IDEALMENUITEM ideal_generator_items[] =
{
  { "Output",     ideal_gen_output_value,    ideal_gen_output_change,    0 },
  { "Frequency",  ideal_gen_frequency_value, ideal_gen_frequency_change, 0 },
  { "Duty cycle", ideal_gen_duty_value,      ideal_gen_duty_change,      0 },
  { "Support",    ideal_gen_support_value,   0,                          0 },
};

const IDEALMENU ideal_generator_menu =
{
  "Signal generator (square)",
  ideal_generator_items,
  sizeof(ideal_generator_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//Diagnostics (read only)
//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_diag_firmware(char *text)
{
  ideal_append_text(ideal_append_text(ideal_append_text(text, PRODUCT_NAME), " "), VERSION_STRING);
}

static void ideal_diag_fpga(char *text)
{
  switch(fpgasettings.fw_FPGA)
  {
    case 1:  ideal_append_text(text, "stock 0x1432"); break;
    case 2:  ideal_append_text(text, "custom AL3 0x1532"); break;
    case 3:  ideal_append_text(text, "custom EF2 0x1632"); break;
    default: ideal_append_text(text, "unknown"); break;
  }
}

static void ideal_diag_clock(char *text)
{
  //Si5351 CLK1 = 400 MHz / p1b (p1b 6 = 50 MHz stock... see clock_synthesizer.c)
  switch(sampling_clock_p1b)
  {
    case 0x06: ideal_append_text(text, "50 MHz (stock)"); break;
    case 0x05: ideal_append_text(text, "57 MHz"); break;
    case 0x04: ideal_append_text(text, "67 MHz"); break;
    case 0x03: ideal_append_text(text, "80 MHz"); break;
    default:   ideal_append_int(text, sampling_clock_p1b); break;
  }
}

static void ideal_diag_sectors(char *text)
{
  *ideal_append_int(ideal_append_text(ideal_append_int(text, SETTINGS_SECTOR), " / "), INPUT_CALIBRATION_SECTOR) = 0;
}

static void ideal_diag_save(char *text)
{
  ideal_append_text(text, (ideal_last_save == 1) ? "not saved yet" : ((ideal_last_save == 0) ? "OK" : "FAILED"));
}

static void ideal_diag_cal(char *text, PCHANNELSETTINGS settings)
{
  //Gain factor of the 100 mV/div range and the zero offset of the same range
  *ideal_append_int(ideal_append_text(ideal_append_int(text, settings->input_calibration[5]), " / "), settings->dc_calibration_offset[5]) = 0;
}

static void ideal_diag_cal1(char *text)
{
  ideal_diag_cal(text, &scopesettings.channel1);
}

static void ideal_diag_cal2(char *text)
{
  ideal_diag_cal(text, &scopesettings.channel2);
}

static void ideal_diag_comp(char *text)
{
  char *ptr = text;

  ptr = ideal_append_int(ptr, scopesettings.channel1.adc1compensation);
  *ptr++ = ' ';
  ptr = ideal_append_int(ptr, scopesettings.channel1.adc2compensation);
  ptr = ideal_append_text(ptr, " | ");
  ptr = ideal_append_int(ptr, scopesettings.channel2.adc1compensation);
  *ptr++ = ' ';
  *ideal_append_int(ptr, scopesettings.channel2.adc2compensation) = 0;
}

static void ideal_diag_samples(char *text)
{
  *ideal_append_int(text, scopesettings.samplecount) = 0;
}

static void ideal_diag_time(char *text, uint32 which)
{
  char *ptr = ideal_append_int(text, ideal_times[which][0]);

  ptr = ideal_append_text(ptr, " ms, peak ");
  ideal_append_text(ideal_append_int(ptr, ideal_times[which][1]), " ms");
}

static void ideal_diag_acquire(char *text)
{
  ideal_diag_time(text, IDEAL_TIME_ACQUIRE);
}

static void ideal_diag_frame(char *text)
{
  ideal_diag_time(text, IDEAL_TIME_FRAME);
}

static void ideal_diag_loop(char *text)
{
  ideal_diag_time(text, IDEAL_TIME_LOOP);
}

static void ideal_diag_keys(char *text)
{
  ideal_diag_time(text, IDEAL_TIME_KEYS);
}

static void ideal_diag_overlay_value(char *text)
{
  ideal_on_off_text(text, ideal_timing_overlay);
}

static void ideal_diag_overlay_change(int32 direction)
{
  ideal_timing_overlay ^= 1;
}

//Live timing line at the bottom of the trace window, so the times can be read while turning knobs
//(any knob closes the menus). Last value / peak of the last 2 seconds, in ms
void ideal_draw_timing_overlay(void)
{
  static const char *names[IDEAL_TIMES] = { "acq ", "  frame ", "  loop ", "  keys " };
  char   text[96];
  char  *ptr = text;
  uint32 i;

  for(i=0;i<IDEAL_TIMES;i++)
  {
    ptr = ideal_append_text(ptr, names[i]);
    ptr = ideal_append_int(ptr, ideal_times[i][0]);
    *ptr++ = '/';
    ptr = ideal_append_int(ptr, ideal_times[i][1]);
  }

  ideal_append_text(ptr, " ms");

  display_set_fg_color(COLOR_BLACK);
  display_fill_rect(8, 438, 420, 16);
  display_set_fg_color(COLOR_WHITE);
  display_set_font(&font_1);
  display_text(12, 440, text);
}

static const IDEALMENUITEM ideal_diagnostics_items[] =
{
  { "Firmware",            ideal_diag_firmware, 0, 0 },
  { "FPGA",                ideal_diag_fpga,     0, 0 },
  { "Sampling clock",      ideal_diag_clock,    0, 0 },
  { "Settings/cal sector", ideal_diag_sectors,  0, 0 },
  { "Last menu save",      ideal_diag_save,     0, 0 },
  { "CH1 gain/zero 100mV", ideal_diag_cal1,     0, 0 },
  { "CH2 gain/zero 100mV", ideal_diag_cal2,     0, 0 },
  { "ADC interleave comp", ideal_diag_comp,     0, 0 },
  { "Samples per capture", ideal_diag_samples,  0, 0 },
  { "Acquisition time",    ideal_diag_acquire,  0, 0 },
  { "Trace frame time",    ideal_diag_frame,    0, 0 },
  { "Main loop time",      ideal_diag_loop,     0, 0 },
  { "Key handling time",   ideal_diag_keys,     0, 0 },
  { "Timing on screen",    ideal_diag_overlay_value, ideal_diag_overlay_change, 0 },
};

static const IDEALMENU ideal_diagnostics_menu =
{
  "Diagnostics",
  ideal_diagnostics_items,
  sizeof(ideal_diagnostics_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//Input (gain) calibration
//
//the six hardware ranges, and the gain factor of the range is set so the reading matches. The 50 mV
//incrementing search the factor is computed directly (ideal_input_calibration_factor).
//----------------------------------------------------------------------------------------------------------------------------------

typedef struct
{
  uint8       voltperdiv;
  int32       volts;
  const char *text;
} IDEALINPUTCALSTEP;

static const IDEALINPUTCALSTEP ideal_input_cal_steps[6] =
{
  { 5,   30000, "Apply +0.30 V DC to CH1 and CH2" },
  { 4,   60000, "Apply +0.60 V DC to CH1 and CH2" },
  { 3,  150000, "Apply +1.50 V DC to CH1 and CH2" },
  { 2,  300000, "Apply +3.00 V DC to CH1 and CH2" },
  { 1,  750000, "Apply +7.50 V DC to CH1 and CH2" },
  { 0, 1500000, "Apply +15.0 V DC to CH1 and CH2" },
};

//Average of the last capture in 1/16 counts relative to the ADC center
static int32 ideal_average_q4(PCHANNELSETTINGS settings)
{
  if(settings->averagecount)
  {
    return((int32)(((int64)settings->averagesum * 16) / (int32)settings->averagecount) - (128 * 16));
  }

  return((settings->average - 128) * 16);
}

static void ideal_do_input_calibration(void)
{
  static SCOPESETTINGS backup;
  int32  newcal[2][7];
  uint32 step, channel, i;
  int32  failed = 0;
  char   line[48];

  backup = scopesettings;

  for(i=0;i<7;i++)
  {
    newcal[0][i] = scopesettings.channel1.input_calibration[i];
    newcal[1][i] = scopesettings.channel2.input_calibration[i];
  }

  //or invert, auto trigger, 20 us/div
  for(channel=0;channel<2;channel++)
  {
    PCHANNELSETTINGS settings = channel ? &scopesettings.channel2 : &scopesettings.channel1;

    settings->enable = 1;
    settings->invert = 0;
    settings->coupling = 0;
    settings->magnification = 1;
    settings->dcoffset = 0;

    fpga_set_channel_enable(settings);
    fpga_set_channel_coupling(settings);
  }

  scopesettings.samplemode = 0;
  scopesettings.triggermode = 0;
  fpga_set_trigger_mode();

  scopesettings.long_mode = 0;
  scopesettings.timeperdiv = 20;
  scopesettings.samplerate = time_per_div_sample_rate[20];
  fpga_set_sample_rate(scopesettings.samplerate);
  fpga_set_time_base(scopesettings.timeperdiv);

  for(step=0;step<6;step++)
  {
    const IDEALINPUTCALSTEP *cal = &ideal_input_cal_steps[step];

    *ideal_append_text(ideal_append_int(ideal_append_text(line, "Input calibration step "), step + 1), " of 6") = 0;
    ideal_message(line, cal->text, "OK = measure, other key = cancel");

    uart1_wait_for_user_input();

    if(lastreceivedcommand != UIC_BUTTON_NAV_OK)
    {
      failed = -1;
      break;
    }

    ideal_message(line, "Measuring...", 0);

    for(channel=0;channel<2;channel++)
    {
      PCHANNELSETTINGS settings = channel ? &scopesettings.channel2 : &scopesettings.channel1;

      settings->displayvoltperdiv = cal->voltperdiv;
      settings->samplevoltperdiv = cal->voltperdiv;
      settings->dcoffset = 0;

      //Always push range and offset: match_volt_per_div_settings skips an unchanged range, which
      //would leave the user's DC offset trim active during the measurement
      fpga_set_channel_voltperdiv(settings);
      fpga_set_channel_offset(settings);
    }

    //Settle, then take two captures and use the second
    timer0_delay(200);

    for(i=0;i<2;i++)
    {
      uint32 timeout = 1000;

      fpga_do_conversion();

      while((fpga_done_conversion() == 0) && timeout)
      {
        timer0_delay(1);
        timeout--;
      }

      fpga_read_sample_data(&scopesettings.channel1, 100);
      fpga_read_sample_data(&scopesettings.channel2, 100);
    }

    for(channel=0;channel<2;channel++)
    {
      PCHANNELSETTINGS settings = channel ? &scopesettings.channel2 : &scopesettings.channel1;
      int32 factor = ideal_input_calibration_factor(ideal_average_q4(settings), cal->volts, volt_calc_data[1][cal->voltperdiv].mul_factor);

      if(factor < 0)
      {
        failed = 1;
        break;
      }

      newcal[channel][cal->voltperdiv] = factor;
    }

    if(failed)
    {
      break;
    }
  }

  //Put the user's acquisition settings back, keep only the new calibration
  scopesettings = backup;

  if(failed == 0)
  {
    newcal[0][6] = newcal[0][5] * 2;
    newcal[1][6] = newcal[1][5] * 2;

    for(i=0;i<7;i++)
    {
      scopesettings.channel1.input_calibration[i] = newcal[0][i];
      scopesettings.channel2.input_calibration[i] = newcal[1][i];
    }

    ideal_last_save = scope_save_input_calibration_data();

    ideal_message("Input calibration done", (ideal_last_save == 0) ? "Saved to the SD card" : "SAVE TO SD CARD FAILED", "Press any key");
  }
  else if(failed > 0)
  {
    ideal_message("Input calibration FAILED", "No signal, wrong polarity or out of range", "Nothing changed. Press any key");
  }

  if(failed >= 0)
  {
    uart1_wait_for_user_input();
  }

  //Restore the hardware to the user's settings
  for(channel=0;channel<2;channel++)
  {
    PCHANNELSETTINGS settings = channel ? &scopesettings.channel2 : &scopesettings.channel1;

    fpga_set_channel_enable(settings);
    fpga_set_channel_coupling(settings);
    fpga_set_channel_voltperdiv(settings);
    fpga_set_channel_offset(settings);
  }

  fpga_set_trigger_mode();
  fpga_set_sample_rate(scopesettings.samplerate);
  fpga_set_time_base(scopesettings.timeperdiv);

  if(scopesettings.long_mode)
  {
    fpga_set_long_timebase(scopesettings.timeperdiv);
  }

  fpga_set_trigger_level();
  scope_calculate_sample_range_properties();

  ideal_finish_blocking_action();
}

//----------------------------------------------------------------------------------------------------------------------------------
//CSV export of the current capture to the SD card root (SCOPE000.CSV .. SCOPE999.CSV)
//----------------------------------------------------------------------------------------------------------------------------------


//Volts per division on the 1x probe row in mV and the probe factor in tenths
static const uint32 ideal_mv_per_div[7]   = { 5000, 2500, 1000, 500, 200, 100, 50 };
static const uint32 ideal_probe_tenths[7] = { 5, 10, 100, 200, 500, 1000, 10000 };

int32 ideal_channel_mv(PCHANNELSETTINGS settings, uint32 index)
{
  uint32 range = (settings->samplevoltperdiv < 7) ? settings->samplevoltperdiv : 6;
  uint32 mvdiv = (ideal_mv_per_div[range] * ideal_probe_tenths[settings->magnification % 7]) / 10;
  int32  mv;

  mv = ideal_sample_to_mv((int32)settings->tracebuffer[index] - 128, settings->input_calibration[range], mvdiv);

  //The FPGA offset trim moves the samples; the display adds the same shift back in display pixels
  //(see scope_get_y_sample), so convert it with the display range
  if(settings->dc_shift_size)
  {
    uint32 display = (settings->displayvoltperdiv < 7) ? settings->displayvoltperdiv : 6;
    uint32 displaymvdiv = (ideal_mv_per_div[display] * ideal_probe_tenths[settings->magnification % 7]) / 10;
    int32  pixels = (settings->dcoffset * 100) / settings->dc_shift_size;

    if(display == 6)
    {
      pixels *= 2;
    }

    mv += (pixels * (int32)displaymvdiv) / 50;
  }

  return(mv);
}

static void ideal_export_csv(void)
{
  static FIL file;
  char   name[16];
  char   line[256];
  char  *ptr;
  uint32 number, index, count, rate;
  UINT   written;
  int32  result = FR_OK;

  if(scopesettings.long_mode)
  {
    ideal_message("CSV export", "Roll mode: not supported, use Save wave", "Press any key");
    uart1_wait_for_user_input();
    ideal_finish_blocking_action();
    return;
  }

  ideal_message("CSV export", "Writing...", 0);

  //First free file name
  for(number=0;number<1000;number++)
  {
    ptr = ideal_append_text(name, "SCOPE");
    *ptr++ = '0' + (number / 100);
    *ptr++ = '0' + ((number / 10) % 10);
    *ptr++ = '0' + (number % 10);
    ideal_append_text(ptr, ".CSV");

    result = f_open(&file, name, FA_CREATE_NEW | FA_WRITE);

    if(result != FR_EXIST)
    {
      break;
    }
  }

  if(result == FR_OK)
  {
    count = scopesettings.samplecount;

    if((count == 0) || (count > MAX_SAMPLE_BUFFER_SIZE))
    {
      count = 3000;
    }

    rate = ideal_effective_sample_rate();

    //Header with the conditions of the capture
    ptr = ideal_append_text(line, "# " PRODUCT_NAME " ");
    ptr = ideal_append_text(ptr, VERSION_STRING);
    ptr = ideal_append_text(ptr, " (FNIRSI 1014D), sample rate ");
    ptr = ideal_append_int(ptr, rate);
    ptr = ideal_append_text(ptr, " Sa/s, trigger at sample ");
    ptr = ideal_append_int(ptr, disp_trigger_index);
    ptr = ideal_append_text(ptr, "\r\n# CH1 ");
    ptr = ideal_append_text(ptr, scopesettings.channel1.enable ? "on" : "off");
    ptr = ideal_append_text(ptr, ", CH2 ");
    ptr = ideal_append_text(ptr, scopesettings.channel2.enable ? "on" : "off");
    ptr = ideal_append_text(ptr, ", values in mV (mA for current probes)\r\nindex,time_ns,ch1_mV,ch2_mV\r\n");

    result = f_write(&file, line, ptr - line, &written);

    for(index=0;(index<count)&&(result==FR_OK);index++)
    {
      int64 time_ns = (((int64)index - (int64)disp_trigger_index) * 1000000000LL) / rate;

      uint32 length = ideal_format_csv_line(line, index, time_ns, ideal_channel_mv(&scopesettings.channel1, index), ideal_channel_mv(&scopesettings.channel2, index));

      result = f_write(&file, line, length, &written);

      if((result == FR_OK) && (written != length))
      {
        result = FR_DISK_ERR;
      }
    }

    if(f_close(&file) != FR_OK)
    {
      result = FR_DISK_ERR;
    }
  }

  ideal_message("CSV export", (result == FR_OK) ? name : "FAILED to write the file", "Press any key");
  uart1_wait_for_user_input();
  ideal_finish_blocking_action();
}

//----------------------------------------------------------------------------------------------------------------------------------
//Utilities menu
//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_open_generator_submenu(void)
{
  ideal_menu_push(&ideal_generator_menu);
}

static void ideal_open_diagnostics_submenu(void)
{
  ideal_menu_push(&ideal_diagnostics_menu);
}

static void ideal_autosetup_value(char *text)
{
  ideal_append_text(text, scopesettings.new_autosetup ? "New" : "Old");
}

static void ideal_autosetup_change(int32 direction)
{
  scopesettings.new_autosetup ^= 1;
}

static void ideal_usb_value(char *text)
{
  ideal_append_text(text, USB_CH340 ? "Serial (CDC)" : "Mass storage");
}

static void ideal_usb_change(int32 direction)
{
  //Serial mode connects as soon as it is selected and stays connected; mass storage connects only
  //from the USB export screen. usb_device_init switches the interrupt handler accordingly
  USB_CH340 ^= 1;
  usb_device_init();
}

static void ideal_save_settings(void)
{
  ideal_last_save = scope_save_configuration_data();
}

static const IDEALMENUITEM ideal_utilities_items[] =
{
  { "Signal generator",  0,                     0,                      ideal_open_generator_submenu },
  { "Input calibration", 0,                     0,                      ideal_do_input_calibration },
  { "Export CSV to SD",  0,                     0,                      ideal_export_csv },
  { "Auto setup",        ideal_autosetup_value, ideal_autosetup_change, 0 },
  { "USB mode",          ideal_usb_value,       ideal_usb_change,       0 },
  { "Save settings now", 0,                     0,                      ideal_save_settings },
  { "Diagnostics",       0,                     0,                      ideal_open_diagnostics_submenu },
};

const IDEALMENU ideal_utilities_menu =
{
  "Utilities",
  ideal_utilities_items,
  sizeof(ideal_utilities_items) / sizeof(IDEALMENUITEM)
};

//----------------------------------------------------------------------------------------------------------------------------------
//FFT display
//
//Drawn in place of the time trace of a channel with FFT on. 1024 samples from the left edge of the
//screen (zero padded when the capture is shorter), Hann window, magnitude in dB relative to an 8 bit
//full scale sine (dBFS). Vertical: 0 dBFS at the top, 10 dB per division over the 8 divisions.
//Horizontal: DC at the left edge to half the sample rate at the right edge.
//----------------------------------------------------------------------------------------------------------------------------------

static int16 ideal_spectrum[IDEAL_FFT_BINS];

#define IDEAL_FFT_LEFT        6
#define IDEAL_FFT_WIDTH     698
#define IDEAL_FFT_TOP        59
#define IDEAL_FFT_HEIGHT    400

static int32 ideal_fft_y(int32 qdb)
{
  //400 pixels for 80 dB = 5 pixels per dB = 1.25 pixels per quarter dB
  int32 y = IDEAL_FFT_TOP + ((-qdb * 5) / 4);

  if(y < IDEAL_FFT_TOP)
  {
    y = IDEAL_FFT_TOP;
  }
  else if(y > (IDEAL_FFT_TOP + IDEAL_FFT_HEIGHT - 1))
  {
    y = IDEAL_FFT_TOP + IDEAL_FFT_HEIGHT - 1;
  }

  return(y);
}

void ideal_draw_fft(PCHANNELSETTINGS settings, uint32 line)
{
  int32  start = disp_first_sample;
  int32  count = scopesettings.samplecount;
  uint32 bin, peak, rate;
  int32  x, y, lastx, lasty;
  char   text[64];
  char  *ptr;

  if((count <= 0) || (count > MAX_SAMPLE_BUFFER_SIZE))
  {
    count = 3000;
  }

  if(start < 0)
  {
    start = 0;
  }

  if((start + IDEAL_FFT_SIZE) > count)
  {
    start = (count > IDEAL_FFT_SIZE) ? (count - IDEAL_FFT_SIZE) : 0;
  }

  count -= start;

  if(count > IDEAL_FFT_SIZE)
  {
    count = IDEAL_FFT_SIZE;
  }

  if(ideal_fft_magnitude_db(&settings->tracebuffer[start], count, ideal_spectrum) != 0)
  {
    return;
  }

  //No time trace points in FFT mode (waveform thumbnails then show no trace for this channel)
  settings->noftracepoints = 0;

  display_set_fg_color(settings->color);

  lastx = IDEAL_FFT_LEFT;
  lasty = ideal_fft_y(ideal_spectrum[0]);

  for(bin=1;bin<IDEAL_FFT_BINS;bin++)
  {
    x = IDEAL_FFT_LEFT + ((bin * IDEAL_FFT_WIDTH) / (IDEAL_FFT_BINS - 1));
    y = ideal_fft_y(ideal_spectrum[bin]);

    display_draw_line(lastx, lasty, x, y);

    lastx = x;
    lasty = y;
  }

  //Readout: peak frequency and level, scales
  rate = ideal_effective_sample_rate();
  peak = ideal_fft_peak_bin(ideal_spectrum, IDEAL_FFT_BINS);

  ptr = ideal_append_text(text, (settings == &scopesettings.channel1) ? "FFT CH1  peak " : "FFT CH2  peak ");
  ptr = ideal_append_frequency(ptr, (uint32)(((uint64)peak * rate) / IDEAL_FFT_SIZE));
  ptr = ideal_append_text(ptr, " ");
  ptr = ideal_append_int(ptr, ideal_spectrum[peak] / IDEAL_FFT_DB_STEPS);
  ptr = ideal_append_text(ptr, " dBFS   10 dB/div   0 to ");
  ideal_append_frequency(ptr, rate / 2);

  //Right of the T / F readout box in the top left corner of the trace area (x 6 to 106)
  display_set_font(&font_1);
  display_text(112, 64 + (line * 16), text);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Entry points
//----------------------------------------------------------------------------------------------------------------------------------

void sm_open_display_menu(void)
{
  ideal_menu_open(&ideal_display_menu, 1);
}

//----------------------------------------------------------------------------------------------------------------------------------

void sm_open_utilities_menu(void)
{
  ideal_menu_open(&ideal_utilities_menu, 1);
}

//----------------------------------------------------------------------------------------------------------------------------------

void sm_open_generator_menu(void)
{
  //Opened with the GEN key from normal operation: any other key closes it, like the stock menus
  fileviewstate   = FILE_VIEW_MENU_CONTROL;
  buttondialstate = BUTTON_DIAL_MENU_HANDLING;

  ideal_menu_open(&ideal_generator_menu, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------

#endif //PORT_1014D
