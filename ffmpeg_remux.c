#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
 
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
 
    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
 
int main(int argc, char **argv)
{
    const AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket *pkt = NULL;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
 
    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }
 
    in_filename  = argv[1];
    out_filename = argv[2];
 
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        return 1;
    }

    //获取输入文件流的上下文结构体
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }
 
    //获取输入文件流的信息
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }
 
    //打印输入文件的相关信息
    av_dump_format(ifmt_ctx, 0, in_filename, 0);
 
    //获取输出文件流的上下文结构体
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
 
    //获取输入文件流的数量
    stream_mapping_size = ifmt_ctx->nb_streams;
    //创建一个输入文件流大小的int型数组，stream_mapping数组头
    stream_mapping = av_calloc(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
 
    //获取输出文件的格式结构体
    ofmt = ofmt_ctx->oformat;
 
    //遍历输入文件中的所有流
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
	//获取与当前流关联的编解码器参数
        AVCodecParameters *in_codecpar = in_stream->codecpar;
 
	//不是音频、视频和字幕的流，并将当前流标记为-1
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }
 
	//否则以递增的形式标记当前流
        stream_mapping[i] = stream_index++;
 
	//avformat_new_stream函数用于创建一个新的流（AVStream）并将其添加到输出格式上下文（AVFormatContext）中。这个函数通常用于在进行媒体文件重封装（remuxing）时，为输出文件添加新的音频、视频或字幕流。
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
 
	//复制输入流的编解码器参数给输出流
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
		//不加会出现下述报错: [flv @ 0x5630cca49500] Tag avc1 incompatible with output codec id '27' ([7][0][0][0])
        out_stream->codecpar->codec_tag = 0;
    }
    //打印输出文件的相关信息
    av_dump_format(ofmt_ctx, 0, out_filename, 1);
 
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }
 
    //为输出文件写入头信息
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }
 
    while (1) {
        AVStream *in_stream, *out_stream;
 
		//读取下一帧数据流
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0)
            break;
 
        in_stream  = ifmt_ctx->streams[pkt->stream_index];
        if (pkt->stream_index >= stream_mapping_size ||
            stream_mapping[pkt->stream_index] < 0) {
            av_packet_unref(pkt);
            continue;
        }
 
        pkt->stream_index = stream_mapping[pkt->stream_index];
        out_stream = ofmt_ctx->streams[pkt->stream_index];
        log_packet(ifmt_ctx, pkt, "in");
 
        /* copy packet */
        av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
        pkt->pos = -1;
        log_packet(ofmt_ctx, pkt, "out");
 
        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
    }
 
    av_write_trailer(ofmt_ctx);
end:
    av_packet_free(&pkt);
 
    avformat_close_input(&ifmt_ctx);
 
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
 
    av_freep(&stream_mapping);
 
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
 
    return 0;
}
