#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <libavutil/pixdesc.h> // av_get_pix_fmt_name

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "rng.h"
#include "avdecode.h"

#define DEBUGINF_PERIOD 100

typedef struct CircBuf {
	pthread_mutex_t lock;
	uint8_t *data;
	size_t len;
	size_t rd;
	size_t wr;
} CircBuf;

static CircBuf *circ_buf_create(size_t len) {
	void *mem = malloc(sizeof(CircBuf) + len);
	if (!mem) return NULL;
	CircBuf *self = mem;
	assert(pthread_mutex_init(&self->lock, NULL) == 0);
	self->data = (uint8_t*)mem + sizeof(CircBuf);
	self->len = len;
	self->rd = self->wr = 0;
	return self;
}

static void circ_buf_destroy(CircBuf *self) {
	assert(pthread_mutex_destroy(&self->lock) == 0);
	free(self);
}

static void circ_buf_read(CircBuf *restrict self, uint8_t *restrict dst, size_t n) {
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

static void circ_buf_write(CircBuf *restrict self, const uint8_t *restrict src, size_t n) {
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

static uint32_t rgba_to_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		return r << 24 | g << 16 | b << 8 | a;
}

static uint32_t random_color_u32(RNG *rng) {
	return (rng_u64(rng) & 0xFFFFFF00) | 0xFF;
}

static CircBuf *audiobuf = NULL;
static atomic_size_t audio_pos; // in bytes
static atomic_size_t audio_len; // in bytes

static CircBuf *videobuf = NULL;
static atomic_size_t video_n_frames;

static void audio_callback(void *userdata, uint8_t *stream, int len) {
	AVDecodeInfo *avinfo = (AVDecodeInfo*)userdata;
	size_t avail = atomic_load(&audio_len) - atomic_load(&audio_pos);
	size_t n = avail < len ? avail : len;
	circ_buf_read(audiobuf, stream, n);
	int silence = (avinfo->a_format == AV_SAMPLE_FMT_U8 || avinfo->a_format == AV_SAMPLE_FMT_U8P) ? 0x80 : 0;
	memset(stream + n, silence, len-n);
	atomic_fetch_add(&audio_pos, n);
}

int on_vframe(uint8_t *data, enum AVPixelFormat format, int width, int height, int linesize, void *userdata) {
	//printf("saving frame %llu, fmt: %s, %d\n", videobuf_frames, av_get_pix_fmt_name(format), linesize);
	//fflush(stdout);	
	for (int i = 0; i < height; ++i) {
		circ_buf_write(videobuf, data + i*linesize, width);
	}
	atomic_fetch_add(&video_n_frames, 1);
	return 0;
}

int on_aframe(uint8_t **data, enum AVSampleFormat format, int n_channels, int n_samples, void *userdata) {
	size_t data_size = av_get_bytes_per_sample(format);
	//printf("ch: %d, fmt: %s\n", n_channels, av_get_sample_fmt_name(format));
	//fflush(stdout);
	if (av_sample_fmt_is_planar(format)) {
		for (int i = 0; i < n_samples; ++i) {
			for (int ch = 0; ch < n_channels; ++ch) {
				circ_buf_write(audiobuf, data[ch] + data_size*i, data_size);
			}
		}
	} else {
		circ_buf_write(audiobuf, data[0], n_samples * n_channels * data_size);
	}
	atomic_fetch_add(&audio_len, n_samples * n_channels * data_size);
	return 0;
}

void img_draw_autostereogram(uint32_t *dst, uint8_t *src, int width, int height, int eyedist /*in pixels*/, double close_ratio, RNG *rng) {
	uint32_t *pix = malloc(sizeof(int) * width);
	int *same = malloc(sizeof(int) * width);
	for (int y = 0; y < height; ++y) {	
		for (int x = 0; x < width; ++x)
			same[x] = x;
		
		for (int x = 0; x < width; ++x) {
			double val = (double)src[y*width + x] / 255.0;
			int s = round((1-close_ratio*val)*eyedist/(2-close_ratio*val));
			int left = x - s/2;
			int right = left + s;
			if (left < 0 || right >= width)
				continue;
			bool visible = false;
			int t = 1;
			double zt;

			do {
				if (x-t < 0 || x+t > width)
					break;
				double vall = (double)src[y*width + (x-t)] / 255.0;
				double valr = (double)src[y*width + (x+t)] / 255.0;
				zt = val + 2*(2-close_ratio*val)*t/(close_ratio*eyedist);
				visible = vall < zt && valr < zt;
				++t;
			} while (visible && zt < 1);
			if (visible) {
				int l = same[left];
				while (l != left && l != right) {
					if (l < right) {
						left = l;
						l = same[left];
					} else {
						same[left] = right;
						left = right;
						l = same[left];
						right = l;
					}
				}
				same[left] = right;
			}
		}
		for (int x = width-1; x >= 0; --x) {
			if (same[x] == x) pix[x] = rng_bool(rng, 0.5) ? rgba_to_u32(255, 255, 255, 255) : rgba_to_u32(0, 0, 0, 255);// random_color_u32(rng);
			else pix[x] = pix[same[x]];
			dst[y*width + x] = pix[x];
		}
	}
	free(same);
	free(pix);
}

typedef struct {
	AVDecodeInfo avinfo;
	void *userdata;
} ThreadDecodeData;

static void *thread_decode(void *vargp) {
	ThreadDecodeData *data = (ThreadDecodeData*)vargp;
	avdecode_run(data->avinfo, on_vframe, on_aframe, data->userdata);
	fflush(stdout);
	return NULL;
}

int main(void) {
	AVDecodeInfo avinfo = avdecode_prepare("bad-apple.mp4");	

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		printf("SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window *win = SDL_CreateWindow("Stereogram", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, avinfo.v_width*2, avinfo.v_height*2, SDL_WINDOW_RESIZABLE);
	if (!win) {
		printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer *rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!rend) {
		printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Texture *tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, avinfo.v_width, avinfo.v_height);
	if (!tex) {
		printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
		return 1;
	}

	audiobuf = circ_buf_create(52428800);
	if (!audiobuf) {
		printf("circ_buf_create failed\n");
		return 1;
	}

	videobuf = circ_buf_create(104857600);
	if (!videobuf) {
		printf("circ_buf_create failed\n");
		return 1;
	}
	
	pthread_t thread_decode_id;

	ThreadDecodeData thread_decode_data = {
		.avinfo = avinfo,
		.userdata = NULL,
	};
	int ret = pthread_create(&thread_decode_id, NULL, thread_decode, &thread_decode_data);
	if (ret) {
		printf("pthread_create failed: %s\n", strerror(ret));
		return 1;
	}

	SDL_AudioSpec spec = {0}, aspec;
	spec.freq = avinfo.a_sample_rate;
	switch (avinfo.a_format) {
	case AV_SAMPLE_FMT_U8:   spec.format = AUDIO_U8;     break;
	case AV_SAMPLE_FMT_S16:  spec.format = AUDIO_S16SYS; break;
	case AV_SAMPLE_FMT_S32:  spec.format = AUDIO_S32SYS; break;
	case AV_SAMPLE_FMT_FLT:  spec.format = AUDIO_F32SYS; break;
	case AV_SAMPLE_FMT_U8P:  spec.format = AUDIO_U8;     break;
	case AV_SAMPLE_FMT_S16P: spec.format = AUDIO_S16SYS; break;
	case AV_SAMPLE_FMT_S32P: spec.format = AUDIO_S32SYS; break;
	case AV_SAMPLE_FMT_FLTP: spec.format = AUDIO_F32SYS; break;
	default:
		printf("unsupported audio format: %s\n", av_get_sample_fmt_name(avinfo.a_format));
		return 1;
	}
	spec.channels = avinfo.a_n_channels;
	spec.samples = 1024;
	spec.callback = audio_callback;
	spec.userdata = &avinfo;
	
	int audiodev = SDL_OpenAudioDevice(NULL, 0, &spec, &aspec, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
	assert(audiodev > 0);
	SDL_PauseAudioDevice(audiodev, 0);

	uint32_t *pxdata = malloc(sizeof(uint32_t) * avinfo.v_height * avinfo.v_width);
	if (!pxdata) {
		printf("malloc failed\n");
		return 1;
	}

	size_t video_frame = 0;
	bool force_redraw = false;
	uint8_t *framebuf = malloc(avinfo.v_width * avinfo.v_height);
	
	uint64_t debuginf_last_time = 0;
	uint64_t fps_last_time = 0;
	size_t fps_acc = 0;
	double fps = 0.0;

	bool paused = false;
	bool stereogram = true;
	int eyedist = 120;
	int close_ratio_den = 8;
	bool quit = false;
	while (!quit) {
		SDL_Event evt;
		while (SDL_PollEvent(&evt) != 0) {
			switch (evt.type) {
			case SDL_QUIT:
				quit = true;
				break;
			case SDL_KEYDOWN:
				switch (evt.key.keysym.sym) {
					case SDLK_SPACE:
						SDL_PauseAudioDevice(audiodev, (paused = !paused));
						break;
					case SDLK_m:
						stereogram = !stereogram;
						force_redraw = true;
						break;
					case SDLK_RIGHT:
						++eyedist;
						force_redraw = true;
						break;
					case SDLK_LEFT:
						if (eyedist-1 >= 10)
							--eyedist;
						force_redraw = true;
						break;
					case SDLK_UP:
						if (close_ratio_den-1 >= 2)
							--close_ratio_den;
						force_redraw = true;
						break;
					case SDLK_DOWN:
						++close_ratio_den;
						force_redraw = true;
						break;
				}
				break;
			}
		}

		double audio_time = (double)atomic_load(&audio_pos) / (double)(aspec.freq * avinfo.a_n_channels * avinfo.a_sample_size);
		size_t video_target_frame = audio_time * avinfo.v_fps;
		
		uint64_t time_now = SDL_GetTicks64();
		if (time_now - debuginf_last_time >= DEBUGINF_PERIOD) {
			size_t bytes_per_sample = avinfo.a_n_channels * avinfo.a_sample_size;
			printf(
				"t=%lfs, fps=%.2lf, vid: %llu/%llu (%llu cached), aud: %llu (%llu cached), eyedist=%dpx, close=1/%d",
				audio_time,
				fps,
				video_frame, video_target_frame, atomic_load(&video_n_frames) - video_frame,
				atomic_load(&audio_pos) / bytes_per_sample, (atomic_load(&audio_len) - atomic_load(&audio_pos)) / bytes_per_sample,
				eyedist,
				close_ratio_den
			);
			printf("     \r");
			debuginf_last_time = time_now;
		}
		
		if (time_now - fps_last_time >= 1000) {
			fps = fps_acc;
			fps_acc = 0;	
			fps_last_time = time_now;
		}

		bool redraw = force_redraw;
		force_redraw = false;
		if (video_frame < video_target_frame) {
			if (video_frame < atomic_load(&video_n_frames)) {
				circ_buf_read(videobuf, framebuf, avinfo.v_width*avinfo.v_height);
				redraw = true;
			}
			++video_frame;
		}

		if (redraw) {
			if (stereogram) {
				RNG_XoShiRo256ss rng_xoshiro = rng_xoshiro256ss(video_frame);
				RNG *rng = (RNG*)&rng_xoshiro;
				img_draw_autostereogram(pxdata, framebuf, avinfo.v_width, avinfo.v_height, eyedist, 1.0/(double)close_ratio_den, rng);
			} else {
				for (size_t y = 0; y < avinfo.v_height; ++y) {
					for (size_t x = 0; x < avinfo.v_width; ++x) {
						uint8_t val = framebuf[y*avinfo.v_width + x];
						pxdata[y*avinfo.v_width + x] = rgba_to_u32(val, val, val, 255);
					}
				}
			}
			if (SDL_UpdateTexture(tex, NULL, pxdata, sizeof(uint32_t) * avinfo.v_width) != 0) {
				printf("SDL_UpdateTexture failed: %s\n", SDL_GetError());
				return 1;
			}
		}
		
		++fps_acc;

		SDL_RenderClear(rend);
		SDL_RenderCopy(rend, tex, NULL, NULL);
		SDL_RenderPresent(rend);
	}
	
	SDL_CloseAudioDevice(audiodev);
	circ_buf_destroy(videobuf);
	circ_buf_destroy(audiobuf);
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(rend);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}