#ifndef __INCLUDED_QUEUE_H__
#define __INCLUDED_QUEUE_H__
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define FRAME_QUEUE_NUMBER 20

typedef struct PacketQueue{
    AVPacketList *first_pkt;
    AVPacketList *last_pkt;
    int nb_packets;
    int max_packets;
    int size;
    char * name;
    int abort_request;
    SDL_cond *cond_putable;
    SDL_cond *cond_getable;
    SDL_mutex *mutex;
}PacketQueue;

typedef struct FrameNode{
    AVFrame *frame;
}FrameNode;

typedef struct FrameQueue{
    FrameNode queue[FRAME_QUEUE_NUMBER];
    int read_index;
    int write_index;
    int nb;
    int max_nb;
    char *name;
    int abort_request;
    SDL_cond *writable_cond;
    SDL_cond *readable_cond;
    SDL_mutex *mutex;
}FrameQueue;

typedef struct RingBuffer{
    void *pHead;
    int rIndex;
    int wIndex;
    int len;
    int data_size;
    int abort_request;
    SDL_cond *cond;
    SDL_mutex *mutex;
}RingBuffer;

int packet_queue_init(PacketQueue *q, int max_packets, const char *name);
int packet_queue_uninit(PacketQueue *q);
int packet_queue_abort(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
int packet_queue_get(PacketQueue *q, AVPacket *pkt);
int packet_queue_nb_packets(PacketQueue *q);

int frame_queue_init(FrameQueue *frameq, const char *name);
int frame_queue_uninit(FrameQueue *frameq);
int frame_queue_abort(FrameQueue *frameq);
int queue_frame(FrameQueue *frameq, FrameNode *fn);
int dequeue_frame(FrameQueue *frameq, FrameNode *fn);
int frame_nb(FrameQueue *frameq);

void RB_Init(RingBuffer *rb, int len);
void RB_Uninit(RingBuffer *rb);
int RB_abort(RingBuffer *rb);
int RB_PushData(RingBuffer *rb, void *data, int size);
int RB_PullData(RingBuffer *rb, void *data, int size);

#endif
