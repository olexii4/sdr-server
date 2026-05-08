/*
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MSISDR_H
#define __MSISDR_H

#ifdef __cplusplus
extern "C" {
#endif

// Set debug level
// 0=no debug
// 1=gain and frequency info.
// 2=extended debug
#define MSISDR_DEBUG 0

#include <stdint.h>
#include <msisdr_export.h>

typedef enum
{
    MSISDR_HW_DEFAULT,
    MSISDR_HW_SDRPLAY,
} msisdr_hw_flavour_t;

typedef enum
{
    MSISDR_BAND_AM1,
    MSISDR_BAND_AM2,
    MSISDR_BAND_VHF,
    MSISDR_BAND_3,
    MSISDR_BAND_45,
    MSISDR_BAND_L,
} msisdr_band_t;

typedef struct msisdr_dev msisdr_dev_t;

/* devices */
MSISDR_API uint32_t msisdr_get_device_count (void);
MSISDR_API const char *msisdr_get_device_name (uint32_t index);
MSISDR_API int msisdr_get_device_usb_strings (uint32_t index, char *manufact, char *product, char *serial);

/* main */
MSISDR_API int msisdr_open (msisdr_dev_t **p, uint32_t index);
MSISDR_API int msisdr_open_fd (msisdr_dev_t **p, int fd, const char *devicePath);
MSISDR_API int msisdr_close (msisdr_dev_t *p);
MSISDR_API int msisdr_reset (msisdr_dev_t *p);                       /* extra */
MSISDR_API int msisdr_reset_buffer (msisdr_dev_t *p);
MSISDR_API int msisdr_get_usb_strings (msisdr_dev_t *dev, char *manufact, char *product, char *serial);
MSISDR_API int msisdr_set_hw_flavour (msisdr_dev_t *p, msisdr_hw_flavour_t hw_flavour);
MSISDR_API int msisdr_rsp1a_gpio_init (msisdr_dev_t *p); /* clear notch filters, open RF path */

/* sync */
MSISDR_API int msisdr_read_sync (msisdr_dev_t *p, void *buf, int len, int *n_read);

/* async */
typedef void(*msisdr_read_async_cb_t) (unsigned char *buf, uint32_t len, void *ctx);
MSISDR_API int msisdr_read_async (msisdr_dev_t *p, msisdr_read_async_cb_t cb, void *ctx, uint32_t num, uint32_t len);
MSISDR_API int msisdr_cancel_async (msisdr_dev_t *p);
MSISDR_API int msisdr_cancel_async_now (msisdr_dev_t *p);            /* extra */
MSISDR_API int msisdr_start_async (msisdr_dev_t *p);                 /* extra */
MSISDR_API int msisdr_stop_async (msisdr_dev_t *p);                  /* extra */

/* adc */
MSISDR_API int msisdr_adc_init (msisdr_dev_t *p);                    /* extra */

/* rate control */
MSISDR_API int msisdr_set_sample_rate (msisdr_dev_t *p, uint32_t rate);
MSISDR_API uint32_t msisdr_get_sample_rate (msisdr_dev_t *p);

/* sample format control */
MSISDR_API int msisdr_set_sample_format (msisdr_dev_t *p, const char *v);  /* extra */
MSISDR_API const char *msisdr_get_sample_format (msisdr_dev_t *p);   /* extra */

/* streaming control */
MSISDR_API int msisdr_streaming_start (msisdr_dev_t *p);             /* extra */
MSISDR_API int msisdr_streaming_stop (msisdr_dev_t *p);              /* extra */

/* frequency */
MSISDR_API int msisdr_set_center_freq (msisdr_dev_t *p, uint32_t freq);
MSISDR_API uint32_t msisdr_get_center_freq (msisdr_dev_t *p);
MSISDR_API int msisdr_set_if_freq (msisdr_dev_t *p, uint32_t freq);  /* extra */
MSISDR_API uint32_t msisdr_get_if_freq (msisdr_dev_t *p);            /* extra */
MSISDR_API int msisdr_set_xtal_freq (msisdr_dev_t *p, uint32_t freq);/* extra */
MSISDR_API uint32_t msisdr_get_xtal_freq (msisdr_dev_t *p);          /* extra */
MSISDR_API int msisdr_set_bandwidth (msisdr_dev_t *p, uint32_t bw);  /* extra */
MSISDR_API uint32_t msisdr_get_bandwidth (msisdr_dev_t *p);          /* extra */
MSISDR_API int msisdr_set_offset_tuning (msisdr_dev_t *p, int on);   /* extra */
MSISDR_API msisdr_band_t msisdr_get_band (msisdr_dev_t *p);         /* extra */

/* not implemented yet */
MSISDR_API int msisdr_set_freq_correction (msisdr_dev_t *p, int ppm);
MSISDR_API int msisdr_set_direct_sampling (msisdr_dev_t *p, int on);

/* transfer */
MSISDR_API int msisdr_set_transfer (msisdr_dev_t *p, const char *v);       /* extra */
MSISDR_API const char *msisdr_get_transfer (msisdr_dev_t *p);        /* extra */

/* gain */
MSISDR_API int msisdr_set_gain (msisdr_dev_t *p);                    /* extra */
MSISDR_API int msisdr_get_tuner_gains (msisdr_dev_t *dev, int *gains);
MSISDR_API int msisdr_set_tuner_gain (msisdr_dev_t *p, int gain);
MSISDR_API int msisdr_get_tuner_gain (msisdr_dev_t *p);              /* extra */
MSISDR_API int msisdr_set_tuner_gain_mode (msisdr_dev_t *p, int mode);
MSISDR_API int msisdr_get_tuner_gain_mode (msisdr_dev_t *p);         /* extra */
MSISDR_API int msisdr_set_mixer_gain (msisdr_dev_t *p, int gain);    /* extra */
MSISDR_API int msisdr_set_mixbuffer_gain (msisdr_dev_t *p, int gain);/* extra */
MSISDR_API int msisdr_set_lna_gain (msisdr_dev_t *p, int gain);      /* extra */
MSISDR_API int msisdr_set_baseband_gain (msisdr_dev_t *p, int gain); /* extra */
MSISDR_API int msisdr_get_mixer_gain (msisdr_dev_t *p);              /* extra */
MSISDR_API int msisdr_get_mixbuffer_gain (msisdr_dev_t *p);          /* extra */
MSISDR_API int msisdr_get_lna_gain (msisdr_dev_t *p);                /* extra */
MSISDR_API int msisdr_get_baseband_gain (msisdr_dev_t *p);           /* extra */
MSISDR_API int msisdr_set_bias (msisdr_dev_t *p, int bias);          /* extra */
MSISDR_API int msisdr_get_bias (msisdr_dev_t *p);                    /* extra */

#ifdef __cplusplus
}
#endif

#endif /* __MSISDR_H */
