//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CLOCK_SYNTHESIZER_H
#define CLOCK_SYNTHESIZER_H

//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"

//----------------------------------------------------------------------------------------------------------------------------------

#define CS_DEVICE_ADDR_WRITE    0xC0

#define CS_CLK_DIS              0x00000003

#define CS_CLK0_CTRL            0x00000010
#define CS_CLK1_CTRL            0x00000011
#define CS_CLK2_CTRL            0x00000012

#define CS_MSNA_P3              0x0000001B
#define CS_MSNA_P1B             0x0000001D

#define CS_MS0_P3               0x0000002B
#define CS_MS0_DIV              0x0000002C

#define CS_MS1_P3               0x00000033
#define CS_MS1_DIV              0x00000034
#define CS_MS1_P1B              0x00000035

#define CS_PLL_RST              0x000000B1

#define CS_CLOCK_DELAY          10
#define CS_DATA_DELAY           10

//----------------------------------------------------------------------------------------------------------------------------------

void clock_synthesizer_setup(void);

// Set the sampling clock (CLK1) frequency by writing the MS1_P1B divider value.
// CLK1 = 800 MHz / (2*p1b + 4):
//   0x06 = 50 MHz (stock)
//   0x05 = 57.1 MHz
//   0x04 = 66.7 MHz
//   0x03 = 80 MHz
// Lower values give higher frequencies. After call, the actual achieved rate
// for a given samplerate index scales proportionally (sampling_clock_scale).
void clock_synthesizer_set_sampling_clock(uint8 ms1_p1b);

// Apply p1b + update sampling_clock_scale (and the globals).
// Safe to call after initial setup.
double clock_synthesizer_apply_sampling_clock(uint8 p1b);

//----------------------------------------------------------------------------------------------------------------------------------
//I2C functions

void i2c_send_data(uint8 reg_addr, uint8 data);

void i2c_send_start(void);
void i2c_send_stop(void);

void i2c_send_byte(uint8 data);

void i2c_clock_ack(void);

void i2c_delay(uint32 usec);


//----------------------------------------------------------------------------------------------------------------------------------

#endif /* CLOCK_SYNTHESIZER_H */

//----------------------------------------------------------------------------------------------------------------------------------
