#include "Queue.h"

int packet_queue_init(PacketQueue *q, int max_packets, const char *name){
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond_putable = SDL_CreateCond();
    q->cond_getable = SDL_CreateCond();
    q->max_packets = max_packets;
    q->abort_request = 0;
    q->name = strdup(name);
    return 0;
}

int packet_queue_abort(PacketQueue *q){
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond_getable);
    SDL_CondSignal(q->cond_putable);
    SDL_UnlockMutex(q->mutex);
    return 0;
}


int packet_queue_uninit(PacketQueue *q){
    AVPacketList *pkt_node;

    for(; q->first_pkt;){
        pkt_node = q->first_pkt;
        q->first_pkt = q->first_pkt->next;
        av_free(pkt_node);
    }

    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond_putable);
    SDL_DestroyCond(q->cond_getable);
    memset(q, 0, sizeof(PacketQueue));
    return 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt){
    AVPacketList *pkt_node;

    if(q->abort_request)
        return -1;

    pkt_node = av_malloc(sizeof(AVPacketList));
    if(!pkt_node){
        return -1;
    }
    pkt_node->pkt = *pkt;
    pkt_node->next = NULL;

    SDL_LockMutex(q->mutex);
    if(q->nb_packets >= q->max_packets){
        //fprintf(stdout, "%s packet nb :%d\n", q->name, q->nb_packets);
        SDL_CondWait(q->cond_putable, q->mutex);
    }

    if(q->abort_request){ //if queue is waiting cond_putable, it will come into here
        SDL_UnlockMutex(q->mutex);
        return -1;
    }

    if(!q->last_pkt){
        q->first_pkt = pkt_node;
    }else{
        q->last_pkt->next = pkt_node;
    }
    q->last_pkt = pkt_node;
    q->nb_packets++;
    q->size += pkt_node->pkt.size;
    SDL_CondSignal(q->cond_getable);
    
    SDL_UnlockMutex(q->mutex);

    return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt){
    AVPacketList *pkt_node;

    if(q->abort_request)
        return -1;

    SDL_LockMutex(q->mutex);
    if(q->nb_packets <= 0){
        //fprintf(stdout, "%s packet nb is 0\n", q->name);
        SDL_CondWait(q->cond_getable, q->mutex);
    }
    
    if(q->abort_request){ //if queue is waiting cond_getable, it will come into here
        SDL_UnlockMutex(q->mutex);
        return -1;
    }

    pkt_node = q->first_pkt;
    q->first_pkt = q->first_pkt->next;
    q->nb_packets--;
    q->size -= pkt_node->pkt.size;
    if(!q->first_pkt){
        q->last_pkt = NULL;
    }
    *pkt = pkt_node->pkt;
    av_free(pkt_node);
    SDL_CondSignal(q->cond_putable);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

int packet_queue_nb_packets(PacketQueue *q){
    return q->nb_packets;
}


int frame_queue_init(FrameQueue *frameq, const char *name){
    int i;
    frameq->mutex = SDL_CreateMutex();
    frameq->writable_cond = SDL_CreateCond();
    frameq->readable_cond = SDL_CreateCond();
    frameq->max_nb = FRAME_QUEUE_NUMBER;
    frameq->name = strdup(name);
    frameq->write_index = 0;
    frameq->read_index = 0;
    frameq->nb = 0;
    frameq->abort_request = 0;
    for(i = 0; i < frameq->max_nb; i++)
        if(!(frameq->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

int frame_queue_uninit(FrameQueue *frameq){
    int i;

    for(i = 0; i < frameq->max_nb; i++)
        av_free(frameq->queue[i].frame);

    SDL_DestroyMutex(frameq->mutex);
    SDL_DestroyCond(frameq->writable_cond);
    SDL_DestroyCond(frameq->readable_cond);
    memset(frameq, 0, sizeof(FrameQueue));
    return 0;
}

int frame_queue_abort(FrameQueue *frameq){
    SDL_LockMutex(frameq->mutex);
    SDL_CondSignal(frameq->readable_cond);
    SDL_CondSignal(frameq->writable_cond);
    frameq->abort_request = 1;
    SDL_UnlockMutex(frameq->mutex);
    return 0;
}


int queue_frame(FrameQueue *frameq, FrameNode *fn){
    FrameNode *f;

    if(frameq->abort_request)
        return -1;

    SDL_LockMutex(frameq->mutex);
    if(frameq->nb >= frameq->max_nb)
        SDL_CondWait(frameq->writable_cond, frameq->mutex);
    
    if(frameq->abort_request){ //if queue is waiting cond_writable, it will come into here
        SDL_UnlockMutex(frameq->mutex);
        return -1;
    }

    f = &frameq->queue[frameq->write_index];
    av_frame_move_ref(f->frame, fn->frame);
    //av_frame_unref(fn->frame);
    frameq->write_index++;
    frameq->nb++;
    if(frameq->write_index == frameq->max_nb)
        frameq->write_index = 0;
    SDL_CondSignal(frameq->readable_cond);
    SDL_UnlockMutex(frameq->mutex);

    return 0;
}

int dequeue_frame(FrameQueue *frameq, FrameNode *fn){
    FrameNode *f;
    
    if(frameq->abort_request)
        return -1;

    SDL_LockMutex(frameq->mutex);
    if(frameq->nb <= 0)
        SDL_CondWait(frameq->readable_cond, frameq->mutex);
    
    if(frameq->abort_request){ //if queue is waiting cond_readable, it will come into here
        SDL_UnlockMutex(frameq->mutex);
        return -1;
    }

    f = &frameq->queue[frameq->read_index];
    av_frame_move_ref(fn->frame, f->frame);
    //av_frame_unref(f->frame);
    frameq->read_index++;
    frameq->nb--;
    if(frameq->read_index == frameq->max_nb)
        frameq->read_index = 0;
    SDL_CondSignal(frameq->writable_cond);
    SDL_UnlockMutex(frameq->mutex);

    return 0;
}

int frame_nb(FrameQueue *frameq){
    return frameq->nb;
}


void RB_Init(RingBuffer *rb, int len){
    rb->pHead = malloc(len);
    rb->rIndex = 0;
    rb->wIndex = 0;
    rb->len = len;
    rb->data_size = 0;
    rb->abort_request = 0;
    rb->cond = SDL_CreateCond();
    rb->mutex = SDL_CreateMutex();
}

void RB_Uninit(RingBuffer *rb){

    free(rb->pHead);
    
    SDL_DestroyCond(rb->cond);
    SDL_DestroyMutex(rb->mutex);
    memset(rb, 0, sizeof(RingBuffer));
}

int RB_abort(RingBuffer *rb){

    SDL_LockMutex(rb->mutex);
    SDL_CondSignal(rb->cond);
    rb->abort_request = 1;
    SDL_UnlockMutex(rb->mutex);
    return 0;
}

int RB_PushData(RingBuffer *rb, void *data, int size){
    int free_size;
    int write_size;
    int w2t;

    if(rb->abort_request)
        return -1;

    SDL_LockMutex(rb->mutex);
    free_size = rb->len-rb->data_size;
    write_size = free_size>size ? size : free_size;
    if(!write_size){
        SDL_CondWait(rb->cond, rb->mutex);
    }

    if(rb->abort_request){ // if RB is waiting cond it will come into here
        SDL_UnlockMutex(rb->mutex);
        return -1;
    }

    //fprintf(stdout, "wIndex=%d, free_size=%d, len=%d, data_size=%d\n", rb->wIndex, free_size, rb->len, rb->data_size);
    if(rb->wIndex+write_size<=rb->len){
        //fprintf(stdout, "pHead=0x%x, wIndex addr=0x%x, data=0x%x\n", rb->pHead, &(rb->pHead[rb->wIndex]), data);
        memcpy(rb->pHead+rb->wIndex, data, write_size);
    }else{
        w2t = rb->len-rb->wIndex;
        memcpy(rb->pHead+rb->wIndex, data, w2t);
        memcpy(rb->pHead, data+w2t, write_size-w2t);
    }
    rb->wIndex = (rb->wIndex+write_size)%rb->len;
    rb->data_size += write_size;
    SDL_UnlockMutex(rb->mutex);
    return write_size;
}

int RB_PullData(RingBuffer *rb, void *data, int size){
    int read_size;
    int r2t;

    if(rb->abort_request)
        return -1;

    SDL_LockMutex(rb->mutex);
    read_size = rb->data_size>size ? size : rb->data_size;
    if(!read_size){
        SDL_UnlockMutex(rb->mutex);
        return 0;
    }
    if(rb->rIndex+read_size<=rb->len){
        memcpy(data, rb->pHead+rb->rIndex, read_size);
    }else{
        r2t = rb->len-rb->rIndex;
        memcpy(data, rb->pHead+rb->rIndex, r2t);
        memcpy(data+r2t, rb->pHead, read_size-r2t);
    }
    rb->rIndex = (rb->rIndex+read_size)%rb->len;
    rb->data_size -= read_size;
    SDL_CondSignal(rb->cond);
    SDL_UnlockMutex(rb->mutex);
    return read_size;
}

