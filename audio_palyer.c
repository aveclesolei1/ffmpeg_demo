#include <stdio.h>
#include <SDL.h>
#define BUFFER_SIZE 4096000
static Uint8* buf = NULL;
static size_t len = 0;
static Uint8* pos = NULL;
#undef main

void read_audio_data(void* udata, Uint8* stream, int l) {
	if (len == 0) {
		return;
	}

	SDL_memset(stream, 0, l);

	l = (l < len) ? l : len;
	SDL_MixAudio(stream, pos, l, SDL_MIX_MAXVOLUME);

	pos += l;
	len -= l;
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
	if (!(file = fopen(file_name, "rb+"))) {
		fprintf(stderr, "Failed to open input file\n");
		goto end;
	}
	
	buf = (Uint8*) malloc(BUFFER_SIZE);
	if (!buf) {
		fprintf(stderr, "Failed to malloc buffer\n");
		goto end;
	}

	SDL_AudioSpec spec;
	spec.freq = 48000;
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

	do {
		len = fread(buf, 1, BUFFER_SIZE, file);
		pos = buf;
		while (pos < (buf + len)) {
			SDL_Delay(1);
		}
	} while(1);

	SDL_CloseAudio();
end:

	if (buf) {
		free(buf);
	}

	if (file) {
		fclose(file);
	}
	SDL_Quit();
	return 0;
}
