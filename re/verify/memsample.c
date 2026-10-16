// memsample <pid> <out> <seconds> [addr:len[:count:stride] ...]: sample the Wine process's memory (via
// /proc/<pid>/mem) about every 300 us and write a record whenever any watched range changed: t_us then
// the ranges in hex (a range with a count is read as count blocks of len bytes, stride apart, joined).
// The first range should be the engine's tick count: a record a tick tore is dropped.  T0_FILE=<file>
// gets the clock's start (CLOCK_MONOTONIC, microseconds).
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static long long now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000; }
int main(int argc, char** argv) {
    char path[64]; snprintf(path, sizeof path, "/proc/%s/mem", argv[1]);
    int fd = open(path, O_RDONLY); if (fd < 0) { perror("open"); return 1; }
    FILE* out = fopen(argv[2], "w");
    const double secs = atof(argv[3]);
    int n = argc - 4; unsigned long addr[64]; int len[64], cnt[64], stride[64]; int total = 0;
    for (int i = 0; i < n; ++i) {
        cnt[i] = 1; stride[i] = 0;
        sscanf(argv[4 + i], "%lx:%d:%d:%x", &addr[i], &len[i], &cnt[i], &stride[i]);
        total += len[i] * cnt[i];
    }
    static unsigned char cur[65536], last[65536]; memset(last, 0xee, sizeof last);
    static unsigned char buf[65536];
    const long long t0 = now_us();
    // T0_FILE: where to write t0 (CLOCK_MONOTONIC, microseconds), to set the records beside another clock
    if (getenv("T0_FILE")) { FILE* tf = fopen(getenv("T0_FILE"), "w"); if (tf) { fprintf(tf, "%lld\n", t0); fclose(tf); } }
    // NAP_US: the pause between reads (300 us; a sampler of the joypad alone can read far more often)
    const char* napEnv = getenv("NAP_US");
    struct timespec nap = {0, (napEnv ? atol(napEnv) : 300) * 1000L};
    long long samples = 0;
    for (;;) {
        const long long t = now_us() - t0;
        if (t > secs * 1e6) break;
        int off = 0, ok = 1;
        for (int i = 0; i < n; ++i) {
            if (cnt[i] == 1) { if (pread(fd, cur + off, len[i], addr[i]) != len[i]) ok = 0; off += len[i]; continue; }
            const int span = (cnt[i] - 1) * stride[i] + len[i];
            if (pread(fd, buf, span, addr[i]) != span) ok = 0;
            for (int k = 0; k < cnt[i]; ++k) { memcpy(cur + off, buf + k * stride[i], len[i]); off += len[i]; }
        }
        ++samples;
        // a tick that began while the ranges were read (the first range, the engine's tick count,
        // read again) tears the record: it is dropped and the memory read again at once
        unsigned char again[64];
        if (n > 0 && cnt[0] == 1 && len[0] <= 64) {
            if (pread(fd, again, len[0], addr[0]) != len[0] || memcmp(again, cur, len[0]) != 0) continue;
        }
        if (ok && memcmp(cur, last, total) != 0) {
            fprintf(out, "%lld", t); off = 0;
            for (int i = 0; i < n; ++i) { fputc(' ', out); for (int k = 0; k < len[i] * cnt[i]; ++k) fprintf(out, "%02x", cur[off + k]); off += len[i] * cnt[i]; }
            fputc('\n', out); memcpy(last, cur, total);
        }
        if (nap.tv_nsec) nanosleep(&nap, NULL);       // (0: read again at once)
    }
    fprintf(stderr, "%lld samples\n", samples);
    fclose(out); return 0;
}
