#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define SAVE_FRAMES 2000
const char *codec_type ,*codec_nane;
char channel_layout[20] = {0};

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
    int itr = 0;
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
    channel0 = (float *)pFrame->extended_data[0];
    channel1 = (float *)pFrame->extended_data[1];
    
    //normal PCM is mixed(interleave) track, but fltp "p" means planar
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

    //Read from stream into packet
    i = 0;
    while(av_read_frame(pFormatCtx, &packet)>=0){
        //Only deal with the video stream of the type "videoStream"
        if(packet.stream_index==AudioStream){
            //Decode video frame
            avcodec_decode_audio4(pCodecCtx, pFrame, &frameFinished, &packet);
            if(frameFinished){
                if(++i<=SAVE_FRAMES){
                    size = av_samples_get_buffer_size(NULL, pCodecCtx->channels, pFrame->nb_samples, pCodecCtx->sample_fmt, 1);
                    SaveFrame2PCM(pFrame, size, i);
                }else{
                    break;
                }
            }
        }
    }


    //free buffers
    av_free_packet(&packet);
    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);
    avformat_close_input(&pFormatCtx);

    return 0;
}
