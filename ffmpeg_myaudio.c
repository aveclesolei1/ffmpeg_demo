#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavcodec/packet.h>

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
    adts_header_buf[3] = 1 << 7;

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
    AVFormatContext* fmt_ctx = NULL;
    AVPacket* pkt = NULL;
    FILE* file = NULL;
    char* in_file = NULL;
    char* out_file = NULL;
    int ret, len;

    av_log_set_level(AV_LOG_INFO);
    
    if (argc < 3) {
	av_log(NULL, AV_LOG_ERROR, "parameters are less than three");
    	goto release;
    }

    in_file = argv[1];
    out_file = argv[2];
    ret = avformat_open_input(&fmt_ctx, in_file, NULL, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "open file failed: %s\n", av_err2str(ret));
        goto release;
    }

    av_dump_format(fmt_ctx, 0, in_file, 0);
    file = fopen(out_file, "wb");
    if (!file) {
    	av_log(NULL, AV_LOG_ERROR, "open %s failed!\n", out_file);
	goto release;
    }

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, -1);
    if (ret < 0) {
	av_log(NULL, AV_LOG_ERROR, "find audio stream failed%s\n", av_err2str(ret));
    }
    pkt = av_packet_alloc();
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
    	if (pkt->stream_index == ret) {
    	    char adts_header_buf[7] = {0};
            get_adts_header(adts_header_buf, pkt->size);
	    fwrite(adts_header_buf, 1, 7, file);
	    len = fwrite(pkt->data, 1, pkt->size, file);  
	    if (len < pkt->size) {
		av_log(NULL, AV_LOG_WARNING, "Waring: Data not fully written!\n");
	    }
	}
	av_packet_unref(pkt);
    }

release:
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }
    if (pkt) {
	av_packet_free(&pkt);
    }
    if (file) {
        fclose(file);
    }
    
    return 0;
}
