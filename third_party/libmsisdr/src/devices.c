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

static msisdr_device_t msisdr_devices[] = {
    { 0x1df7, 0x2500, "Mirics MSi2500 default (e.g. VTX3D card)", "Mirics", "MSi2500"},
    { 0x1df7, 0x3000, "SDRplay RSP1A", "SDRPlay", "RSP1A"},
    { 0x1df7, 0x3010, "SDRplay RSP1A", "SDRPlay", "RSP2"},
    { 0x2040, 0xd300, "Hauppauge WinTV 133559 LF", "Hauppauge", "WinTV 133559 LF"},
    { 0x07ca, 0x8591, "AverMedia A859 Pure DVBT", "AverTV", "A859 Pure DVBT"},
    { 0x04bb, 0x0537, "IO-DATA GV-TV100 stick", "IO-DATA", "GV-TV100"},
    { 0x0511, 0x0037, "Logitec LDT-1S310U/J", "Logitec", "LDT-1S310U/J"}
};

static msisdr_device_t *msisdr_device_get (uint16_t vid, uint16_t pid) {
    size_t i;

    for (i = 0; i < sizeof(msisdr_devices) / sizeof(msisdr_device_t); i++) {
        if ((msisdr_devices[i].vid == vid) && (msisdr_devices[i].pid == pid)) return &msisdr_devices[i];
    }

    return NULL;
}

/* počet dostupných zařízení */
uint32_t msisdr_get_device_count (void) {
#ifdef __ANDROID__
    return 0;
#else
    ssize_t i, i_max;
    uint32_t ret = 0;
    libusb_context *ctx;
    libusb_device **list;
    struct libusb_device_descriptor dd;

    libusb_init(&ctx);

    i_max = libusb_get_device_list(ctx, &list);

    for (i = 0; i < i_max; i++) {
        libusb_get_device_descriptor(list[i], &dd);

        if (msisdr_device_get(dd.idVendor, dd.idProduct)) ret++;
    }

    libusb_free_device_list(list, 1);

    libusb_exit(ctx);

    return ret;
#endif
}

/* název zařízení */
const char *msisdr_get_device_name (uint32_t index) {
#ifdef __ANDROID__
    return "";
#else
    ssize_t i, i_max;
    size_t j = 0;
    libusb_context *ctx;
    libusb_device **list;
    struct libusb_device_descriptor dd;
    msisdr_device_t *device = NULL;

    libusb_init(&ctx);
    i_max = libusb_get_device_list(ctx, &list);

    for (i = 0; i < i_max; i++) {
        libusb_get_device_descriptor(list[i], &dd);

        if ((device = msisdr_device_get(dd.idVendor, dd.idProduct)) &&
            (j++ == index)) {
            libusb_free_device_list(list, 1);
            libusb_exit(ctx);
            return device->name;
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    return "";
#endif
}

/* vlastní implementace */
int msisdr_get_device_usb_strings (uint32_t index, char *manufact, char *product, char *serial) {
#ifdef __ANDROID__
    memset(manufact, 0, 256);
    memset(product, 0, 256);
    memset(serial, 0, 256);
    return -1;
#else
    ssize_t i, i_max;
    size_t j = 0;
    libusb_context *ctx;
    libusb_device **list;
    struct libusb_device_descriptor dd;
    msisdr_device_t *device = NULL;

    libusb_init(&ctx);
    i_max = libusb_get_device_list(ctx, &list);

    for (i = 0; i < i_max; i++) {
        libusb_get_device_descriptor(list[i], &dd);

        if ((device = msisdr_device_get(dd.idVendor, dd.idProduct)) &&
            (j++ == index)) {
            strcpy(manufact, device->manufacturer);
            strcpy(product, device->product);

            char* serial_cursor = serial;
            serial_cursor += sprintf(serial_cursor, "%d:", libusb_get_bus_number(list[i]));

#if LIBUSBX_API_VERSION >= 0x01000102
            uint8_t usb_path[16];
            int path_len = libusb_get_port_numbers(list[i], usb_path, sizeof(usb_path));
            if (path_len == LIBUSB_ERROR_OVERFLOW) {
                path_len = sizeof(usb_path);
            }

            for (int u = 0; u < path_len; u++) {
                serial_cursor += sprintf(serial_cursor, "%d.", usb_path[u]);
            }

#endif
            *(serial_cursor - 1) = '\0';

            libusb_free_device_list(list, 1);
            libusb_exit(ctx);
            return 0;
        }
    }

    memset(manufact, 0, 256);
    memset(product, 0, 256);
    memset(serial, 0, 256);

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    return -1;
#endif
}
