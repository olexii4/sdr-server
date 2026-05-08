/*
 * msisdr_lib_mock.c — test mock for the MSi SDR device library.
 *
 * Mirrors the structure of airspy_lib_mock.c.  Both Airspy and MSi SDR
 * deliver CS16 (int16_t I/Q) samples, so the mock data path is identical.
 *
 * The real msisdr_read_async() is a *blocking* call that drives the libusb
 * event loop internally.  This mock simulates the blocking behaviour using
 * a pthread condition variable: the mock thread sleeps until test data is
 * provided via msisdr_setup_mock_data(), delivers one buffer to the callback,
 * and repeats until msisdr_stop_mock() is called.
 */
#include "msisdr_lib_mock.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/sdr/msisdr_lib.h"

/* Opaque device type required by the lib header */
struct msisdr_dev {
  int dummy;
};

struct mock_status {
  int16_t *buffer;
  int len;

  pthread_t worker_thread;
  msisdr_read_async_cb_t callback;
  void *cb_ctx;

  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool stopped;
  bool data_was_read;
};

static struct mock_status msisdr_mock = {0};

/* ── Mock hardware functions ─────────────────────────────────── */

static int mock_msisdr_open(msisdr_dev_t **dev, uint32_t index) {
  (void)index;
  static struct msisdr_dev dummy = {0};
  *dev = &dummy;
  return 0;
}

static int mock_msisdr_close(msisdr_dev_t *dev) {
  (void)dev;
  return 0;
}

static int mock_msisdr_set_center_freq(msisdr_dev_t *dev, uint32_t freq) {
  (void)dev; (void)freq; return 0;
}

static int mock_msisdr_set_sample_rate(msisdr_dev_t *dev, uint32_t rate) {
  (void)dev; (void)rate; return 0;
}

static int mock_msisdr_set_sample_format(msisdr_dev_t *dev, const char *fmt) {
  (void)dev; (void)fmt; return 0;
}

static int mock_msisdr_set_bandwidth(msisdr_dev_t *dev, uint32_t bw) {
  (void)dev; (void)bw; return 0;
}

static int mock_msisdr_set_if_freq(msisdr_dev_t *dev, uint32_t freq) {
  (void)dev; (void)freq; return 0;
}

static int mock_msisdr_set_hw_flavour(msisdr_dev_t *dev, msisdr_hw_flavour_t f) {
  (void)dev; (void)f; return 0;
}

static int mock_msisdr_rsp1a_gpio_init(msisdr_dev_t *dev) {
  (void)dev; return 0;
}

static int mock_msisdr_set_tuner_gain(msisdr_dev_t *dev, int gain) {
  (void)dev; (void)gain; return 0;
}

static int mock_msisdr_set_tuner_gain_mode(msisdr_dev_t *dev, int mode) {
  (void)dev; (void)mode; return 0;
}

static int mock_msisdr_get_tuner_gains(msisdr_dev_t *dev, int *gains) {
  (void)dev;
  if (gains != NULL) {
    gains[0] = 0; gains[1] = 102;
  }
  return 2;
}

static int mock_msisdr_reset_buffer(msisdr_dev_t *dev) {
  (void)dev; return 0;
}

/* The blocking read_async mock: delivers one buffer per iteration. */
static int mock_msisdr_read_async(msisdr_dev_t *dev,
                                   msisdr_read_async_cb_t cb,
                                   void *ctx,
                                   uint32_t num, uint32_t len) {
  (void)dev; (void)num; (void)len;
  msisdr_mock.callback = cb;
  msisdr_mock.cb_ctx   = ctx;

  pthread_mutex_lock(&msisdr_mock.mutex);
  while (!msisdr_mock.stopped) {
    if (msisdr_mock.data_was_read) {
      pthread_cond_broadcast(&msisdr_mock.condition);
    }
    if (msisdr_mock.buffer == NULL) {
      pthread_cond_wait(&msisdr_mock.condition, &msisdr_mock.mutex);
    }
    if (msisdr_mock.buffer != NULL) {
      /* Deliver buffer to the device callback as raw bytes (CS16) */
      cb((unsigned char *)msisdr_mock.buffer,
         (uint32_t)(msisdr_mock.len * sizeof(int16_t)),
         ctx);
      msisdr_mock.buffer = NULL;
      msisdr_mock.data_was_read = true;
      pthread_cond_broadcast(&msisdr_mock.condition);
    }
  }
  pthread_mutex_unlock(&msisdr_mock.mutex);
  return 0;
}

static int mock_msisdr_cancel_async(msisdr_dev_t *dev) {
  (void)dev;
  msisdr_stop_mock();
  return 0;
}

/* ── Public mock control functions ───────────────────────────── */

int msisdr_lib_create(msisdr_lib **lib) {
  msisdr_lib *result = malloc(sizeof(msisdr_lib));
  if (result == NULL) {
    return -ENOMEM;
  }
  *result = (msisdr_lib){0};

  result->msisdr_open              = mock_msisdr_open;
  result->msisdr_close             = mock_msisdr_close;
  result->msisdr_set_center_freq   = mock_msisdr_set_center_freq;
  result->msisdr_set_sample_rate   = mock_msisdr_set_sample_rate;
  result->msisdr_set_sample_format = mock_msisdr_set_sample_format;
  result->msisdr_set_bandwidth     = mock_msisdr_set_bandwidth;
  result->msisdr_set_if_freq       = mock_msisdr_set_if_freq;
  result->msisdr_set_hw_flavour    = mock_msisdr_set_hw_flavour;
  result->msisdr_rsp1a_gpio_init   = mock_msisdr_rsp1a_gpio_init;
  result->msisdr_set_tuner_gain    = mock_msisdr_set_tuner_gain;
  result->msisdr_set_tuner_gain_mode = mock_msisdr_set_tuner_gain_mode;
  result->msisdr_get_tuner_gains   = mock_msisdr_get_tuner_gains;
  result->msisdr_reset_buffer      = mock_msisdr_reset_buffer;
  result->msisdr_read_async        = mock_msisdr_read_async;
  result->msisdr_cancel_async      = mock_msisdr_cancel_async;

  msisdr_mock = (struct mock_status){0};
  pthread_mutex_init(&msisdr_mock.mutex, NULL);
  pthread_cond_init(&msisdr_mock.condition, NULL);
  msisdr_mock.stopped       = false;
  msisdr_mock.data_was_read = false;

  *lib = result;
  return 0;
}

void msisdr_lib_destroy(msisdr_lib *lib) {
  if (lib == NULL) {
    return;
  }
  free(lib);
}

void msisdr_wait_for_data_read(void) {
  pthread_mutex_lock(&msisdr_mock.mutex);
  while (!msisdr_mock.data_was_read) {
    pthread_cond_wait(&msisdr_mock.condition, &msisdr_mock.mutex);
  }
  pthread_cond_broadcast(&msisdr_mock.condition);
  pthread_mutex_unlock(&msisdr_mock.mutex);
}

void msisdr_setup_mock_data(int16_t *buf, int len) {
  pthread_mutex_lock(&msisdr_mock.mutex);
  msisdr_mock.buffer       = buf;
  msisdr_mock.len          = len;
  msisdr_mock.data_was_read = false;
  pthread_cond_broadcast(&msisdr_mock.condition);
  pthread_mutex_unlock(&msisdr_mock.mutex);
}

void msisdr_stop_mock(void) {
  pthread_mutex_lock(&msisdr_mock.mutex);
  msisdr_mock.stopped = true;
  msisdr_mock.buffer  = NULL;
  pthread_cond_broadcast(&msisdr_mock.condition);
  pthread_mutex_unlock(&msisdr_mock.mutex);
}
