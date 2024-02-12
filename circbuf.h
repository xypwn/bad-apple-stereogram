#ifndef __CIRCBUF_H__
#define __CIRCBUF_H__

#include <pthread.h>
#include <stdint.h>

typedef struct CircBuf {
	pthread_mutex_t lock;
	uint8_t *data;
	size_t len;
	size_t rd;
	size_t wr;
} CircBuf;

CircBuf *circ_buf_create(size_t len);
void circ_buf_destroy(CircBuf *self);
void circ_buf_read(CircBuf *restrict self, uint8_t *restrict dst, size_t n);
void circ_buf_write(CircBuf *restrict self, const uint8_t *restrict src, size_t n);

#endif // __CIRCBUF_H__