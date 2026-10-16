// xgrab <seconds> <out> [interval-ms] [x y w h]: the virtual display's game area (320x224 at 144,159
// by default, where the original's window puts it) grabbed every interval (50 ms by default) for
// that long or until a SIGTERM.  Each picture that differs from the one before goes to <out> (RGB24,
// one after the other; through gzip when the name ends in .gz) and every grab to <out>.times: the
// picture's number and the times the grab was asked for and came back, between which the server
// read the screen (CLOCK_MONOTONIC, microseconds: memsample's clock, see its T0_FILE), for
// framepred.py.  Build: gcc -O2 -o xgrab xgrab.c -lX11 -lXext
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;
static void onTerm(int sig) { (void)sig; stop = 1; }
static long long now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000; }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "xgrab <seconds> <out> [interval-ms] [x y w h]\n"); return 2; }
    const double secs = atof(argv[1]);
    const char* out = argv[2];
    const long long interval = (argc > 3 ? atoi(argv[3]) : 50) * 1000LL;
    const int x0 = argc > 7 ? atoi(argv[4]) : 144, y0 = argc > 7 ? atoi(argv[5]) : 159;
    const int w = argc > 7 ? atoi(argv[6]) : 320, h = argc > 7 ? atoi(argv[7]) : 224;
    Display* d = NULL;
    for (int i = 0; i < 50 && !(d = XOpenDisplay(NULL)); ++i) usleep(100000);   // (the server may be starting)
    if (!d) { fprintf(stderr, "xgrab: no display\n"); return 1; }
    const int scr = DefaultScreen(d);
    Window root = RootWindow(d, scr);
    XShmSegmentInfo shm;
    XImage* img = XShmCreateImage(d, DefaultVisual(d, scr), DefaultDepth(d, scr), ZPixmap, NULL, &shm, w, h);
    if (!img) { fprintf(stderr, "xgrab: no XShm\n"); return 1; }
    shm.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0600);
    shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
    shm.readOnly = False;
    XShmAttach(d, &shm);
    XSync(d, False);
    shmctl(shm.shmid, IPC_RMID, NULL);                   // (gone once both have let go of it)
    const unsigned long rm = img->red_mask, gm = img->green_mask, bm = img->blue_mask;
    int rs = 0, gs = 0, bs = 0;
    while (rm && !((rm >> rs) & 1)) ++rs;
    while (gm && !((gm >> gs) & 1)) ++gs;
    while (bm && !((bm >> bs) & 1)) ++bs;
    const size_t n = (size_t)w * h * 3;
    unsigned char* cur = malloc(n);
    unsigned char* last = calloc(n, 1);
    FILE* f;
    const size_t len = strlen(out);
    int piped = len > 3 && !strcmp(out + len - 3, ".gz");
    if (piped) {
        char cmd[4200];
        snprintf(cmd, sizeof cmd, "gzip -1 > '%s'", out);
        f = popen(cmd, "w");
    } else f = fopen(out, "wb");
    char tn[4200];
    snprintf(tn, sizeof tn, "%s.times", out);
    FILE* tf = fopen(tn, "w");
    if (!f || !tf) { fprintf(stderr, "xgrab: cannot write %s\n", out); return 1; }
    signal(SIGTERM, onTerm);                             // (a kill ends it as its time would)
    signal(SIGINT, onTerm);
    const long long t0 = now_us(), end = t0 + (long long)(secs * 1e6);
    long long next = t0;
    long pictures = -1;
    while (!stop && now_us() < end) {
        const long long a = now_us();
        if (!XShmGetImage(d, root, img, x0, y0, AllPlanes)) break;
        const long long b = now_us();
        unsigned char* p = cur;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned long px = XGetPixel(img, x, y);
                *p++ = (unsigned char)((px & rm) >> rs);
                *p++ = (unsigned char)((px & gm) >> gs);
                *p++ = (unsigned char)((px & bm) >> bs);
            }
        if (pictures < 0 || memcmp(cur, last, n) != 0) {
            fwrite(cur, 1, n, f);
            unsigned char* t = last; last = cur; cur = t;
            ++pictures;
        }
        fprintf(tf, "%ld %lld %lld\n", pictures, a, b);     // (the server read it between the two)
        next += interval;
        const long long wait = next - now_us();
        if (wait > 0) usleep((useconds_t)wait);
        else next = now_us();                                // (behind: from now on)
    }
    fclose(tf);
    if (piped) pclose(f); else fclose(f);
    XShmDetach(d, &shm);
    shmdt(shm.shmaddr);
    XCloseDisplay(d);
    return 0;
}
