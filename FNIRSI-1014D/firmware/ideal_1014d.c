//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "variables.h"
#include "statemachine.h"
#include "ideal_1014d.h"

uint32 ideal_times[IDEAL_TIMES][2];
uint8  ideal_timing_overlay;

static uint32 ideal_times_peak[IDEAL_TIMES];
static uint32 ideal_times_window;

//Keep the last value and the peak of the previous 2 second window per measurement
void ideal_record_time(uint32 which, uint32 ms, uint32 now)
{
  uint32 i;

  if((now - ideal_times_window) >= 2000)
  {
    ideal_times_window = now;

    for(i=0;i<IDEAL_TIMES;i++)
    {
      ideal_times[i][1] = ideal_times_peak[i];
      ideal_times_peak[i] = 0;
    }
  }

  if(which < IDEAL_TIMES)
  {
    ideal_times[which][0] = ms;

    if(ms > ideal_times_peak[which])
    {
      ideal_times_peak[which] = ms;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Settings sanitizing
//
//The settings sector and the waveform files are shared with 1013D builds of the same tree and with
//older firmware versions. A 1013D build can store states the 1014D button UI has no control for
//(X-Y mode, math, references, 0.5x..1000x probes, ...) and a damaged file can store anything. Every
//value is brought into the range the 1014D code indexes tables with, and the 1014D-only mirror of
//the X-Y flag is rebuilt from the shared one, so whatever is active is also visible in the menus.
//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_clamp_flag(uint8 *value)
{
  if(*value > 1)
  {
    *value = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

static void ideal_sanitize_channel(PCHANNELSETTINGS settings, uint16 defaultposition)
{
  ideal_clamp_flag(&settings->enable);
  ideal_clamp_flag(&settings->coupling);
  ideal_clamp_flag(&settings->V_A);
  ideal_clamp_flag(&settings->fftenable);
  ideal_clamp_flag(&settings->invert);

  //Seven probe rows: 0.5x, 1x, 10x, 20x, 50x, 100x, 1000x
  if(settings->magnification > 6)
  {
    settings->magnification = 1;
  }

  //Seven volt per div steps per probe row
  if(settings->displayvoltperdiv > 6)
  {
    settings->displayvoltperdiv = 0;
  }

  if(settings->samplevoltperdiv > 6)
  {
    settings->samplevoltperdiv = settings->displayvoltperdiv;
  }

  if((settings->traceposition < VERTICAL_POINTER_POS_MIN) || (settings->traceposition > VERTICAL_POINTER_POS_MAX))
  {
    settings->traceposition = defaultposition;
  }

  if((settings->triggerverticalposition < VERTICAL_POINTER_POS_MIN) || (settings->triggerverticalposition > VERTICAL_POINTER_POS_MAX))
  {
    settings->triggerverticalposition = settings->traceposition;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void ideal_sanitize_settings(void)
{
  ideal_sanitize_channel(&scopesettings.channel1, 300);
  ideal_sanitize_channel(&scopesettings.channel2, 100);

  //35 time base entries and 29 sample rate entries
  if(scopesettings.timeperdiv > 34)
  {
    scopesettings.timeperdiv = 20;
  }

  if(scopesettings.samplerate > 28)
  {
    scopesettings.samplerate = time_per_div_sample_rate[scopesettings.timeperdiv];
  }

  if(scopesettings.triggermode > 2)
  {
    scopesettings.triggermode = 0;
  }

  ideal_clamp_flag(&scopesettings.triggeredge);
  ideal_clamp_flag(&scopesettings.triggerchannel);

  if((scopesettings.triggerverticalposition < VERTICAL_POINTER_POS_MIN) || (scopesettings.triggerverticalposition > VERTICAL_POINTER_POS_MAX))
  {
    scopesettings.triggerverticalposition = 300;
  }

  //The horizontal trigger position may legitimately lie off screen when the record is longer than
  //the screen (scope_calculate_sample_range_properties limits it); only reject absurd values
  if((scopesettings.triggerhorizontalposition < -16384) || (scopesettings.triggerhorizontalposition > 16384))
  {
    scopesettings.triggerhorizontalposition = 357;
  }

  //Move speed is MOVE_SPEED_FAST (10 on the 1014D, 0 on the 1013D) or MOVE_SPEED_SLOW
  if((scopesettings.movespeed != MOVE_SPEED_FAST) && (scopesettings.movespeed != MOVE_SPEED_SLOW))
  {
    scopesettings.movespeed = MOVE_SPEED_FAST;
  }

  if(scopesettings.screenbrightness > 100)
  {
    scopesettings.screenbrightness = 100;
  }

  if(scopesettings.gridbrightness > 100)
  {
    scopesettings.gridbrightness = 100;
  }

  ideal_clamp_flag(&scopesettings.gridenable);
  ideal_clamp_flag(&scopesettings.alwaystrigger50);
  ideal_clamp_flag(&scopesettings.xymodedisplay);
  ideal_clamp_flag(&scopesettings.confirmationmode);
  ideal_clamp_flag(&scopesettings.average_mode);
  ideal_clamp_flag(&scopesettings.new_autosetup);
  ideal_clamp_flag(&scopesettings.maxlight);
  ideal_clamp_flag(&scopesettings.ref_on_startup);
  ideal_clamp_flag(&scopesettings.lockcursors);
  ideal_clamp_flag(&scopesettings.timecursorsenable);
  ideal_clamp_flag(&scopesettings.voltcursorsenable);

  ideal_clamp_flag(&scopesettings.ref1);
  ideal_clamp_flag(&scopesettings.ref2);
  ideal_clamp_flag(&scopesettings.ref3);
  ideal_clamp_flag(&scopesettings.ref4);
  ideal_clamp_flag(&scopesettings.ref5);
  ideal_clamp_flag(&scopesettings.ref6);
  ideal_clamp_flag(&scopesettings.ref7);
  ideal_clamp_flag(&scopesettings.ref8);

  //Trace styles: linear, dots, peak, pro, sin(x)/x (the last one only exists on the 1014D)
#if PORT_1014D
  if(scopesettings.ACQ_trace >= IDEAL_TRACE_STYLES)
#else
  if(scopesettings.ACQ_trace > 3)
#endif
  {
    scopesettings.ACQ_trace = 0;
  }

  //The 1014D menus and drawing code use the X-Y flag of the shared settings; keep the 1014D
  //mirror equal so the on off panel always shows the active state
#if PORT_1014D
  uint32 index;

  scopesettings.tracedisplaymode = scopesettings.xymodedisplay;

  if(scopesettings.selectedcursor > CURSOR_VOLT_BOTTOM)
  {
    scopesettings.selectedcursor = CURSOR_TIME_LEFT;
  }

  for(index=0;index<6;index++)
  {
    if(scopesettings.measurementitems[index].channel > 1)
    {
      scopesettings.measurementitems[index].channel = 0;
    }

    if(scopesettings.measurementitems[index].index >= 12)
    {
      scopesettings.measurementitems[index].index = 0;
    }

    if(scopesettings.measurementitems[index].channel == 0)
    {
      scopesettings.measurementitems[index].channelsettings = &scopesettings.channel1;
    }
    else
    {
      scopesettings.measurementitems[index].channelsettings = &scopesettings.channel2;
    }
  }
#endif

  //Time cursors live inside the trace window and the left one stays left of the right one
  if((scopesettings.timecursor1position < 6) || (scopesettings.timecursor2position > 705) || (scopesettings.timecursor1position >= scopesettings.timecursor2position))
  {
    scopesettings.timecursor1position = 183;
    scopesettings.timecursor2position = 547;
  }

  //Volt cursors likewise, top above bottom
  if((scopesettings.voltcursor1position < 59) || (scopesettings.voltcursor2position > 458) || (scopesettings.voltcursor1position >= scopesettings.voltcursor2position))
  {
    scopesettings.voltcursor1position = 167;
    scopesettings.voltcursor2position = 328;
  }

  //Math channel
  if(mathmode >= IDEAL_MATH_MODES)
  {
    mathmode = IDEAL_MATH_OFF;
  }

  if(channelA >= IDEAL_MATH_SOURCES)
  {
    channelA = 0;
  }

  if(channelB >= IDEAL_MATH_SOURCES)
  {
    channelB = 1;
  }

  ideal_clamp_flag(&USB_CH340);

  //Generator: 1 Hz .. 10 MHz, duty in percent
  if((fpgasettings.gen_freq < 1) || (fpgasettings.gen_freq > 10000000))
  {
    fpgasettings.gen_freq = 1000;
  }

  if(fpgasettings.gen_duty > 100)
  {
    fpgasettings.gen_duty = 50;
  }

  if(fpgasettings.gen_enable > 1)
  {
    fpgasettings.gen_enable = 0;
  }

  //Software trigger hold off in milliseconds
  if(fpgasettings.en_holdoff_trigger > 1)
  {
    fpgasettings.en_holdoff_trigger = 0;
  }

  if(fpgasettings.value_holdoff_trigger > ideal_holdoff_ms(IDEAL_HOLDOFF_STEPS - 1))
  {
    fpgasettings.value_holdoff_trigger = ideal_holdoff_ms(IDEAL_HOLDOFF_STEPS - 1);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Math channel
//
//Samples are 8 bit with the signal center at 128. The operations are done on the centered values
//and the result is clamped back into the 8 bit range, so the math trace is drawn with the same
//scaling as a channel trace (in divisions of the channel 1 setting). Multiplication is scaled so
//that 1 division (25 counts) times 1 division gives 1 division; division gives 1 division when
//the operands are equal.
//----------------------------------------------------------------------------------------------------------------------------------

#define IDEAL_COUNTS_PER_DIV    25

void ideal_math_compute(const uint8 *a, const uint8 *b, uint8 *out, uint32 count, uint32 mode)
{
  uint32 i;
  int32  va, vb, result;

  for(i=0;i<count;i++)
  {
    va = (int32)a[i] - 128;
    vb = (int32)b[i] - 128;

    switch(mode)
    {
      case IDEAL_MATH_ADD:
        result = va + vb;
        break;

      case IDEAL_MATH_SUB:
        result = va - vb;
        break;

      case IDEAL_MATH_MUL:
        result = (va * vb) / IDEAL_COUNTS_PER_DIV;
        break;

      case IDEAL_MATH_DIV:
        if(vb == 0)
        {
          //Division by zero saturates in the direction of the dividend
          result = (va < 0) ? -128 : ((va > 0) ? 127 : 0);
        }
        else
        {
          result = (va * IDEAL_COUNTS_PER_DIV) / vb;
        }
        break;

      default:
        result = 0;
        break;
    }

    if(result > 127)
    {
      result = 127;
    }
    else if(result < -128)
    {
      result = -128;
    }

    out[i] = (uint8)(result + 128);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//FFT
//
//1024 point radix 2 decimation in time FFT in fixed point. The input is the 8 bit sample stream with
//its mean removed and a Hann window applied. Every butterfly stage halves the values, so nothing can
//overflow and the result is the DFT divided by N. The magnitude is returned in quarter dB relative
//to an 8 bit full scale sine (amplitude 127 counts), so 0 is full scale and -320 is -80 dB.
//No floating point: the ARM926 in the F1C100s has no FPU.
//----------------------------------------------------------------------------------------------------------------------------------

//Quarter wave sine table in Q15, sin(pi/2 * i / 256) for i = 0..256
static const int16 ideal_sine_q15[257] =
{
      0,   201,   402,   603,   804,  1005,  1206,  1407,  1608,  1809,  2009,  2210,
   2410,  2611,  2811,  3012,  3212,  3412,  3612,  3811,  4011,  4210,  4410,  4609,
   4808,  5007,  5205,  5404,  5602,  5800,  5998,  6195,  6393,  6590,  6786,  6983,
   7179,  7375,  7571,  7767,  7962,  8157,  8351,  8545,  8739,  8933,  9126,  9319,
   9512,  9704,  9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605,
  11793, 11980, 12167, 12353, 12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
  14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269, 15446, 15623, 15800, 15976,
  16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
  18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000,
  20159, 20317, 20475, 20631, 20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
  22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027, 23170, 23311, 23452, 23592,
  23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
  25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674,
  26790, 26905, 27019, 27133, 27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
  28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803, 28898, 28992, 29085, 29177,
  29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
  30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050,
  31113, 31176, 31237, 31297, 31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
  31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098, 32137, 32176, 32213, 32250,
  32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
  32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752,
  32757, 32761, 32765, 32766, 32767
};

//log2(1 + i/64) in Q8 for i = 0..64
static const uint8 ideal_log2_fraction_q8[65] =
{
    0,   6,  11,  17,  22,  28,  33,  38,  44,  49,  54,  59,  63,  68,  73,  78,
   82,  87,  92,  96, 100, 105, 109, 113, 118, 122, 126, 130, 134, 138, 142, 146,
  150, 154, 157, 161, 165, 169, 172, 176, 179, 183, 186, 190, 193, 197, 200, 203,
  207, 210, 213, 216, 220, 223, 226, 229, 232, 235, 238, 241, 244, 247, 250, 253,
  255
};

//Work buffers, 8 KB in total
static int32 ideal_fft_re[IDEAL_FFT_SIZE];
static int32 ideal_fft_im[IDEAL_FFT_SIZE];

//----------------------------------------------------------------------------------------------------------------------------------
//sin(2 * pi * k / N) in Q15 for k = 0..N-1

static int32 ideal_sin_q15(uint32 k)
{
  k &= IDEAL_FFT_SIZE - 1;

  if(k <= 256)
  {
    return(ideal_sine_q15[k]);
  }
  else if(k <= 512)
  {
    return(ideal_sine_q15[512 - k]);
  }
  else if(k <= 768)
  {
    return(-ideal_sine_q15[k - 512]);
  }

  return(-ideal_sine_q15[1024 - k]);
}

//----------------------------------------------------------------------------------------------------------------------------------

static int32 ideal_cos_q15(uint32 k)
{
  return(ideal_sin_q15(k + (IDEAL_FFT_SIZE / 4)));
}

//----------------------------------------------------------------------------------------------------------------------------------
//10 * log10(value) in quarter dB, value > 0

static int32 ideal_power_to_qdb(uint32 value)
{
  int32  msb = 31;
  uint32 fraction;
  int32  log2q8;

  while((value & 0x80000000) == 0)
  {
    value <<= 1;
    msb--;
  }

  //The six bits below the leading one index the fraction table
  fraction = (value >> 25) & 0x3F;

  log2q8 = (msb << 8) + ideal_log2_fraction_q8[fraction];

  //10 * log10(2) * 4 = 12.0412 quarter dB per octave; 12.0412 * 65536 / 256 = 3083
  return((log2q8 * 3083) >> 16);
}

//----------------------------------------------------------------------------------------------------------------------------------

int32 ideal_fft_magnitude_db(const uint8 *samples, uint32 count, int16 *out)
{
  uint32 i, j, k, bit;
  uint32 size, half, step;
  int32  mean = 0;
  int32  tr, ti, wr, wi;
  int32  reference;
  uint32 power;

  if((samples == 0) || (out == 0) || (count == 0))
  {
    return(-1);
  }

  if(count > IDEAL_FFT_SIZE)
  {
    count = IDEAL_FFT_SIZE;
  }

  for(i=0;i<count;i++)
  {
    mean += samples[i];
  }

  mean /= (int32)count;

  //Remove the mean, scale up by 128 for headroom, apply the Hann window and store bit reversed.
  //Samples beyond count are zero padded
  for(i=0;i<IDEAL_FFT_SIZE;i++)
  {
    int32 value = 0;

    if(i < count)
    {
      //Hann: 0.5 * (1 - cos(2 pi i / N)) in Q15
      int32 window = (32767 - ideal_cos_q15(i)) >> 1;

      value = (((int32)samples[i] - mean) * 128 * window) >> 15;
    }

    for(j=0,k=i,bit=0;bit<IDEAL_FFT_LOG2;bit++)
    {
      j = (j << 1) | (k & 1);
      k >>= 1;
    }

    ideal_fft_re[j] = value;
    ideal_fft_im[j] = 0;
  }

  //Butterfly stages
  for(size=2;size<=IDEAL_FFT_SIZE;size<<=1)
  {
    half = size >> 1;
    step = IDEAL_FFT_SIZE / size;

    for(i=0;i<IDEAL_FFT_SIZE;i+=size)
    {
      for(j=0;j<half;j++)
      {
        uint32 a = i + j;
        uint32 b = a + half;

        //Twiddle e^(-2 pi j k / N)
        wr = ideal_cos_q15(j * step);
        wi = -ideal_sin_q15(j * step);

        tr = (ideal_fft_re[b] * wr - ideal_fft_im[b] * wi) >> 15;
        ti = (ideal_fft_re[b] * wi + ideal_fft_im[b] * wr) >> 15;

        ideal_fft_re[b] = (ideal_fft_re[a] - tr) >> 1;
        ideal_fft_im[b] = (ideal_fft_im[a] - ti) >> 1;
        ideal_fft_re[a] = (ideal_fft_re[a] + tr) >> 1;
        ideal_fft_im[a] = (ideal_fft_im[a] + ti) >> 1;
      }
    }
  }

  //A full scale sine of 127 counts scaled by 128 and Hann windowed (coherent gain 0.5) gives a bin
  //amplitude of 127 * 128 * 0.5 / 2 = 4064
  reference = ideal_power_to_qdb(4064 * 4064);

  for(i=0;i<IDEAL_FFT_BINS;i++)
  {
    power = (uint32)(ideal_fft_re[i] * ideal_fft_re[i]) + (uint32)(ideal_fft_im[i] * ideal_fft_im[i]);

    if(power == 0)
    {
      out[i] = -32000;
    }
    else
    {
      out[i] = ideal_power_to_qdb(power) - reference;
    }
  }

  return(0);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Highest bin, ignoring the DC bin and its direct neighbor (window leakage of the removed mean)

uint32 ideal_fft_peak_bin(const int16 *magnitude, uint32 bins)
{
  uint32 i;
  uint32 peak = 2;

  if(bins <= 2)
  {
    return(0);
  }

  for(i=2;i<bins;i++)
  {
    if(magnitude[i] > magnitude[peak])
    {
      peak = i;
    }
  }

  return(peak);
}

//----------------------------------------------------------------------------------------------------------------------------------
//CSV export helpers
//----------------------------------------------------------------------------------------------------------------------------------

char *ideal_append_int(char *buffer, int32 value)
{
  char   digits[12];
  uint32 count = 0;
  uint32 magnitude;

  if(value < 0)
  {
    *buffer++ = '-';
    magnitude = (uint32)(-(value + 1)) + 1;
  }
  else
  {
    magnitude = (uint32)value;
  }

  do
  {
    digits[count++] = '0' + (magnitude % 10);
    magnitude /= 10;
  } while(magnitude);

  while(count)
  {
    *buffer++ = digits[--count];
  }

  return(buffer);
}

//----------------------------------------------------------------------------------------------------------------------------------

char *ideal_append_int64(char *buffer, int64 value)
{
  char   digits[21];
  uint32 count = 0;
  uint64 magnitude;

  if(value < 0)
  {
    *buffer++ = '-';
    magnitude = (uint64)(-(value + 1)) + 1;
  }
  else
  {
    magnitude = (uint64)value;
  }

  do
  {
    digits[count++] = '0' + (magnitude % 10);
    magnitude /= 10;
  } while(magnitude);

  while(count)
  {
    *buffer++ = digits[--count];
  }

  return(buffer);
}

//----------------------------------------------------------------------------------------------------------------------------------
//One CSV row: sample index, time in ns relative to the trigger, both channels in mV. Returns the
//length; the buffer needs room for 60 characters.

uint32 ideal_format_csv_line(char *buffer, uint32 index, int64 time_ns, int32 ch1_mv, int32 ch2_mv)
{
  char *ptr = buffer;

  ptr = ideal_append_int(ptr, (int32)index);
  *ptr++ = ',';
  ptr = ideal_append_int64(ptr, time_ns);
  *ptr++ = ',';
  ptr = ideal_append_int(ptr, ch1_mv);
  *ptr++ = ',';
  ptr = ideal_append_int(ptr, ch2_mv);
  *ptr++ = '\r';
  *ptr++ = '\n';
  *ptr = 0;

  return(ptr - buffer);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Convert a centered sample to millivolts. The display maps a centered sample to screen pixels with
//(sample * input_calibration) >> VOLTAGE_SHIFTER and one division is 50 pixels, so the same
//mapping with the volts per division setting (in mV, probe factor applied) gives the voltage.

int32 ideal_sample_to_mv(int32 sample, int32 input_calibration, uint32 mv_per_div)
{
  int64 scaled = (int64)sample * input_calibration * (int64)mv_per_div;

  //Round to nearest, symmetric around zero
  int64 divisor = (int64)50 << VOLTAGE_SHIFTER;

  if(scaled < 0)
  {
    return((int32)-((-scaled + (divisor / 2)) / divisor));
  }

  return((int32)((scaled + (divisor / 2)) / divisor));
}

//----------------------------------------------------------------------------------------------------------------------------------
//Trigger hold off steps in milliseconds

static const uint16 ideal_holdoff_table[IDEAL_HOLDOFF_STEPS] =
{
  0, 10, 20, 50, 100, 200, 500, 1000, 2000
};

uint32 ideal_holdoff_ms(uint32 step)
{
  if(step >= IDEAL_HOLDOFF_STEPS)
  {
    step = IDEAL_HOLDOFF_STEPS - 1;
  }

  return(ideal_holdoff_table[step]);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Copy a zero terminated text and return the position of the terminator (like this tree's strcpy)

char *ideal_append_text(char *buffer, const char *text)
{
  while(*text)
  {
    *buffer++ = *text++;
  }

  *buffer = 0;

  return(buffer);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Frequency as text with Hz, kHz or MHz and at most two decimals without trailing zeros: 1500 gives
//"1.5kHz", 2000000 gives "2MHz"

char *ideal_append_frequency(char *buffer, uint32 hz)
{
  uint32 scale = 1;
  const char *unit = "Hz";
  uint32 whole, fraction;

  if(hz >= 1000000)
  {
    scale = 1000000;
    unit = "MHz";
  }
  else if(hz >= 1000)
  {
    scale = 1000;
    unit = "kHz";
  }

  whole = hz / scale;
  fraction = ((hz % scale) * 100) / scale;

  buffer = ideal_append_int(buffer, (int32)whole);

  if(fraction)
  {
    *buffer++ = '.';
    *buffer++ = '0' + (fraction / 10);

    if(fraction % 10)
    {
      *buffer++ = '0' + (fraction % 10);
    }
  }

  return(ideal_append_text(buffer, unit));
}

//----------------------------------------------------------------------------------------------------------------------------------
//Generator frequency steps: 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8 per decade from 1 Hz to 10 MHz

static const uint8 ideal_frequency_mantissa[10] = { 10, 12, 15, 20, 25, 30, 40, 50, 60, 80 };

uint32 ideal_frequency_step(uint32 hz, int32 direction)
{
  uint32 decade = 1;
  uint32 index = 0;
  uint32 value;
  int32  step;

  if(hz < 1)
  {
    hz = 1;
  }

  //Find the largest step value that is not above the given frequency
  while((hz / decade) >= 10)
  {
    decade *= 10;
  }

  for(step=0;step<10;step++)
  {
    if(((ideal_frequency_mantissa[step] * decade) / 10) <= hz)
    {
      index = step;
    }
  }

  value = (ideal_frequency_mantissa[index] * decade) / 10;

  //Between steps going down lands on the lower step. Otherwise move along the steps until the
  //value changes (below 10 Hz several steps round to the same whole Hz value)
  if((direction > 0) || (value == hz))
  {
    step = (int32)index;

    do
    {
      step += (direction > 0) ? 1 : -1;

      if(step >= 10)
      {
        step = 0;
        decade *= 10;
      }
      else if(step < 0)
      {
        step = 9;
        decade /= 10;
      }

      if(decade == 0)
      {
        return(1);
      }

      value = (ideal_frequency_mantissa[step] * decade) / 10;
    } while((value == hz) && (value < 10000000));
  }

  if(value < 1)
  {
    value = 1;
  }

  if(value > 10000000)
  {
    value = 10000000;
  }

  return(value);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Input (gain) calibration factor. The display computes volts = ((sample * factor) >> VOLTAGE_SHIFTER)
//row). sample_q4 is the measured average in 1/16 ADC counts relative to the center. Returns the
//factor, or -1 when the signal is missing, has the wrong polarity or the result is implausible.

int32 ideal_input_calibration_factor(int32 sample_q4, int32 calibration_volts, int32 mul_factor)
{
  int64 factor;

  if((sample_q4 <= 0) || (calibration_volts <= 0) || (mul_factor <= 0))
  {
    return(-1);
  }

  factor = (((int64)calibration_volts << (VOLTAGE_SHIFTER + 4)) + ((int64)sample_q4 * mul_factor / 2)) / ((int64)sample_q4 * mul_factor);

  if((factor < IDEAL_INPUT_CAL_MIN) || (factor > IDEAL_INPUT_CAL_MAX))
  {
    return(-1);
  }

  return((int32)factor);
}

//----------------------------------------------------------------------------------------------------------------------------------

const char *ideal_unit_text(PCHANNELSETTINGS settings)
{
  return(settings->V_A ? "A" : "V");
}

//----------------------------------------------------------------------------------------------------------------------------------
//Sin(x)/x interpolation
//
//Band limited reconstruction between samples with a Lanczos (a = 3) windowed sinc kernel: six taps,
//32 phases between two samples, weights in Q14 normalized to a sum of 1. Used to draw the trace when
//the screen shows fewer samples than pixels (fast time bases), where linear interpolation turns a
//sampled sine into a polygon.
//----------------------------------------------------------------------------------------------------------------------------------

static const int16 ideal_sinc_weights[32][6] =
{
  {      0,      0,  16384,      0,      0,      0 },
  {    100,   -405,  16356,    442,   -111,      2 },
  {    190,   -772,  16272,    919,   -232,      7 },
  {    268,  -1101,  16131,   1432,   -363,     17 },
  {    335,  -1393,  15936,   1977,   -501,     30 },
  {    391,  -1646,  15686,   2553,   -647,     47 },
  {    436,  -1862,  15384,   3157,   -799,     68 },
  {    470,  -2040,  15029,   3787,   -955,     93 },
  {    493,  -2184,  14628,   4440,  -1114,    121 },
  {    507,  -2292,  14179,   5112,  -1274,    152 },
  {    513,  -2367,  13685,   5800,  -1432,    185 },
  {    510,  -2411,  13153,   6499,  -1587,    220 },
  {    499,  -2425,  12583,   7206,  -1736,    257 },
  {    482,  -2411,  11980,   7916,  -1877,    294 },
  {    459,  -2372,  11349,   8624,  -2007,    331 },
  {    432,  -2309,  10693,   9326,  -2125,    367 },
  {    401,  -2226,  10017,  10017,  -2226,    401 },
  {    367,  -2125,   9326,  10693,  -2309,    432 },
  {    331,  -2007,   8624,  11349,  -2372,    459 },
  {    294,  -1877,   7916,  11980,  -2411,    482 },
  {    257,  -1736,   7206,  12583,  -2425,    499 },
  {    220,  -1587,   6499,  13153,  -2411,    510 },
  {    185,  -1432,   5799,  13686,  -2367,    513 },
  {    152,  -1274,   5113,  14178,  -2292,    507 },
  {    121,  -1114,   4441,  14627,  -2184,    493 },
  {     93,   -955,   3786,  15030,  -2040,    470 },
  {     68,   -799,   3158,  15383,  -1862,    436 },
  {     47,   -647,   2553,  15686,  -1646,    391 },
  {     30,   -501,   1977,  15936,  -1393,    335 },
  {     17,   -363,   1432,  16131,  -1101,    268 },
  {      7,   -232,    919,  16272,   -772,    190 },
  {      2,   -111,    442,  16356,   -405,    100 }
};

//Value at position (in 1/256 samples) in 1/16 counts. Samples outside 0..count-1 repeat the edge
int32 ideal_sinc_value_q4(const uint8 *samples, int32 count, int32 position_q8)
{
  int32 index = position_q8 >> 8;
  int32 phase = (position_q8 & 0xFF) >> 3;
  int32 sum = 0;
  int32 tap, at;

  if(count <= 0)
  {
    return(128 * 16);
  }

  for(tap=0;tap<6;tap++)
  {
    at = index + tap - 2;

    if(at < 0)
    {
      at = 0;
    }
    else if(at >= count)
    {
      at = count - 1;
    }

    sum += samples[at] * ideal_sinc_weights[phase][tap];
  }

  //Q14 weights to Q4 result, rounded
  return((sum + (1 << 9)) >> 10);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Actual sample rate of the current setting in samples per second, including a non stock sampling
//clock (Factory settings > Sampling clock), the same way scope_calculate_sample_range_properties does

uint32 ideal_effective_sample_rate(void)
{
  uint32 index = (scopesettings.samplerate < 29) ? scopesettings.samplerate : 28;

  return((uint32)((sample_rate[index] * sampling_clock_scale) + 0.5));
}

//----------------------------------------------------------------------------------------------------------------------------------
