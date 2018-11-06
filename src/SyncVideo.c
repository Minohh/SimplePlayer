#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <SDL2/SDL.h>

#define SAVE_FRAMES 50

#define DEF_WIDTH 1920
#define DEF_HEIGHT 1080
#define FRAMERATE 24

typedef struct SDL_Display{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;  //not used yet
    SDL_Texture *texture;
    SDL_Rect rect;
    unsigned char *YPlane;
    unsigned char *UPlane;
    unsigned char *VPlane;
    int windowWidth;
    int windowHeight;
}SDL_Display;

typedef struct VideoState{
    int64_t frame_cur_pts;
    int64_t frame_last_pts;
    int64_t cur_display_time;
    int64_t last_display_time;
    int64_t sleep_time;
    int is_first_frame;
    int last_frame_displayed;
}VideoState;

int InitSDLDisplay(SDL_Display *pDisplay, int width, int height){
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect rect;
    unsigned char *YPlane;
    unsigned char *UPlane;
    unsigned char *VPlane;

    if(SDL_Init(SDL_INIT_VIDEO)){
        fprintf(stderr, "SDL init video failed\n");
        return -1;
    }

    window = SDL_CreateWindow("Simple Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, 0);
    if(!window){
        fprintf(stderr, "SDL create window failed\n");
        return -1;
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!renderer){
        fprintf(stderr, "SDL create renderer failed\n");
        return -1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
    if(!texture){
        fprintf(stderr, "SDL create texture failed\n");
        return -1;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    YPlane = (unsigned char *)malloc(width*height);
    UPlane = (unsigned char *)malloc(width*height/4);
    VPlane = (unsigned char *)malloc(width*height/4);

    pDisplay->window = window;
    pDisplay->renderer = renderer;
    pDisplay->texture = texture;
    pDisplay->YPlane = YPlane;
    pDisplay->UPlane = UPlane;
    pDisplay->VPlane = VPlane;
    pDisplay->windowWidth = width;
    pDisplay->windowHeight = height;

    return 0;
}

void UninitSDLDisplay(SDL_Display *pDisplay){
    if(!pDisplay)
        return;
    if(pDisplay->texture)
        SDL_DestroyTexture(pDisplay->texture);
    if(pDisplay->renderer)
        SDL_DestroyRenderer(pDisplay->renderer);
    if(pDisplay->window)
        SDL_DestroyWindow(pDisplay->window);
    if(pDisplay->YPlane)
        free(pDisplay->YPlane);
    if(pDisplay->UPlane)
        free(pDisplay->UPlane);
    if(pDisplay->VPlane)
        free(pDisplay->VPlane);
}

void DisplayFrame(SDL_Display *pDisplay){
    if(0!=SDL_UpdateYUVTexture(pDisplay->texture, NULL, \
                pDisplay->YPlane, pDisplay->windowWidth, \
                pDisplay->UPlane, pDisplay->windowWidth/2, \
                pDisplay->VPlane, pDisplay->windowWidth/2)){
        fprintf(stdout, "Render Update Texture failed, reason: %s\n", SDL_GetError());
    }
    SDL_RenderCopyEx(pDisplay->renderer, pDisplay->texture, NULL, NULL, 0, NULL, 0);
    SDL_RenderPresent(pDisplay->renderer);
}

void SaveFrame2YUV(AVFrame *pFrame, int width, int height, int iFrame){
    static FILE *pFile;
    char szFilename[32];
    int y;

    //Open file
    if(iFrame==1){
         sprintf(szFilename, "Video.yuv");
        pFile = fopen(szFilename, "wb");
        if(pFile==NULL)
            return;
    }

    //Write YUV Data, Only support YUV420
    //Y
    for(y=0; y<height; y++){
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, pFrame->linesize[0], pFile);
    }
    //U
    for(y=0; y<(height+1)/2; y++){
        fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, pFrame->linesize[1], pFile);
    }
    //V
    for(y=0; y<(height+1)/2; y++){
        fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, pFrame->linesize[2], pFile);
    }

    //Close FIle
    if(iFrame==SAVE_FRAMES){
        fclose(pFile);
    }
}

int main(int argc, char *argv[]){
    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext *pCodecCtxOrig = NULL, *pCodecCtx = NULL;
    AVCodec *pCodec = NULL;
    AVPacket packet;
    AVFrame *pFrame = NULL;
    int videoStream = -1, frameFinished;
    SDL_Display Display;
    int ret, bufsize;
    VideoState vs;
    double time_base;
    int64_t pts_delay, time, delay;

    //Register all codecs and formats
    av_register_all();

    //Open and get stream info
    //pFormatCtx = avformat_alloc_context();
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        fprintf(stderr, "open input failed\n");
        return -1;
    }
    fprintf(stdout, "format context param codec addr %d\n", pFormatCtx->streams[0]->codecpar);
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "find stream info failed\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    //One file may have multipule streams ,only get the first video stream
    videoStream=-1;
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    
    if(videoStream<0){
        fprintf(stderr, "no video stream\n");
        return -1;
    }else{
        fprintf(stdout, "stream %d is video\n", videoStream);
    }

    fprintf(stdout, "format context param codec addr %d\n", pFormatCtx->streams[videoStream]->codecpar);
    //copy param from format context to codec context
    pCodecCtx = avcodec_alloc_context3(NULL);
    fprintf(stdout, "copy param\n");
    if(avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar)<0){
        fprintf(stderr, "copy param from format context to codec context failed\n");
        return -1;
    }
    //find the codec
    //pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL){
        fprintf(stderr, "Unsupported codec,codec id %d\n", pCodecCtx->codec_id);
        return -1;
    }else{
        fprintf(stdout, "codec id is %d\n", pCodecCtx->codec_id);
    }

    //Open codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        fprintf(stderr, "open codec failed\n");
        return -1;
    }

    //Buffer to save the decoded frame
    pFrame = av_frame_alloc();
    if(pFrame == NULL){
        fprintf(stderr, "cannot get buffer of frame\n");
        return -1;
    }

    //Init SDL
    ret = InitSDLDisplay(&Display, pCodecCtx->width, pCodecCtx->height);
    if(ret != 0){
        UninitSDLDisplay(&Display);
        return -1;
    }
    
    //Read from stream into packet
    bufsize = pCodecCtx->width * pCodecCtx->height;
    time_base = av_q2d(pFormatCtx->streams[videoStream]->time_base);
    while(av_read_frame(pFormatCtx, &packet)>=0){
        //Only deal with the video stream of the type "videoStream"
        if(packet.stream_index==videoStream){
            //Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            //fprintf(stdout, "Frame : %d ,pts=%lld, timebase=%lf\n", i, pFrame->pts, av_q2d(pFormatCtx->streams[videoStream]->time_base));
            if(frameFinished){
                memcpy(Display.YPlane, pFrame->data[0], bufsize);
                memcpy(Display.UPlane, pFrame->data[1], bufsize/4);
                memcpy(Display.VPlane, pFrame->data[2], bufsize/4);

                vs.frame_last_pts = vs.frame_cur_pts;
                vs.frame_cur_pts = pFrame->pts * time_base * 1000000;
                pts_delay = vs.frame_cur_pts - vs.frame_last_pts;
                vs.cur_display_time = vs.last_display_time + pts_delay;
                //vs.last_frame_displayed = 0;

                if(!vs.is_first_frame){
                    time = av_gettime_relative();
                    delay = vs.cur_display_time - time;
                    while(delay > 0){
                        if(delay > 10000)
                            vs.sleep_time = 10000;
                        else
                            vs.sleep_time = delay;
                        av_usleep(vs.sleep_time);
                        time = av_gettime_relative();
                        delay = vs.cur_display_time - time;
                    }
                    vs.last_display_time = time;
                    DisplayFrame(&Display);
                    //vs.last_frame_displayed = 1;
                }else{
                    vs.last_display_time = av_gettime_relative();
                    DisplayFrame(&Display);
                    vs.is_first_frame = 0;
                    //vs.last_frame_displated = 1;
                }

                SDL_PumpEvents();
            }
        }
    }

    //free buffers
    av_free_packet(&packet);
    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);
    avformat_close_input(&pFormatCtx);
    UninitSDLDisplay(&Display);

    return 0;
}

