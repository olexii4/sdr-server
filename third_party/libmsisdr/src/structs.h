/*
 * Copyright (C) 2013 by Miroslav Slugen <thunder.m@email.cz
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

typedef struct msisdr_device {
    uint16_t            vid;
    uint16_t            pid;
    const char          *name;
    const char          *manufacturer;
    const char          *product;
} msisdr_device_t;

struct msisdr_dev {
    libusb_context      *ctx;
    struct libusb_device_handle *dh;

    /* parametry */
    uint32_t            index;
    uint16_t            pid;
    uint32_t            freq;
    uint32_t            rate;
    int                 gain;
    int                 gain_reduction_lna;
    int                 gain_reduction_mixbuffer;
    int                 gain_reduction_mixer;
    int                 gain_reduction_baseband;
    msisdr_hw_flavour_t hw_flavour;
    msisdr_band_t      band;
    enum {
        MSISDR_FORMAT_AUTO_ON = 0,
        MSISDR_FORMAT_AUTO_OFF
    } format_auto;
    enum {
        MSISDR_FORMAT_252_S16 = 0,
        MSISDR_FORMAT_336_S16,
        MSISDR_FORMAT_384_S16,
        MSISDR_FORMAT_504_S16,
        MSISDR_FORMAT_504_S8
    } format;
    enum {
        MSISDR_BW_200KHZ = 0,
        MSISDR_BW_300KHZ,
        MSISDR_BW_600KHZ,
        MSISDR_BW_1536KHZ,
        MSISDR_BW_5MHZ,
        MSISDR_BW_6MHZ,
        MSISDR_BW_7MHZ,
        MSISDR_BW_8MHZ,
        MSISDR_BW_MAX
    } bandwidth;
    enum {
        MSISDR_IF_ZERO = 0,
        MSISDR_IF_450KHZ,
        MSISDR_IF_1620KHZ,
        MSISDR_IF_2048KHZ
    } if_freq;
    enum {
        MSISDR_XTAL_19_2M = 0,
        MSISDR_XTAL_22M,
        MSISDR_XTAL_24M,
        MSISDR_XTAL_24_576M,
        MSISDR_XTAL_26M,
        MSISDR_XTAL_38_4M
    } xtal;
    enum {
        MSISDR_TRANSFER_BULK = 0,
        MSISDR_TRANSFER_ISOC
    } transfer;

    /* async */
    enum {
        MSISDR_ASYNC_INACTIVE = 0,
        MSISDR_ASYNC_CANCELING,
        MSISDR_ASYNC_RUNNING,
        MSISDR_ASYNC_PAUSED,
        MSISDR_ASYNC_FAILED
    } async_status;
    msisdr_read_async_cb_t cb;
    void                *cb_ctx;
    size_t              xfer_buf_num;
    struct libusb_transfer **xfer;
    unsigned char       **xfer_buf;
    size_t              xfer_out_len;
    size_t              xfer_out_pos;
    unsigned char       *xfer_out;
    uint32_t            addr;
    int                 driver_active;
    int                 bias;
    char                usb_manufacturer[256];
    char                usb_product[256];
    char                usb_serial[256];
    int                 reg8;
    uint8_t             *samples;
    int                 samples_size;
    int                 sync_loss_cnt;
};

