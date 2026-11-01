//----------------------------------------------------------------------------------------------------------------------------------
//FFT and CSV formatting. Nothing in here touches the FPGA, the display or the SD card, so the host
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef IDEAL_1014D_H
#define IDEAL_1014D_H

#include "types.h"
#include "variables.h"

//----------------------------------------------------------------------------------------------------------------------------------
//Math channel modes (stored in the global mathmode, persisted in the settings sector)

#define IDEAL_MATH_OFF                  0
#define IDEAL_MATH_ADD                  1
#define IDEAL_MATH_SUB                  2
#define IDEAL_MATH_MUL                  3
#define IDEAL_MATH_DIV                  4
#define IDEAL_MATH_MODES                5

//Math channel sources (stored in the globals channelA and channelB)
#define IDEAL_MATH_SOURCES              4

//1014D extras word in the settings sector (OTHER_SETTING_OFFSET + index, the block uses 28 of 40 words)
#define IDEAL_SETTINGS_INDEX            30
#define IDEAL_SETTINGS_MARKER           0x1D00

//Trace styles in ACQ_trace: linear, dots, peak, pro and (1014D) sin(x)/x
#define IDEAL_TRACE_SINC                4
#define IDEAL_TRACE_STYLES              5

//----------------------------------------------------------------------------------------------------------------------------------
//FFT

#define IDEAL_FFT_SIZE                  1024
#define IDEAL_FFT_LOG2                  10
#define IDEAL_FFT_BINS                  (IDEAL_FFT_SIZE / 2)

//Per channel display mode stored in the channel fftenable byte
#define IDEAL_FFT_OFF                   0
#define IDEAL_FFT_ON                    1

//The magnitude output is in 1/4 dB steps relative to an 8 bit full scale sine (0 = full scale)
#define IDEAL_FFT_DB_STEPS              4

//----------------------------------------------------------------------------------------------------------------------------------
//Trigger hold off (software, MCU side re-arm delay; the stock FPGA has no hold off register)

#define IDEAL_HOLDOFF_STEPS             9

//----------------------------------------------------------------------------------------------------------------------------------

void ideal_sanitize_settings(void);

void ideal_math_compute(const uint8 *a, const uint8 *b, uint8 *out, uint32 count, uint32 mode);

int32 ideal_fft_magnitude_db(const uint8 *samples, uint32 count, int16 *out);
uint32 ideal_fft_peak_bin(const int16 *magnitude, uint32 bins);

uint32 ideal_format_csv_line(char *buffer, uint32 index, int64 time_ns, int32 ch1_mv, int32 ch2_mv);
char *ideal_append_int64(char *buffer, int64 value);
int32 ideal_sample_to_mv(int32 sample, int32 input_calibration, uint32 mv_per_div);

uint32 ideal_holdoff_ms(uint32 step);

const char *ideal_unit_text(PCHANNELSETTINGS settings);

int32 ideal_sinc_value_q4(const uint8 *samples, int32 count, int32 position_q8);

uint32 ideal_effective_sample_rate(void);

//Main loop timing in ms for the diagnostics page: [0] last value, [1] peak of the last 2 seconds
#define IDEAL_TIME_ACQUIRE    0
#define IDEAL_TIME_FRAME      1
#define IDEAL_TIME_LOOP       2
#define IDEAL_TIME_KEYS       3
#define IDEAL_TIMES           4

extern uint32 ideal_times[IDEAL_TIMES][2];

void ideal_record_time(uint32 which, uint32 ms, uint32 now);

//Timing overlay on the trace screen (Diagnostics), not saved
extern uint8 ideal_timing_overlay;

char *ideal_append_int(char *buffer, int32 value);
char *ideal_append_text(char *buffer, const char *text);
char *ideal_append_frequency(char *buffer, uint32 hz);
uint32 ideal_frequency_step(uint32 hz, int32 direction);

#define IDEAL_INPUT_CAL_MIN             2500000
#define IDEAL_INPUT_CAL_MAX             5000000

int32 ideal_input_calibration_factor(int32 sample_q4, int32 calibration_volts, int32 mul_factor);

#endif /* IDEAL_1014D_H */
