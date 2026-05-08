/*
 * msisdr_lib.c — static-link loader for libmsisdr.
 *
 * Unlike rtlsdr_lib.c / airspy_lib.c which use dlopen, libmsisdr is vendored
 * as a static library and linked directly into the executable.  This file
 * simply populates the function-pointer struct from the linked symbols.
 *
 * The mock version (test/msisdr_lib_mock.c) populates the same struct with
 * test implementations, allowing unit tests without real hardware.
 */
#include "msisdr_lib.h"
#include <errno.h>
#include <stdlib.h>

int msisdr_lib_create(msisdr_lib **lib) {
  msisdr_lib *result = (msisdr_lib *) malloc(sizeof(msisdr_lib));
  if (result == NULL) {
    return -ENOMEM;
  }

  result->handle = NULL; /* statically linked — no dlopen handle */

  result->msisdr_open             = msisdr_open;
  result->msisdr_close            = msisdr_close;
  result->msisdr_set_center_freq  = msisdr_set_center_freq;
  result->msisdr_set_sample_rate  = msisdr_set_sample_rate;
  result->msisdr_set_sample_format = msisdr_set_sample_format;
  result->msisdr_set_bandwidth    = msisdr_set_bandwidth;
  result->msisdr_set_if_freq      = msisdr_set_if_freq;
  result->msisdr_set_hw_flavour   = msisdr_set_hw_flavour;
  result->msisdr_rsp1a_gpio_init  = msisdr_rsp1a_gpio_init;
  result->msisdr_set_tuner_gain   = msisdr_set_tuner_gain;
  result->msisdr_set_tuner_gain_mode = msisdr_set_tuner_gain_mode;
  result->msisdr_get_tuner_gains  = msisdr_get_tuner_gains;
  result->msisdr_reset_buffer     = msisdr_reset_buffer;
  result->msisdr_read_async       = msisdr_read_async;
  result->msisdr_cancel_async     = msisdr_cancel_async;

  *lib = result;
  return 0;
}

void msisdr_lib_destroy(msisdr_lib *lib) {
  if (lib == NULL) {
    return;
  }
  /* handle is NULL (static link) — nothing to dlclose */
  free(lib);
}
