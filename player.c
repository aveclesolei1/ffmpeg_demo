#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>

#undef main
#define WINDOW_DEFAULT_WIDTH 1920
#define WINDOW_DEFAULT_HEIGHT 1080
//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
//Break
#define BREAK_EVENT  (SDL_USEREVENT + 2)

#define BUFFER_SIZE 4096000
#define BUFFER_REFRESH_SIZE 51200

static unsigned int pkt_num = 0;

typedef struct AVPacketNode {
    struct AVPacketNode *next;
    AVPacket *pkt;
} AVPacketNode;

typedef struct LinkedListAVPacketQueue {
    AVPacketNode *head;
    AVPacketNode *tail;
    AVPacketNode *node;
    size_t count;
    const size_t max_size;
} AVPacketQueue;

typedef struct ArrayAVFrameQueue {
    AVFrame *frame_array[30];
    unsigned int head_index;
    unsigned int tail_index;
    size_t count;
} AVFrameQueue;

typedef struct AudioAndVideoContext {
    char *file_name;
    double delay_mills;

    //audio parameters
    int audio_stream_index;
    int channels;
    int sample_rate;
    AVCodecContext *acodec_ctx;
    AVPacketQueue *audio_queue;
    Uint8* audio_buffer;
    Uint8* audio_position;
    size_t audio_buffer_len;

    //video parameters
    int video_stream_index;
    int width;
    int height;
    double frame_rate;
    AVCodecContext *vcodec_ctx;
    enum AVPixelFormat pix_fmt;
    uint8_t *video_dst_data[4];
    int video_dst_linesize[4];
    int video_dst_bufsize;
    AVFrameQueue *frame_queue;
    AVPacketQueue *video_queue;

    int quit;
} AudioVideoContext;

static int getCount(AVPacketQueue *queue) {
    return queue->count;
}

static AVPacketNode* peek_node(AVPacketQueue *queue) {
    if (!queue || queue->count == 0) {
        return NULL;
    }
    return queue->head;
}

static AVPacketNode* remove_node(AVPacketQueue *queue) {
    if (!queue || queue->count == 0) {
        return NULL;
    }
    AVPacketNode *top = queue->head;
    queue->head = top->next;
    queue->count--;
    return top;
}

//return: succeed >= 0 or failed < 0
int add_node(AVPacketQueue *queue, AVPacket *pkt) {
    if (!queue) {
        return -1;
    }
    AVPacketNode *node = (AVPacketNode *) malloc(sizeof(AVPacketNode));
    node->pkt = pkt;
    node->next = NULL;
    if (queue->count == 0) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->count++;
    return 0;
}

void decode_audio(AudioVideoContext* avctx) {
    AVFrame *frame;
    Uint8 *temp_buffer;
	int i, ch, ret, data_size, temp_buffer_len;

    //将未播放的音频数据从audio_buffer尾部移至头部
    memmove(avctx->audio_buffer, avctx->audio_position, avctx->audio_buffer_len);
    avctx->audio_position = avctx->audio_buffer;

    if (!(frame = av_frame_alloc())) {
        fprintf(stderr, "Failed to alloc AVFrame when decode audio\n");
        return;
    }

    temp_buffer = (Uint8 *) malloc(BUFFER_SIZE - BUFFER_REFRESH_SIZE);
    temp_buffer_len = 0;

    //解码BUFFER_SIZE - 2 * BUFFER_REFRESH_SIZE长度的音频数据放入temp_buffer_len中
    while (temp_buffer_len < BUFFER_SIZE - 2 * BUFFER_REFRESH_SIZE && avctx->audio_queue->count > 0) {
        //从音频packet队列中取出一个packet
        AVPacketNode *node = remove_node(avctx->audio_queue);

        ret = avcodec_send_packet(avctx->acodec_ctx, node->pkt);
        if (ret < 0) {
            printf("Failed to send packet: %s\n", av_err2str(ret));
            exit(1);
        }
        
        while (ret >= 0) {
            ret = avcodec_receive_frame(avctx->acodec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                fprintf(stderr, "failed to receive frame\n");
                exit(1);
            }
            data_size = av_get_bytes_per_sample(avctx->acodec_ctx->sample_fmt);
            if (data_size < 0) {
                /* This should not occur, checking just for paranoia */
                fprintf(stderr, "Failed to calculate data size\n");
                exit(1);
            }

            for (int i = 0; i < frame->nb_samples; i++) {
                for (int ch = 0; ch < avctx->acodec_ctx->ch_layout.nb_channels; ch++) {
                    memmove(temp_buffer + temp_buffer_len, frame->data[ch] + data_size*i, data_size);
                    temp_buffer_len += data_size;
                }
            }
        }

        av_packet_free(&node->pkt);
        free(node);
    }

    //将temp_buffer暂存区中的数据放入audio_buffer音频缓冲区
    if (temp_buffer) {
        if (temp_buffer_len <= 0) {
            fprintf(stderr, "the strlen(buffer) <= 0\n");
            return;
        }

        memmove(avctx->audio_buffer + avctx->audio_buffer_len, temp_buffer, temp_buffer_len);
        avctx->audio_buffer_len += temp_buffer_len;
    }

    av_frame_free(&frame);
    free(temp_buffer);
}

static void decode_video(AudioVideoContext *avctx, AVPacket *pkt)
{
    int ret;
    //fprintf(stderr, "the count is %d\n", avctx->frame_queue->count);
    if (avctx->frame_queue->count <= 25 /*&& avctx->video_queue->count > 1*/) {
        AVPacket *packet = pkt;//remove_node(avctx->video_queue)->pkt;
        ret = avcodec_send_packet(avctx->vcodec_ctx, packet);
        if (ret < 0) {
            fprintf(stderr, "Error sending a packet for decoding: %s\n", av_err2str(ret));
            exit(1);
        }
        
        while (ret >= 0) {
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(avctx->vcodec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                //av_frame_free(&frame);
                return;
            } else if (ret < 0) {
                fprintf(stderr, "Error during decoding\n");
                exit(1);
            }
            //fprintf(stderr, "the avctx->frame_queue->tail_index is %d\n", avctx->frame_queue->tail_index);
            avctx->frame_queue->frame_array[avctx->frame_queue->tail_index] = frame;
            avctx->frame_queue->count++;
            avctx->frame_queue->tail_index = (avctx->frame_queue->tail_index + 1) % 30;
        }
        
        //av_packet_free(&packet);
    }
    /*if (avctx->video_queue->count > 25) {
        //wait for other thread wake
        SDL_Delay(16.67);
    }*/
    //add_node(avctx->video_queue, pkt);
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

int demux_and_decode(void *argv) {
    AudioVideoContext *avctx;
    AVFormatContext *fmt_ctx = NULL;
    AVPacket *pkt = NULL;
    FILE* file;
    int ret;

    if (!argv) {
        fprintf(stderr, "demux_decode_thread's argv is NULL\n");
        return -1;
    }

    avctx = (AudioVideoContext *)argv;

    if ((ret = avformat_open_input(&fmt_ctx, avctx->file_name, NULL, NULL) < 0)) {
        fprintf(stderr, "Failed to alloc output AVFormatContext: %s\n", av_err2str(ret));
        exit(1);
    }

    av_dump_format(fmt_ctx, 0, avctx->file_name, 0);

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if ((avctx->audio_stream_index = init_codec_context(&avctx->acodec_ctx, fmt_ctx, AVMEDIA_TYPE_AUDIO)) < 0) {
        fprintf(stderr, "Failed to init %s decodec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    } else {
        avctx->channels = avctx->acodec_ctx->ch_layout.nb_channels;
        avctx->sample_rate = avctx->acodec_ctx->sample_rate;
    }

    if ((avctx->video_stream_index = init_codec_context(&avctx->vcodec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO)) < 0) {
        fprintf(stderr, "Failed to init %s decodec context\n", av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        goto end;
    } else {
        /* allocate image where the decoded image will be put */
        avctx->width = avctx->vcodec_ctx->width;
        avctx->height = avctx->vcodec_ctx->height;
        avctx->frame_rate = av_q2d(fmt_ctx->streams[avctx->video_stream_index]->avg_frame_rate);
        avctx->pix_fmt = avctx->vcodec_ctx->pix_fmt;
        ret = av_image_alloc(avctx->video_dst_data, avctx->video_dst_linesize,
                                avctx->width, avctx->height, avctx->pix_fmt, 1);
        if (ret < 0) {
            fprintf(stderr, "Failed to alloc raw video buffer\n");
            goto end;
        }
        avctx->video_dst_bufsize = ret;
    }

    if (!(file = fopen(avctx->file_name, "rb"))) {
        fprintf(stderr, "Failed to open input file\n");
        goto end;
    }
    if (!(pkt = av_packet_alloc())) {
        fprintf(stderr, "Failed to alloc AVPacket\n");
        goto end;
    }

    avctx->delay_mills = 1000 / avctx->frame_rate;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (avctx->audio_stream_index == pkt->stream_index) {
            if (pkt->size > 0) {
                while (avctx->audio_queue->count > 60) {
                    SDL_Delay(avctx->delay_mills);
                }
                add_node(avctx->audio_queue, pkt);
            }
        } else if (avctx->video_stream_index == pkt->stream_index) {
            if (pkt->size > 0) {
                while (avctx->frame_queue->count > 25) {
                    SDL_Delay(avctx->delay_mills);
                }
                decode_video(avctx, pkt);
            }
        }

        if (!(pkt = av_packet_alloc())) {
            fprintf(stderr, "Failed to alloc AVPacket\n");
            goto end;
        }
    }

end:
    av_packet_free(&pkt);
    if (file) {
        fclose(file);
    }
    avformat_free_context(fmt_ctx);

    return 0;
}

void read_audio_data(void* udata, Uint8* stream, int len) {
    AudioVideoContext *avctx = (AudioVideoContext *) udata;

	if (avctx->audio_buffer_len <= 0) {
		return;
	}

	SDL_memset(stream, 0, len);

	len = (len < avctx->audio_buffer_len) ? len : avctx->audio_buffer_len;
	SDL_MixAudio(stream, avctx->audio_position, len, SDL_MIX_MAXVOLUME);

	avctx->audio_position += len;
	avctx->audio_buffer_len -= len;
}

int refresh_audio_data(void *argv) {
    AudioVideoContext *avctx;
    if (!argv) {
        fprintf(stderr, "refresh_audio_thread's argv is NULL\n");
        return -1;
    }
    avctx = (AudioVideoContext *)argv;
    
    avctx->audio_buffer = (Uint8*) malloc(BUFFER_SIZE);
	if (!avctx->audio_buffer) {
		fprintf(stderr, "Failed to malloc buffer\n");
		return -1;
	}
    avctx->audio_buffer_len = 0;
    avctx->audio_position = avctx->audio_buffer;

	SDL_AudioSpec spec;
	spec.freq = avctx->sample_rate;
	spec.channels = avctx->channels;
	spec.format = AUDIO_F32LSB;
	spec.silence = 0;
	spec.callback = read_audio_data;
	spec.userdata = avctx;
	
	if (SDL_OpenAudio(&spec, NULL)) {
		fprintf(stderr, "Failed to open audio file\n");
		return -1;
	}

    // wait decoder thread
    while (avctx->audio_queue->count < 0) {
        SDL_Delay(1);
    }

	SDL_PauseAudio(0);

    do {
        //如果音频缓冲区的数据量大于BUFFER_REFRESH_SIZE，则持续等待声卡消费音频数据
        while (avctx->audio_buffer_len > BUFFER_REFRESH_SIZE) {
            SDL_Delay(avctx->delay_mills);
        }

        decode_audio(avctx);
        
    } while(avctx->quit != 1);

    //todo: the remaining audio data needs to be played

    if (avctx->audio_buffer) {
		free(avctx->audio_buffer);
	}

    return 0;
}

int refresh_video_data(void *argv) {
    AudioVideoContext *avctx;
    double delay_mills;
    if (!argv) {
        fprintf(stderr, "refresh_video_thread's argv is NULL\n");
        return -1;
    }
    avctx = (AudioVideoContext *)argv;
    avctx->quit = 0;

    while (avctx->quit == 0) {
        //每隔delay_mills毫秒推送一次刷新事件
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(avctx->delay_mills);
    }
    avctx->quit = 0;
    //break
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);//将结束事件推送出去
    return 0;
}

AudioVideoContext* alloc_audio_video_context() {
    AudioVideoContext *avctx;
    if (!(avctx = (AudioVideoContext *) malloc(sizeof(AudioVideoContext)))) {
        fprintf(stderr, "Failed to malloc avctx\n");
        return NULL;
    }

    avctx->audio_queue = (AVPacketQueue *) malloc(sizeof(AVPacketQueue));
    avctx->audio_queue->count = 0;
    avctx->video_queue = (AVPacketQueue *) malloc(sizeof(AVPacketQueue));
    avctx->video_queue->count = 0;

    avctx->frame_queue = (AVFrameQueue *) malloc(sizeof(AVFrameQueue));
    avctx->frame_queue->count=0;
    avctx->frame_queue->head_index = 0;
    avctx->frame_queue->tail_index = 0;

    avctx->video_dst_data[0] = (uint8_t *) malloc(sizeof(uint8_t *) * 4);

    return avctx;
}

void free_audio_video_context(AudioVideoContext* avctx) {
    if (!avctx) {
        return;
    }

    free(avctx->audio_queue);
    free(avctx->video_queue);

    free(avctx->video_dst_data[0]);
    //free(avctx->video_dst_linesize[0]);

    free(avctx);
}

int main(int argc, char *argv[]) {
    AudioVideoContext *avctx;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect rect;
    SDL_Event event;
    SDL_Thread *demux_decode_thread, *refresh_video_thread, *refresh_audio_thread;
    char *buffer;
    int ret;

    /*
    if (argc != 2) {
        fprintf(stderr, "please use the following command: %s <input file name>\n", argv[0]);
        return -1;
    }
    */

    if ((ret = SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO)) < 0) {
        fprintf(stderr, "Failed to init SDL2: %s\n", SDL_GetError());
        return ret;
    }

    if (!(window = SDL_CreateWindow(argv[0], 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
        WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT, 
        SDL_WINDOW_RESIZABLE))) {
        fprintf(stderr, "Failed to create SDL window\n");
        goto end;
    }

    if (!(renderer = SDL_CreateRenderer(window, -1, 0))) {
        fprintf(stderr, "Failed to create renderer\n");
        goto end;
    }

    //videos in yuv420p format need to use SDL_PIXELFORMAT_IYUV as the texture format
    if (!(texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, 
        SDL_TEXTUREACCESS_STREAMING, WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT))) {
        fprintf(stderr, "Failed to create texture\n");
        goto end;
    }

    if (!(avctx = alloc_audio_video_context())) {
        fprintf(stderr, "Failed to alloc audio video context\n");
        goto end;
    }
    avctx->file_name = "D:\\Server\\c_code\\build\\source.mp4";//argv[1];
    SDL_Delay(100);

    if (!(demux_decode_thread = SDL_CreateThread(demux_and_decode, "demux_decode_thread", avctx))) {
        fprintf(stderr, "Failed to create demux_decode_thread: %s\n", SDL_GetError());
        goto end;
    }
    //
    SDL_Delay(500);
    
    if (!(refresh_video_thread = SDL_CreateThread(refresh_video_data, "refresh_video_thread", avctx))) {
        fprintf(stderr, "Failed to create refresh_video_thread: %s\n", SDL_GetError());
        goto end;
    }

    if (!(refresh_audio_thread = SDL_CreateThread(refresh_audio_data, "refresh_audio_thread", avctx))) {
        fprintf(stderr, "Failed to create refresh_audio_thread: %s\n", SDL_GetError());
        goto end;
    }

    buffer = (char *) malloc(avctx->width * avctx->height * 3 / 2);

    while (1) {
        //等待SDL事件进入
        SDL_WaitEvent(&event);
        //收到刷新事件对页面进行刷新
        if (event.type == REFRESH_EVENT) {
            //这里是读取一帧视频真，数据格式是YUV420P，像素排列是4:2:0，一行像素=width*height+width*1/4+height*1/4 = width*height*3/2
            //所以下面这句话刚好就是读取了一个视频帧YUV的数据长度
            //fprintf(stderr, "the size is %d\n", avctx->frame_queue->count);
            if (avctx->frame_queue->count > 0) {
                AVFrame *frame = avctx->frame_queue->frame_array[avctx->frame_queue->head_index];

                av_image_copy(avctx->video_dst_data, avctx->video_dst_linesize,
                    (const uint8_t **)(frame->data), frame->linesize,
                    avctx->pix_fmt, avctx->width, avctx->height);

                memcpy(buffer, avctx->video_dst_data[0], avctx->video_dst_bufsize);
                avctx->frame_queue->count--;
                avctx->frame_queue->head_index  = (avctx->frame_queue->head_index + 1) % 30;
                av_frame_free(&frame);

                SDL_UpdateTexture(texture, NULL, buffer, avctx->width);
                rect.x = 0;
                rect.y = 0;
                rect.w = WINDOW_DEFAULT_WIDTH;
                rect.h = WINDOW_DEFAULT_HEIGHT;//把视频就显示到这个区域

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, &rect);
                SDL_RenderPresent(renderer);
            }
            
        } else if (event.type == SDL_QUIT) {//点击右上角的叉号退出线程
            avctx->quit = 1;
        } else if (event.type == BREAK_EVENT) {//退出标志
            break;
        }
    }

end:

    free_audio_video_context(avctx);

    if (texture) {
        SDL_DestroyTexture(texture);
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }

    if (window) {
        SDL_DestroyWindow(window);
    }

    SDL_Quit();

    return 0;
}
