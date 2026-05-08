#ifndef SDR_SERVER_MSISDR_LIB_H
#define SDR_SERVER_MSISDR_LIB_H

#include <msisdr.h>

typedef struct msisdr_lib_t msisdr_lib;

struct msisdr_lib_t {
  void *handle; /* NULL — statically linked (no dlopen) */

  int (*msisdr_open)(msisdr_dev_t **dev, uint32_t index);
  int (*msisdr_close)(msisdr_dev_t *dev);
  int (*msisdr_set_center_freq)(msisdr_dev_t *dev, uint32_t freq);
  int (*msisdr_set_sample_rate)(msisdr_dev_t *dev, uint32_t rate);
  int (*msisdr_set_sample_format)(msisdr_dev_t *dev, const char *format);
  int (*msisdr_set_bandwidth)(msisdr_dev_t *dev, uint32_t bw);
  int (*msisdr_set_if_freq)(msisdr_dev_t *dev, uint32_t freq);
  int (*msisdr_set_hw_flavour)(msisdr_dev_t *dev, msisdr_hw_flavour_t flavour);
  int (*msisdr_rsp1a_gpio_init)(msisdr_dev_t *dev);
  int (*msisdr_set_tuner_gain)(msisdr_dev_t *dev, int gain);
  int (*msisdr_set_tuner_gain_mode)(msisdr_dev_t *dev, int mode);
  int (*msisdr_get_tuner_gains)(msisdr_dev_t *dev, int *gains);
  int (*msisdr_reset_buffer)(msisdr_dev_t *dev);
  int (*msisdr_read_async)(msisdr_dev_t *dev, msisdr_read_async_cb_t cb,
                            void *ctx, uint32_t num, uint32_t len);
  int (*msisdr_cancel_async)(msisdr_dev_t *dev);
};

int msisdr_lib_create(msisdr_lib **lib);
void msisdr_lib_destroy(msisdr_lib *lib);

#endif /* SDR_SERVER_MSISDR_LIB_H */
