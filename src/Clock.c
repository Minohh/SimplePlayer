#include <stdio.h>
#include <libavutil/time.h>
#include "Clock.h"

int set_audio_pts(SyncClock *sc, int64_t pts){
    sc->audio_pts = pts;
    sc->audio_set_time = av_gettime_relative();
    sc->audio_pts_drift = sc->audio_pts - sc->audio_set_time;
    return 0;
}

int set_video_pts(SyncClock *sc, int64_t pts){
    sc->video_pts = pts;
    sc->video_set_time = av_gettime_relative();
    sc->video_pts_drift = sc->video_pts - sc->video_set_time;
    return 0;
}

inline int64_t get_audio_pts(SyncClock *sc){
    return sc->audio_pts;
}

inline int64_t get_video_pts(SyncClock *sc){
    return sc->video_pts;
}

inline int64_t get_audio_clock(SyncClock *sc){
    return sc->audio_pts_drift + av_gettime_relative();
}

inline int64_t get_video_clock(SyncClock *sc){
    return sc->video_pts_drift + av_gettime_relative();
}

int64_t adjust_delay(SyncClock *sc, int64_t delay){
    int64_t av_delay = sc->video_pts_drift - sc->audio_pts_drift;
    if(av_delay<sc->acceptable_delay && av_delay>-sc->acceptable_delay)
        return delay;
    else if(av_delay >= sc->acceptable_delay)
        return 2*delay;
    else //if(av_delay <= -sc->acceptable_delay)
        return 0;
}

inline int set_acceptable_delay(SyncClock *sc, int64_t acceptable_delay){
    sc->acceptable_delay = acceptable_delay;
    return 0;
}
