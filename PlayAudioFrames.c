#include <SDL2/SDL.h>

#define DEF_CHANNELS 2
#define DEF_SAMPLERATE 48000
#define DEF_SAMPLES 4096

int fileItr = 0;
FILE* pFile=NULL;
int print_once = 1;
uint32_t cur_tick, last_tick;

void SimpleCallback(void* userdata, Uint8 *stream, int queryLen){
    unsigned char *buf, *itr;
    int readsize = 0, len, bufsize, lengthOfRead;
 
 #if 0   
    if(print_once){
        fprintf(stdout, "callback len = %d\n", queryLen);
        print_once = 0;
    }

    cur_tick = SDL_GetTicks();
    fprintf(stdout, "callback interval = %u\n", cur_tick-last_tick);
    last_tick = cur_tick;
#endif

    len = queryLen;
    buf = (unsigned char *)malloc(len);
    memset(buf, 0, len);
    
    itr = buf;
    while(len > 0){
        readsize = fread(itr, 1, len, pFile);
        if(!readsize)
            len = 0;
        len = len - readsize;
        itr = itr + readsize;
    }

    memcpy(stream, buf, queryLen);
    free(buf);
}



int main(int argc, char *argv[]){
    SDL_AudioSpec wanted, obtained;
    
    if(argc != 2){
        fprintf(stderr, "Usage: %s : audio.pcm\n", argv[0]);
        return -1;
    }

    pFile = fopen(argv[1], "rb");

    SDL_Init(SDL_INIT_AUDIO);
    
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = DEF_SAMPLERATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = DEF_CHANNELS;
    wanted.samples = DEF_SAMPLES;
    wanted.silence = 0;
    wanted.callback = SimpleCallback;

    
    if(0>SDL_OpenAudio(&wanted, &obtained)){
        fprintf(stderr, "SDL Open Audio failed, reason:%s\n", SDL_GetError());
        return -1;
    }
    fprintf(stdout, "wanted freq:%d, format:%d, channels:%d, samples:%d\n", wanted.freq, wanted.format, wanted.channels, wanted.samples);
    fprintf(stdout, "obtained freq:%d, format:%d, channels:%d, samples:%d\n", obtained.freq, obtained.format, obtained.channels, obtained.samples);

    SDL_PauseAudio(0);
    SDL_Delay(50000);
    SDL_CloseAudio();
    fclose(pFile);
    return 0;
}
    
    

