/*
 * Decodes a Bink file with the libraries of Dependencies/FFmpegWeb (built for WebAssembly and run
 * with node, or built natively) and prints what it found as "key value" lines, which
 * run_decode_test.py compares to the sidecar JSON of make_test_bik.py. The file is read through
 * custom I/O callbacks like the engine's FFmpegFile does it (a stream that cannot seek is the
 * worst case for the Bink demuxer).
 *
 *   bik_decode_test file.bik [seekable]
 */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static long long g_bytes_read;

static int read_cb(void *opaque, uint8_t *buf, int size)
{
	int n = (int)fread(buf, 1, (size_t)size, (FILE *)opaque);
	if (n > 0) g_bytes_read += n;
	return n > 0 ? n : AVERROR_EOF;
}

static int64_t seek_cb(void *opaque, int64_t offset, int whence)
{
	FILE *f = (FILE *)opaque;
	if (whence == AVSEEK_SIZE) {
		long cur = ftell(f);
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		fseek(f, cur, SEEK_SET);
		return size;
	}
	return fseek(f, (long)offset, whence & ~AVSEEK_FORCE) == 0 ? ftell(f) : -1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s file.bik [seekable]\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		printf("error cannot open %s\n", argv[1]);
		return 1;
	}
	int seekable = argc > 2;

	AVFormatContext *fmt = avformat_alloc_context();
	unsigned char *iobuf = av_malloc(0x10000);
	AVIOContext *io = avio_alloc_context(iobuf, 0x10000, 0, f, read_cb, NULL, seekable ? seek_cb : NULL);
	fmt->pb = io;
	fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
	int r = avformat_open_input(&fmt, NULL, NULL, NULL);
	if (r < 0) {
		printf("error avformat_open_input %d\n", r);
		return 1;
	}
	r = avformat_find_stream_info(fmt, NULL);
	if (r < 0) {
		printf("error avformat_find_stream_info %d\n", r);
		return 1;
	}
	printf("open_bytes_read %lld\n", g_bytes_read);
	printf("format %s\n", fmt->iformat->name);
	printf("streams %u\n", fmt->nb_streams);

	AVCodecContext *ctx[8] = {0};
	for (unsigned i = 0; i < fmt->nb_streams && i < 8; ++i) {
		const AVCodec *codec = avcodec_find_decoder(fmt->streams[i]->codecpar->codec_id);
		if (!codec) {
			printf("error no decoder for stream %u\n", i);
			return 1;
		}
		ctx[i] = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(ctx[i], fmt->streams[i]->codecpar);
		if ((r = avcodec_open2(ctx[i], codec, NULL)) < 0) {
			printf("error avcodec_open2 %d\n", r);
			return 1;
		}
		printf("stream %u %s %s\n", i, av_get_media_type_string(codec->type), codec->name);
	}

	AVStream *vs = fmt->streams[0];
	printf("width %d\nheight %d\n", ctx[0]->width, ctx[0]->height);
	printf("pix_fmt %s\n", av_get_pix_fmt_name(ctx[0]->pix_fmt));
	printf("fps %d/%d\n", vs->avg_frame_rate.num, vs->avg_frame_rate.den);
	printf("duration_frames %lld\n", (long long)vs->duration);
	if (fmt->nb_streams > 1) {
		printf("audio_rate %d\naudio_channels %d\naudio_fmt %s\n", ctx[1]->sample_rate, ctx[1]->ch_layout.nb_channels,
			av_get_sample_fmt_name(ctx[1]->sample_fmt));
	}

	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	int frames = 0;
	long long samples = 0;
	double peak = 0, sumsq = 0;
	long long crossings = 0;
	float prev = 0;
	/* BIK_SWS=bicubic|fast|point picks the scaler of the conversion (default bilinear, what the game uses) */
	const char *flags_name = getenv("BIK_SWS");
	const int sws_flags = !flags_name ? SWS_BILINEAR : !strcmp(flags_name, "bicubic") ? SWS_BICUBIC : !strcmp(flags_name, "fast") ? SWS_FAST_BILINEAR : !strcmp(flags_name, "point") ? SWS_POINT : SWS_BILINEAR;
	struct SwsContext *sws = NULL;
	uint8_t *rgb = NULL;
	double decode_ms = 0, convert_ms = 0;

	while (av_read_frame(fmt, pkt) >= 0) {
		AVCodecContext *c = ctx[pkt->stream_index];
		const double t0 = now_ms();
		if (avcodec_send_packet(c, pkt) < 0) {
			printf("error send_packet\n");
			return 1;
		}
		av_packet_unref(pkt);
		while (avcodec_receive_frame(c, frame) >= 0) {
			decode_ms += now_ms() - t0;
			if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
				const int W = frame->width;
				printf("frame %d y %d %d %d %d u %d v %d\n", frames,
					frame->data[0][0], frame->data[0][8], frame->data[0][8 * frame->linesize[0]],
					frame->data[0][16 * frame->linesize[0] + 24],
					frame->data[1][0], frame->data[2][0]);
				if (frames == 0) {
					sws = sws_getContext(W, frame->height, frame->format, W, frame->height, AV_PIX_FMT_BGR0, sws_flags, NULL, NULL, NULL);
					rgb = malloc((size_t)W * 4 * frame->height);
				}
				uint8_t *dst[1] = { rgb };
				int dst_stride[1] = { W * 4 };
				const double c0 = now_ms();
				sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
				convert_ms += now_ms() - c0;
				if (frames == 0)
					printf("rgb0 %d %d %d\n", rgb[2], rgb[1], rgb[0]);
				++frames;
			} else {
				const int n = frame->nb_samples;
				const int chn = frame->ch_layout.nb_channels;
				samples += n;
				for (int i = 0; i < n; ++i) {
					float s = av_sample_fmt_is_planar(frame->format) ? ((float *)frame->extended_data[0])[i]
						: ((float *)frame->data[0])[i * chn];
					double a = fabs(s);
					if (a > peak) peak = a;
					sumsq += (double)s * s;
					if ((prev < 0) != (s < 0)) ++crossings;
					prev = s;
				}
			}
		}
	}
	printf("video_frames %d\n", frames);
	if (seekable) {
		/* what FFmpegFile::rewind() does: back to the first frame, flush the decoders, decode again */
		if (av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) {
			printf("error av_seek_frame\n");
			return 1;
		}
		for (unsigned i = 0; i < fmt->nb_streams && i < 8; ++i)
			avcodec_flush_buffers(ctx[i]);
		int again = 0, first_y = -1;
		while (av_read_frame(fmt, pkt) >= 0) {
			AVCodecContext *c = ctx[pkt->stream_index];
			avcodec_send_packet(c, pkt);
			av_packet_unref(pkt);
			while (avcodec_receive_frame(c, frame) >= 0)
				if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
					if (again == 0) first_y = frame->data[0][0];
					++again;
				}
		}
		printf("rewind_frames %d\nrewind_first_y %d\n", again, first_y);
	}
	printf("audio_samples %lld\n", samples);
	printf("audio_peak %.4f\n", peak);
	printf("audio_rms %.4f\n", samples ? sqrt(sumsq / (double)samples) : 0.0);
	printf("audio_crossings %lld\n", crossings);
	printf("decode_ms_per_frame %.3f\nconvert_ms_per_frame %.3f\n", frames ? decode_ms / frames : 0.0, frames ? convert_ms / frames : 0.0);
	printf("done 1\n");
	return 0;
}
