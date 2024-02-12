#include "circbuf.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <xmmintrin.h>

CircBuf *circ_buf_create(size_t len) {
	void *mem = malloc(sizeof(CircBuf) + len);
	if (!mem) return NULL;
	CircBuf *self = mem;
	assert(pthread_mutex_init(&self->lock, NULL) == 0);
	self->data = (uint8_t*)mem + sizeof(CircBuf);
	self->len = len;
	self->rd = self->wr = 0;
	return self;
}

void circ_buf_destroy(CircBuf *self) {
	assert(pthread_mutex_destroy(&self->lock) == 0);
	free(self);
}

void circ_buf_read(CircBuf *restrict self, uint8_t *restrict dst, size_t n) {
	//printf("readable: %llu, attempt to read: %llu\n", (self->rd <= self->wr ? self->wr - self->rd : self->len - (self->rd - self->wr)), n);
	assert(n <= self->len-1);
	while (1) {
		pthread_mutex_lock(&self->lock);
		const size_t readable = self->rd <= self->wr ? self->wr - self->rd : self->len - (self->rd - self->wr);
		if (readable >= n)
			break;
		pthread_mutex_unlock(&self->lock);
		_mm_pause();
	}
	size_t n1 = n > self->len-self->rd ? self->len-self->rd : n;
	size_t n2 = n - n1;
	memcpy(dst, self->data+self->rd, n1);
	self->rd = (self->rd + n1) % self->len;
	memcpy(dst+n1, self->data+self->rd, n2);
	self->rd = (self->rd + n2) % self->len;
	pthread_mutex_unlock(&self->lock);
}

void circ_buf_write(CircBuf *restrict self, const uint8_t *restrict src, size_t n) {
	//printf("writeable: %llu, attempt to write: %llu\n", (self->rd <= self->wr ? self->len - (self->wr - self->rd) : self->rd - self->wr)-1, n);
	assert(n <= self->len-1);
	while (1) {
		pthread_mutex_lock(&self->lock);
		const size_t writeable = (self->rd <= self->wr ? self->len - (self->wr - self->rd) : self->rd - self->wr) - 1;
		if (writeable >= n)
			break;
		pthread_mutex_unlock(&self->lock);
		_mm_pause();
	}
	size_t n1 = n > self->len-self->wr ? self->len-self->wr : n;
	size_t n2 = n - n1;
	memcpy(self->data+self->wr, src, n1);
	self->wr = (self->wr + n1) % self->len;
	memcpy(self->data+self->wr, src+n1, n2);
	self->wr = (self->wr + n2) % self->len;
	pthread_mutex_unlock(&self->lock);
}