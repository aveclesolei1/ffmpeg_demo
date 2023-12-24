#include <stdio.h>
#include <SDL.h>
#undef main

int main(int argc, char* argv[]) {
	SDL_Window* window = NULL;
	SDL_Renderer* render = NULL;
	SDL_Event event;
	int quit = 1, ret;

	ret = SDL_Init(SDL_INIT_VIDEO);
	SDL_Log("Hello World! the ret is %d\n", ret);
	if (ret < 0) {
		fprintf(stderr, "the error is %s\n", SDL_GetError());
	}

	window = SDL_CreateWindow("SDL2 Window",
					200,
					400,
					640,
					480,
					SDL_WINDOW_SHOWN);

	if (!window) {
		fprintf(stderr, "Failed to create window\n");
		goto end;
	}

	render = SDL_CreateRenderer(window, -1, 0);
	if (!render) {
		fprintf(stderr, "Failed to create renderer\n");
		goto end;
	}
	ret = SDL_SetRenderDrawColor(render, 255, 0, 0, 255);
	SDL_Log("the SDL_SetRenderDrawColor ret is %d", ret);


	ret = SDL_RenderClear(render);
	SDL_Log("the SDL_SetRenderDrawColor ret is %d", ret);
	
	SDL_RenderPresent(render);
	
	do {
		SDL_WaitEvent(&event);
		switch(event.type) {
			case SDL_QUIT:
				SDL_Log("quit SDL2 Window\n");
				quit = 0;
				break;
			default:
				SDL_Log("event type is %d\n", event.type);
		}
	} while(quit);

end:	
	if (window) {
		SDL_DestroyWindow(window);
	}

	/*
	if (render) {
		SDL_DestroyRenderer(render);
	}
	*/

	SDL_Quit();
	return 0;
}
