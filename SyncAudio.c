#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>

#include "Queue.h"

#define SAVE_FRAMES 2000
#define DEF_SAMPLES 2048

const char *codec_type ,*codec_nane;
char channel_layout[20] = {0};
FrameQueue fq;
RingBuffer ring_buffer;
FILE *pFile;


void SimpleCallback_File(void* userdata, Uint8 *stream, int queryLen){
    unsigned char *buf, *itr;
    int readsize = 0, len;
    
    len = queryLen;
    //Query audio format is S16 with length=len.However the input audio format is F32, so that the buffersize should be len*2
    buf = (unsigned char *)malloc(len);//4 for 32 float format(input audio data)
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
            memset(buf+queryLen-len, 0, len);
        }
        len = len - read_size;
        buf = buf + read_size;
    }
}

void dumpAudioInfo(AVCodecContext *pCodecCtx){
    av_get_channel_layout_string(&channel_layout[0], 20, pCodecCtx->channels, pCodecCtx->channel_layout);
    fprintf(stdout, "codec channel layout:%s\n", channel_layout);
    fprintf(stdout, "codec type:%s\ncodec name:%s\nsample format:%s\n", \ 
            av_get_media_type_string(pCodecCtx->codec_type), \
            avcodec_get_name(pCodecCtx->codec_id), \
            av_get_sample_fmt_name(pCodecCtx->sample_fmt));
    fprintf(stdout, "codec channel layout:%I64u, channels:%d, sample rate:%d\n", pCodecCtx->channel_layout, pCodecCtx->channels, pCodecCtx->sample_rate);
}

void SaveFrame2PCM(AVFrame *pFrame, int size, int iFrame){
    static FILE *pFile;
    char szFilename[32];
    unsigned int sample_count, i;
    float *channel0, *channel1;
    short sample0, sample1;

    //Open file
    if(iFrame==1){
         sprintf(szFilename, "audio.pcm");
        pFile = fopen(szFilename, "wb");
        if(pFile==NULL)
            return;
    }

    sample_count = pFrame->nb_samples;
    channel0 = (float *)pFrame->data[0];
    channel1 = (float *)pFrame->data[1];
    //Write YUV Data, Only support YUV420
    //normal PCM is mixed track, but fltp "p" means planar
    if(pFrame->format == AV_SAMPLE_FMT_FLTP) 
    {
        for(i=0; i<sample_count; i++){ //stereo 
            sample0 = (short)(channel0[i]*32767.0f);
            sample1 = (short)(channel1[i]*32767.0f);
            fwrite(&sample0, 2, 1, pFile);
            fwrite(&sample1, 2, 1, pFile);
        }
    }else{
        fwrite(pFrame->extended_data[0], 1, size, pFile);
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
    int i = 0, AudioStream = -1, frameFinished, size=0;
    SDL_AudioSpec wanted, obtained;
    void *buf=NULL, *ptr;
    short *itr;
    int frame_size, write_size, left_size;
    unsigned int sample_count;
    float *channel0, *channel1;
    short sample0, sample1;
    unsigned int audio_sleep;

    pFile = fopen("audio.pcm", "rb");

    //Register all codecs and formats
    av_register_all();

    //Open and get stream info
    //pFormatCtx = avformat_alloc_context();
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        fprintf(stderr, "open input failed\n");
        return -1;
    }

    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "find stream info failed\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    //One file may have multipule streams ,only get the first video stream
    AudioStream=-1;
    AudioStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    
    if(AudioStream<0){
        fprintf(stderr, "no Audio stream\n");
        return -1;
    }else{
        fprintf(stdout, "stream %d is Audio\n", AudioStream);
    }

    //copy param from format context to codec context
    pCodecCtx = avcodec_alloc_context3(NULL);
    fprintf(stdout, "copy param\n");
    if(avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[AudioStream]->codecpar)<0){
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

    //Buffer to save the decoded frame
    pFrame = av_frame_alloc();
    if(pFrame == NULL){
        fprintf(stderr, "cannot get buffer of frame\n");
        return -1;
    }

    //Audio playback init
    SDL_Init(SDL_INIT_AUDIO);
    
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = pCodecCtx->sample_rate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = pCodecCtx->channels;
    wanted.samples = DEF_SAMPLES;
    wanted.silence = 0;
    wanted.callback = SimpleCallback;

    
    if(0>SDL_OpenAudio(&wanted, &obtained)){
        fprintf(stderr, "SDL Open Audio failed, reason:%s\n", SDL_GetError());
        return -1;
    }

    //start to play audio
    SDL_PauseAudio(0);

    frame_queue_init(&fq, "AudioFrameQueue");
    RB_Init(&ring_buffer, 240*DEF_SAMPLES);
    audio_sleep = (unsigned int)((240*DEF_SAMPLES/4)/(obtained.freq)/2*1000.0);

    //Read from stream into packet
    while(av_read_frame(pFormatCtx, &packet)>=0){
        //Only deal with the video stream of the type "videoStream"
        if(packet.stream_index==AudioStream){
            //Decode video frame
            avcodec_decode_audio4(pCodecCtx, pFrame, &frameFinished, &packet);
            if(frameFinished){
                size = av_samples_get_buffer_size(NULL, pCodecCtx->channels, pFrame->nb_samples, pCodecCtx->sample_fmt, 1);
                if(!buf)
                    buf = malloc(size);
                ptr = buf;
                itr = (short *)buf;
                //SaveFrame2PCM(pFrame, size, i);
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
                    else
                        ;//fprintf(stdout, "new write_size = %d, audio_sleep=%d, ptr=0x%x\n", write_size, audio_sleep, ptr);
                    ptr += write_size;
                    left_size -= write_size;
                }
            }
        }
    }

    if(buf)
        free(buf);
    //stop audio playback
    SDL_Delay(5000);
    SDL_CloseAudio();

    //free buffers
    av_free_packet(&packet);
    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);
    avformat_close_input(&pFormatCtx);

    return 0;
}

