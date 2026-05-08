#ifndef SDR_SERVER_MSISDR_DEVICE_H
#define SDR_SERVER_MSISDR_DEVICE_H

#include "../config.h"
#include "msisdr_lib.h"

int msisdr_device_create(struct server_config *server_config, msisdr_lib *lib,
                          void (*sdr_callback)(uint8_t *buf, uint32_t len, void *ctx),
                          void *ctx, void **plugin);

void msisdr_device_destroy(void *plugin);

int msisdr_device_start_rx(uint32_t band_freq, void *plugin);

void msisdr_device_stop_rx(void *plugin);

#endif /* SDR_SERVER_MSISDR_DEVICE_H */
