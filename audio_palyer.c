#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#define BUFFER_SIZE 4096000
#define BUFFER_REFRESH_SIZE 3072000//51200
#undef main

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

int main(int argc, char* argv[]) {
	char* file_name;
	FILE* file = NULL;

	if (argc < 2) {
		fprintf(stderr, "Using the following command: %s <audio name>\n", argv[0]);
		return -1;
	}

	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Failed to init SDL\n");
		return -1;
	}
	
	file_name = argv[1];
	if (!(file = fopen(file_name, "rb"))) {
		fprintf(stderr, "Failed to open input file\n");
		goto end;
	}
	
	audio_buffer = (Uint8*) malloc(BUFFER_SIZE);
	if (!audio_buffer) {
		fprintf(stderr, "Failed to malloc buffer\n");
		goto end;
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
		goto end;
	}

	SDL_PauseAudio(0);
	
	/*
	while(1) {
	size_t read_length = fread(audio_buffer, 1, BUFFER_SIZE, file);
		audio_position = audio_buffer;
		audio_buffer_len = read_length;
		if (read_length <= 0) {
			printf("break\n");
			break;
		}

		while (audio_position < (audio_buffer + read_length)) {
			SDL_Delay(1);
		}
	}
	*/
	size_t read_length = fread(audio_buffer, 1, BUFFER_SIZE, file);
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
		read_length = fread(audio_buffer + audio_buffer_len, 1, BUFFER_SIZE - audio_buffer_len, file);
		if (read_length > 0) {
			audio_buffer_len += read_length;
		}
	}

	SDL_CloseAudio();

end:
	if (audio_buffer) {
		free(audio_buffer);
	}

	if (file) {
		fclose(file);
	}
	SDL_Quit();
	return 0;
}
