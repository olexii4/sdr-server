/*
 * sync_stream.c — synchronous bulk streaming for macOS / desktop.
 *
 * Replaces msisdr_read_async() on macOS where the IOUSBLib async path is
 * fragile (LIBUSB_ERROR_PIPE on concurrent transfers, endpoint stalls, etc.).
 *
 * One libusb_bulk_transfer call at a time — same model as the Android driver's
 * USBDEVFS_BULK ioctl path.  No concurrent transfers, no async state machine,
 * no overflow / cancel races.
 *
 * Usage (same signature as msisdr_read_async but buf_num is ignored):
 *   msisdr_read_sync_stream(dev, cb, ctx, buf_len);
 *
 * Stop with msisdr_cancel_async() from another thread — same as async.
 */

#include <unistd.h>   /* usleep */

int msisdr_read_sync_stream(msisdr_dev_t *p,
                             msisdr_read_async_cb_t cb,
                             void *ctx,
                             uint32_t buf_len)
{
    if (!p || !cb) return -1;
    if (buf_len == 0) buf_len = DEFAULT_BULK_BUFFER;

    /* Set alt-setting 3 → bulk IN endpoint 0x81 becomes available */
    int r = libusb_set_interface_alt_setting(p->dh, 0, 3);
    if (r < 0)
        fprintf(stderr, "sync_stream: alt_setting(3) failed (%s), continuing\n",
                libusb_error_name(r));

    /* Clear any stale halt / endpoint state from a previous run */
    libusb_clear_halt(p->dh, 0x81);
    usleep(5000);

    /* Tell the device to start streaming */
    msisdr_streaming_start(p);
    usleep(10000);

    /* Flush any data the device already generated before we were ready */
    libusb_clear_halt(p->dh, 0x81);

    uint8_t *raw_buf = malloc(buf_len);
    if (!raw_buf) return -1;

    p->async_status = MSISDR_ASYNC_RUNNING;

    while (p->async_status == MSISDR_ASYNC_RUNNING) {
        int n_read = 0;
        r = libusb_bulk_transfer(p->dh, 0x81,
                                  raw_buf, (int)buf_len,
                                  &n_read, DEFAULT_BULK_TIMEOUT);

        if (r < 0 && r != LIBUSB_ERROR_TIMEOUT) {
            fprintf(stderr, "sync_stream: bulk error: %s\n",
                    libusb_error_name(r));
            break;
        }

        if (n_read > 0 && p->async_status == MSISDR_ASYNC_RUNNING) {
            /* Convert raw USB bytes → int16_t IQ pairs using the format
             * selected by msisdr_set_sample_rate (AUTO picks 252_S16 for
             * rates up to 6 MHz, which covers all typical SDR sample rates). */
            uint8_t *samples = NULL;
            int bytes = 0;

            switch (p->format) {
            case MSISDR_FORMAT_252_S16:
                samples = samples_realloc(p, (buf_len / 1024) * 1008);
                bytes = msisdr_samples_convert_252_s16(p, raw_buf, samples, n_read);
                break;
            case MSISDR_FORMAT_336_S16:
                samples = samples_realloc(p, (buf_len / 1024) * 1344);
                bytes = msisdr_samples_convert_336_s16(p, raw_buf, samples, n_read);
                break;
            case MSISDR_FORMAT_384_S16:
                samples = samples_realloc(p, (buf_len / 1024) * 1536);
                bytes = msisdr_samples_convert_384_s16(p, raw_buf, samples, n_read);
                break;
            case MSISDR_FORMAT_504_S16:
            case MSISDR_FORMAT_504_S8:
                samples = samples_realloc(p, (buf_len / 1024) * 2016);
                bytes = msisdr_samples_convert_504_s16(p, raw_buf, samples, n_read);
                break;
            default:
                break;
            }

            if (bytes > 0)
                cb(samples, (uint32_t)bytes, ctx);
        }
    }

    msisdr_streaming_stop(p);
    free(raw_buf);
    p->async_status = MSISDR_ASYNC_INACTIVE;
    return 0;
}
