//----------------------------------------------------------------------------------------------------------------------------------
// Build variant: 1=1014D (Si5351 clock gen + UART1 keys), 0=1013D (GT911 touch + RTC + battery)
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define PORT_1014D 1

//Set to 1 for boot-stage boxes and heartbeat/FPGA status overlay during bring-up
#define PORT_A_KEYDEBUG 0

#if PORT_A_KEYDEBUG
#define DBG_STAGE(n) do { int _dbgi; \
    display_set_screen_buffer((uint16 *)maindisplaybuffer); \
    display_set_fg_color(0x00000000); display_fill_rect(10, 55, 470, 44); \
    display_set_fg_color(0x0000FFFF); \
    for(_dbgi=0;_dbgi<(n);_dbgi++) display_fill_rect(14 + _dbgi*46, 59, 36, 36); \
  } while(0)
#else
#define DBG_STAGE(n) do {} while(0)
#endif

#endif /* PORT_CONFIG_H */
