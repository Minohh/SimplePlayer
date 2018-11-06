#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define SAVE_FRAMES 50
#define START_FRAME 1
#define END_FRAME 400

void SaveFrame2YUV(AVFrame *pFrame, int width, int height, int iFrame){
    static FILE *pFile;
    char szFilename[32];
    int y;

    //Open file
    if(iFrame==START_FRAME){
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
    if(iFrame==END_FRAME){
        fclose(pFile);
    }
}

int main(int argc, char *argv[]){
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec = NULL;
    AVPacket *pPacket;
    AVFrame *pFrame = NULL;
    int i = 0, videoStream = -1, frameFinished;

    //Register all codecs and formats
    //av_register_all();

    //Open and get stream info
    pFormatCtx = avformat_alloc_context();
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
    pPacket = av_packet_alloc();
    pFrame = av_frame_alloc();
    if(pFrame == NULL||pPacket == NULL){
        fprintf(stderr, "cannot get buffer of frame or packet\n");
        return -1;
    }

    //Read from stream into packet
    i = 0;
    while(av_read_frame(pFormatCtx, pPacket)>=0){
        //Only deal with the video stream of the type "videoStream"
        if(pPacket->stream_index==videoStream){
            //Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, pPacket);
            //fprintf(stdout, "Frame : %d ,pts=%lld, timebase=%lf\n", i, pFrame->pts, av_q2d(pFormatCtx->streams[videoStream]->time_base));
            if(frameFinished){
                if(i>=START_FRAME && i<=END_FRAME){
                    SaveFrame2YUV(pFrame, pCodecCtx->width, pCodecCtx->height, i);
                    i++;
                }else{
                    i++;
                    continue;
                }
            }
        }
        av_packet_unref(pPacket);
    }


    //free buffers
    av_free(pPacket);
    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}
