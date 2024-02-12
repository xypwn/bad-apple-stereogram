#ifndef __AVDECODE_H__
#define __AVDECODE_H__

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

typedef struct AVDecodePrivState AVDecodePrivState;

typedef struct AVDecodeInfo {
	int v_width;
	int v_height;
	double v_fps;
	enum AVPixelFormat v_format;
	int a_n_channels;
	int a_sample_rate;
	int a_sample_size;
	enum AVSampleFormat a_format;
	AVDecodePrivState *priv;
} AVDecodeInfo;

AVDecodeInfo avdecode_prepare(const char *filename);

void avdecode_run(
	AVDecodeInfo info,
	int (*on_vframe)(uint8_t *data, enum AVPixelFormat format, int width, int height, int linesize, void *userdata),
	int (*on_aframe)(uint8_t **data, enum AVSampleFormat format, int n_channels, int n_samples, void *userdata),
	void *userdata
);

#endif // __AVDECODE_H__