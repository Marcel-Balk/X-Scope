//----------------------------------------------------------------------------------------------------------------------------------
//1014D glue for analysis_1014d.c: runs the measurements, statistics and serial decoding once per new
//capture, draws their panels on the trace screen and keeps their settings.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ANALYSIS_SCOPE_H
#define ANALYSIS_SCOPE_H

#include "types.h"
#include "variables.h"
#include "analysis_1014d.h"

//----------------------------------------------------------------------------------------------------------------------------------

#define AS_DECODE_OFF            0
#define AS_DECODE_UART           1
#define AS_DECODE_I2C            2
#define AS_DECODE_MODES          3

#define AS_UART_FORMATS          3          //8N1, 8E1, 8O1

#define AS_EXTRA_OFF             0          //Extra measurement panel: off, CH1, CH2
#define AS_EXTRA_CHOICES         3

//Extra measurements shown in the panel and tracked by the statistics
#define AS_EXTRA_ITEMS           8

//Statistics: the six F1 to F6 slots plus the extra panel items
#define AS_STATS                 (6 + AS_EXTRA_ITEMS)

//Settings sector words (OTHER_SETTING_OFFSET + index to index + 2), after the IDEAL_SETTINGS_INDEX word
#define AS_SETTINGS_INDEX        31
#define AS_SETTINGS_MARKER_1     0x1E00
#define AS_SETTINGS_MARKER_2     0x1F00
#define AS_SETTINGS_MARKER_3     0x2000
#define AS_SETTINGS_WORDS        3

typedef struct
{
  uint8 decode;                             //AS_DECODE_*
  uint8 uart_baud;                          //0 = auto, else an_uart_bauds[uart_baud - 1]
  uint8 uart_format;                        //0 = 8N1, 1 = 8E1, 2 = 8O1
  uint8 uart_idle_low;                      //0 = idle high (TTL), 1 = idle low (inverted)
  uint8 uart_source;                        //0 = CH1, 1 = CH2
  uint8 event_list;                         //Show the decoded event list
  uint8 stats;                              //Statistics on (collect and show)
  uint8 stats_n;                            //Index into an_stat_counts
  uint8 extra;                              //AS_EXTRA_*: channel of the extra measurement panel
} ASCONFIG;

extern ASCONFIG  as_config;
extern ANMEASURE as_measure[2];
extern uint32    as_measure_captures;      //Captures measured since start up
extern ANDECODE  as_decode;
extern ANSTAT    as_stats[AS_STATS];
extern const uint8 as_extra_items[AS_EXTRA_ITEMS];

void   as_sanitize(void);
void   as_reset_stats(void);
void   as_capture_done(void);
void   as_measure_now(void);
void   as_draw(void);

uint16 as_settings_word(uint32 which);
void   as_settings_load(uint16 word1, uint16 word2, uint16 word3);

uint32 as_stat_channel(uint32 stat);
uint32 as_stat_measurement(uint32 stat);

void   as_channel_scale(PCHANNELSETTINGS settings, double *volts_per_code, double *volts_offset);

#endif /* ANALYSIS_SCOPE_H */
