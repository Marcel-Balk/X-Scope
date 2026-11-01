//----------------------------------------------------------------------------------------------------------------------------------
//The clock generator on the scope is not connected to a true I2C interface of the processor, so bit banging is used
//
//The connections are:
//  PA0:  SDA
//  PA1:  SCL
//
//  CLK0 is used for the generator and ranges from 200MHz down to ??
//  CLK1 is used for the sampling system and needs to be fixed at 50MHz
//
//----------------------------------------------------------------------------------------------------------------------------------

#include "gpio_control.h"
#include "ccu_control.h"
#include "clock_synthesizer.h"
#include "variables.h"   // for sampling_clock_p1b / sampling_clock_scale globals

//----------------------------------------------------------------------------------------------------------------------------------
//ClockGen could use register file from ClockBuilder Pro, but currently there are only few registers set, so setting them separately

void clock_synthesizer_setup(void)
{
  //Make sure SCL and SDA are high before enabling the pins
  *PORTA_DATA_REG |= 0x00000003;

  //Setup the used pins to output
  *PORTA_CFG0_REG &= 0xFFFFFF11;

  // 0x03 3 Output Enable Control -- Disable all clock outputs
  i2c_send_data(CS_CLK_DIS,0xFF);

  // Power down clocks - we will rewrite these shortly.
  i2c_send_data(CS_CLK0_CTRL, 0x80);
  i2c_send_data(CS_CLK1_CTRL, 0x80);
  i2c_send_data(CS_CLK2_CTRL, 0x80);

  // PLL setup -- 200MHz && 50MHz config

  // 0x0F 15 PLL Input Source left on default

  // 0x10 16 CLK0 Control -- MS0_INT=Integer |  PLLA | CLK0_SRC=MultiSynth0 | CLK0_IDRV=8mA
  i2c_send_data(CS_CLK0_CTRL, 0x4F);

  // 0x11 17 CLK1 Control -- PLLA | CLK1_SRC=MultiSynth1 | CLK1_IDRV=8mA
  i2c_send_data(CS_CLK1_CTRL, 0x0F);


  // MultiSync PLLA Divider

  // 0x1B 27 MSNA_P3[7:0] -- No zero denominator
  i2c_send_data(CS_MSNA_P3, 0x01);

  // 0x1d 29 MSNA_P1[15:8] -- ( Integer part - 4 ) * 128
  i2c_send_data(CS_MSNA_P1B, 0x0E);  // 800 MHz max

  // MultiSynth0 -- 200MHz configuration
  // 0x2B 43 MS0_P3[7:0] -- No zero denominator
  i2c_send_data(CS_MS0_P3, 0x01);

  // 0x2C 44 R0_DIV[2:0] -- MS0_DIVBY4 for 200MHz clock
  i2c_send_data(CS_MS0_DIV, 0x0C);

  // MultiSynth1 -- use configured value (default stock 50MHz)
  // 0x33 51 MS1_P3[7:0] -- No zero denominator
  i2c_send_data(CS_MS1_P3, 0x01);

  // 0x35 53 MS1_P1[15:8] -- Integer part * 128 - 4
  // sampling_clock_p1b is set before this call (or defaults below)
  {
    uint8 p1b = sampling_clock_p1b;
    if (p1b == 0) p1b = 0x06;   // default 50 MHz if not yet configured
    i2c_send_data(CS_MS1_P1B, p1b);
  }

  // 0xB1 177 PLL_RST
  i2c_send_data(CS_PLL_RST,0xAC);

  // Enable clock outputs
  i2c_send_data(CS_CLK_DIS, 0xFC);
}

// Set only the sampling clock (CLK1) rate. Can be called later to overclock.
// Reprograms the relevant multisynth and does a PLL reset.
void clock_synthesizer_set_sampling_clock(uint8 ms1_p1b)
{
  // Make sure the pins are configured (in case called later)
  *PORTA_DATA_REG |= 0x00000003;
  *PORTA_CFG0_REG &= 0xFFFFFF11;

  // Power down CLK1 temporarily
  i2c_send_data(CS_CLK1_CTRL, 0x80);

  // Set the divider for CLK1 (MS1_P1B controls the rate from PLLA)
  i2c_send_data(CS_MS1_P3, 0x01);
  i2c_send_data(CS_MS1_P1B, ms1_p1b);

  // Power CLK1 back up: PLLA | CLK1_SRC=MultiSynth1 | CLK1_IDRV=8mA (same as setup).
  // Without this the FPGA loses its reference clock permanently (backlight PWM strobes,
  // sampling never completes).
  i2c_send_data(CS_CLK1_CTRL, 0x0F);

  // PLL reset to apply change
  i2c_send_data(CS_PLL_RST, 0xAC);

  // Re-enable clocks (leave CLK0/2 as they were)
  i2c_send_data(CS_CLK_DIS, 0xFC);
}

// Apply a sampling clock choice and update the global scale factor used by math.
// Returns the exact scale (actual / 50 MHz).
//
// Only MS1_P1[15:8] is written (P1[17:16] and P1[7:0] stay 0), so P1 = p1b * 256 and the
// MultiSynth integer divider is (P1 + 512) / 128 = 2*p1b + 4. With PLLA at 800 MHz:
//   CLK1 = 800 / (2*p1b + 4)  =>  scale = CLK1 / 50 = 8 / (p1b + 2)
//   0x06 -> 50 MHz (1.0)   0x05 -> 57.1 MHz (1.143)
//   0x04 -> 66.7 MHz (1.333)   0x03 -> 80 MHz (1.6)
// (Historical comments claiming 0x05=66 / 0x04=75 MHz were wrong.)
double clock_synthesizer_apply_sampling_clock(uint8 p1b)
{
  double scale = 8.0 / (double)(p1b + 2);

  sampling_clock_p1b = p1b;
  sampling_clock_scale = scale;

  clock_synthesizer_set_sampling_clock(p1b);

  return scale;
}

//----------------------------------------------------------------------------------------------------------------------------------
//I2C functions

void i2c_send_data(uint8 reg_addr, uint8 data)
{
  //Start a communication sequence
  i2c_send_start();

  //Send the device address for writing
  i2c_send_byte(CS_DEVICE_ADDR_WRITE);

  //Send the register address
  i2c_send_byte(reg_addr);

  //Send a byte
  i2c_send_byte(data);

  //Stop the communication sequence
  i2c_send_stop();
}

//----------------------------------------------------------------------------------------------------------------------------------
//Since no read is done the port pins can remain as outputs and is there no need to set them again in each function

void i2c_send_byte(uint8 data)
{
  int i;

  //Send 8 bits
  for(i=0;i<8;i++)
  {
    //Check if bit to send is high or low
    if(data & 0x80)
    {
      //Make SDA high
      *PORTA_DATA_REG |= 0x00000001;
    }
    else
    {
      //Make SDA low
      *PORTA_DATA_REG &= 0x00000002;
    }

    //Wait for a while
    i2c_delay(CS_DATA_DELAY);

    //Make SCL high
    *PORTA_DATA_REG |= 0x00000002;

    //Wait for a while
    i2c_delay(CS_CLOCK_DELAY);

    //Make SCL low
    *PORTA_DATA_REG &= 0x00000001;

    //Wait for a while
    i2c_delay(CS_DATA_DELAY);

    //Select the next bit to send
    data <<= 1;
  }

  //Clock the ack bit
  i2c_clock_ack();
}

//----------------------------------------------------------------------------------------------------------------------------------

void i2c_send_start(void)
{
  //Make SDA high
  *PORTA_DATA_REG |= 0x00000001;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);

  //Make SCL high
  *PORTA_DATA_REG |= 0x00000002;

  //Wait for a while
  i2c_delay(CS_CLOCK_DELAY);

  //Make SDA low
  *PORTA_DATA_REG &= 0x0000000E;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);

  //Make SCL low
  *PORTA_DATA_REG &= 0x0000000D;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);
}

//----------------------------------------------------------------------------------------------------------------------------------

void i2c_send_stop(void)
{
  //Make SDA low
  *PORTA_DATA_REG &= 0x0000000E;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);

  //Make SCL high
  *PORTA_DATA_REG |= 0x00000002;

  //Wait for a while
  i2c_delay(CS_CLOCK_DELAY);

  //Make SDA high
  *PORTA_DATA_REG |= 0x00000001;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);
}

//----------------------------------------------------------------------------------------------------------------------------------

void i2c_clock_ack(void)
{
  //Make SDA low
  *PORTA_DATA_REG &= 0x00000002;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);

  //Make SCL high
  *PORTA_DATA_REG |= 0x00000002;

  //Wait for a while
  i2c_delay(CS_CLOCK_DELAY);

  //Make SCL low
  *PORTA_DATA_REG &= 0x00000001;

  //Wait for a while
  i2c_delay(CS_DATA_DELAY);
}

//----------------------------------------------------------------------------------------------------------------------------------
//A count of 4 is approximately 3uS when running on 600MHz with cache enabled

void i2c_delay(uint32 usec)
{
  //Lower then 64 does not work properly, because the panel fails to hold the new configuration when coming from the original code
  unsigned int loops = usec * 90;

  __asm__ __volatile__ ("1:\n" "subs %0, %1, #1\n"  "bne 1b":"=r"(loops):"0"(loops));
}

//----------------------------------------------------------------------------------------------------------------------------------
