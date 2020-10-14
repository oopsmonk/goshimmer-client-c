#ifndef __UTILS_BYTE_BUFFER_H__
#define __UTILS_BYTE_BUFFER_H__

#include <stdbool.h>
#include <stdlib.h>

#include "core/types.h"

typedef struct {
  size_t len;
  size_t cap;
  byte_t* data;
} byte_buf_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocates data buffer
 *
 * @return byte_buf_t*
 */
byte_buf_t* byte_buf_new();

/**
 * @brief Allocates data buffer with given data
 *
 * @param[in] data Inital data
 * @param[in] len The size of data
 * @return byte_buf_t*
 */
byte_buf_t* byte_buf_new_with_data(byte_t data[], size_t len);

/**
 * @brief Appends data to buffer
 *
 * @param[in] buf A buffer object
 * @param[in] data The data for appending
 * @param[in] len The size of data
 * @return true On success
 * @return false On failed
 */
bool byte_buf_append(byte_buf_t* buf, byte_t const data[], size_t len);

/**
 * @brief Sets data to the buffer
 *
 * @param[in] buf A buffer object
 * @param[in] data The data to set
 * @param[in] len The length of data
 * @return true On success
 * @return false On failed
 */
bool byte_buf_set(byte_buf_t* buf, byte_t const data[], size_t len);

/**
 * @brief Frees data buffer
 *
 * @param[in] buf A byte buffer object
 */
void byte_buf_free(byte_buf_t* buf);

/**
 * @brief Converts byte buffer to string
 *
 * @param[out] buf A byte buffer object
 */
void byte_buf2str(byte_buf_t* buf);

byte_buf_t* byte_buf_clonen(byte_buf_t* buf, size_t length);

static byte_buf_t* byte_buf_clone(byte_buf_t* buf) { return byte_buf_clonen(buf, buf->len); };

bool byte_buf_reserve(byte_buf_t* buf, size_t len);

void byte_buf_print(byte_buf_t* buf);

#ifdef __cplusplus
}
#endif

#endif