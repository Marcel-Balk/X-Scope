//----------------------------------------------------------------------------------------------------------------------------------
//Remote control over the USB serial (CDC) mode, see remote.c
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef REMOTE_H
#define REMOTE_H

#include "types.h"

uint32 remote_queue_line(const char *line);
uint8  remote_take_key(void);
void   remote_poll(void);
void   remote_press_key(uint8 key);

int32  remote_text(const char *text);
void   remote_screen_command(const char *argument);
void   remote_wave_command(const char *argument);
void   remote_waveb_command(const char *argument);

#endif /* REMOTE_H */
