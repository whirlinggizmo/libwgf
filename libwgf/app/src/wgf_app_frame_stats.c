#include <stdlib.h>
#include <string.h>

#include "wgf_app_priv.h"
#include "wgf_debug.h"

/* The frames' stats (wgf_debug.h): helix-raylib's Debug extra's GetFrameStats and its
 * recording, as stats read one at a time. The loop calls frame_counted at each frame's end
 * once a call here has set it, so a program that never asks links none of this. */

#define WINDOW 0.5          /* seconds: the last half second's frames */
#define WINDOW_MAX 4096     /* frames the window holds: its latest, at above 8,000 a second */
#define RECORDING_MAX 65536 /* frames a recording keeps: its latest */
#define STATS (WGF_FRAME_STAT_COST_SLOWEST + 1)

static struct {
    bool counting;
    /* the window: a ring of the latest frames */
    float times[WINDOW_MAX], costs[WINDOW_MAX];
    double ends[WINDOW_MAX];
    double now; /* the latest frame's end, on the stats' own clock: the frames' times summed */
    int next, held;
    long counted;      /* frames counted, for the window's stats kept a frame */
    long computed;     /* the frame count the window's stats are of; -1 for none */
    float stats[STATS];
    float scratch[WINDOW_MAX];
    /* the recording: allocated at its first start */
    bool recording;
    bool rec_begun; /* the frame the start was called in has ended: the first recorded is the next */
    float *rec_times, *rec_costs; /* rings while recording; oldest first once stopped */
    long rec_count;               /* frames recorded, more than kept past RECORDING_MAX */
    int rec_held;                 /* kept once stopped */
    float rec_stats[STATS];
} fs = {.computed = -1};

static int by_value(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* The least of the slowest `permille` of `sorted`'s `n` values, at least the slowest one. */
static float share(const float *sorted, int n, int permille)
{
    int k = (int)(((long)n * permille + 999) / 1000);
    if (k < 1) k = 1;
    return sorted[n - k];
}

/* `n` frames' stats into `out`, sorting `scratch` (room for n). */
static void compute(const float *times, const float *costs, int n, float *scratch, float *out)
{
    double total = 0.0, cost_total = 0.0;
    int i;
    memset(out, 0, sizeof(float) * STATS);
    if (n <= 0) return;
    for (i = 0; i < n; i++) {
        total += times[i];
        cost_total += costs[i];
    }
    out[WGF_FRAME_STAT_FRAMES] = (float)n;
    out[WGF_FRAME_STAT_FPS] = total > 0.0 ? (float)(n / total) : 0.0f;
    out[WGF_FRAME_STAT_TIME_MEAN] = (float)(total / n);
    out[WGF_FRAME_STAT_COST_MEAN] = (float)(cost_total / n);
    memcpy(scratch, times, sizeof(float) * (size_t)n);
    qsort(scratch, (size_t)n, sizeof(float), by_value);
    out[WGF_FRAME_STAT_TIME_MEDIAN] = share(scratch, n, 500);
    out[WGF_FRAME_STAT_TIME_P95] = share(scratch, n, 50);
    out[WGF_FRAME_STAT_TIME_P99] = share(scratch, n, 10);
    out[WGF_FRAME_STAT_TIME_SLOWEST] = scratch[n - 1];
    memcpy(scratch, costs, sizeof(float) * (size_t)n);
    qsort(scratch, (size_t)n, sizeof(float), by_value);
    out[WGF_FRAME_STAT_COST_P95] = share(scratch, n, 50);
    out[WGF_FRAME_STAT_COST_SLOWEST] = scratch[n - 1];
}

static void frame_counted(double interval, float cost)
{
    if (interval <= 0.0) return; /* the run's first frame: no frame before it */
    fs.now += interval;
    fs.times[fs.next] = (float)interval;
    fs.costs[fs.next] = cost;
    fs.ends[fs.next] = fs.now;
    fs.next = (fs.next + 1) % WINDOW_MAX;
    if (fs.held < WINDOW_MAX) fs.held++;
    fs.counted++;
    if (fs.recording && !fs.rec_begun) {
        fs.rec_begun = true;
    } else if (fs.recording) {
        const int at = (int)(fs.rec_count % RECORDING_MAX);
        fs.rec_times[at] = (float)interval;
        fs.rec_costs[at] = cost;
        fs.rec_count++;
    }
}

static void start_counting(void)
{
    if (fs.counting) return;
    fs.counting = true;
    wgf_app_priv_set_frame_counted(frame_counted);
}

float wgf_debug_get_frame_stat(wgf_frame_stat_t stat)
{
    start_counting();
    if ((int)stat < 0 || (int)stat >= STATS) return 0.0f;
    if (fs.computed != fs.counted) { /* the window's frames, oldest first, into a run */
        static float times[WINDOW_MAX], costs[WINDOW_MAX];
        int n = 0, i;
        for (i = 0; i < fs.held; i++) {
            const int at = (fs.next - fs.held + i + WINDOW_MAX) % WINDOW_MAX;
            if (fs.ends[at] < fs.now - WINDOW) continue;
            times[n] = fs.times[at];
            costs[n] = fs.costs[at];
            n++;
        }
        compute(times, costs, n, fs.scratch, fs.stats);
        fs.computed = fs.counted;
    }
    return fs.stats[stat];
}

void wgf_debug_start_frame_recording(void)
{
    start_counting();
    if (fs.rec_times == NULL) {
        fs.rec_times = malloc(sizeof(float) * RECORDING_MAX);
        fs.rec_costs = malloc(sizeof(float) * RECORDING_MAX);
        if (fs.rec_times == NULL || fs.rec_costs == NULL) {
            free(fs.rec_times);
            free(fs.rec_costs);
            fs.rec_times = fs.rec_costs = NULL;
            return;
        }
    }
    fs.recording = true;
    fs.rec_begun = false;
    fs.rec_count = 0;
    fs.rec_held = 0;
    memset(fs.rec_stats, 0, sizeof(fs.rec_stats));
}

/* The ring turned so its oldest is first. */
static void unroll(float *ring, int held, int oldest, float *scratch)
{
    int i;
    for (i = 0; i < held; i++) scratch[i] = ring[(oldest + i) % RECORDING_MAX];
    memcpy(ring, scratch, sizeof(float) * (size_t)held);
}

bool wgf_debug_stop_frame_recording(void)
{
    float *scratch;
    int oldest;
    if (!fs.recording) return false;
    fs.recording = false;
    fs.rec_held = fs.rec_count < RECORDING_MAX ? (int)fs.rec_count : RECORDING_MAX;
    oldest = fs.rec_count < RECORDING_MAX ? 0 : (int)(fs.rec_count % RECORDING_MAX);
    scratch = malloc(sizeof(float) * RECORDING_MAX);
    if (scratch == NULL) { /* nothing to sort with: the stats left 0, the frames left out */
        fs.rec_held = 0;
        return true;
    }
    unroll(fs.rec_times, fs.rec_held, oldest, scratch);
    unroll(fs.rec_costs, fs.rec_held, oldest, scratch);
    compute(fs.rec_times, fs.rec_costs, fs.rec_held, scratch, fs.rec_stats);
    free(scratch);
    return true;
}

bool wgf_debug_is_frame_recording(void)
{
    return fs.recording;
}

float wgf_debug_get_recorded_frame_stat(wgf_frame_stat_t stat)
{
    if (fs.recording || (int)stat < 0 || (int)stat >= STATS) return 0.0f;
    return fs.rec_stats[stat];
}

int wgf_debug_get_recorded_frame_times(float *out, int count, bool costs)
{
    const int n = fs.recording || out == NULL ? 0 : (count < fs.rec_held ? count : fs.rec_held);
    if (n > 0) memcpy(out, costs ? fs.rec_costs : fs.rec_times, sizeof(float) * (size_t)n);
    return n < 0 ? 0 : n;
}
