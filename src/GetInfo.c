#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define SAVE_FRAMES 50
#define START_FRAME 1
#define END_FRAME 400

typedef struct Codec{
    AVFormatContext *FCtx;
    AVCodecContext *CCtx;
    AVCodec *Codec;
    int stream;
    int type;
    int drained;
    double time_base;
}Codec;

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
        fprintf(stderr, "no video stream\n");
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

    if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
        fprintf(stderr, "open codec failed\n");
        return -1;
    }

    c->FCtx = pFormatCtx;
    c->CCtx = pCodecCtx;
    c->Codec = pCodec;
    c->stream = Stream;
    c->type = type;
    c->time_base = av_q2d(pFormatCtx->streams[Stream]->time_base);

    return 0;
}

static int decode_packet(Codec *c, AVPacket *pkt, AVFrame *frame, FILE *file){
    int ret;
    double pkt_pts, frame_pts;

    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(c->CCtx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting the packet to the decoder\n");
        exit(1);
    }

    /* read all the output frames (in general there may be any number of them */
    while (ret >= 0) {
        ret = avcodec_receive_frame(c->CCtx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        //to do
        pkt_pts = pkt->pts*c->time_base;
        frame_pts = frame->best_effort_timestamp*c->time_base;
        //fprintf(stdout, "%d\t %s pts = %d\n", VCodec.CCtx->frame_number, media_name, frame->pts);
        fprintf(file, "%4d\t%10lf\t%10lf\n", c->CCtx->frame_number, pkt_pts, frame_pts);

        av_frame_unref(frame);
    }
    return 1;
}

static int get_frame(Codec *c, AVPacket *pkt, AVFrame *frame, int *packet_consumed){
    int ret;

    do{
        ret = avcodec_receive_frame(c->CCtx, frame);
        if(ret==0)
            return 1;
        else if(ret==AVERROR(EAGAIN)){
            avcodec_send_packet(c->CCtx, pkt);
            *packet_consumed = 1;
        }else if(ret==AVERROR_EOF){
            return 0;
            *packet_consumed = 1;
        }else{
            fprintf(stdout, "receive error, return %d\n", ret);
            return -1;
        }
    }while(1);
    return 1;
}

int main(int argc, char *argv[]){
    AVFormatContext *pFormatCtx = NULL;
    Codec VCodec, ACodec, *c;
    AVPacket *packet;
    AVFrame *frame;
    FILE *vfile=NULL, *afile=NULL, *file=NULL;
    int i;
    
    if(argc != 2){
        fprintf(stderr, "Usage: GetInfo mediafile\n");
        return -1;
    }

    pFormatCtx = avformat_alloc_context();

    //Open and get stream info
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0){
        fprintf(stderr, "open input failed\n");
        return -1;
    }
    
    if(avformat_find_stream_info(pFormatCtx, NULL)<0){
        fprintf(stderr, "find stream info failed\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);
    
    fprintf(stdout, "chapter number = %d\n", pFormatCtx->nb_chapters);
    for(i=0; i<pFormatCtx->nb_chapters; i++){
        AVChapter *chapter = pFormatCtx->chapters[i];
        double start = chapter->start * av_q2d(chapter->time_base);
        double end   = chapter->end   * av_q2d(chapter->time_base);
        fprintf(stdout, "[%d] start = %lf, end = %lf\n", chapter->id, start, end);
    }

    CodecInit(AVMEDIA_TYPE_VIDEO, pFormatCtx, &VCodec);
    CodecInit(AVMEDIA_TYPE_AUDIO, pFormatCtx, &ACodec);

    frame = av_frame_alloc();
    packet = av_packet_alloc();

    vfile = fopen("video.pts", "wb+");
    afile = fopen("audio.pts", "wb+");
    /*
    //Read from stream into packet
    while(av_read_frame(pFormatCtx, packet)>=0){
        //deal with the video stream of the type "videoStream" and audioStream
        if(packet->stream_index==VCodec.stream){
            c = &VCodec;
            file = vfile;
        }else if(packet->stream_index==ACodec.stream){
            c = &ACodec;
            file = afile;
        }else{
            av_packet_unref(packet);
            continue;
        }
        //fprintf(stdout, "packet size = %d\n", packet->size);
        decode_packet(c, packet, frame, file);
        av_packet_unref(packet);
    }*/

    //free buffers
    av_free(frame);
    av_free(packet);
    avcodec_close(VCodec.CCtx);
    avcodec_close(ACodec.CCtx);
    avformat_close_input(&pFormatCtx);
    fclose(vfile);
    fclose(afile);
    return 0;
}
