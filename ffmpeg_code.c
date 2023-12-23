#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavcodec/avcodec.h>

#define STREAM_BUFFER_SIZE 20480
#define STREAM_REFRESH_SIZE 4096

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
		printf("Failed to send packet: %s\n", av_err2str(ret));
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
			for (ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
                fwrite(frame->data[ch] + data_size*i, 1, data_size, out_file);
            }
	}
}

void get_adts_header(char* adts_header_buf, const int frame_size) {
    //parse audio parameters
    //int sample_rate = codec_ctx->sample_rate;
    //int channels = codec_ctx->ch_layout.nb_channels;

    //synword 12bits
    adts_header_buf[0] = 0xff;
    adts_header_buf[1] = 0xf << 4;

    //id 1bit
    adts_header_buf[1] = adts_header_buf[1] | 0 << 3;

    //layer 2bits
    //adts_header_buf[1] = adts_header_buf[1] | 0 << 2;
    
    //protection_absent 1bit
    adts_header_buf[1] = adts_header_buf[1] | 1;
    
    //pro 2bits
    adts_header_buf[2] = 1 << 6;

    //sampling_frequency_index 4bits
    adts_header_buf[2] = adts_header_buf[2] | 3 << 2;

    //private_bit 1bit
    //adts_header_buf[2] = adts_header_buf[2] | 0;

    //channel_configuration 3bits
    //adts_header_buf[2] = adts_header_buf[2] | 0;
    adts_header_buf[3] = 1 << 6;

    //originality 1bit
    //adts_header_buf[3] = adts_header_buf[3] | 0;

    //home 1bit
    //adts_header_buf[3] = adts_header_buf[3] | 0;

    //copyright_id_bit 1bit
    //adts_header_buf[3] = adts_header_buf[3] | 0;

    //copyright_id_start 1bit
    //adts_header_buf[3] = adts_header_buf[3] | 0;

    //frame_length 13bits
    int length = frame_size + 7;
    adts_header_buf[3] = adts_header_buf[3] | length >> 11;
    adts_header_buf[4] = (length >> 3) & 0x1FF;
    adts_header_buf[5] = (length & 0x7) << 5;

    //adts_buffer_fullness 11bits
    adts_header_buf[5] = adts_header_buf[5] | 0x1f;
    adts_header_buf[6] = 0xfc;

    //number_of_raw_data_blocks_in_frame 2bits
    //adts_header_buf = adts_header_buf[5] | 0;
}

int main(int argc, char* argv[]) {
    AVFormatContext* ifmt_ctx = NULL, * ofmt_ctx = NULL;
    AVCodecContext* codec_ctx = NULL;
    const AVCodec* codec = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    AVCodecParserContext* parser_ctx = NULL;
    FILE* in_file, * out_file;
    char* in_filename, * out_filename, * data;
    char buf[STREAM_BUFFER_SIZE + STREAM_REFRESH_SIZE], header_buf[7];
    size_t data_size;
    int ret, audio_stream_index, video_stream_index;
    int n_channels;
    enum AVSampleFormat sfmt;
    const char *fmt;

    if (argc < 3) {
        fprintf(stderr, "Using the following command: %s <input> <output>\n", argv[0]);
        exit(1);
    }

    in_filename = argv[1];
    out_filename = argv[2];

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL) < 0)) {
        fprintf(stderr, "Failed to alloc output AVFormatContext: %s\n", av_err2str(ret));
        exit(1);
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    if (!(pkt = av_packet_alloc())) {
        fprintf(stderr, "Failed to alloc AVPacket\n");
        exit(1);
    }

    if ((ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, -1)) < 0) {
        fprintf(stderr, "Failed to find audio stream: %s\n", av_err2str(ret));
        exit(1);
    }
    audio_stream_index = ret;

    if ((ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, -1)) < 0) {
        fprintf(stderr, "Failed to find video stream: %s\n", av_err2str(ret));
        exit(1);
    }
    video_stream_index = ret;

    if (!(codec = avcodec_find_decoder(AV_CODEC_ID_AAC))) {
        fprintf(stderr, "Failed to find AV_CODEC_ID_AAC decoder\n");
        exit(1);
    }

    if (!(codec_ctx = avcodec_alloc_context3(codec))) {
        fprintf(stderr, "Failed to alloc AVCodecContext\n");
        exit(1);
    }

    AVStream* audio_stream = ifmt_ctx->streams[audio_stream_index];
    ret = avcodec_parameters_to_context(codec_ctx, audio_stream->codecpar);

    if ((ret = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
        fprintf(stderr, "Failed to use codec open\n");
        exit(1);
    }

    if (!(parser_ctx = av_parser_init(codec->id))) {
        fprintf(stderr, "Failed to init parser\n");
        exit(1);
    }

    if (!(in_file = fopen(in_filename, "rb"))) {
        fprintf(stderr, "Failed to open input file\n");
        exit(1);
    }

    if (!(out_file = fopen(out_filename, "wb"))) {
        fprintf(stderr, "Failed to open output file\n");
        exit(1);
    }

    if (!(frame = av_frame_alloc())) {
        fprintf(stderr, "failed to alloc AVFrame\n");
        exit(1);
    }
    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (audio_stream_index = pkt->stream_index) {
            if (pkt->size > 0) {
                decode(codec_ctx, pkt, frame, out_file);
            }
        }
    }
/*
    data = buf;
    data_size = 0;

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (audio_stream_index = pkt->stream_index) {
            if (pkt->size > 0) {
                //AVStream* audio_stream = ifmt_ctx->streams[audio_stream_index];
                //ret = avcodec_parameters_to_context(codec_ctx, audio_stream->codecpar);
                get_adts_header(header_buf, pkt->size);
                memcpy(data + data_size, header_buf, 7);
                data_size += 7;
                memcpy(data + data_size, pkt->data, (size_t)pkt->size);
                data_size += pkt->size;
            }
        }
        av_packet_unref(pkt);
        if (data_size > STREAM_BUFFER_SIZE) {
            while (data_size > 0) {
                ret = av_parser_parse2(parser_ctx, codec_ctx, &pkt->data, &pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                if (ret < 0) {
                    fprintf(stderr, "failed to parse parser\n");
                    exit(1);
                }

                data = data + ret;
                data_size = data_size - ret;

                //如果解析后构建packet有数据，则针对packet解码
                if (pkt->size) {
                    decode(codec_ctx, pkt, frame, out_file);
                }

                if (data_size < STREAM_REFRESH_SIZE) {
                    memmove(buf, data, data_size);
                    data = buf;
                    break;
                }
            }
        }
    }

    while (data_size > 0) {
        ret = av_parser_parse2(parser_ctx, codec_ctx, &pkt->data, &pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            fprintf(stderr, "failed to parse parser\n");
            exit(1);
        }

        data = data + ret;
        data_size = data_size - ret;

        //如果解析后构建packet有数据，则针对packet解码
        if (pkt->size) {
            decode(codec_ctx, pkt, frame, out_file);
        }
    }

    pkt->data = NULL;
	pkt->size = 0;
	decode(codec_ctx, pkt, frame, out_file);
*/
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
    av_parser_close(parser_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
