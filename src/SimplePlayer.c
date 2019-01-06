#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <SDL2/SDL.h>

#include "Queue.h"
#include "Clock.h"

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
    /* video display parameter */
    int64_t frame_cur_pts;
    int64_t frame_last_pts;
    int64_t last_display_time;
    int64_t cur_display_time;
    int64_t sleep_time;
    double time_base;   //for calculation accuracy time_base can only be double
    int last_frame_displayed;
    int is_first_frame;
    double usecond_per_byte;  //for calculation accuracy usecond_per_byte can only be double
    int64_t audio_bytes_consumed;
    SyncClock sc;
    AVFrame *cur_frame;

    /* audio/video stream info  */
    int has_video;
    int has_audio;
    AVFormatContext *FCtx;
    int AStream;
    int VStream;
}VideoState;

typedef struct Codec{
    AVFormatContext *FCtx;
    AVCodecContext *CCtx;
    AVCodec *Codec;
    AVFilterGraph *filter_graph;
    AVFilterContext *in_filter;
    AVFilterContext *out_filter;
    int stream;
}Codec;

FrameQueue AFQ, VFQ;
RingBuffer ring_buffer;
PacketQueue APQ, VPQ;
SDL_Thread *read_tid;
SDL_Thread *audio_tid;
SDL_Thread *video_tid;

int read_finished;

double audio_frame_pts;
//int ii = 0;
//int jj = 0;

void SimpleCallback(void* userdata, Uint8 *stream, int queryLen){
    void *buf;
    int read_size = 0, len;
    VideoState *vs = (VideoState *)userdata;
    
    len = queryLen;
    buf = (void *)stream;
    
    while(len > 0){
        read_size = RB_PullData(&ring_buffer, buf, len);
        
        //fprintf(stdout, "frame.format = %d, chennels = %d, samplerate = %d, nb_samples=%d\n", fn.frame->format, fn.frame->channels, fn.frame->sample_rate, fn.frame->nb_samples);
        if(!read_size){
            //fprintf(stdout, "no data to play\n");
            memset(buf, 0, len);
            break;
        }else if(read_size<0){
            memset(stream, 0, queryLen);
            break;
        }
        len = len - read_size;
        buf = buf + read_size;
    }

    vs->audio_bytes_consumed += (queryLen-len);
    set_audio_pts(&vs->sc, vs->audio_bytes_consumed * (vs->usecond_per_byte));
    //if(get_audio_pts(&vs->sc)>32000000)
    //    fprintf(stdout, "[%d]video pts %lld, audio pts %lld\n", ii, get_video_pts(&vs->sc), get_audio_pts(&vs->sc));
    //ii++;
}

int AudioFilterInit(Codec *c){
    int ret;

    // input format, sample rate, layout, buffersrc must set this options while initializing
    char args[256] = {0};
    const char    *in_sample_fmt_name  = av_get_sample_fmt_name(c->CCtx->sample_fmt);
    int           in_sample_rate       = c->CCtx->sample_rate;
    int           in_channels          = c->CCtx->channels;
    int64_t       in_channel_layout    = c->CCtx->channel_layout;

    snprintf(args, sizeof(args), "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d:channel_layout=0x%lld",
            in_sample_rate, in_sample_fmt_name, in_channels, 1, in_sample_rate, in_channel_layout);

    
    AVFilterContext *in_audio_filter = NULL, *out_audio_filter = NULL;
    AVFilterGraph *filter_graph = NULL;

    const AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    
    filter_graph = avfilter_graph_alloc();
    
    avfilter_graph_create_filter(&in_audio_filter, abuffersrc, "in", args, NULL, filter_graph);
    avfilter_graph_create_filter(&out_audio_filter, abuffersink, "out", NULL, NULL, filter_graph);
   
    av_opt_show2(in_audio_filter->priv, NULL, 8|(1<<16), 0);
    av_opt_show2(out_audio_filter->priv, NULL, 8|(1<<16), 0);

    // output format
    static const enum AVSampleFormat out_sample_fmts[2]     = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    ret = av_opt_set_int_list(out_audio_filter, "sample_fmts",     out_sample_fmts,     AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if(ret !=0)
        fprintf(stderr, "set sample_fmts error %s\n", av_err2str(ret));

    avfilter_link(in_audio_filter, 0, out_audio_filter, 0);
    
    avfilter_graph_config(filter_graph, NULL);

    c->filter_graph = filter_graph;
    c->in_filter    = in_audio_filter;
    c->out_filter   = out_audio_filter;
    return 0;
}

int VideoFilterInit(Codec *c){
    // input format, sample rate, layout, buffersrc must set this options while initializing
    char args[256] = {0};
    int        width     = c->CCtx->width;
    int        height    = c->CCtx->height;
    int        pix_fmt   = c->CCtx->pix_fmt;
    AVRational time_base = c->FCtx->streams[c->stream]->time_base;
    AVRational ratio     = c->CCtx->sample_aspect_ratio;

    snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            width, height, pix_fmt, time_base.num, time_base.den, ratio.num, ratio.den);

    
    AVFilterContext *in_video_filter = NULL;
    AVFilterContext *out_video_filter = NULL;
    AVFilterGraph *filter_graph = NULL;

    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");

    filter_graph = avfilter_graph_alloc();
    
    avfilter_graph_create_filter(&in_video_filter, buffersrc, "in", args, NULL, filter_graph);
    avfilter_graph_create_filter(&out_video_filter, buffersink, "out", NULL, NULL, filter_graph);
    
    avfilter_link(in_video_filter, 0, out_video_filter, 0);

    avfilter_graph_config(filter_graph, NULL);

    c->filter_graph = filter_graph;
    c->in_filter    = in_video_filter;
    c->out_filter   = out_video_filter;
    return 0;
}

int VideoStateInit(VideoState *pVS){

    memset(pVS, 0, sizeof(VideoState));
    
    pVS->last_frame_displayed = 0;
    pVS->is_first_frame = 1;
    pVS->cur_frame = av_frame_alloc();

    return 0;
}

int VideoStateSetForComputingPTS(VideoState *vs, int freq, int channels){

    /* 
     * [Important] for computing audio pts 
     *             accuracy is very important, will cause av lag if accuracy not enougth
     * [Audio]
     *             Similarly vs.usecond_per_byte must be double type.
     *             1000000 * (audio_stream_len/audio_bytes_per_second) = usecond of stream
     *             usecond_per_byte = 1000000 * audio_bytes_per_second
     *             usecond of stream = usecond_per_byte * audio_stream_len
     *             2 for wanted.format = AUDIO_S16SYS;
     */
    vs->usecond_per_byte = 1000000.0f/(freq*2*channels); 
    fprintf(stdout, "usecond per byte is %lf\n", vs->usecond_per_byte);

    return 0;
}

int InitSDLAudioOutput(SDL_Output *pOutput, VideoState *vs, int freq, int channels){
    SDL_AudioSpec wanted, obtained;
    int ret = 0;

    if(SDL_WasInit(0)) {
        ret = SDL_InitSubSystem(SDL_INIT_AUDIO);
    }else {
        ret = SDL_Init(SDL_INIT_AUDIO);
    }
    if(ret) {
        fprintf(stderr, "SDL init audio failed\n");
        return -1;
    }
    
    /*init SDL Audio Output*/
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = freq;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = channels;
    wanted.samples = DEF_SAMPLES;
    wanted.silence = 0;
    wanted.callback = SimpleCallback;
    wanted.userdata = (void *)(vs);

    pOutput->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if(!pOutput->audio_dev){
        fprintf(stderr, "SDL Open Audio failed, reason:%s\n", SDL_GetError());
        return -1;
    }

    return 0;
}
int InitSDLVideoOutput(SDL_Output *pOutput, VideoState *vs, int width, int height){
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    unsigned char *YPlane;
    unsigned char *UPlane;
    unsigned char *VPlane;
    int ret = 0;

    if(SDL_WasInit(0)) {
        ret = SDL_InitSubSystem(SDL_INIT_VIDEO);
    }else {
        ret = SDL_Init(SDL_INIT_VIDEO);
    }
    if(ret) {
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

    return 0;
}

void UninitSDLVideoOutput(SDL_Output *pOutput){
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
}

void UninitSDLAudioOutput(SDL_Output *pOutput){
    if(!pOutput)
        return;
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
    int ret;

    pFrame = av_frame_alloc();
    if(pFrame == NULL){
        fprintf(stderr, "cannot get buffer of frame\n");
        return -1;
    }

    while(1){
        ret = packet_queue_get(&VPQ, &packet);
        if(ret<0)
            break;

        ret = avcodec_send_packet(pCodecCtx, &packet);

        //receive video frame
        while(ret>=0){
            ret = avcodec_receive_frame(pCodecCtx, pFrame);
            if(ret>=0){

                ret = av_buffersrc_add_frame(c->in_filter, pFrame);
                if(ret < 0){
                    fprintf(stderr, "filter error\n");
                }
                while((ret = av_buffersink_get_frame_flags(c->out_filter, pFrame, 0))>=0){

                    fn.frame = pFrame;
        
                    //fprintf(stdout, "filtered frame pts = %d\n", pFrame->pts);
                    ret = queue_frame(&VFQ, &fn);
                    if(ret<0)
                        break;
                }
            }
        }
        av_packet_unref(&packet);
    }

    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avfilter_graph_free(&(c->filter_graph));
    
    fprintf(stdout, "VideoThread exit\n");
    return 0;
}

int AudioThread(void *arg){
    fprintf(stdout, "AudioThread start\n");
    Codec *c = arg;
    AVCodecContext *pCodecCtx = c->CCtx;
    AVPacket packet;
    AVFrame *pFrame = NULL;
    void *ptr;
    int write_size, left_size;
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

        ret = avcodec_send_packet(pCodecCtx, &packet);

        while(ret>=0){
            //Decode audio frame
            ret = avcodec_receive_frame(pCodecCtx, pFrame);
            
            if(ret >=0){
                ret = av_buffersrc_add_frame(c->in_filter, pFrame);
                if(ret < 0){
                    fprintf(stderr, "filter error\n");
                }
                while((ret = av_buffersink_get_frame_flags(c->out_filter, pFrame, 0))>=0){
                    //origin pFrame->linesize[0] = 8192
                    //filtered pFrame->linesize[0] = 4224
                    //why ?
                    ptr = pFrame->data[0];
                    left_size = pFrame->nb_samples * pFrame->channels * 2;

                    while(left_size){
                        write_size = RB_PushData(&ring_buffer, ptr, left_size);
                        if(write_size == 0)
                            SDL_Delay(audio_sleep);
                        else if(write_size<0)
                            break;
                        ptr += write_size;
                        left_size -= write_size;
                    }
                }
            }
        }
        av_packet_unref(&packet);
    }

    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avfilter_graph_free(&(c->filter_graph));

    fprintf(stdout, "AudioThread exit\n");
    return 0;
}

int ReadThread(void *arg){
    fprintf(stdout, "ReadThread start\n");
    VideoState *pVS = arg;
    AVFormatContext *pFormatCtx = pVS->FCtx;
    int AudioStream = pVS->AStream;
    int VideoStream = pVS->VStream;
    int audio_available = pVS->has_audio;
    int video_available = pVS->has_video;
    int ret;

    AVPacket packet;
    while(1){
        ret = av_read_frame(pFormatCtx, &packet);
        if(ret>=0){
            if(packet.stream_index==AudioStream){
                if(audio_available)
                    ret = packet_queue_put(&APQ, &packet);
            }else if(packet.stream_index==VideoStream){
                if(video_available)
                    ret = packet_queue_put(&VPQ, &packet);
            }
            if(ret<0)
                break;
        }else{
            //read finished
            //av_usleep(1000000);
            packet.data=NULL;
            packet.size=0;
            if(audio_available)
                packet_queue_put(&APQ, &packet);
            if(video_available)
                packet_queue_put(&VPQ, &packet);
            break;
        }
    }

    read_finished = 1;
    fprintf(stdout, "ReadThread exit\n");
    return 0;
}

/**********************************************************
 * Param: type: 
 *              AVMEDIA_TYPE_VIDEO/AVMEDIA_TYPE_AUDIO
 *        pFormatCtx: 
 *              the AVFormatContext provides parameters for codec init
 *        c: 
 *              Codec struct for combination
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
    
    /* 
     * 1. One file may have multipule streams ,only get the first video/audio stream
     * 2. Allocate codec context which is the field maintainace all the info/context while codec working
     * 3. Copy param from format context to codec context
     * 4. Find the codec
     * 5. Open the codec
     */
    Stream=-1;
    Stream = av_find_best_stream(pFormatCtx, type, -1, -1, NULL, 0);
    
    if(Stream<0){
        if(audioVideo == 1)
            fprintf(stderr, "no video stream\n");
        else if(audioVideo == 0)
            fprintf(stderr, "no video stream\n");
        else
            fprintf(stderr, "stream type error\n");
        return -1;
    }else{
        fprintf(stdout, "stream %d is %s\n", Stream, audioVideo?"video":"audio");
    }

    pCodecCtx = avcodec_alloc_context3(NULL);

    if(avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[Stream]->codecpar)<0){
        fprintf(stderr, "copy param from format context to codec context failed\n");
        return -1;
    }
    
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL){
        fprintf(stderr, "Unsupported codec,codec id %d\n", pCodecCtx->codec_id);
        return -1;
    }else{
        fprintf(stdout, "codec id is %d\n", pCodecCtx->codec_id);
    }

    pCodecCtx->thread_count = 4;
    pCodecCtx->thread_type = FF_THREAD_SLICE;

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

int AudioInit(AVFormatContext *pFormatCtx, Codec *pACodec, SDL_Output *pOutput, VideoState *pVS) {
    int ret = 0;

    if(CodecInit(AVMEDIA_TYPE_AUDIO, pFormatCtx, pACodec)!=0){
        pVS->has_audio = 0;
        return -1;
    }else{
        pVS->has_audio = 1;
    }
    AudioFilterInit(pACodec);
    
    VideoStateSetForComputingPTS(pVS, pACodec->CCtx->sample_rate, pACodec->CCtx->channels);
   
    ret = InitSDLAudioOutput(pOutput, pVS, pACodec->CCtx->sample_rate, pACodec->CCtx->channels);
    if(ret != 0){
        fprintf(stderr, "init SDL output error:%s\n", SDL_GetError());
        UninitSDLAudioOutput(pOutput);
        return -1;
    }
   
    packet_queue_init(&APQ, 500, "audio queue");
    RB_Init(&ring_buffer, 240*DEF_SAMPLES);

    audio_tid   = SDL_CreateThread(AudioThread, "AudioThread", pACodec);
    
    //start to play audio
    SDL_PauseAudioDevice(pOutput->audio_dev, 0);

    return 0;
}

int VideoInit(AVFormatContext *pFormatCtx, Codec *pVCodec, SDL_Output *pOutput, VideoState *pVS) {
    int ret = 0;
    
    VideoStateInit(pVS);

    if(CodecInit(AVMEDIA_TYPE_VIDEO, pFormatCtx, pVCodec)!=0)
        pVS->has_video = 0;
    else
        pVS->has_video = 1;
 
    
    if(pVS->has_video){
        /* 
         * [Important] for computing video pts 
         *             accuracy is very important, will cause av lag if accuracy not enougth
         * [Video]     
         *             AVRational time_base is a fraction consist of num and den
         *             We can get decimal of this fraction with av_q2d that is equivalent to num/den. 
         *             For accuracy, vs.time_base must be double type.
         *             And firstly, multi 1000000 to convert to usecond.
         */
        AVRational tb =  pVCodec->FCtx->streams[pVCodec->stream]->time_base;
        pVS->time_base = tb.num*1000000.0f/(double)tb.den;
        fprintf(stdout, "timebase = %lf, %lf\n", pVS->time_base, av_q2d(tb));
    
        VideoFilterInit(pVCodec);
    
        //Init SDL
        ret = InitSDLVideoOutput(pOutput, pVS, pVCodec->CCtx->width, pVCodec->CCtx->height);
        if(ret != 0){
            fprintf(stderr, "init SDL video output error:%s\n", SDL_GetError());
            UninitSDLVideoOutput(pOutput);
            return -1;
        }
    
        packet_queue_init(&VPQ, 300, "video queue");
        frame_queue_init(&VFQ, "video frame queue");
  
        video_tid   = SDL_CreateThread(VideoThread, "VideoThread", pVCodec);
    } else {
        //Init SDL for audio
        ret = InitSDLVideoOutput(pOutput, pVS, 600, 1);
        if(ret != 0){
            fprintf(stderr, "init SDL video output error:%s\n", SDL_GetError());
            UninitSDLVideoOutput(pOutput);
            return -1;
        }
    }
    return 0;
}


/*
 *  1. dequeue frame
 *  2. display timely frame
 *  3. if there is spare time before displaying, calculate the time for sleeping
 */ 
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
        vs->frame_cur_pts = vs->cur_frame->pts * vs->time_base;
        pts_delay = vs->frame_cur_pts - vs->frame_last_pts;
        if(vs->has_audio) {
            set_acceptable_delay(&vs->sc, pts_delay);
            pts_delay = adjust_delay(&vs->sc, pts_delay);
        }
        vs->cur_display_time = vs->last_display_time + pts_delay;

        vs->last_frame_displayed = 0;
    }

    time = av_gettime_relative();
    if(!vs->is_first_frame){
        delay = vs->cur_display_time - time;

        if(delay <= 0){
            vs->last_display_time = time;
            set_video_pts(&vs->sc, vs->frame_cur_pts);
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
        set_video_pts(&vs->sc, vs->frame_cur_pts);
        DisplayFrame(Output);
        av_frame_unref(vs->cur_frame);
        vs->last_frame_displayed = 1;
        vs->is_first_frame = 0;
        vs->sleep_time = 0;
    }
    return 0;
}

int main(int argc, char *argv[]){
    Codec ACodec, VCodec;
    AVFormatContext *pFormatCtx = NULL;
    SDL_Output Output;
    SDL_Event event;
    VideoState vs;

    //Register all codecs and formats
    //av_register_all();

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
    
    VideoInit(pFormatCtx, &VCodec, &Output, &vs);
    AudioInit(pFormatCtx, &ACodec, &Output, &vs);
    
    if(!vs.has_video && !vs.has_audio)
        return -1;

    vs.FCtx = pFormatCtx;
    vs.AStream = ACodec.stream;
    vs.VStream = VCodec.stream;
    read_tid    = SDL_CreateThread(ReadThread, "ReadThread", &vs);
    
    while(1){
        if(vs.has_video)
            Display(&Output, &vs);
        else
            vs.sleep_time = 40000;
        SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        switch(event.type){
        case SDL_QUIT :
            /* 
             * 1. set queue->abort_request = 1
             * 2. queue->abort_request==1 leads thread loop break
             * 3. we should wait the thread loop return ,then uninit the queues
             * 4. close audio device will wait callback thread return;
             * 5. uninit queue
             */
            if(vs.has_audio) {
                packet_queue_abort(&APQ);
                RB_abort(&ring_buffer);
            }
            if(vs.has_video) {
                packet_queue_abort(&VPQ);
                frame_queue_abort(&VFQ);
            }

            //abort queue will cause threads break from loop

            SDL_WaitThread(read_tid, NULL);
            if(vs.has_video)
                SDL_WaitThread(video_tid, NULL);
            if(vs.has_audio)
                SDL_WaitThread(audio_tid, NULL);
            
            UninitSDLVideoOutput(&Output);
            if(vs.has_audio)
                UninitSDLAudioOutput(&Output);

            if(vs.has_audio) {
                packet_queue_uninit(&APQ);
                RB_Uninit(&ring_buffer);
            }
            if(vs.has_video) {
                packet_queue_uninit(&VPQ);
                frame_queue_uninit(&VFQ);
            }
            //audio codec close in AudioThread
            //video codec close in VideoThread
            
            if(vs.has_video)
                av_free(vs.cur_frame);

            avformat_close_input(&pFormatCtx);
            SDL_Quit();
            exit(0);
            break;
        default :
            break;
        }
        av_usleep(vs.sleep_time);

        /* 
         * SDL_PumpEvents has two functions here:
         * 1. peep events would not pump events, we use SDL_PumpEvents to pump event queue
         * 2. SDL_PumpEvents must in the same thread as video sub system init thread to keep updating video device
         *    SDL_Init(SDL_INIT_VIDEO) will init video sub system
         */ 
        SDL_PumpEvents();
    }

    return 0;
}


