#ifndef MSISDR_LIB_MOCK_H
#define MSISDR_LIB_MOCK_H

#include <stdint.h>

/* Block until the mock has delivered one buffer to the registered callback. */
void msisdr_wait_for_data_read(void);

/* Set the CS16 buffer to deliver on the next mock iteration.
 * buf: int16_t I/Q interleaved pairs, len: number of int16_t values. */
void msisdr_setup_mock_data(int16_t *buf, int len);

/* Signal the blocking msisdr_read_async mock to return. */
void msisdr_stop_mock(void);

#endif /* MSISDR_LIB_MOCK_H */
