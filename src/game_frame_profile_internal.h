#ifndef SLAYER3D_GAME_FRAME_PROFILE_INTERNAL_H
#define SLAYER3D_GAME_FRAME_PROFILE_INTERNAL_H

#include <stdbool.h>

#define SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY 2048

typedef struct slayer3d_game_frame_profile_sample
{
    double frame_ms;
    double poll_ms;
    double tick_ms;
    double render_ms;
    double present_ms;
    int ticks;
} slayer3d_game_frame_profile_sample;

typedef struct slayer3d_game_frame_profile
{
    slayer3d_game_frame_profile_sample samples[SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY];
    slayer3d_game_frame_profile_sample slowest;
    double frame_ms;
    double poll_ms;
    double tick_ms;
    double render_ms;
    double present_ms;
    double budget_ms;
    int frame_count;
    int sample_count;
    int missed_budget_count;
    int hitch_33ms_count;
    int hitch_50ms_count;
    int ticks_count;
    int max_ticks;
} slayer3d_game_frame_profile;

typedef struct slayer3d_game_frame_profile_summary
{
    slayer3d_game_frame_profile_sample slowest;
    double frame_average_ms;
    double frame_p50_ms;
    double frame_p95_ms;
    double frame_p99_ms;
    double poll_average_ms;
    double tick_average_ms;
    double render_average_ms;
    double present_average_ms;
    double ticks_per_frame;
    double budget_ms;
    int frame_count;
    int sampled_frame_count;
    int missed_budget_count;
    int hitch_33ms_count;
    int hitch_50ms_count;
    int max_ticks;
} slayer3d_game_frame_profile_summary;

void slayer3d_game_frame_profile_reset(slayer3d_game_frame_profile *profile, double budget_ms);
void slayer3d_game_frame_profile_record(slayer3d_game_frame_profile *profile,
                                        slayer3d_game_frame_profile_sample sample);
bool slayer3d_game_frame_profile_summarize(const slayer3d_game_frame_profile *profile,
                                           slayer3d_game_frame_profile_summary *summary);

#endif
