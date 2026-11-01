//----------------------------------------------------------------------------------------------------------------------------------
//Remote control over the USB serial (CDC) mode, see remote.c
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef REMOTE_H
#define REMOTE_H

#include "types.h"

uint32 remote_queue_line(const char *line);
uint8  remote_take_key(void);
void   remote_poll(void);

#endif /* REMOTE_H */
