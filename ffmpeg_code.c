#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

#define STREAM_BUFFER_SIZE 20480
#define STREAM_REFRESH_SIZE 4096

static int video_frame_count;
static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_bufsize;
static int width, height;
static enum AVPixelFormat pix_fmt;

typedef struct Audio_Parameters {
    int channels;
    int sample_rate;
} Audio_Para;

typedef struct Video_Parameters {
    int width;
    int height;
    double frame_rate;
} Video_Para;

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

void decode_audio(AVCodecContext* codec_ctx, AVPacket* pkt, AVFrame* frame, FILE* out_file) {
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

static void decode_video(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame, FILE *out_file)
{
    int ret;
 
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }
 
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
 
        printf("saving frame %3"PRId64"\n", dec_ctx->frame_num);
        fflush(stdout);
 
        /* the picture is allocated by the decoder. no need to
           free it */
        //snprintf(buf, sizeof(buf), "%s-%"PRId64, filename, dec_ctx->frame_num);
        if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
            /* To handle this change, one could call av_image_alloc again and
            * decode the following frames into another rawvideo file. */
            fprintf(stderr, "Error: Width, height and pixel format have to be "
                    "constant in a rawvideo file, but the width, height or "
                    "pixel format of the input video changed:\n"
                    "old: width = %d, height = %d, format = %s\n"
                    "new: width = %d, height = %d, format = %s\n",
                    width, height, av_get_pix_fmt_name(pix_fmt),
                    frame->width, frame->height,
                    av_get_pix_fmt_name(frame->format));
            return;
        }
    
        printf("video_frame n:%d\n", video_frame_count++);
    
        /* copy decoded frame to destination buffer:
        * this is required since rawvideo expects non aligned data */
        av_image_copy(video_dst_data, video_dst_linesize,
                    (const uint8_t **)(frame->data), frame->linesize,
                    pix_fmt, width, height);
        /* write to rawvideo file */
        //fwrite时只需要video_dst_data[0]是因为，video_dst_data是一个指针数组，
        //在读取完video_dst_data[0]后会继续读取video_dst_data[1] video_dst_data[2] video_dst_data[3]
        fwrite(video_dst_data[0], 1, video_dst_bufsize, out_file);
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

/*
** return: Returns the stream index of the lookup type
*/
int init_codec_context(AVCodecContext **codec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
    const AVStream *stream;
    const AVCodec *decodec;
    int ret = -1, stream_index = -1;
    if ((ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, -1)) < 0) {
        fprintf(stderr, "Failed to find %s stream: %s\n", av_get_media_type_string(type), av_err2str(ret));
        return ret;
    }
    stream_index = ret;
    stream = fmt_ctx->streams[stream_index];
    if (!(decodec = avcodec_find_decoder(stream->codecpar->codec_id))) {
        fprintf(stderr, "Failed to find %s decoder\n", av_get_media_type_string(type));
        return ret;
    }
    if (!(*codec_ctx = avcodec_alloc_context3(decodec))) {
        fprintf(stderr, "Failed to alloc %s AVCodecContext\n", av_get_media_type_string(type));
        return ret;
    }
    if ((ret = avcodec_parameters_to_context(*codec_ctx, stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to get %s parameters from input AVFormatContext\n", av_get_media_type_string(type));
        return ret;
    }
    if ((ret = avcodec_open2(*codec_ctx, decodec, NULL)) < 0) {
        fprintf(stderr, "Failed to use %s decodec open\n", av_get_media_type_string(type));
        return ret;
    }
    return stream_index;
}

int main(int argc, char* argv[]) {
    AVFormatContext* ifmt_ctx = NULL, * afmt_ctx = NULL, * vfmt_ctx = NULL;
    AVCodecContext* acodec_ctx = NULL, * vcodec_ctx = NULL;
    const AVCodec* acodec = NULL, * vcodec = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    FILE* in_file, * audio_out_file, * video_out_file;
    char* in_filename, * audio_out_filename, * video_out_filename, * data;
    Video_Para *video_para = NULL;
    Audio_Para *audio_para = NULL;
    char buf[STREAM_BUFFER_SIZE + STREAM_REFRESH_SIZE], header_buf[7];
    size_t data_size;
    int ret, audio_stream_index, video_stream_index;
    int n_channels;
    enum AVSampleFormat sfmt;
    const char *fmt;

    if (argc < 4) {
        fprintf(stderr, "Using the following command: %s <input> <audio_filename> <video_filename>\n", argv[0]);
        exit(1);
    }

    in_filename = argv[1];
    audio_out_filename = argv[2];
    video_out_filename = argv[3];

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL) < 0)) {
        fprintf(stderr, "Failed to alloc output AVFormatContext: %s\n", av_err2str(ret));
        exit(1);
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    /* retrieve stream information */
    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    /*
    if ((ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, -1)) < 0) {
        fprintf(stderr, "Failed to find audio stream: %s\n", av_err2str(ret));
        exit(1);
    }
    audio_stream_index = ret;
    stream = ifmt_ctx->streams[audio_stream_index];
    if (!(acodec = avcodec_find_decoder(stream->codecpar->codec_id))) {
        fprintf(stderr, "Failed to find audio decoder\n");
        exit(1);
    }
    if (!(acodec_ctx = avcodec_alloc_context3(acodec))) {
        fprintf(stderr, "Failed to alloc audio AVCodecContext\n");
        exit(1);
    }
    if ((ret = avcodec_parameters_to_context(acodec_ctx, stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to get audio parameters input AVFormatContext\n");
        exit(1);
    }
    if ((ret = avcodec_open2(acodec_ctx, acodec, NULL)) < 0) {
        fprintf(stderr, "Failed to use audio codec open\n");
        exit(1);
    }
    */

    if ((audio_stream_index = init_codec_context(&acodec_ctx, ifmt_ctx, AVMEDIA_TYPE_AUDIO)) < 0) {
        fprintf(stderr, "Failed to init %s decodec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    } else {
        audio_para = (Audio_Para *) malloc(sizeof(Audio_Para));
        audio_para->channels = acodec_ctx->ch_layout.nb_channels;
        audio_para->sample_rate = acodec_ctx->sample_rate;
    }

    if ((video_stream_index = init_codec_context(&vcodec_ctx, ifmt_ctx, AVMEDIA_TYPE_VIDEO)) < 0) {
        fprintf(stderr, "Failed to init %s decodec context\n", av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        goto end;
    } else {
        /* allocate image where the decoded image will be put */
        video_para = (Video_Para *) malloc(sizeof(Video_Para));
        video_para->width = vcodec_ctx->width;
        video_para->height = vcodec_ctx->height;
        video_para->frame_rate = av_q2d(ifmt_ctx->streams[video_stream_index]->avg_frame_rate);
        width = vcodec_ctx->width;
        height = vcodec_ctx->height;
        pix_fmt = vcodec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                                width, height, pix_fmt, 1);
        if (ret < 0) {
            fprintf(stderr, "Failed to alloc raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }
    

    /*
    if ((ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, -1)) < 0) {
        fprintf(stderr, "Failed to find video stream: %s\n", av_err2str(ret));
        exit(1);
    }
    video_stream_index = ret;
    stream = ifmt_ctx->streams[video_stream_index];
    if (!(vcodec = avcodec_find_decoder(stream->codecpar->codec_id))) {
        fprintf(stderr, "Failed to find audio decoder\n");
        exit(1);
    }
    if (!(vcodec_ctx = avcodec_alloc_context3(vcodec))) {
        fprintf(stderr, "Failed to alloc video AVCodecContext\n");
        exit(1);
    }
    if ((ret = avcodec_parameters_to_context(vcodec_ctx, stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to get video parameters input AVFormatContext\n");
        exit(1);
    }
    if ((ret = avcodec_open2(vcodec_ctx, vcodec, NULL)) < 0) {
        fprintf(stderr, "Failed to use audio codec open\n");
        exit(1);
    }
    */

    if (!(in_file = fopen(in_filename, "rb"))) {
        fprintf(stderr, "Failed to open input file\n");
        exit(1);
    }

    if (!(audio_out_file = fopen(audio_out_filename, "wb"))) {
        fprintf(stderr, "Failed to open audio output file\n");
        exit(1);
    }

    if (!(video_out_file = fopen(video_out_filename, "wb"))) {
        fprintf(stderr, "Failed to open video output file\n");
        exit(1);
    }

    if (!(pkt = av_packet_alloc())) {
        fprintf(stderr, "Failed to alloc AVPacket\n");
        exit(1);
    }

    if (!(frame = av_frame_alloc())) {
        fprintf(stderr, "Failed to alloc AVFrame\n");
        exit(1);
    }

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (audio_stream_index == pkt->stream_index) {
            if (pkt->size > 0) {
                decode_audio(acodec_ctx, pkt, frame, audio_out_file);
            }
        } else if (video_stream_index == pkt->stream_index) {
            if (pkt->size > 0) {
                decode_video(vcodec_ctx, pkt, frame, video_out_file);
            }
        }
    }

    /* flush the decoders */
    if (acodec_ctx)
        decode_audio(acodec_ctx, pkt, frame, audio_out_file);
    if (vcodec_ctx)
        decode_video(vcodec_ctx, pkt, frame, video_out_file);

    
    printf("Play the output video file with the command:\n"
            "ffplay -f rawvideo -framerate %.0f -video_size %dx%d %s\n",
            video_para->frame_rate, video_para->width, video_para->height,
            video_out_filename);
    

    sfmt = acodec_ctx->sample_fmt;
 
    if (av_sample_fmt_is_planar(sfmt)) {
        const char *packed = av_get_sample_fmt_name(sfmt);
        printf("Warning: the sample format the decoder produced is planar "
               "(%s). This example will output the first channel only.\n",
               packed ? packed : "?");
        sfmt = av_get_packed_sample_fmt(sfmt);
    }
 
    n_channels = acodec_ctx->ch_layout.nb_channels;
    if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
        goto end;
 
    printf("Play the output audio file with the command:\n"
           "ffplay -f %s -ac %d -ar %d %s\n",
           fmt, audio_para->channels, audio_para->sample_rate,
           audio_out_filename);

end:
    if (video_out_file)
        fclose(video_out_file);
    if (audio_out_file)
        fclose(audio_out_file);
    if (in_file)
        fclose(in_file);

    avcodec_free_context(&vcodec_ctx);
    avcodec_free_context(&acodec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_free_context(ifmt_ctx);

    av_free(video_dst_data[0]);

    return 0;
}
