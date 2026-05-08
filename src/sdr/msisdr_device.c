/*
 * msisdr_device.c — MSi SDR (MSi2500/MSi001) device driver for sdr-server.
 *
 * libmsisdr outputs interleaved int16 I/Q pairs (CS16) after internal USB
 * packet conversion, identical to the Airspy CS16 format.  The sdr_callback
 * receives these as (uint8_t *buf, uint32_t len) and forwards them into the
 * DSP worker queue with no additional format conversion.
 *
 * msisdr_read_async() is a blocking call that drives the libusb event loop
 * internally.  It runs in a dedicated rx_thread and is stopped by calling
 * msisdr_cancel_async() from another thread, which causes read_async to return.
 */
#include "msisdr_device.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define ERROR_CHECK(x, msg)                         \
  do {                                              \
    int __rc = (x);                                 \
    if (__rc < 0) {                                 \
      fprintf(stderr, "%s: %d\n", (msg), __rc);    \
      if (device->dev != NULL) {                    \
        device->lib->msisdr_close(device->dev);     \
        device->dev = NULL;                         \
      }                                             \
      return __rc;                                  \
    }                                               \
  } while (0)

struct msisdr_device_t {
  msisdr_dev_t *dev;
  pthread_t rx_thread;
  atomic_bool running;

  void (*sdr_callback)(uint8_t *buf, uint32_t len, void *ctx);
  void *ctx;
  struct server_config *server_config;
  msisdr_lib *lib;
};

/* Called by libmsisdr on each completed USB transfer. */
static void msisdr_async_callback(unsigned char *buf, uint32_t len, void *ctx) {
  struct msisdr_device_t *device = (struct msisdr_device_t *) ctx;
  if (atomic_load(&device->running)) {
    device->sdr_callback(buf, len, device->ctx);
  }
}

/* Worker thread: blocks inside msisdr_read_async until cancel is called. */
static void *msisdr_rx_thread_fn(void *arg) {
  struct msisdr_device_t *device = (struct msisdr_device_t *) arg;
  /* Pass buffer_size as xfer_out_len so libmsisdr assembles fixed-size chunks
   * matching the server's configured buffer_size before invoking the callback. */
  device->lib->msisdr_read_async(device->dev, msisdr_async_callback, device,
                                  0, device->server_config->buffer_size);
  return NULL;
}

int msisdr_device_create(struct server_config *server_config, msisdr_lib *lib,
                          void (*sdr_callback)(uint8_t *buf, uint32_t len, void *ctx),
                          void *ctx, void **plugin) {
  struct msisdr_device_t *device = malloc(sizeof(struct msisdr_device_t));
  if (device == NULL) {
    return -ENOMEM;
  }
  *device = (struct msisdr_device_t){0};
  device->lib = lib;
  device->sdr_callback = sdr_callback;
  device->ctx = ctx;
  device->server_config = server_config;
  atomic_init(&device->running, false);
  fprintf(stdout, "msisdr device created\n");
  *plugin = device;
  return 0;
}

void msisdr_device_destroy(void *plugin) {
  if (plugin == NULL) {
    return;
  }
  free(plugin);
  fprintf(stdout, "msisdr device destroyed\n");
}

int msisdr_device_start_rx(uint32_t band_freq, void *plugin) {
  struct msisdr_device_t *device = (struct msisdr_device_t *) plugin;
  struct server_config *cfg = device->server_config;

  ERROR_CHECK(device->lib->msisdr_open(&device->dev, (uint32_t)cfg->device_index),
              "<3>unable to open msisdr device");

  /* Force HW_DEFAULT for all devices (incl. RSP1A) — the standard MSi001
   * PLL table works correctly on both MSi2500 and RSP1A clones. */
  ERROR_CHECK(device->lib->msisdr_set_hw_flavour(device->dev, MSISDR_HW_DEFAULT),
              "<3>unable to set msisdr hw_flavour");

  /* RSP1A (hw_flavour=1 in config): clear FM-band notch filter (GPIO 0x13). */
  if (cfg->msisdr_hw_flavour == 1) {
    device->lib->msisdr_rsp1a_gpio_init(device->dev);
  }

  /* 504_S16: 504 I/Q pairs per 1024-byte USB block, output as int16_t CS16.
   * This matches the Airspy CS16 format used by sdr-server's DSP workers. */
  ERROR_CHECK(device->lib->msisdr_set_sample_format(device->dev, "504_S16"),
              "<3>unable to set msisdr sample format");

  /* IF frequency: 0 = zero-IF (direct conversion, default). */
  uint32_t if_freq = (uint32_t)(cfg->msisdr_if_freq < 0 ? 0 : cfg->msisdr_if_freq);
  ERROR_CHECK(device->lib->msisdr_set_if_freq(device->dev, if_freq),
              "<3>unable to set msisdr if_freq");

  /* Bandwidth: 0 in config means use 8 MHz default. */
  uint32_t bw = cfg->msisdr_bandwidth > 0 ? cfg->msisdr_bandwidth : 8000000U;
  ERROR_CHECK(device->lib->msisdr_set_bandwidth(device->dev, bw),
              "<3>unable to set msisdr bandwidth");

  ERROR_CHECK(device->lib->msisdr_set_sample_rate(device->dev, cfg->band_sampling_rate),
              "<3>unable to set msisdr sample rate");

  ERROR_CHECK(device->lib->msisdr_set_center_freq(device->dev, band_freq),
              "<3>unable to set msisdr center frequency");

  /* Gain: reuse the top-level gain_mode/gain config fields.
   * 0 = auto (limited hardware support on MSi2500), 1 = manual 0..102 dB. */
  ERROR_CHECK(device->lib->msisdr_set_tuner_gain_mode(device->dev, cfg->msisdr_gain_mode),
              "<3>unable to set msisdr gain mode");
  if (cfg->msisdr_gain_mode == 1) {
    ERROR_CHECK(device->lib->msisdr_set_tuner_gain(device->dev, cfg->msisdr_gain),
                "<3>unable to set msisdr gain");
    fprintf(stdout, "msisdr gain: %d dB\n", cfg->msisdr_gain);
  } else {
    fprintf(stdout, "msisdr gain: auto\n");
  }

  ERROR_CHECK(device->lib->msisdr_reset_buffer(device->dev),
              "<3>unable to reset msisdr buffer");

  atomic_store(&device->running, true);

  int rc = pthread_create(&device->rx_thread, NULL, msisdr_rx_thread_fn, device);
  if (rc != 0) {
    fprintf(stderr, "<3>failed to create msisdr rx thread: %d\n", rc);
    device->lib->msisdr_close(device->dev);
    device->dev = NULL;
    atomic_store(&device->running, false);
    return -rc;
  }

  fprintf(stdout, "msisdr rx started at %u Hz, band_freq %u Hz\n",
          cfg->band_sampling_rate, band_freq);
  return 0;
}

void msisdr_device_stop_rx(void *plugin) {
  struct msisdr_device_t *device = (struct msisdr_device_t *) plugin;
  if (device->dev == NULL) {
    return;
  }
  /* Signal the callback to stop forwarding buffers, then unblock read_async. */
  atomic_store(&device->running, false);
  device->lib->msisdr_cancel_async(device->dev);
  pthread_join(device->rx_thread, NULL);
  device->lib->msisdr_close(device->dev);
  device->dev = NULL;
  fprintf(stdout, "msisdr rx stopped\n");
}
