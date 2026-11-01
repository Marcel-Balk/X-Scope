//----------------------------------------------------------------------------------------------------------------------------------
//Signal analysis on a captured buffer. See analysis_1014d.h.
//
//The loops over the samples use integer arithmetic only (the ARM926 has no floating point unit);
//floating point is used per edge, per measurement and per decoded bit, which keeps one capture of
//3000 samples per channel well below a millisecond of work.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "analysis_1014d.h"

//----------------------------------------------------------------------------------------------------------------------------------
//Names, units and lookup

static const char *an_names[AN_MEASUREMENTS] =
{
  "VMAX", "VMIN", "VAVG", "VRMS", "VPP", "VP", "FREQ", "PERIOD", "PWIDTH", "NWIDTH", "PDUTY", "NDUTY",
  "RISE", "FALL", "OVERSHOOT", "UNDERSHOOT", "VRMSAC", "VTOP", "VBASE", "VAMP", "PHASE", "DELAY"
};

static const char *an_units[AN_MEASUREMENTS] =
{
  "V", "V", "V", "V", "V", "V", "Hz", "s", "s", "s", "%", "%",
  "s", "s", "%", "%", "V", "V", "V", "V", "deg", "s"
};

const char *an_measure_name(uint32 measurement)
{
  return((measurement < AN_MEASUREMENTS) ? an_names[measurement] : "");
}

const char *an_measure_unit(uint32 measurement)
{
  return((measurement < AN_MEASUREMENTS) ? an_units[measurement] : "");
}

static char an_upper(char c)
{
  return(((c >= 'a') && (c <= 'z')) ? (c - 32) : c);
}

//Case insensitive full match of a measurement name, ended by the string end, a space or a comma
int32 an_measure_lookup(const char *name)
{
  uint32 i, j;

  for(i=0;i<AN_MEASUREMENTS;i++)
  {
    for(j=0;an_names[i][j] && (an_upper(name[j]) == an_names[i][j]);j++);

    if((an_names[i][j] == 0) && ((name[j] == 0) || (name[j] == ' ') || (name[j] == ',')))
    {
      return(i);
    }
  }

  return(-1);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Square root without libm: an exponent halving first guess and Newton steps

double an_sqrt(double value)
{
  union { double d; uint64 u; } x;
  uint32 i;

  if(value <= 0.0)
  {
    return(0.0);
  }

  x.d = value;
  x.u = (x.u >> 1) + 0x1FF7A3BEA91D9B1BULL;

  for(i=0;i<6;i++)
  {
    x.d = 0.5 * (x.d + (value / x.d));
  }

  return(x.d);
}

static double an_abs(double value)
{
  return((value < 0.0) ? -value : value);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Measurements
//----------------------------------------------------------------------------------------------------------------------------------

#define AN_SET(r, m, v)   { (r)->value[m] = (v); (r)->valid |= (1u << (m)); }

//Fractional position where the samples cross level between index - 1 and index
static double an_cross(const uint8 *s, uint32 index, double level)
{
  double a = s[index - 1];
  double b = s[index];

  if(a == b)
  {
    return((double)index);
  }

  return((double)(index - 1) + ((level - a) / (b - a)));
}

void an_measure(const ANCHANNEL *channel, double samplerate, ANMEASURE *result)
{
  const uint8 *s = channel->samples;
  uint32 n = channel->count;
  uint32 histogram[256];
  uint32 i, j, minimum = 255, maximum = 0;
  uint64 sum = 0, sumsq = 0;
  double k = channel->volts_per_code;
  double offset = channel->volts_offset;
  double mean, variance, a, b;

  result->valid = 0;
  result->nrising = 0;
  result->nfalling = 0;

  if((s == 0) || (n < 4) || (samplerate <= 0.0))
  {
    return;
  }

  for(i=0;i<256;i++)
  {
    histogram[i] = 0;
  }

  for(i=0;i<n;i++)
  {
    uint32 c = s[i];
    int32  d = (int32)c - 128;

    histogram[c]++;
    sum += c;
    sumsq += (uint64)(d * d);

    if(c < minimum)
    {
      minimum = c;
    }

    if(c > maximum)
    {
      maximum = c;
    }
  }

  //Level measurements
  mean = ((double)sum / n) - 128.0;
  variance = ((double)sumsq / n) - (mean * mean);

  if(variance < 0.0)
  {
    variance = 0.0;
  }

  a = (((double)maximum - 128.0) * k) + offset;
  b = (((double)minimum - 128.0) * k) + offset;

  AN_SET(result, AN_VMAX, (a > b) ? a : b);
  AN_SET(result, AN_VMIN, (a > b) ? b : a);
  AN_SET(result, AN_VAVG, (mean * k) + offset);
  AN_SET(result, AN_VRMS_AC, an_abs(k) * an_sqrt(variance));
  AN_SET(result, AN_VRMS, an_sqrt((result->value[AN_VRMS_AC] * result->value[AN_VRMS_AC]) + (result->value[AN_VAVG] * result->value[AN_VAVG])));
  AN_SET(result, AN_VPP, (double)(maximum - minimum) * an_abs(k));
  AN_SET(result, AN_VP, (an_abs(a) > an_abs(b)) ? an_abs(a) : an_abs(b));

  //Top and base: the most frequent code in the upper and lower 40 % of the range, when it holds at
  //least 5 % of the samples (a pulse); otherwise the extremes (a sine or a triangle)
  {
    uint32 span = maximum - minimum;
    uint32 top = maximum, base = minimum, best;
    double amplitude, mid, low10, high90;
    int32  midq, highq, lowq, state = -1;
    uint32 lastedge = 0;
    double risesum = 0.0, fallsum = 0.0;
    uint32 risecount = 0, fallcount = 0;
    double firstrise = 0.0, lastrise = 0.0, firstfall = 0.0, lastfall = 0.0;
    uint32 totalrise = 0, totalfall = 0;

    if(span >= 4)
    {
      best = 0;

      for(i=maximum;i>=(minimum + ((span * 6) / 10));i--)
      {
        if(histogram[i] > best)
        {
          best = histogram[i];
          top = i;
        }
      }

      if((best * 20) < n)
      {
        top = maximum;
      }

      best = 0;

      for(i=minimum;i<=(minimum + ((span * 4) / 10));i++)
      {
        if(histogram[i] > best)
        {
          best = histogram[i];
          base = i;
        }
      }

      if((best * 20) < n)
      {
        base = minimum;
      }

      if(top <= base)
      {
        top = maximum;
        base = minimum;
      }
    }

    amplitude = (double)top - (double)base;

    AN_SET(result, AN_VTOP, (((double)top - 128.0) * k) + offset);
    AN_SET(result, AN_VBASE, (((double)base - 128.0) * k) + offset);
    AN_SET(result, AN_VAMP, amplitude * an_abs(k));

    if((span < 4) || (amplitude < 3.0))
    {
      //A flat line: no edges to measure
      return;
    }

    AN_SET(result, AN_OVERSHOOT, (((double)maximum - (double)top) * 100.0) / amplitude);
    AN_SET(result, AN_UNDERSHOOT, (((double)base - (double)minimum) * 100.0) / amplitude);

    //Edges: a transition needs to pass the mid level by 10 % of the amplitude (hysteresis); the edge
    //time is where the samples cross the mid level, interpolated between two samples
    mid    = ((double)top + (double)base) / 2.0;
    low10  = (double)base + (amplitude * 0.1);
    high90 = (double)base + (amplitude * 0.9);
    midq   = (int32)(mid * 16.0);
    highq  = (int32)((mid + (amplitude * 0.1)) * 16.0);
    lowq   = (int32)((mid - (amplitude * 0.1)) * 16.0);

    for(i=0;i<n;i++)
    {
      int32 c = (int32)s[i] * 16;

      if((state != 1) && (c > highq))
      {
        if(state == 0)
        {
          double position, start, end;
          uint32 p;

          //Back to the mid level crossing
          for(j=i;(j>lastedge) && (((int32)s[j - 1] * 16) > midq);j--);

          position = (j > lastedge) ? an_cross(s, j, mid) : (double)j;

          //10 % level before the crossing and 90 % level after it
          for(p=j;(p>lastedge) && ((double)s[p - 1] > low10);p--);

          start = (p > lastedge) ? an_cross(s, p, low10) : -1.0;

          for(p=i;(p<n) && ((double)s[p] < high90) && (((int32)s[p] * 16) > lowq);p++);

          end = ((p < n) && ((double)s[p] >= high90) && (p > 0)) ? an_cross(s, p, high90) : -1.0;

          if((start >= 0.0) && (end >= start))
          {
            risesum += end - start;
            risecount++;
          }

          if(result->nrising < AN_MAX_EDGES)
          {
            result->rising[result->nrising++] = position;
          }

          if(totalrise == 0)
          {
            firstrise = position;
          }

          lastrise = position;
          totalrise++;
          lastedge = i;
        }

        state = 1;
      }
      else if((state != 0) && (c < lowq))
      {
        if(state == 1)
        {
          double position, start, end;
          uint32 p;

          for(j=i;(j>lastedge) && (((int32)s[j - 1] * 16) < midq);j--);

          position = (j > lastedge) ? an_cross(s, j, mid) : (double)j;

          for(p=j;(p>lastedge) && ((double)s[p - 1] < high90);p--);

          start = (p > lastedge) ? an_cross(s, p, high90) : -1.0;

          for(p=i;(p<n) && ((double)s[p] > low10) && (((int32)s[p] * 16) < highq);p++);

          end = ((p < n) && ((double)s[p] <= low10) && (p > 0)) ? an_cross(s, p, low10) : -1.0;

          if((start >= 0.0) && (end >= start))
          {
            fallsum += end - start;
            fallcount++;
          }

          if(result->nfalling < AN_MAX_EDGES)
          {
            result->falling[result->nfalling++] = position;
          }

          if(totalfall == 0)
          {
            firstfall = position;
          }

          lastfall = position;
          totalfall++;
          lastedge = i;
        }

        state = 0;
      }
    }

    if(risecount)
    {
      AN_SET(result, AN_RISE, (risesum / risecount) / samplerate);
    }

    if(fallcount)
    {
      AN_SET(result, AN_FALL, (fallsum / fallcount) / samplerate);
    }

    //Period from the average distance of all complete periods
    {
      double period = 0.0;

      if(totalrise >= 2)
      {
        period = (lastrise - firstrise) / (totalrise - 1);
      }
      else if(totalfall >= 2)
      {
        period = (lastfall - firstfall) / (totalfall - 1);
      }

      if(period > 0.0)
      {
        AN_SET(result, AN_PERIOD, period / samplerate);
        AN_SET(result, AN_FREQ, samplerate / period);
      }

      //Pulse widths: each rising edge to the next falling edge and the other way around
      {
        double widthsum = 0.0;
        uint32 widthcount = 0;

        for(i=0,j=0;i<result->nrising;i++)
        {
          while((j < result->nfalling) && (result->falling[j] <= result->rising[i]))
          {
            j++;
          }

          if(j < result->nfalling)
          {
            widthsum += result->falling[j] - result->rising[i];
            widthcount++;
          }
        }

        if(widthcount)
        {
          AN_SET(result, AN_PWIDTH, (widthsum / widthcount) / samplerate);
        }

        widthsum = 0.0;
        widthcount = 0;

        for(i=0,j=0;i<result->nfalling;i++)
        {
          while((j < result->nrising) && (result->rising[j] <= result->falling[i]))
          {
            j++;
          }

          if(j < result->nrising)
          {
            widthsum += result->rising[j] - result->falling[i];
            widthcount++;
          }
        }

        if(widthcount)
        {
          AN_SET(result, AN_NWIDTH, (widthsum / widthcount) / samplerate);
        }
      }

      if((period > 0.0) && (result->valid & (1u << AN_PWIDTH)))
      {
        AN_SET(result, AN_PDUTY, (result->value[AN_PWIDTH] * 100.0) / result->value[AN_PERIOD]);
      }

      if((period > 0.0) && (result->valid & (1u << AN_NWIDTH)))
      {
        AN_SET(result, AN_NDUTY, (result->value[AN_NWIDTH] * 100.0) / result->value[AN_PERIOD]);
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Delay: average distance from each CH1 rising edge to the nearest CH2 rising edge. Phase: that delay
//as a part of the CH1 period, -180 to 180 degrees (positive when CH2 lags CH1).

void an_measure_pair(ANMEASURE *ch1, const ANMEASURE *ch2, double samplerate)
{
  double sum = 0.0;
  uint32 i, j = 0;

  ch1->valid &= ~((1u << AN_PHASE) | (1u << AN_DELAY));

  if((ch1->nrising == 0) || (ch2->nrising == 0) || (samplerate <= 0.0))
  {
    return;
  }

  for(i=0;i<ch1->nrising;i++)
  {
    while(((j + 1) < ch2->nrising) && (an_abs(ch2->rising[j + 1] - ch1->rising[i]) <= an_abs(ch2->rising[j] - ch1->rising[i])))
    {
      j++;
    }

    sum += ch2->rising[j] - ch1->rising[i];
  }

  AN_SET(ch1, AN_DELAY, (sum / ch1->nrising) / samplerate);

  if((ch1->valid & (1u << AN_PERIOD)) && (ch1->value[AN_PERIOD] > 0.0))
  {
    double phase = (ch1->value[AN_DELAY] * 360.0) / ch1->value[AN_PERIOD];

    while(phase > 180.0)
    {
      phase -= 360.0;
    }

    while(phase <= -180.0)
    {
      phase += 360.0;
    }

    AN_SET(ch1, AN_PHASE, phase);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Statistics over the last N values. The sums are kept relative to a shift value close to the data,
//so the standard deviation of a large value with a small spread (a frequency) keeps its precision.
//----------------------------------------------------------------------------------------------------------------------------------

const uint16 an_stat_counts[AN_STAT_COUNTS] = { 10, 20, 50, 100, 200, 500, 1000 };

void an_stat_reset(ANSTAT *stat, uint32 window)
{
  if(window < 1)
  {
    window = 1;
  }
  else if(window > AN_STAT_MAX_N)
  {
    window = AN_STAT_MAX_N;
  }

  stat->window = window;
  stat->count = 0;
  stat->next = 0;
  stat->total = 0;
  stat->shift = 0.0;
  stat->sum = 0.0;
  stat->sumsq = 0.0;
  stat->min = 0.0;
  stat->max = 0.0;
  stat->last = 0.0;
}

//Exact sums and extremes from the window, relative to its newest value
static void an_stat_rebuild(ANSTAT *stat)
{
  uint32 i;

  stat->shift = stat->last;
  stat->sum = 0.0;
  stat->sumsq = 0.0;
  stat->min = stat->values[0];
  stat->max = stat->values[0];

  for(i=0;i<stat->count;i++)
  {
    double v = stat->values[i];
    double d = v - stat->shift;

    stat->sum += d;
    stat->sumsq += d * d;

    if(v < stat->min)
    {
      stat->min = v;
    }

    if(v > stat->max)
    {
      stat->max = v;
    }
  }
}

//Running sums with one rebuild per N values, so an add costs O(1) apart from the occasional rescan
//of the extremes when the value leaving the window was one of them
void an_stat_add(ANSTAT *stat, double value)
{
  uint32 rebuild = 0;

  if((stat->window < 1) || (stat->window > AN_STAT_MAX_N))
  {
    an_stat_reset(stat, 10);
  }

  if(stat->count == stat->window)
  {
    double out = stat->values[stat->next];
    double d = out - stat->shift;

    stat->sum -= d;
    stat->sumsq -= d * d;

    rebuild = (out <= stat->min) || (out >= stat->max);
  }
  else
  {
    stat->count++;
  }

  stat->values[stat->next] = value;
  stat->next = (stat->next + 1) % stat->window;
  stat->total++;
  stat->last = value;

  if((stat->count == 1) || rebuild || (stat->next == 0))
  {
    an_stat_rebuild(stat);
  }
  else
  {
    double d = value - stat->shift;

    stat->sum += d;
    stat->sumsq += d * d;

    if(value < stat->min)
    {
      stat->min = value;
    }

    if(value > stat->max)
    {
      stat->max = value;
    }
  }
}

double an_stat_mean(const ANSTAT *stat)
{
  if(stat->count == 0)
  {
    return(0.0);
  }

  return(stat->shift + (stat->sum / stat->count));
}

//Population standard deviation of the window
double an_stat_stddev(const ANSTAT *stat)
{
  double mean, variance;

  if(stat->count < 2)
  {
    return(0.0);
  }

  mean = stat->sum / stat->count;
  variance = (stat->sumsq / stat->count) - (mean * mean);

  return((variance > 0.0) ? an_sqrt(variance) : 0.0);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Serial decoding
//----------------------------------------------------------------------------------------------------------------------------------

const uint32 an_uart_bauds[AN_UART_BAUDS] = { 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200 };

static void an_range(const uint8 *s, uint32 n, uint32 *minimum, uint32 *maximum)
{
  uint32 i;

  *minimum = 255;
  *maximum = 0;

  for(i=0;i<n;i++)
  {
    if(s[i] < *minimum)
    {
      *minimum = s[i];
    }

    if(s[i] > *maximum)
    {
      *maximum = s[i];
    }
  }
}

static void an_add_event(ANDECODE *result, uint8 type, uint16 value, uint8 flags, uint32 start, uint32 end)
{
  if(result->count < AN_MAX_EVENTS)
  {
    ANEVENT *event = &result->events[result->count++];

    event->type  = type;
    event->value = value;
    event->flags = flags;
    event->start = start;
    event->end   = (end >= start) ? end : start;
  }
  else
  {
    result->truncated = 1;
  }
}

static void an_clear_decode(ANDECODE *result)
{
  result->count = 0;
  result->truncated = 0;
  result->baud = 0;
  result->status = AN_DECODE_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Auto baud: the shortest stable level run is one bit. Every run is then divided by its whole number of
//bits and the average bit time is snapped to the nearest standard rate within 10 %.

uint32 an_uart_detect_baud(const uint8 *s, uint32 n, double samplerate)
{
  uint32 minimum, maximum, i, last = 0, shortest = 0xFFFFFFFF, runs = 0;
  int32  state = -1, high, low;
  double bitsum = 0.0, estimate;
  uint32 bitcount = 0;

  if((s == 0) || (n < 8))
  {
    return(0);
  }

  an_range(s, n, &minimum, &maximum);

  if((maximum - minimum) < 4)
  {
    return(0);
  }

  high = (int32)((minimum + maximum) / 2) + (int32)((maximum - minimum) / 8);
  low  = (int32)((minimum + maximum) / 2) - (int32)((maximum - minimum) / 8);

  //First pass: the shortest complete run (the runs at the buffer ends are cut off)
  for(i=0;i<n;i++)
  {
    int32 newstate = state;

    if(s[i] > high)
    {
      newstate = 1;
    }
    else if(s[i] < low)
    {
      newstate = 0;
    }

    if(newstate != state)
    {
      if((state != -1) && runs)
      {
        if((i - last) < shortest)
        {
          shortest = i - last;
        }
      }

      if(state != -1)
      {
        runs++;
      }

      state = newstate;
      last = i;
    }
  }

  if((shortest == 0xFFFFFFFF) || (shortest < 3))
  {
    return(0);
  }

  //Second pass: average bit time over all complete runs of up to ten bits
  state = -1;
  runs = 0;

  for(i=0;i<n;i++)
  {
    int32 newstate = state;

    if(s[i] > high)
    {
      newstate = 1;
    }
    else if(s[i] < low)
    {
      newstate = 0;
    }

    if(newstate != state)
    {
      if((state != -1) && runs)
      {
        uint32 length = i - last;
        uint32 bits = (length + (shortest / 2)) / shortest;

        if((bits >= 1) && (bits <= 10))
        {
          bitsum += (double)length;
          bitcount += bits;
        }
      }

      if(state != -1)
      {
        runs++;
      }

      state = newstate;
      last = i;
    }
  }

  if(bitcount == 0)
  {
    return(0);
  }

  estimate = samplerate / (bitsum / bitcount);

  for(i=0;i<AN_UART_BAUDS;i++)
  {
    if(an_abs(estimate - an_uart_bauds[i]) <= (an_uart_bauds[i] * 0.1))
    {
      return(an_uart_bauds[i]);
    }
  }

  return((uint32)(estimate + 0.5));
}

//----------------------------------------------------------------------------------------------------------------------------------

void an_decode_uart(const uint8 *s, uint32 n, double samplerate, const ANUARTCONFIG *config, ANDECODE *result)
{
  uint32 minimum, maximum, i, bits, b;
  double spb, mid, edge;
  uint32 baud;

  an_clear_decode(result);

  if((s == 0) || (n < 8) || (samplerate <= 0.0))
  {
    result->status = AN_DECODE_NO_SIGNAL;
    return;
  }

  an_range(s, n, &minimum, &maximum);

  if((maximum - minimum) < 4)
  {
    result->status = AN_DECODE_NO_SIGNAL;
    return;
  }

  baud = config->baud ? config->baud : an_uart_detect_baud(s, n, samplerate);

  if(baud == 0)
  {
    result->status = AN_DECODE_NO_BAUD;
    return;
  }

  result->baud = baud;
  spb = samplerate / baud;

  if(spb < 4.0)
  {
    result->status = AN_DECODE_TOO_SLOW;
    return;
  }

  mid = ((double)minimum + (double)maximum) / 2.0;
  bits = 8 + ((config->parity != AN_PARITY_NONE) ? 1 : 0);

#define AN_UART_BIT(index)  ((((double)s[index] >= mid) ? 1 : 0) == (config->idle_high ? 1 : 0))

  //Start after the line has been idle once
  for(i=0;(i<n) && !AN_UART_BIT(i);i++);

  while(i < n)
  {
    uint32 value = 0, ones = 0, flags = 0, position;

    //Next start bit
    for(;(i<n) && AN_UART_BIT(i);i++);

    if(i >= n)
    {
      break;
    }

    //Edge between the last idle sample and the first start bit sample
    edge = (i > 0) ? an_cross(s, i, mid) : 0.0;

    //The whole frame must be in the buffer
    if((edge + ((bits + 2) * spb)) > (double)n)
    {
      break;
    }

    //A glitch shorter than half a bit is not a start bit
    position = (uint32)(edge + (0.5 * spb) + 0.5);

    if(AN_UART_BIT(position))
    {
      i++;
      continue;
    }

    for(b=0;b<8;b++)
    {
      position = (uint32)(edge + ((1.5 + b) * spb) + 0.5);

      if(AN_UART_BIT(position))
      {
        value |= 1u << b;
        ones++;
      }
    }

    if(config->parity != AN_PARITY_NONE)
    {
      position = (uint32)(edge + (9.5 * spb) + 0.5);
      ones += AN_UART_BIT(position) ? 1 : 0;

      if(((config->parity == AN_PARITY_EVEN) && (ones & 1)) || ((config->parity == AN_PARITY_ODD) && !(ones & 1)))
      {
        flags |= AN_FLAG_PARITY;
      }
    }

    position = (uint32)(edge + ((bits + 1.5) * spb) + 0.5);

    if(position >= n)
    {
      break;
    }

    if(!AN_UART_BIT(position))
    {
      flags |= AN_FLAG_FRAMING;
    }

    an_add_event(result, AN_EVENT_BYTE, value, flags, (uint32)(edge + 0.5), (uint32)(edge + ((bits + 2) * spb)) - 1);

    //Continue from the middle of the stop bit; after a framing error wait for the idle level
    i = position;

    if(flags & AN_FLAG_FRAMING)
    {
      for(;(i<n) && !AN_UART_BIT(i);i++);
    }
  }

#undef AN_UART_BIT
}

//----------------------------------------------------------------------------------------------------------------------------------
//I2C: SDA on one channel and SCL on the other. A start is SDA falling while SCL is high, a stop SDA
//rising while SCL is high; data bits are taken on the rising SCL edges, MSB first, the ninth bit is
//the acknowledge (low = ACK). The first byte after a start is the address with the R/W bit.

void an_decode_i2c(const uint8 *sda, const uint8 *scl, uint32 n, ANDECODE *result)
{
  uint32 dmin, dmax, cmin, cmax, i;
  int32  dhigh, dlow, chigh, clow;
  int32  d, c, pd, pc;
  uint32 value = 0, bitcount = 0, bytestart = 0, address = 0, inframe = 0;
  uint32 lastclock = 0, shortest = 0xFFFFFFFF;
  uint32 pending = 0, pendingbit = 0, pendingstart = 0;

  an_clear_decode(result);

  if((sda == 0) || (scl == 0) || (n < 8))
  {
    result->status = AN_DECODE_NO_SIGNAL;
    return;
  }

  an_range(sda, n, &dmin, &dmax);
  an_range(scl, n, &cmin, &cmax);

  if(((dmax - dmin) < 4) || ((cmax - cmin) < 4))
  {
    result->status = AN_DECODE_NO_SIGNAL;
    return;
  }

  dhigh = (int32)((dmin + dmax) / 2) + (int32)((dmax - dmin) / 8);
  dlow  = (int32)((dmin + dmax) / 2) - (int32)((dmax - dmin) / 8);
  chigh = (int32)((cmin + cmax) / 2) + (int32)((cmax - cmin) / 8);
  clow  = (int32)((cmin + cmax) / 2) - (int32)((cmax - cmin) / 8);

  pd = (sda[0] >= ((dmin + dmax) / 2)) ? 1 : 0;
  pc = (scl[0] >= ((cmin + cmax) / 2)) ? 1 : 0;

  for(i=1;i<n;i++)
  {
    d = pd;
    c = pc;

    if(sda[i] > dhigh)
    {
      d = 1;
    }
    else if(sda[i] < dlow)
    {
      d = 0;
    }

    if(scl[i] > chigh)
    {
      c = 1;
    }
    else if(scl[i] < clow)
    {
      c = 0;
    }

    if(c != pc)
    {
      //Shortest clock phase, to tell when the sample rate is too low
      if(lastclock && ((i - lastclock) < shortest))
      {
        shortest = i - lastclock;
      }

      lastclock = i;
    }

    if(c && pc && (d != pd))
    {
      //A start or stop; a clock high phase it happens in carries no data bit
      pending = 0;

      //A byte cut off by a start or stop
      if(inframe && bitcount)
      {
        an_add_event(result, address ? AN_EVENT_ADDRESS : AN_EVENT_DATA, value, AN_FLAG_PARTIAL, bytestart, i);
      }

      if(d == 0)
      {
        an_add_event(result, AN_EVENT_START, 0, 0, i, i);
        inframe = 1;
        address = 1;
      }
      else
      {
        an_add_event(result, AN_EVENT_STOP, 0, 0, i, i);
        inframe = 0;
      }

      bitcount = 0;
      value = 0;
    }
    else if(c && !pc && inframe)
    {
      //Data is taken on the rising clock edge but only counts once the clock falls again, because the
      //clock of a repeated start or a stop rises before SDA changes
      pending = 1;
      pendingbit = (uint32)d;
      pendingstart = i;
    }
    else if(!c && pc && pending)
    {
      pending = 0;

      if(bitcount == 0)
      {
        bytestart = pendingstart;
      }

      if(bitcount < 8)
      {
        value = (value << 1) | pendingbit;
        bitcount++;
      }
      else
      {
        uint32 flags = pendingbit ? AN_FLAG_NAK : 0;

        if(address)
        {
          an_add_event(result, AN_EVENT_ADDRESS, value >> 1, flags | ((value & 1) ? AN_FLAG_READ : 0), bytestart, i);
          address = 0;
        }
        else
        {
          an_add_event(result, AN_EVENT_DATA, value, flags, bytestart, i);
        }

        bitcount = 0;
        value = 0;
      }
    }

    pd = d;
    pc = c;
  }

  if((shortest != 0xFFFFFFFF) && (shortest < 2))
  {
    result->count = 0;
    result->truncated = 0;
    result->status = AN_DECODE_TOO_SLOW;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Number formatting: d.dddE+xx with the given number of significant digits (1 to 9)
//----------------------------------------------------------------------------------------------------------------------------------

char *an_format_double(char *buffer, double value, uint32 digits)
{
  int32  exponent = 0;
  uint32 scaled, limit = 1, i;
  char   text[12];

  if((digits < 1) || (digits > 9))
  {
    digits = 5;
  }

  for(i=1;i<digits;i++)
  {
    limit *= 10;
  }

  if(value < 0.0)
  {
    *buffer++ = '-';
    value = -value;
  }

  //Not a number or infinite: no measurement can produce these, but a reply must stay parseable
  if((value != value) || (value > 1e300))
  {
    value = 0.0;
  }

  if(value != 0.0)
  {
    while(value >= 10.0)
    {
      value /= 10.0;
      exponent++;
    }

    while(value < 1.0)
    {
      value *= 10.0;
      exponent--;
    }
  }

  scaled = (uint32)((value * limit) + 0.5);

  if(scaled >= (limit * 10))
  {
    scaled /= 10;
    exponent++;
  }

  for(i=digits;i>0;i--)
  {
    text[i - 1] = '0' + (scaled % 10);
    scaled /= 10;
  }

  *buffer++ = text[0];

  if(digits > 1)
  {
    *buffer++ = '.';

    for(i=1;i<digits;i++)
    {
      *buffer++ = text[i];
    }
  }

  *buffer++ = 'E';
  *buffer++ = (exponent < 0) ? '-' : '+';

  if(exponent < 0)
  {
    exponent = -exponent;
  }

  if(exponent >= 100)
  {
    *buffer++ = '0' + (exponent / 100);
    exponent %= 100;
  }

  *buffer++ = '0' + (exponent / 10);
  *buffer++ = '0' + (exponent % 10);
  *buffer = 0;

  return(buffer);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Number parsing: [sign] digits [. digits] [e|E [sign] digits] [n|u|m|M|k|K] [unit letters]. M is milli,
//as in SCPI. Returns 1 when a number was read; the pointer is moved past it and any unit.

uint32 an_parse_double(const char **text, double *value)
{
  const char *ptr = *text;
  double result = 0.0, fraction = 0.1;
  int32  negative = 0, exponent = 0, expnegative = 0, digits = 0;

  while(*ptr == ' ')
  {
    ptr++;
  }

  if((*ptr == '+') || (*ptr == '-'))
  {
    negative = (*ptr == '-');
    ptr++;
  }

  while((*ptr >= '0') && (*ptr <= '9'))
  {
    result = (result * 10.0) + (*ptr - '0');
    ptr++;
    digits++;
  }

  if(*ptr == '.')
  {
    ptr++;

    while((*ptr >= '0') && (*ptr <= '9'))
    {
      result += (*ptr - '0') * fraction;
      fraction /= 10.0;
      ptr++;
      digits++;
    }
  }

  if(digits == 0)
  {
    return(0);
  }

  if(((*ptr == 'e') || (*ptr == 'E')) && (((ptr[1] >= '0') && (ptr[1] <= '9')) || (((ptr[1] == '-') || (ptr[1] == '+')) && (ptr[2] >= '0') && (ptr[2] <= '9'))))
  {
    ptr++;

    if((*ptr == '+') || (*ptr == '-'))
    {
      expnegative = (*ptr == '-');
      ptr++;
    }

    while((*ptr >= '0') && (*ptr <= '9'))
    {
      if(exponent < 400)
      {
        exponent = (exponent * 10) + (*ptr - '0');
      }

      ptr++;
    }
  }

  //Multiplier
  switch(*ptr)
  {
    case 'n': case 'N': exponent += expnegative ? 9 : -9; ptr++; break;
    case 'u': case 'U': exponent += expnegative ? 6 : -6; ptr++; break;
    case 'm': case 'M': exponent += expnegative ? 3 : -3; ptr++; break;
    case 'k': case 'K': exponent += expnegative ? -3 : 3; ptr++; break;
  }

  if(exponent > 300)
  {
    return(0);
  }

  while(exponent > 0)
  {
    if(expnegative)
    {
      result /= 10.0;
    }
    else
    {
      result *= 10.0;
    }

    exponent--;
  }

  while(exponent < 0)
  {
    if(expnegative)
    {
      result *= 10.0;
    }
    else
    {
      result /= 10.0;
    }

    exponent++;
  }

  //Unit letters (V, S, HZ, A, DEG, %) carry no meaning here
  while(((*ptr >= 'a') && (*ptr <= 'z')) || ((*ptr >= 'A') && (*ptr <= 'Z')) || (*ptr == '%'))
  {
    ptr++;
  }

  *value = negative ? -result : result;
  *text = ptr;

  return(1);
}

//----------------------------------------------------------------------------------------------------------------------------------
//SCPI keyword match: the pattern holds the short form in upper case and the rest of the long form in
//lower case ("CHANnel"); the text matches when it is the short or the long form, in any case.

uint32 an_mnemonic(const char *pattern, const char *text, uint32 length)
{
  uint32 shortlength = 0, longlength = 0, i;

  while(pattern[longlength])
  {
    if((pattern[longlength] >= 'A') && (pattern[longlength] <= 'Z'))
    {
      shortlength = longlength + 1;
    }
    else if((pattern[longlength] >= '0') && (pattern[longlength] <= '9') && (shortlength == longlength))
    {
      shortlength = longlength + 1;
    }

    longlength++;
  }

  if((length != shortlength) && (length != longlength))
  {
    return(0);
  }

  for(i=0;i<length;i++)
  {
    if(an_upper(text[i]) != an_upper(pattern[i]))
    {
      return(0);
    }
  }

  return(1);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Display formatting: three significant digits with an SI prefix, "12.3 us", "-450 mV", "1.00 kHz"

char *an_format_si(char *buffer, double value, const char *unit)
{
  static const char prefixes[] = { 'p', 'n', 'u', 'm', 0, 'k', 'M', 'G' };
  int32  index = 4, decimals;
  uint32 scaled;

  if(value < 0.0)
  {
    *buffer++ = '-';
    value = -value;
  }

  if((value != value) || (value > 1e12))
  {
    value = 0.0;
  }

  if(value != 0.0)
  {
    while((value >= 999.5) && (index < 7))
    {
      value /= 1000.0;
      index++;
    }

    while((value < 0.9995) && (index > 0))
    {
      value *= 1000.0;
      index--;
    }
  }

  decimals = (value >= 99.95) ? 0 : (value >= 9.995) ? 1 : 2;

  scaled = (uint32)((value * ((decimals == 2) ? 100.0 : (decimals == 1) ? 10.0 : 1.0)) + 0.5);

  {
    char   text[12];
    uint32 length = 0, i;

    do
    {
      text[length++] = '0' + (scaled % 10);
      scaled /= 10;
    } while(scaled || (length <= (uint32)decimals));

    for(i=length;i>0;i--)
    {
      *buffer++ = text[i - 1];

      if(((i - 1) == (uint32)decimals) && decimals)
      {
        *buffer++ = '.';
      }
    }
  }

  *buffer++ = ' ';

  if(prefixes[index])
  {
    *buffer++ = prefixes[index];
  }

  while(*unit)
  {
    *buffer++ = *unit++;
  }

  *buffer = 0;

  return(buffer);
}
