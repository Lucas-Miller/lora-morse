#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void radio_init(void);

void radio_send(char*);

void radio_start_receive(void);

bool radio_read(char*, size_t);

#ifdef __cplusplus
}
#endif