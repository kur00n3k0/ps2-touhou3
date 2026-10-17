/* Render one PMD (.M) song to a 16-bit stereo WAV containing intro + one loop,
 * with a sample-exact loop point.
 *
 * Writes <out>.loop with "<loop_start_sample> <total_samples> <match>":
 * the player plays [0, total) and then jumps back to loop_start forever.
 * loop_start is -1 for songs that do not loop (jingles). <match> is how well
 * the audio after the loop end matches the audio at loop_start (1.0 = identical).
 *
 * How the loop is found: PMD counts completed loops. We render past two loop
 * ends and note the sample where the counter changes each time (E1, E2).
 * Loop length L = E2 - E1, loop start = E1 - L. A small search around that
 * point then corrects the timing jitter of the counter (it only updates once
 * per sound driver tick).
 *
 * usage: pmd2wav <song.M> <out.wav> [rate=44100] [pcmdir]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pmdmini.h"

extern int pmd_length, pmd_loop;   /* milliseconds; set by pmd_play() */
int getloopcount(void);             /* from PMDWin */
_Bool loadrhythmsample(char *path); /* from PMDWin */

enum { STEP = 32 };                 /* frames rendered per call while watching the counter */
enum { MAX_SECONDS = 600 };         /* longest render when channel loops take a while to line up */

/* ---- Per-channel loop tracking (hooks patched into PMDWin's pmdwin.cpp) ----
 * PMD loops each channel ("part") independently. When channel loop lengths
 * differ, the song only truly repeats after all of them line up again. */
extern long pmd2wav_tick;           /* sequencer ticks since playback start */
enum { MAX_PARTS = 64, MAX_HITS = 4 };
static const void *part_id[MAX_PARTS];
static long part_hit[MAX_PARTS][MAX_HITS];
static int part_nhits[MAX_PARTS], nparts;

void pmd2wav_part_looped(void *part)
{
    int i;
    for (i = 0; i < nparts && part_id[i] != part; i++) {}
    if (i == nparts) {
        if (nparts == MAX_PARTS) return;
        part_id[nparts++] = part;
    }
    if (part_nhits[i] < MAX_HITS) part_hit[i][part_nhits[i]++] = pmd2wav_tick;
}

/* Has every channel that loops at all looped at least k times? */
static int parts_done(int k)
{
    for (int i = 0; i < nparts; i++)
        if (part_nhits[i] < k) return 0;
    return nparts > 0;
}

static long gcd(long a, long b) { while (b) { long t = a % b; a = b; b = t; } return a; }

/* Song period in ticks: P = lcm of all channel loop lengths, repeating from
 * T0 = the tick at which the last channel first entered its loop.
 * Returns 0 if the channels' loop lengths are not consistent. */
static int song_period(long *T0, long *P)
{
    long t0 = 0, p = 1;
    if (nparts == 0) return 0;
    for (int i = 0; i < nparts; i++) {
        if (part_nhits[i] < 2) return 0;
        long len = part_hit[i][1] - part_hit[i][0];
        if (len <= 0) return 0;
        for (int h = 2; h < part_nhits[i]; h++)
            if (part_hit[i][h] - part_hit[i][h - 1] != len) return 0;
        long start = part_hit[i][0] - len;   /* tick it first entered its loop body */
        if (start > t0) t0 = start;
        p = p / gcd(p, len) * len;
        if (p > 1000000) return 0;           /* channel loops never line up in practice */
    }
    *T0 = t0;
    *P = p;
    return 1;
}

/* First sample (on a STEP boundary) at or after the given tick, or -1. */
static long sample_at_tick(const long *tick_at, long n, long tick)
{
    for (long i = 0; i <= n / STEP; i++)
        if (tick_at[i] >= tick) return i * STEP;
    return -1;
}

static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

/* Running sum of squared samples, so any window's loudness is O(1). */
static double *energy_prefix(const short *p, long n)
{
    double *s = malloc((size_t)(n + 1) * sizeof *s);
    s[0] = 0;
    for (long i = 0; i < n; i++)
        s[i + 1] = s[i] + (double)p[i * 2] * p[i * 2] + (double)p[i * 2 + 1] * p[i * 2 + 1];
    return s;
}

/* Correlation (-1..1) of the loudness curves (w-frame windows, 8 s) after
 * frame a and after frame b. FM phases differ between loop passes, so the
 * samples never repeat exactly, but the same music has the same loudness curve. */
static double envelope_match(const double *s, long n, long a, long b, long w, int rate)
{
    long frames = 8L * rate / w;
    long lim = n - (a > b ? a : b);
    if (frames * w > lim) frames = lim / w;
    if (frames < 10) return 0;
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (long f = 0; f < frames; f++) {
        long i = a + f * w, j = b + f * w;
        double x = sqrt(s[i + w] - s[i]), y = sqrt(s[j + w] - s[j]);
        sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y;
    }
    double cov = sab - sa * sb / frames;
    double va = saa - sa * sa / frames, vb = sbb - sb * sb / frames;
    return (va > 0 && vb > 0) ? cov / sqrt(va * vb) : 0;
}

/* Best loop length near L0: the L where the music after frame t lines up with
 * the music after t - L. Coarse search in 1 ms steps, then sample-exact. */
static long find_loop_length(const double *s, long n, long t, long L0, long R, int rate, double *match)
{
    long w = rate / 1000, best = L0;
    double bm = -2;
    for (long L = L0 - R; L <= L0 + R; L += w) {
        if (t - L < 0) continue;
        double m = envelope_match(s, n, t, t - L, w, rate);
        if (m > bm) { bm = m; best = L; }
    }
    long coarse = best;
    for (long L = coarse - w; L <= coarse + w; L++) {
        if (t - L < 0) continue;
        double m = envelope_match(s, n, t, t - L, w, rate);
        if (m > bm) { bm = m; best = L; }
    }
    *match = bm;
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s song.M out.wav [rate] [pcmdir]\n", argv[0]);
        return 2;
    }
    int rate = argc > 3 ? atoi(argv[3]) : 44100;
    char pcmdir[4096] = "";
    if (argc > 4) snprintf(pcmdir, sizeof pcmdir, "%s", argv[4]);

    pmd_init();
    /* pmd_init() only looks for the YM2608 drum samples (2608_*.WAV) in the
     * current directory; load them from pcmdir explicitly. */
    if (pcmdir[0] && !loadrhythmsample(pcmdir)) {
        fprintf(stderr, "could not load YM2608 drum samples from %s\n", pcmdir);
        return 1;
    }
    if (!pmd_is_pmd(argv[1])) { fprintf(stderr, "%s: not a PMD file\n", argv[1]); return 1; }
    pmd_setrate(rate);
    pmd_play(argv[1], pcmdir);
    if (pmd_length <= 0) { fprintf(stderr, "%s: could not determine length\n", argv[1]); return 1; }

    /* Render long enough to see every channel loop at least twice, recording
     * the driver tick of each channel loop and which tick each sample is at. */
    long est = (long)((long long)(pmd_length + pmd_loop + 10000) * rate / 1000);
    long cap = est > (long)MAX_SECONDS * rate ? est : (long)MAX_SECONDS * rate;
    short *pcm = malloc((size_t)cap * 4);
    long *tick_at = malloc((size_t)(cap / STEP + 1) * sizeof *tick_at);  /* tick at each STEP boundary */
    long ends[2] = { -1, -1 }, n = 0;
    int seen = getloopcount();
    pmd2wav_tick = 0;
    while (n + STEP <= cap) {
        tick_at[n / STEP] = pmd2wav_tick;
        pmd_renderer(pcm + n * 2, STEP);
        n += STEP;
        int c = getloopcount();
        if (c != seen) {
            if (ends[0] < 0) ends[0] = n;
            else if (ends[1] < 0) ends[1] = n;
            seen = c;
        }
        if (pmd_loop <= 0 && n >= (long long)pmd_length * rate / 1000) break;
        /* Stop once past 2 counter loops and every channel has looped 3 times
         * (or we hit the cap). */
        if (ends[1] > 0 && n >= est && parts_done(3)) break;
    }
    tick_at[n / STEP] = pmd2wav_tick;
    pmd_stop();
    if (getenv("PMD2WAV_RAW")) { FILE *r = fopen(getenv("PMD2WAV_RAW"), "wb"); fwrite(pcm, 4, n, r); fclose(r); }
    if (getenv("PMD2WAV_DEBUG"))
        fprintf(stderr, "length=%dms loop=%dms ends=%ld,%ld rendered=%ld\n", pmd_length, pmd_loop, ends[0], ends[1], n);

    long total, loop_start = -1;
    double match = -1;
    if (pmd_loop > 0) {
        /* Loop length: the distance between two loop-counter changes. Checked
         * against a loudness-curve search, it agrees within a few samples on
         * most songs; PMD's own millisecond estimate runs ~0.14% long.
         * Fall back to the search only if the counter never advanced twice. */
        long L0 = (long)((long long)pmd_loop * rate / 1000);
        long X = rate / 100;          /* crossfade length, see below */
        double *s = energy_prefix(pcm, n);
        long L, t;
        long T0, P;
        const char *method = "counter";
        if (song_period(&T0, &P) && sample_at_tick(tick_at, n, T0 + P) > 0) {
            /* Exact: every channel's state repeats P ticks after T0. */
            long a = sample_at_tick(tick_at, n, T0), b = sample_at_tick(tick_at, n, T0 + P);
            t = b;
            L = b - a;
            method = "channels";
        } else if (ends[1] > 0) {
            L = ends[1] - ends[0];
            t = ends[0];
        } else {
            long R = rate * 3 / 10;
            t = (long)((long long)pmd_length * rate / 1000);
            if (t < L0 + R) t = L0 + R;
            L = find_loop_length(s, n, t, L0, R, rate, &match);
        }
        /* The cut point t can be anywhere after the loop has started once;
         * loops that start right at 0 need t moved so t - L leaves room for
         * the crossfade. */
        if (t - L < X) t = L + X;
        match = envelope_match(s, n, t, t - L, rate / 100, rate);
        free(s);
        total = t;
        loop_start = t - L;
        if (getenv("PMD2WAV_DEBUG"))
            fprintf(stderr, "  loop length: PMD estimate %ld, counter %ld, used %ld frames (%s)\n",
                    L0, ends[1] > 0 ? ends[1] - ends[0] : -1, L, method);
        if (getenv("PMD2WAV_DEBUG")) {
            for (int i = 0; i < nparts; i++) {
                fprintf(stderr, "    part %2d loops at ticks", i);
                for (int h = 0; h < part_nhits[i]; h++) fprintf(stderr, " %ld", part_hit[i][h]);
                fprintf(stderr, "\n");
            }
        }

        /* Bake a short crossfade into the end of the file: its last X frames
         * fade into the X frames that lead up to loop_start in the first pass.
         * After the jump, playback continues at loop_start, so the seam is
         * continuous even though FM synthesis never repeats bit-exactly. */
        for (long i = 0; i < X; i++) {
            double fade = (double)(i + 1) / (X + 1);
            for (int ch = 0; ch < 2; ch++) {
                long a = (total - X + i) * 2 + ch, b = (loop_start - X + i) * 2 + ch;
                pcm[a] = (short)lrint(pcm[a] * (1 - fade) + pcm[b] * fade);
            }
        }
    } else {
        total = (long)((long long)pmd_length * rate / 1000);
        if (total > n) total = n;
    }

    FILE *f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    unsigned bytes = (unsigned)total * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(pcm, 4, total, f);
    fclose(f);
    free(pcm);

    char lp[4200];
    snprintf(lp, sizeof lp, "%s.loop", argv[2]);
    FILE *l = fopen(lp, "w");
    if (l) { fprintf(l, "%ld %ld %.4f\n", loop_start, total, match); fclose(l); }

    printf("%-14s %6.1fs", argv[1], (double)total / rate);
    if (loop_start >= 0) printf("  loop from %6.2fs  match %.4f\n", (double)loop_start / rate, match);
    else printf("  no loop\n");
    return 0;
}
