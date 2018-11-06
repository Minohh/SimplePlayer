#include <SDL2/SDL.h>

#define DEF_WIDTH 1920
#define DEF_HEIGHT 1080
//#define DEF_WIDTH 1280
//#define DEF_HEIGHT 720
#define FRAMERATE 24

int main(int argc, char *argv[]){
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect rect;
    unsigned char *YPlane=NULL, *UPlane=NULL, *VPlane=NULL;
    FILE *pFile;
    int sizeY=0, sizeU=0, sizeV=0;

    if(argc != 2){
        fprintf(stderr, "Usage: %s Video.yuv\n", argv[0]);
        return -1;
    }

    if(SDL_Init(SDL_INIT_VIDEO)){
        fprintf(stderr, "SDL init video failed\n");
        return -1;
    }

    window = SDL_CreateWindow("Simple Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, DEF_WIDTH, DEF_HEIGHT, 0);
    if(!window){
        fprintf(stderr, "SDL create window failed\n");
        return -1;
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!renderer){
        fprintf(stderr, "SDL create renderer failed\n");
        return -1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, DEF_WIDTH, DEF_HEIGHT);
    if(!renderer){
        fprintf(stderr, "SDL create renderer failed\n");
        return -1;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    YPlane = (unsigned char *)malloc(DEF_WIDTH*DEF_HEIGHT);
    UPlane = (unsigned char *)malloc(DEF_WIDTH*DEF_HEIGHT/4);
    VPlane = (unsigned char *)malloc(DEF_WIDTH*DEF_HEIGHT/4);

    pFile = fopen(argv[1], "rb");
    if(pFile==NULL)
        return -1;

    while(1){
        sizeY = fread(YPlane, 1, DEF_WIDTH*DEF_HEIGHT, pFile);
        sizeU = fread(UPlane, 1, DEF_WIDTH*DEF_HEIGHT/4, pFile);
        sizeV = fread(VPlane, 1, DEF_WIDTH*DEF_HEIGHT/4, pFile);
        if(!sizeY||!sizeU||!sizeV)
            break;
        if(0!=SDL_UpdateYUVTexture(texture, NULL, YPlane, DEF_WIDTH, UPlane, DEF_WIDTH/2, VPlane, DEF_WIDTH/2)){
            fprintf(stdout, "Render Update Texture failed, reason: %s\n", SDL_GetError());
        }
        SDL_RenderCopyEx(renderer, texture, NULL, NULL, 0, NULL, 0);
        SDL_RenderPresent(renderer);
        SDL_Delay(1000/FRAMERATE);

        SDL_PumpEvents();
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    free(YPlane);
    free(UPlane);
    free(VPlane);
    fclose(pFile);
    return 0;
}
