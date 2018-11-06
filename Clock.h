#ifndef __INCLUDED_CLOCK_H__
#define __INCLUDED_CLOCK_H__

/* 
 * pts_drift       = pts - set_time
 *
 * cur_time_on_pts = pts + av_gettime_relative() - set_time 
 *                 = pts - set_time + av_gettime_relative()
 *                 = pts_drift + av_gettime_relative()
 *
 * av_delay        = cur_video_time_on_pts - cur_audio_time_on_pts 
 *                 = (video_pts_drift + av_gettime_relative()) - (audio_pts_drift + av_gettime_relative())
 *                 = video_pts_drift - audio_pts_drift
 * */
typedef struct SyncClock{
    int64_t audio_pts;
    int64_t video_pts;
    int64_t audio_pts_drift;
    int64_t video_pts_drift;
    int64_t audio_set_time;
    int64_t video_set_time;
    int64_t acceptable_delay;
}SyncClock;

int set_audio_pts(SyncClock *sc, int64_t pts);
int set_video_pts(SyncClock *sc, int64_t pts);
int64_t get_audio_pts(SyncClock *sc);
int64_t get_video_pts(SyncClock *sc);
int64_t get_audio_clock(SyncClock *sc);
int64_t get_video_clock(SyncClock *sc);
int64_t adjust_delay(SyncClock *sc, int64_t delay);
int set_acceptable_delay(SyncClock *sc, int64_t acceptable_delay);
#endif
