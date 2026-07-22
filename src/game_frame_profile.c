#include "game_frame_profile_internal.h"

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double percentile(const double *sorted, int count, int numerator, int denominator)
{
    if (sorted == NULL || count <= 0 || numerator <= 0 || denominator <= 0)
        return 0.0;
    int rank = (numerator * count + denominator - 1) / denominator;
    rank = SDL_clamp(rank, 1, count);
    return sorted[rank - 1];
}

void slayer3d_game_frame_profile_reset(slayer3d_game_frame_profile *profile, double budget_ms)
{
    if (profile == NULL)
        return;
    SDL_zero(*profile);
    profile->budget_ms = budget_ms > 0.0 ? budget_ms : (1000.0 / 60.0);
}

void slayer3d_game_frame_profile_record(slayer3d_game_frame_profile *profile, slayer3d_game_frame_profile_sample sample)
{
    if (profile == NULL)
        return;

    if (profile->sample_count < SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY)
        profile->samples[profile->sample_count++] = sample;
    profile->frame_ms += sample.frame_ms;
    profile->poll_ms += sample.poll_ms;
    profile->tick_ms += sample.tick_ms;
    profile->render_ms += sample.render_ms;
    profile->present_ms += sample.present_ms;
    profile->ticks_count += sample.ticks;
    profile->frame_count++;

    if (profile->frame_count == 1 || sample.frame_ms > profile->slowest.frame_ms)
        profile->slowest = sample;
    if (sample.frame_ms > profile->budget_ms + 0.5)
        profile->missed_budget_count++;
    if (sample.frame_ms > (1000.0 / 30.0))
        profile->hitch_33ms_count++;
    if (sample.frame_ms > 50.0)
        profile->hitch_50ms_count++;
    if (sample.ticks > profile->max_ticks)
        profile->max_ticks = sample.ticks;
}

bool slayer3d_game_frame_profile_summarize(const slayer3d_game_frame_profile *profile,
                                           slayer3d_game_frame_profile_summary *summary)
{
    if (profile == NULL || summary == NULL || profile->frame_count <= 0 || profile->sample_count <= 0)
        return false;

    double frame_samples[SLAYER3D_GAME_FRAME_PROFILE_SAMPLE_CAPACITY];
    for (int i = 0; i < profile->sample_count; ++i)
        frame_samples[i] = profile->samples[i].frame_ms;
    SDL_qsort(frame_samples, (size_t)profile->sample_count, sizeof(frame_samples[0]), compare_double);

    SDL_zero(*summary);
    const double frames = (double)profile->frame_count;
    summary->slowest = profile->slowest;
    summary->frame_average_ms = profile->frame_ms / frames;
    summary->frame_p50_ms = percentile(frame_samples, profile->sample_count, 50, 100);
    summary->frame_p95_ms = percentile(frame_samples, profile->sample_count, 95, 100);
    summary->frame_p99_ms = percentile(frame_samples, profile->sample_count, 99, 100);
    summary->poll_average_ms = profile->poll_ms / frames;
    summary->tick_average_ms = profile->tick_ms / frames;
    summary->render_average_ms = profile->render_ms / frames;
    summary->present_average_ms = profile->present_ms / frames;
    summary->ticks_per_frame = (double)profile->ticks_count / frames;
    summary->budget_ms = profile->budget_ms;
    summary->frame_count = profile->frame_count;
    summary->sampled_frame_count = profile->sample_count;
    summary->missed_budget_count = profile->missed_budget_count;
    summary->hitch_33ms_count = profile->hitch_33ms_count;
    summary->hitch_50ms_count = profile->hitch_50ms_count;
    summary->max_ticks = profile->max_ticks;
    return true;
}
