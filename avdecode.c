#include "avdecode.h"

#include <assert.h>

typedef struct AVDecodePrivState {
	int video_stream_index;
	int audio_stream_index;
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
	AVCodecContext *audio_dec_ctx;
} AVDecodePrivState;

static void open_codec(int *stream_index, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
	int ret;
	const AVCodec *codec;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, &codec, 0);
	assert(ret >= 0);
	*stream_index = ret;

	*dec_ctx = avcodec_alloc_context3(codec);
	assert(*dec_ctx);

	ret = avcodec_parameters_to_context(*dec_ctx, fmt_ctx->streams[*stream_index]->codecpar);
	assert(ret == 0);

	ret = avcodec_open2(*dec_ctx, codec, NULL);
	assert(ret == 0);
}

static void decode_packet(
	AVCodecContext *dec_ctx,
	AVFrame *frame,
	const AVPacket *packet,
	int (*on_vframe)(uint8_t *data, enum AVPixelFormat format, int width, int height, int linesize, void *userdata),
	int (*on_aframe)(uint8_t **data, enum AVSampleFormat format, int n_channels, int n_samples, void *userdata),
	void *userdata
) {
	int ret = avcodec_send_packet(dec_ctx, packet);
	assert(ret == 0);

	while (1) {
		ret = avcodec_receive_frame(dec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		} else
			assert(ret == 0);

		if (dec_ctx->codec->type == AVMEDIA_TYPE_VIDEO) {
			ret = on_vframe(frame->data[0], frame->format, frame->width, frame->height, frame->linesize[0], userdata);
			assert(ret == 0);
		} else if (dec_ctx->codec->type == AVMEDIA_TYPE_AUDIO) {
			ret = on_aframe(frame->data, frame->format, frame->ch_layout.nb_channels, frame->nb_samples, userdata);
			assert(ret == 0);
		}

		av_frame_unref(frame);
	}
}

AVDecodeInfo avdecode_prepare(const char *filename) {
	AVDecodeInfo info = {0};
	info.priv = malloc(sizeof(AVDecodePrivState));
	*info.priv = (AVDecodePrivState){0};

	int ret;

	ret = avformat_open_input(&info.priv->fmt_ctx, filename, NULL, NULL);
	assert(ret == 0);

	ret = avformat_find_stream_info(info.priv->fmt_ctx, NULL);
	assert(ret == 0);

	open_codec(&info.priv->video_stream_index, &info.priv->video_dec_ctx, info.priv->fmt_ctx, AVMEDIA_TYPE_VIDEO);
	open_codec(&info.priv->audio_stream_index, &info.priv->audio_dec_ctx, info.priv->fmt_ctx, AVMEDIA_TYPE_AUDIO);

	info.v_width = info.priv->video_dec_ctx->width;
	info.v_height = info.priv->video_dec_ctx->height;
	{
		AVRational fps = info.priv->video_dec_ctx->framerate;
		info.v_fps = (double)fps.num / (double)fps.den;
	}
	info.v_format = info.priv->video_dec_ctx->pix_fmt;

	info.a_n_channels = info.priv->audio_dec_ctx->ch_layout.nb_channels;
	info.a_sample_rate = info.priv->audio_dec_ctx->sample_rate;
	info.a_sample_size = av_get_bytes_per_sample(info.priv->audio_dec_ctx->sample_fmt);
	info.a_format = info.priv->audio_dec_ctx->sample_fmt;

	return info;
}

void avdecode_run(
	AVDecodeInfo info,
	int (*on_vframe)(uint8_t *data, enum AVPixelFormat format, int width, int height, int linesize, void *userdata),
	int (*on_aframe)(uint8_t **data, enum AVSampleFormat format, int n_channels, int n_samples, void *userdata),
	void *userdata
) {
	AVFrame *frame = av_frame_alloc();
	assert(frame);

	AVPacket *packet = av_packet_alloc();
	assert(packet);

	while (av_read_frame(info.priv->fmt_ctx, packet) >= 0) {		
		if (packet->stream_index == info.priv->video_stream_index) {
			decode_packet(info.priv->video_dec_ctx, frame, packet, on_vframe, on_aframe, userdata);
		} else if (packet->stream_index == info.priv->audio_stream_index) {
			decode_packet(info.priv->audio_dec_ctx, frame, packet, on_vframe, on_aframe, userdata);
		}

		av_packet_unref(packet);
	}

	decode_packet(info.priv->video_dec_ctx, frame, NULL, on_vframe, on_aframe, userdata);
	decode_packet(info.priv->audio_dec_ctx, frame, NULL, on_vframe, on_aframe, userdata);

	avcodec_free_context(&info.priv->video_dec_ctx);
	avcodec_free_context(&info.priv->audio_dec_ctx);
	avformat_close_input(&info.priv->fmt_ctx);
	av_frame_free(&frame);
	av_packet_free(&packet);
	free(info.priv);
}