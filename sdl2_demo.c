#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#undef main
#define BUFFER_SIZE 4096000
#define BUFFER_REFRESH_SIZE 51200

//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)

//Break
#define BREAK_EVENT  (SDL_USEREVENT + 2)
int thread_exit = 0;
static Uint8* audio_buffer = NULL;
static Uint8* audio_position = NULL;
static size_t audio_buffer_len = 0;

void read_audio_data(void* udata, Uint8* stream, int len) {
	if (audio_buffer_len == 0) {
		return;
	}

	SDL_memset(stream, 0, len);

	len = (len < audio_buffer_len) ? len : audio_buffer_len;
	SDL_MixAudio(stream, audio_position, len, SDL_MIX_MAXVOLUME);

	audio_position += len;
	audio_buffer_len -= len;
}

int refresh_video(void* args) {
    thread_exit = 0;
    while (thread_exit == 0) {
        //每隔40毫秒推送一次刷新事件
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }
    thread_exit = 0;
    //break
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);//将结束事件推送出去
    return 0;
}

int refresh_audio(void* args) {
    FILE* audio_file = NULL;
    audio_file = fopen("D:\\Study\\c_code\\build\\audio.pcm", "rb+");
    if (!audio_file) {
        fprintf(stderr, "Failed to open audio file\n");
        return -1;
    }
    audio_buffer = (Uint8*) malloc(BUFFER_SIZE);
	if (!audio_buffer) {
		fprintf(stderr, "Failed to malloc buffer\n");
		return -1;
	}

	SDL_AudioSpec spec;
	spec.freq = 44100;
	spec.channels = 2;
	spec.format = AUDIO_F32LSB;
	spec.silence = 0;
	spec.callback = read_audio_data;
	spec.userdata = NULL;
	
	if (SDL_OpenAudio(&spec, NULL)) {
		fprintf(stderr, "Failed to open audio file\n");
		return -1;
	}

	SDL_PauseAudio(0);
    
    size_t read_length = fread(audio_buffer, 1, BUFFER_SIZE, audio_file);
	audio_position = audio_buffer;
	audio_buffer_len = read_length;

    while(1) {
        if (read_length <= 0) {
                printf("there are audio_buffer_len: %zu\n", audio_buffer_len);
                break;
            }

            while (audio_buffer_len > BUFFER_REFRESH_SIZE) {
                SDL_Delay(1);
            }

            memmove(audio_buffer, audio_position, audio_buffer_len);
            audio_position = audio_buffer;
            read_length = fread(audio_buffer + audio_buffer_len, 1, BUFFER_SIZE - audio_buffer_len, audio_file);
            if (read_length > 0) {
                audio_buffer_len += read_length;
            }
    }
    if (audio_file) {
        fclose(audio_file);
    }
    if (audio_buffer) {
		free(audio_buffer);
	}
    return 0;
}

int main(int argc, char* argv[]) {

    //window窗体的宽高
    int window_w = 576, window_h = 432;
    //像素的宽高
    const int pixel_w = 576, pixel_h = 432;
    //设置一帧像素的容量
    unsigned char buffer[pixel_w * pixel_h * 3/2];

    if (SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "初始化SDL失败, %s", SDL_GetError());
        return -1;
    }

    SDL_Window* window;
    window = SDL_CreateWindow("Simplest Video Play SDL2",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        window_w,
        window_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!window) {
        fprintf(stderr, "SDL: could not create window, %s", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);

    Uint32 pixFormat = 0;
    pixFormat = SDL_PIXELFORMAT_IYUV;

    SDL_Texture* texture = SDL_CreateTexture(renderer, pixFormat, SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

    

    FILE* video_file = NULL;
    video_file = fopen("D:\\Study\\c_code\\build\\video.yuv", "rb+");
    if (!video_file) {
        fprintf(stderr, "Failed to open video file\n");
        goto end;
    }

    //这个区域会存放显示的视频
    SDL_Rect sdlRect;

    //初始化SDL事件
    SDL_Event event;
    //创建一个SDL线程,SDL_CreateThread最后一个参数传递的参数
    SDL_Thread* video_thread = SDL_CreateThread(refresh_video, NULL, NULL);

    SDL_Thread* audio_thread = SDL_CreateThread(refresh_audio, NULL, NULL);

    while (1) {
        //等待SDL事件进入
        SDL_WaitEvent(&event);
        //收到刷新事件对页面进行刷新
        if (event.type == REFRESH_EVENT) {
            //这里是读取一帧视频真，数据格式是YUV420P，像素排列是4:2:0，一行像素=width*height+width*1/4+height*1/4 = width*height*3/2
        //所以下面这句话刚好就是读取了一个视频帧YUV的数据长度
            if (fread(buffer, 1, pixel_w * pixel_h * 3/2, video_file) != pixel_w * pixel_h * 3/2) {
                // Loop
                fseek(video_file, 0, SEEK_SET);
                fread(buffer, 1, pixel_w * pixel_h * 3/2, video_file);//读取一帧数据的容量并放入buffer中
            }

            SDL_UpdateTexture(texture, NULL, buffer, pixel_w);

            sdlRect.x = 0;
            sdlRect.y = 0;
            sdlRect.w = window_w;
            sdlRect.h = window_h;//把视频就显示到这个区域

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, &sdlRect);
            SDL_RenderPresent(renderer);
        }
        else if (event.type == SDL_WINDOWEVENT) {
            //resize
            SDL_GetWindowSize(window, &window_w, &window_h);
        }
        else if (event.type == SDL_QUIT) {//点击右上角的叉号退出线程
            thread_exit = 1;
        }
        else if (event.type == BREAK_EVENT) {//退出标志
            break;
        }
    }

    SDL_CloseAudio();
end:
	if (video_file) {
		fclose(video_file);
	}
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

}
