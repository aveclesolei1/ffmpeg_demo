#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavcodec/avcodec.h>

#define STREAM_BUFFER_SIZE 20480

void get_adts_header(char* adts_header_buf, const int frame_length) {
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
    int length = frame_length + 7;
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
    AVCodec* codec = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    AVCodecParserContext* parser_ctx = NULL;
    FILE* in_file, * out_file;
    char* in_filename, * out_filename, * data;
    size_t data_size;
    int ret, audio_stream_index, video_stream_index;

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
        fprintf(stderr, "Failed to find best stream: %s\n", av_err2str(ret));
        exit(1);
    }
    audio_stream_index = ret;

    if (!(codec = avcodec_find_decoder(AV_CODEC_ID_AAC))) {
        fprintf(stderr, "Failed to find AV_CODEC_ID_AAC decoder\n");
        exit(1);
    }

    if (!(codec_ctx = avcodec_alloc_context3(codec))) {
        fprintf(stderr, "Failed to alloc AVCodecContext\n");
        exit(1);
    }

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

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (audio_stream_index = pkt->stream_index) {
            if (pkt->size > 0) {
                char header_buffer[7] = {0};
                get_adts_header(header_buffer, pkt->size);
                fwrite(header_buffer, 1, 7, out_file);
                fwrite(pkt->data, 1, pkt->size, out_file);
            }
        }
        av_packet_unref(pkt);
    }

end:
    avformat_close_input(&ifmt_ctx);
    av_packet_free(&pkt);
    fclose(out_file);

    return 0;
}