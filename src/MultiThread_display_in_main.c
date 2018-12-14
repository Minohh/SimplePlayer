

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <SDL2/SDL.h>

#include "Queue.h"

#define DEF_SAMPLES 2048
#define DATATEST 30

typedef struct SDL_Output{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;  //not used yet
    SDL_Texture *texture;
    SDL_Rect rect;
    SDL_AudioDeviceID audio_dev;
    unsigned char *YPlane;
    unsigned char *UPlane;
    unsigned char *VPlane;
    int window_width;
    int window_height;
    int buf_size;
}SDL_Output;

typedef struct VideoState{
    int64_t frame_cur_pts;
    int64_t frame_last_pts;
    int64_t cur_display_time;
    int64_t last_display_time;
    int64_t sleep_time;
    int is_first_frame;
    int last_frame_displayed;
    double time_base;
    AVFrame *cur_frame;
}VideoState;

typedef struct Codec{
    AVFormatContext *FCtx;
    AVCodecContext *CCtx;
    AVCodec *Codec;
    int stream;
}Codec;

typedef struct ReadThreadParam{
    AVFormatContext *FCtx;
    int AStream;
    int VStream;
}ReadThreadParam;

int VideoStream, AudioStream;
FrameQueue AFQ, VFQ;
RingBuffer ring_buffer;
PacketQueue APQ, VPQ;
int read_finished;

void SimpleCallback(void* userdata, Uint8 *stream, int queryLen){
    void *buf;
    int read_size = 0, len;
    
    len = queryLen;
    buf = (void *)stream;
    
    while(len > 0){
        read_size = RB_PullData(&ring_buffer, buf, len);
        
        //fprintf(stdout, "frame.format = %d, chennels = %d, samplerate = %d, nb_samples=%d\n", fn.frame->format, fn.frame->channels, fn.frame->sample_rate, fn.frame->nb_samples);
        if(!read_size){
            //fprintf(stdout, "no data to play\n");
            memset(stream+queryLen-len, 0, len);
            break;
        }else if(read_size<0){
            memset(stream, 0, queryLen);
            break;
        }
        len = len - read_size;
        buf = buf + read_size;
    }
}

int InitSDLAVOutput(SDL_Output *pOutput, int width, int height, int freq, int channels){
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_AudioSpec wanted, obtained;
    unsigned char *YPlane;
    unsigned char *UPlane;
    unsigned char *VPlane;

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)){
        fprintf(stderr, "SDL init video failed\n");
        return -1;
    }

    /*init SDL video display*/
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

    pOutput->window = window;
    pOutput->renderer = renderer;
    pOutput->texture = texture;
    pOutput->YPlane = YPlane;
    pOutput->UPlane = UPlane;
    pOutput->VPlane = VPlane;
    pOutput->window_width = width;
    pOutput->window_height = height;
    pOutput->buf_size = width*height;

    /*init SDL Audio Output*/
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = freq;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = channels;
    wanted.samples = DEF_SAMPLES;
    wanted.silence = 0;
    wanted.callback = SimpleCallback;

    pOutput->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if(!pOutput->audio_dev){
        fprintf(stderr, "SDL Open Audio failed, reason:%s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void UninitSDLAVOutput(SDL_Output *pOutput){
    if(!pOutput)
        return;
    if(pOutput->texture)
        SDL_DestroyTexture(pOutput->texture);
    if(pOutput->renderer)
        SDL_DestroyRenderer(pOutput->renderer);
    if(pOutput->window)
        SDL_DestroyWindow(pOutput->window);
    if(pOutput->YPlane)
        free(pOutput->YPlane);
    if(pOutput->UPlane)
        free(pOutput->UPlane);
    if(pOutput->VPlane)
        free(pOutput->VPlane);
    if(pOutput->audio_dev)
        SDL_CloseAudioDevice(pOutput->audio_dev);
}

void DisplayFrame(SDL_Output *pOutput){
    if(0!=SDL_UpdateYUVTexture(pOutput->texture, NULL, \
                pOutput->YPlane, pOutput->window_width, \
                pOutput->UPlane, pOutput->window_width/2, \
                pOutput->VPlane, pOutput->window_width/2)){
        fprintf(stdout, "Render Update Texture failed, reason: %s\n", SDL_GetError());
    }
    SDL_RenderCopyEx(pOutput->renderer, pOutput->texture, NULL, NULL, 0, NULL, 0);
    SDL_RenderPresent(pOutput->renderer);
}

int VideoThread(void *arg){
    fprintf(stdout, "VideoThread start\n");
    Codec *c = arg;
    AVFrame *pFrame;
    AVCodecContext *pCodecCtx = c->CCtx;
    AVPacket packet;
    FrameNode fn;
    int frameFinished = 0;
    int ret;

    pFrame = av_frame_alloc();
    if(pFrame == NULL){
        fprintf(stderr, "cannot get buffer of frame\n");
        return -1;
    }

    pCodecCtx->refcounted_frames = 1;
    while(1){
        ret = packet_queue_get(&VPQ, &packet);
        if(ret<0)
            break;

        //Decode video frame
        avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
        av_packet_unref(&packet);
        if(frameFinished){
            fn.frame = pFrame;
        
            ret = queue_frame(&VFQ, &fn);
            if(ret<0)
                break;
        }
    }

    av_free_packet(&packet);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    
    fprintf(stdout, "VideoThread exit\n");
    return 0;
}

int AudioThread(void *arg){
    fprintf(stdout, "AudioThread start\n");
    Codec *c = arg;
    AVCodecContext *pCodecCtx = c->CCtx;
    AVPacket packet;
    AVFrame *pFrame = NULL;
    int Stream = c->stream;
    int i = 0, frameFinished, size=0;
    void *buf=NULL, *ptr;
    short *itr;
    int frame_size, write_size, left_size;
    unsigned int sample_count;
    float *channel0, *channel1;
    short sample0, sample1;
    unsigned int audio_sleep;
    int ret;

    pFrame = av_frame_alloc();
    if(pFrame == NULL){
        fprintf(stderr, "cannot get buffer of frame\n");
        return -1;
    }
    

    audio_sleep = (unsigned int)((240*DEF_SAMPLES/4)/(pCodecCtx->sample_rate)/2*1000.0);

    //Read from stream into packet
    while(1){
        ret = packet_queue_get(&APQ, &packet);
        if(ret<0)
            break;
        //Only deal with the video stream of the type "videoStream"
        if(packet.stream_index==Stream){
            //Decode audio frame
            avcodec_decode_audio4(pCodecCtx, pFrame, &frameFinished, &packet);
            av_packet_unref(&packet);
            if(frameFinished){
                size = av_samples_get_buffer_size(NULL, pCodecCtx->channels, pFrame->nb_samples, pCodecCtx->sample_fmt, 1);
                if(!buf)
                    buf = malloc(size);
                ptr = buf;
                itr = (short *)buf;
                
                sample_count = pFrame->nb_samples;
                channel0 = (float *)pFrame->data[0];
                channel1 = (float *)pFrame->data[1];
                //normal PCM is mixed track, but fltp "p" means planar
                if(pFrame->format == AV_SAMPLE_FMT_FLTP) 
                {
                    for(i=0; i<sample_count; i++){ //stereo 
                        sample0 = (short)(channel0[i]*32767.0f);
                        sample1 = (short)(channel1[i]*32767.0f);
                        itr[2*i] = sample0;
                        itr[2*i+1] = sample1;
                    }
                    frame_size = sample_count*4;
                }else{
                    memcpy(itr, pFrame->data[0], pFrame->linesize[0]);
                    frame_size = pFrame->linesize[0];
                }

                left_size = frame_size;
                while(left_size){
                    write_size = RB_PushData(&ring_buffer, ptr, left_size);
                    if(write_size == 0)
                        SDL_Delay(audio_sleep);
                    else if(write_size<0)
                        break;
                    //fprintf(stdout, "new write_size = %d, audio_sleep=%d, ptr=0x%x\n", write_size, audio_sleep, ptr);
                    ptr += write_size;
                    left_size -= write_size;
                }
            }
        }
    }

    //free buffers
    if(buf)
        free(buf);

    av_free_packet(&packet);
    av_free(pFrame);
    avcodec_close(pCodecCtx);

    fprintf(stdout, "AudioThread exit\n");
    return 0;
}

/**********************************************************
 * Param: type: AVMEDIA_TYPE_VIDEO/AVMEDIA_TYPE_AUDIO
 *        pFormatCtx: the AVFormatContext for codec init
 *        c: Codec for saving params
 * ********************************************************/
int CodecInit(int type, AVFormatContext *pFormatCtx, Codec *c){
    AVCodecContext *pCodecCtx = NULL;
    AVCodec *pCodec = NULL;
    int audioVideo = -1;
    int Stream = -1;

    if(!pFormatCtx)
        return -1;
    
    if(type == AVMEDIA_TYPE_AUDIO)
        audioVideo = 0;
    else if(type == AVMEDIA_TYPE_VIDEO)
        audioVideo = 1;
    
    //One file may have multipule streams ,only get the first video stream
    Stream=-1;
    Stream = av_find_best_stream(pFormatCtx, type, -1, -1, NULL, 0);
    
    if(Stream<0){
        fprintf(stderr, "no video stream\n");
        return -1;
    }else{
        fprintf(stdout, "stream %d is %s\n", Stream, audioVideo?"video":"audio");
    }

    //copy param from format context to codec context
    pCodecCtx = avcodec_alloc_context3(NULL);
    if(avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[Stream]->codecpar)<0){
        fprintf(stderr, "copy param from format context to codec context failed\n");
        return -1;
    }
    
    //find the codec
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

    c->FCtx = pFormatCtx;
    c->CCtx = pCodecCtx;
    c->Codec = pCodec;
    c->stream = Stream;

    return 0;
}

int ReadThread(void *arg){
    fprintf(stdout, "ReadThread start\n");
    ReadThreadParam *param = arg;
    AVFormatContext *pFormatCtx = param->FCtx;
    int AudioStream = param->AStream;
    int VideoStream = param->VStream;
    int ret;

    AVPacket packet;
    while(1){
        ret = av_read_frame(pFormatCtx, &packet);
        if(ret>=0){
            if(packet.stream_index==AudioStream){
                ret = packet_queue_put(&APQ, &packet);
            }else if(packet.stream_index==VideoStream){
                ret = packet_queue_put(&VPQ, &packet);
            }
            if(ret<0)
                break;
        }else{
            //read finished
            //av_usleep(1000000);
            break;
        }
    }

    read_finished = 1;
    fprintf(stdout, "ReadThread exit\n");
    return 0;
}

int Display(SDL_Output *Output, VideoState *vs){
    FrameNode frameNode;
    int64_t delay, pts_delay, time;
    int ret;

    //video display loop
    frameNode.frame = vs->cur_frame;
    if(vs->last_frame_displayed||vs->is_first_frame){
        if(read_finished && !frame_nb(&VFQ))
            return 0;
        ret = dequeue_frame(&VFQ, &frameNode);
        if(ret<0)
            return -1;
    
        memcpy(Output->YPlane, vs->cur_frame->data[0], Output->buf_size);
        memcpy(Output->UPlane, vs->cur_frame->data[1], Output->buf_size/4);
        memcpy(Output->VPlane, vs->cur_frame->data[2], Output->buf_size/4);
            
        vs->frame_last_pts = vs->frame_cur_pts;
        vs->frame_cur_pts = vs->cur_frame->pts * vs->time_base * 1000000;
        pts_delay = vs->frame_cur_pts - vs->frame_last_pts;
        vs->cur_display_time = vs->last_display_time + pts_delay;
        vs->last_frame_displayed = 0;
    }

    time = av_gettime_relative();
    if(!vs->is_first_frame){
        delay = vs->cur_display_time - time;

        if(delay <= 0){
            vs->last_display_time = time;
            DisplayFrame(Output);
            av_frame_unref(vs->cur_frame);
            vs->last_frame_displayed = 1;
            vs->sleep_time = 0;
        }else if(delay > 10000){
            vs->sleep_time = 10000;
        }else{
            vs->sleep_time = delay;
        }
    }else{
        vs->last_display_time = time;
        DisplayFrame(Output);
        av_frame_unref(vs->cur_frame);
        vs->last_frame_displayed = 1;
        vs->is_first_frame = 0;
        vs->sleep_time = 0;
    }
    return 0;
}

int main(int argc, char *argv[]){
    ReadThreadParam param;
    Codec ACodec, VCodec;
    AVFormatContext *pFormatCtx = NULL;
    SDL_Output Output;
    int ret;
    SDL_Event event;
    VideoState vs;
    SDL_Thread *read_tid, *audio_tid, *video_tid;

    //Register all codecs and formats
    av_register_all();

    //Open and get stream info
    pFormatCtx = avformat_alloc_context();
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        fprintf(stderr, "open input failed\n");
        return -1;
    }
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "find stream info failed\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    if(CodecInit(AVMEDIA_TYPE_VIDEO, pFormatCtx, &VCodec)!=0)
        return -1;
    if(CodecInit(AVMEDIA_TYPE_AUDIO, pFormatCtx, &ACodec)!=0)
        return -1;

    //Init SDL
    ret = InitSDLAVOutput(&Output, VCodec.CCtx->width, VCodec.CCtx->height, ACodec.CCtx->sample_rate, ACodec.CCtx->channels);
    if(ret != 0){
        fprintf(stderr, "init SDL output error:%s\n", SDL_GetError());
        UninitSDLAVOutput(&Output);
        return -1;
    }
   
    packet_queue_init(&APQ, 500, "audio queue");
    packet_queue_init(&VPQ, 300, "video queue");
    frame_queue_init(&VFQ, "video frame queue");
    RB_Init(&ring_buffer, 240*DEF_SAMPLES);
  
    param.FCtx = ACodec.FCtx;
    param.AStream = ACodec.stream;
    param.VStream = VCodec.stream;

    memset(&vs, 0, sizeof(VideoState));
    vs.time_base = av_q2d(VCodec.FCtx->streams[VCodec.stream]->time_base);
    vs.last_frame_displayed = 0;
    vs.is_first_frame = 1;
    vs.cur_frame = av_frame_alloc();

    read_tid    = SDL_CreateThread(ReadThread, "ReadThread", &param);
    video_tid   = SDL_CreateThread(VideoThread, "VideoThread", &VCodec);
    audio_tid   = SDL_CreateThread(AudioThread, "AudioThread", &ACodec);
    //start to play audio
    SDL_PauseAudioDevice(Output.audio_dev, 0);
    
    while(1){
        Display(&Output, &vs);
        SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        switch(event.type){
        case SDL_QUIT :
            //abort queue will cause threads break from loop
            packet_queue_abort(&APQ);
            packet_queue_abort(&VPQ);
            frame_queue_abort(&VFQ);
            RB_abort(&ring_buffer);

            SDL_WaitThread(read_tid, NULL);
            SDL_WaitThread(video_tid, NULL);
            SDL_WaitThread(audio_tid, NULL);

            /* queue_abort : set queue->abort_request
             * queue->abort_request leads thread loop break
             * we should wait the thread loop return ,then uninit the queues
             * */
            UninitSDLAVOutput(&Output);//close audio device will wait the callback thread return, 
            
            packet_queue_uninit(&APQ);
            packet_queue_uninit(&VPQ);
            frame_queue_uninit(&VFQ);
            RB_Uninit(&ring_buffer);
           
            //audio codec close in AudioThread
            //video codec close in VideoThread
            
            av_free(vs.cur_frame);

            avformat_close_input(&pFormatCtx);
            SDL_Quit();
            exit(0);
            break;
        default :
            break;
        }
        av_usleep(vs.sleep_time);
        SDL_PumpEvents();
    }

    return 0;
}


