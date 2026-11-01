//----------------------------------------------------------------------------------------------------------------------------------
//Signal analysis on a captured buffer: automatic measurements, statistics over captures and serial
//decoding (UART, I2C). Nothing in here touches the FPGA, the display or the SD card, so the host
//
//Samples are the raw 8 bit ADC codes of one capture. A channel is described by its sample buffer and
//the linear code to volt mapping volts = (code - 128) * volts_per_code + volts_offset.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ANALYSIS_1014D_H
#define ANALYSIS_1014D_H

#include "types.h"

//----------------------------------------------------------------------------------------------------------------------------------
//Measurements. The first twelve are the measurements of the F1 to F6 slots, in the slot index order.

#define AN_VMAX                0
#define AN_VMIN                1
#define AN_VAVG                2
#define AN_VRMS                3          //True RMS, AC + DC
#define AN_VPP                 4
#define AN_VP                  5
#define AN_FREQ                6
#define AN_PERIOD              7
#define AN_PWIDTH              8
#define AN_NWIDTH              9
#define AN_PDUTY              10
#define AN_NDUTY              11
#define AN_RISE               12          //10 % to 90 % of the amplitude
#define AN_FALL               13          //90 % to 10 % of the amplitude
#define AN_OVERSHOOT          14          //Percent of the amplitude above the top level
#define AN_UNDERSHOOT         15          //Percent of the amplitude below the base level
#define AN_VRMS_AC            16          //True RMS of the AC part only
#define AN_VTOP               17
#define AN_VBASE              18
#define AN_VAMP               19
#define AN_PHASE              20          //CH1 to CH2 in degrees, -180 to 180 (pair measurement)
#define AN_DELAY              21          //CH1 rising edge to the nearest CH2 rising edge in seconds (pair)
#define AN_MEASUREMENTS       22

#define AN_SLOT_MEASUREMENTS  12

//Edge positions kept per direction for the edge based measurements
#define AN_MAX_EDGES         256

typedef struct
{
  const uint8 *samples;
  uint32       count;
  double       volts_per_code;
  double       volts_offset;
} ANCHANNEL;

typedef struct
{
  double value[AN_MEASUREMENTS];
  uint32 valid;                           //Bit per measurement

  //Mid level crossings in fractional sample positions, used for the pair measurements
  double rising[AN_MAX_EDGES];
  double falling[AN_MAX_EDGES];
  uint32 nrising;
  uint32 nfalling;
} ANMEASURE;

void   an_measure(const ANCHANNEL *channel, double samplerate, ANMEASURE *result);
void   an_measure_pair(ANMEASURE *ch1, const ANMEASURE *ch2, double samplerate);

const char *an_measure_name(uint32 measurement);
const char *an_measure_unit(uint32 measurement);
int32  an_measure_lookup(const char *name);

double an_sqrt(double value);

//----------------------------------------------------------------------------------------------------------------------------------
//Statistics over the last N values (one value per capture)

#define AN_STAT_MAX_N       1000
#define AN_STAT_COUNTS         7

typedef struct
{
  double values[AN_STAT_MAX_N];
  uint32 window;                          //N
  uint32 count;                           //Values in the window, up to N
  uint32 next;                            //Ring write position
  uint32 total;                           //Values added since the last reset
  double shift;                           //The sums are relative to this value
  double sum;
  double sumsq;
  double min;
  double max;
  double last;
} ANSTAT;

extern const uint16 an_stat_counts[AN_STAT_COUNTS];

void   an_stat_reset(ANSTAT *stat, uint32 window);
void   an_stat_add(ANSTAT *stat, double value);
double an_stat_mean(const ANSTAT *stat);
double an_stat_stddev(const ANSTAT *stat);

//----------------------------------------------------------------------------------------------------------------------------------
//Serial decoding

#define AN_MAX_EVENTS        128

#define AN_EVENT_BYTE          0          //UART data byte
#define AN_EVENT_START         1          //I2C start or repeated start
#define AN_EVENT_STOP          2          //I2C stop
#define AN_EVENT_ADDRESS       3          //I2C address byte, value is the 7 bit address
#define AN_EVENT_DATA          4          //I2C data byte

#define AN_FLAG_PARITY      0x01          //UART parity error
#define AN_FLAG_FRAMING     0x02          //UART stop bit not high
#define AN_FLAG_NAK         0x04          //I2C byte not acknowledged
#define AN_FLAG_READ        0x08          //I2C address with the read bit set
#define AN_FLAG_PARTIAL     0x10          //I2C byte cut off by a start or stop

#define AN_DECODE_OK           0
#define AN_DECODE_NO_SIGNAL   -1          //Flat line, no levels to decode
#define AN_DECODE_TOO_SLOW    -2          //Less than 4 samples per bit or clock phase
#define AN_DECODE_NO_BAUD     -3          //Auto baud found no usable bit time

#define AN_PARITY_NONE         0
#define AN_PARITY_EVEN         1
#define AN_PARITY_ODD          2

#define AN_UART_BAUDS         11

extern const uint32 an_uart_bauds[AN_UART_BAUDS];

typedef struct
{
  uint32 start;                           //First sample of the event
  uint32 end;                             //Last sample of the event
  uint16 value;
  uint8  type;
  uint8  flags;
} ANEVENT;

typedef struct
{
  ANEVENT events[AN_MAX_EVENTS];
  uint32  count;
  uint32  truncated;                      //More events than fit
  uint32  baud;                           //UART: the bit rate used
  int32   status;
} ANDECODE;

typedef struct
{
  uint32 baud;                            //0 = detect
  uint8  parity;                          //AN_PARITY_*
  uint8  idle_high;                       //1 for a TTL UART line, 0 for an inverted line
} ANUARTCONFIG;

uint32 an_uart_detect_baud(const uint8 *samples, uint32 count, double samplerate);
void   an_decode_uart(const uint8 *samples, uint32 count, double samplerate, const ANUARTCONFIG *config, ANDECODE *result);
void   an_decode_i2c(const uint8 *sda, const uint8 *scl, uint32 count, ANDECODE *result);

//----------------------------------------------------------------------------------------------------------------------------------
//Number formatting and parsing for the command channel

char  *an_format_double(char *buffer, double value, uint32 digits);
char  *an_format_si(char *buffer, double value, const char *unit);
uint32 an_parse_double(const char **text, double *value);
uint32 an_mnemonic(const char *pattern, const char *text, uint32 length);

#endif /* ANALYSIS_1014D_H */
