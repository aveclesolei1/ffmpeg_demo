#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include <libavutil/frame.h>
#include <libavutil/mem.h>
 
#include <libavcodec/avcodec.h>

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;
 
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }
 
    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}

void decode(AVCodecContext* codec_ctx, AVPacket* pkt, AVFrame* frame, FILE* out_file) {
	int i, ch;
	int ret, data_size;

	ret = avcodec_send_packet(codec_ctx, pkt);
	if (ret < 0) {
		printf("AVERROR(ENOMEM): %d\n", AVERROR(ENOMEM));
		exit(1);
	}

	while (ret >= 0) {
		ret = avcodec_receive_frame(codec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
		} else if (ret < 0) {
            fprintf(stderr, "failed to receive frame\n");
            exit(1);
        } 

		data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }
        for (i = 0; i < frame->nb_samples; i++)
			for (ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++)
                fwrite(frame->data[ch] + data_size*i, 1, data_size, out_file);
	}
}

int main(int argc, char* argv[]) {
	const char* in_filename, * out_filename;
	AVCodecContext* codec_ctx = NULL;
	const AVCodec* codec = NULL;
	AVFrame* frame = NULL;
	AVPacket* pkt = NULL;
	AVCodecParserContext* parser = NULL;
	FILE* in_file, * out_file;
	int ret, len, n_channels;
	uint8_t buf[AUDIO_INBUF_SIZE + AUDIO_REFILL_THRESH];
	uint8_t* data;
	size_t data_size;
	enum AVSampleFormat sfmt;
	const char *fmt;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(1);
	}

	in_filename = argv[1];
	out_filename = argv[2];

	//获取解码器
	codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
	if (!codec) {
		fprintf(stderr, "failed to find codec\n");
		exit(1);
	}

	//为AVCodecContext分配空间
	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		fprintf(stderr, "failed to alloc AVCodecContext\n");
		exit(1);
	}

	//使用指定的解码器codec初始化AVCodecContext
	ret = avcodec_open2(codec_ctx, codec, 0);
	if (ret < 0) {
		fprintf(stderr, "failed to init AVCodecContext\n");
		exit(1);
	}

	//分配packet的空间
	pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "failed to alloc packet\n");
		exit(1);
	}

	//获取指定解码的解析方式
	parser = av_parser_init(codec->id);
	if (!parser) {
		fprintf(stderr, "failed to init parser\n");
		exit(1);
	}

	//打开输入文件
	in_file = fopen(in_filename, "rb");
	if (!in_file) {
		fprintf(stderr, "failed to open input file\n");
		exit(1);
	}

	//打开输出文件
	out_file = fopen(out_filename, "wb");
	if (!out_file) {
		fprintf(stderr, "failed to open output file\n");
        exit(1);
	}

	//开辟的缓存空间
	data = buf;
	//从输入文件中读取20480字节的数据
	data_size = fread(buf, 1, AUDIO_INBUF_SIZE, in_file);

	//当从输入文件中还能读取到数据
	while (data_size > 0) {
		//首次为AVFrame分配空间
		if (!frame) {
			if (!(frame = av_frame_alloc())) {
				fprintf(stderr, "failed to alloc AVFrame\n");
				exit(1);
			}
		}

		//通过指定的解析方式从裸流data中分割数据，构建packet，返回值ret表示解析了裸流中的多少数据 ret = pkt->size
		//每次解析构建的packet实际上大小远小于从输入文件中读取的裸流大小--AUDIO_INBUF_SIZE 20480
		ret = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (ret < 0) {
			fprintf(stderr, "failed to parse parser");
			exit(1);
		}

		//剩余的缓存空间首地址
		data = data + ret;
		//剩余缓存空间的大小
		data_size = data_size - ret;

		//如果解析后构建packet有数据，则针对packet解码
		if (pkt->size) {
			decode(codec_ctx, pkt, frame, out_file);
		}

		//如果剩余的缓存空间大小小于预制的刷新的大小，则刷新缓存区
		if (data_size < AUDIO_REFILL_THRESH) {
			//
			memmove(buf, data, data_size);
			data = buf;
			len = fread(data + data_size, 1, AUDIO_INBUF_SIZE - data_size, in_file);
			if (len > 0) {
				data_size = data_size + len;
			}
		}
	}

	pkt->data = NULL;
	pkt->size = 0;
	decode(codec_ctx, pkt, frame, out_file);

    sfmt = codec_ctx->sample_fmt;
 
    if (av_sample_fmt_is_planar(sfmt)) {
        const char *packed = av_get_sample_fmt_name(sfmt);
        printf("Warning: the sample format the decoder produced is planar "
               "(%s). This example will output the first channel only.\n",
               packed ? packed : "?");
        sfmt = av_get_packed_sample_fmt(sfmt);
    }
 
    n_channels = codec_ctx->ch_layout.nb_channels;
    if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
        goto end;
 
    printf("Play the output audio file with the command:\n"
           "ffplay -f %s -ac %d -ar %d %s\n",
           fmt, n_channels, codec_ctx->sample_rate,
           out_filename);

end:
    fclose(out_file);
    fclose(in_file);

    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    av_frame_free(&frame);
    av_packet_free(&pkt);

	return 0;
}
