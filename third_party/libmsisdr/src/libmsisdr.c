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

/* potřebné funkce */
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#if !defined (_WIN32) || defined(__MINGW32__)
#include <unistd.h>
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* Use system libusb-1.0 instead of the Android-embedded fork */
#include <libusb-1.0/libusb.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
/* On Samsung Android, USBDEVFS_SUBMITURB (used by libusb_control_transfer) is
 * blocked/times-out — the interface claim belongs to Android's USB service fd.
 * USBDEVFS_CONTROL (synchronous, not ownership-checked) works correctly.
 * Pass raw fd to avoid incomplete-struct forward-declaration issues. */
#define MSISDR_CTRL(p, rt, rq, v, i, d, l, t) msisdr_ctrl_fd(libusb_get_fd((p)->dh), rt, rq, v, i, d, l, t)
static int msisdr_ctrl_fd(int fd, uint8_t reqtype, uint8_t req,
                           uint16_t value, uint16_t index,
                           void *data, uint16_t length, unsigned int timeout);
#else
#define MSISDR_CTRL(p, rt, rq, v, i, d, l, t) libusb_control_transfer((p)->dh, rt, rq, v, i, (unsigned char*)(d), l, t)
#endif

#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

/* hlavní hlavičkový soubor */
#include "msisdr.h"

/* interní definice */
#include "constants.h"
#include "structs.h"

/* interní funkce - inline */
#ifdef __ANDROID__
static int msisdr_ctrl_fd(int fd, uint8_t reqtype, uint8_t req,
                           uint16_t value, uint16_t index,
                           void *data, uint16_t length, unsigned int timeout)
{
    if (fd < 0) return -1;
    struct usbdevfs_ctrltransfer ctrl = {
        .bRequestType = reqtype,
        .bRequest     = req,
        .wValue       = value,
        .wIndex       = index,
        .wLength      = length,
        .timeout      = timeout,
        .data         = data,
    };
    int r = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
    if (r < 0) __android_log_print(ANDROID_LOG_DEBUG, "msisdr",
        "USBDEVFS_CONTROL req=0x%02x val=0x%04x: %s", req, value, strerror(errno));
    return r;
}
#endif

#include "reg.c"
#include "adc.c"

/* RSP1A-specific GPIO initialisation.
 * GPIO 0x13 bits: 0x01 = DSB_NOTCH, 0x04 = BROADCAST_NOTCH.
 * The broadcast notch covers 88-108 MHz — if ON at firmware boot it silently
 * blocks the whole FM band.  Clear both notches and write CMD_WGPIO for the
 * other known RSP1A GPIO registers so the RF path is in the default open state. */
int msisdr_rsp1a_gpio_init(msisdr_dev_t *p)
{
    /* CMD_WGPIO (0x49): wValue = (gpio_addr << 8) | gpio_value */
#define WGPIO(addr, val) \
    MSISDR_CTRL(p, 0x42, 0x49, ((addr) << 8) | (val), 0, NULL, 0, CTRL_TIMEOUT)

    WGPIO(0x13, 0x00); /* DSB_NOTCH=off, BROADCAST_NOTCH=off */
    WGPIO(0x12, 0x00); /* any additional GPIO — neutral */
    WGPIO(0x11, 0x00); /* any additional GPIO — neutral */

#undef WGPIO
    return 0;
}
#include "convert/base.c"
#include "async.c"
#include "devices.c"
#include "gain.c"
#include "hard.c"
#include "streaming.c"
#include "soft.c"
#include "sync.c"

int msisdr_setup (msisdr_dev_t **out_dev, msisdr_dev_t *dev) {
    int r;

    if (libusb_kernel_driver_active(dev->dh, 0) == 1) {
        dev->driver_active = 1;

#ifdef DETACH_KERNEL_DRIVER
        if (!libusb_detach_kernel_driver(dev->dh, 0)) {
            fprintf(stderr, "Detached kernel driver\n");
        } else {
            fprintf(stderr, "Detaching kernel driver failed!");
            dev->driver_active = 0;
            goto failed;
        }
#else
        fprintf(stderr, "\nKernel driver is active, or device is "
                "claimed by second instance of libmsisdr."
                "\nIn the first case, please either detach"
                " or blacklist the kernel module\n"
                "(msi001 and msi2500), or enable automatic"
                " detaching at compile time.\n\n");
#endif
    } else {
        dev->driver_active = 0;
    }

    if ((r = libusb_claim_interface(dev->dh, 0)) < 0) {
        fprintf(stderr, "failed to claim miri usb device %u with code %d: %s\n", dev->index, r, libusb_error_name(r));
        if (r == LIBUSB_ERROR_BUSY) {
            fprintf(stderr, "Verify that the SDRplay background service is not running by `sudo systemctl stop sdrplay` and try again.\n");
        }

        goto failed;
    }

    /* read USB strings before reset - reset invalidates descriptors on macOS */
    {
        struct libusb_device_descriptor dd;
        libusb_device *d = libusb_get_device(dev->dh);
        msisdr_device_t *devinfo;
        memset(dev->usb_manufacturer, 0, sizeof(dev->usb_manufacturer));
        memset(dev->usb_product, 0, sizeof(dev->usb_product));
        memset(dev->usb_serial, 0, sizeof(dev->usb_serial));
        if (d && libusb_get_device_descriptor(d, &dd) == 0) {
            if (dd.iManufacturer)
                libusb_get_string_descriptor_ascii(dev->dh, dd.iManufacturer,
                    (unsigned char *)dev->usb_manufacturer, sizeof(dev->usb_manufacturer));
            if (dd.iProduct)
                libusb_get_string_descriptor_ascii(dev->dh, dd.iProduct,
                    (unsigned char *)dev->usb_product, sizeof(dev->usb_product));
            if (dd.iSerialNumber)
                libusb_get_string_descriptor_ascii(dev->dh, dd.iSerialNumber,
                    (unsigned char *)dev->usb_serial, sizeof(dev->usb_serial));
            /* most SDRplay clones lack EEPROM strings - use device table + bus path */
            if (!dev->usb_manufacturer[0]) {
                devinfo = msisdr_device_get(dd.idVendor, dd.idProduct);
                if (devinfo) {
                    strncpy(dev->usb_manufacturer, devinfo->manufacturer, sizeof(dev->usb_manufacturer) - 1);
                    strncpy(dev->usb_product, devinfo->product, sizeof(dev->usb_product) - 1);
                }
            }
            if (!dev->usb_serial[0] && d) {
                char *cursor = dev->usb_serial;
                cursor += sprintf(cursor, "%d:", libusb_get_bus_number(d));
                uint8_t usb_path[16];
                int path_len = libusb_get_port_numbers(d, usb_path, sizeof(usb_path));
                if (path_len > 0) {
                    for (int u = 0; u < path_len; u++)
                        cursor += sprintf(cursor, "%d.", usb_path[u]);
                    *(cursor - 1) = '\0';
                }
            }
        }
    }

    /* reset je potřeba, jinak občas zařízení odmítá komunikovat */
    msisdr_reset(dev);

    /* ještě je třeba vždy ukončit i streamování, které může být při otevření aktivní */
    msisdr_streaming_stop(dev);
    msisdr_adc_stop(dev);

    /* inicializace tuneru */
    dev->freq = DEFAULT_FREQ;
    dev->rate = DEFAULT_RATE;
    dev->gain = DEFAULT_GAIN;
    dev->band = MSISDR_BAND_VHF; // matches always the default frequency of 90 MHz

    dev->gain_reduction_lna = 0;
    dev->gain_reduction_mixer = 0;
    dev->gain_reduction_baseband = 43;
    dev->if_freq = MSISDR_IF_ZERO;
    dev->format_auto = MSISDR_FORMAT_AUTO_ON;
    dev->bandwidth = MSISDR_BW_8MHZ;
    dev->xtal = MSISDR_XTAL_24M;
    dev->bias = 0;

    if (dev->pid == 0x3000 || dev->pid == 0x3010) {
        dev->hw_flavour = MSISDR_HW_SDRPLAY;
    } else {
        dev->hw_flavour = MSISDR_HW_DEFAULT;
    }

    /* ISOC works on Linux; macOS and Windows need BULK */
#if defined(__APPLE__) || defined(__ANDROID__) || defined(_WIN32) && !defined(__MINGW32__)
    dev->transfer = MSISDR_TRANSFER_BULK;
#else
    dev->transfer = MSISDR_TRANSFER_ISOC;
#endif

    msisdr_adc_init(dev);
    msisdr_set_hard(dev);
    msisdr_set_soft(dev);
    msisdr_set_gain(dev);

    *out_dev = dev;

    return 0;

failed:
    if (dev) {
        if (dev->dh) {
            libusb_release_interface(dev->dh, 0);
            libusb_close(dev->dh);
        }
        if (dev->ctx) libusb_exit(dev->ctx);
        free(dev);
    }

    return -1;
}

#ifndef __ANDROID__
int msisdr_open (msisdr_dev_t **p, uint32_t index) {
    msisdr_dev_t *dev = NULL;
    libusb_device **list, *device = NULL;
    struct libusb_device_descriptor dd;
    ssize_t i, i_max;
    size_t count = 0;
    int r;

    *p = NULL;

    if (!(dev = malloc(sizeof(*dev)))) return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    /* ostatní parametry */
    dev->index = index;

    libusb_init(&dev->ctx);
    i_max = libusb_get_device_list(dev->ctx, &list);

    for (i = 0; i < i_max; i++) {
        libusb_get_device_descriptor(list[i], &dd);

        if ((msisdr_device_get(dd.idVendor, dd.idProduct)) &&
            (count++ == index)) {
            device = list[i];
            dev->pid = dd.idProduct;
            break;
        }
    }

    /* nenašli jsme zařízení */
    if (!device) {
        libusb_free_device_list(list, 1);
        fprintf( stderr, "no miri device %u found\n", dev->index);
        goto failed;
    }

    /* otevření zařízení */
    if ((r = libusb_open(device, &dev->dh)) < 0) {
        libusb_free_device_list(list, 1);
        fprintf( stderr, "failed to open miri usb device %u with code %d\n", dev->index, r);
        goto failed;
    }

    libusb_free_device_list(list, 1);

    return msisdr_setup(p, dev);

failed:
    if (dev) {
        if (dev->dh) {
            libusb_close(dev->dh);
        }
        if (dev->ctx) libusb_exit(dev->ctx);
        free(dev);
    }

    return -1;
}
#endif

#ifdef __ANDROID__
int msisdr_open_fd (msisdr_dev_t **p, int fd, const char *devicePath) {
    msisdr_dev_t *dev = NULL;
    libusb_device *device = NULL;
    struct libusb_device_descriptor dd;
    int r;

    *p = NULL;

    if (!(dev = malloc(sizeof(*dev)))) return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    r = libusb_init(&dev->ctx);
    if (r < 0) {
        free(dev);
        return -1;
    }

    device = libusb_get_device2(dev->ctx, devicePath);
    if (!device) {
        libusb_exit(dev->ctx);
        free(dev);
        return -1;
    }

    r = libusb_open2(device, &dev->dh, fd);
    if (r < 0 || dev->dh == NULL) {
        libusb_exit(dev->ctx);
        free(dev);
        return -1;
    }

    libusb_get_device_descriptor(device, &dd);
    dev->pid = dd.idProduct;

    return msisdr_setup(p, dev);
}

#endif /* __ANDROID__ */
int msisdr_close (msisdr_dev_t *p) {
    if (!p) goto failed;

    /* ukončení async čtení okamžitě */
    msisdr_cancel_async_now(p);

    // similar to rtl-sdr
#if defined(_WIN32) && !defined(__MINGW32__)
            Sleep(1);
#else
            usleep(1000);
#endif

    /* deinicializace tuneru */
    if (p->dh)
    {
        libusb_release_interface(p->dh, 0);

#ifdef DETACH_KERNEL_DRIVER
        if (p->driver_active) {
            if (!libusb_attach_kernel_driver(p->dh, 0))
                fprintf(stderr, "Reattached kernel driver\n");
            else
                fprintf(stderr, "Reattaching kernel driver failed!\n");
        }
#endif
        if (p->async_status != MSISDR_ASYNC_FAILED) {
            libusb_close(p->dh);
        }
    }

    if (p->ctx) libusb_exit(p->ctx);

    if (p->samples) free(p->samples);

    free(p);

    return 0;

failed:
    return -1;
}

int msisdr_reset (msisdr_dev_t *p) {
    int r;

    if (!p) goto failed;
    if (!p->dh) goto failed;

#if defined(__APPLE__) || defined(__ANDROID__) || (defined(_WIN32) && !defined(__MINGW32__))
    /* libusb_reset_device sends USBDEVFS_RESET which causes the USB device to
     * re-enumerate. On Android this invalidates the fd provided by the Android
     * USB service — all subsequent ioctls return ENODEV and streaming fails.
     * Skip the reset; the device works correctly without it on these platforms. */
    (void) r;
    return 0;
#else
    if ((r = libusb_reset_device(p->dh)) < 0) {
        fprintf( stderr, "USB reset skipped for device %u (%s), continuing\n", p->index, libusb_error_name(r));
        return 0;
    }

    return 0;
#endif

failed:
    return -1;
}

int msisdr_reset_buffer (msisdr_dev_t *p) {
    if (!p) goto failed;
    if (!p->dh) goto failed;

    /* zatím není jasné k čemu by bylo, proto pouze provedeme reset async části */
    msisdr_stop_async(p);
    msisdr_start_async(p);

    return 0;

failed:
    return -1;
}

int msisdr_get_usb_strings (msisdr_dev_t *dev, char *manufact, char *product, char *serial) {
    memset(manufact, 0, 256);
    memset(product, 0, 256);
    memset(serial, 0, 256);

    if (!dev) return -1;

    strncpy(manufact, dev->usb_manufacturer, 255);
    strncpy(product, dev->usb_product, 255);
    strncpy(serial, dev->usb_serial, 255);

    return 0;
}

int msisdr_set_hw_flavour (msisdr_dev_t *p, msisdr_hw_flavour_t hw_flavour) {
    if (!p) goto failed;

    p->hw_flavour = hw_flavour;
    msisdr_set_soft(p);
    msisdr_set_gain(p);
    return 0;

failed:
    return -1;
}