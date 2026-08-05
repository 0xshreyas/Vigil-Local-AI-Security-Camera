#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <vfw.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* Tuning knobs */
#define PIXEL_THRESHOLD  25    /* brightness change to call a pixel "changed" */
#define MOTION_PERCENT   2.0   /* % of changed pixels to call it "motion" */
#define SAMPLE_STEP      4     /* check every Nth pixel (speed vs accuracy) */

/* Globals for the callback */
static unsigned char *g_jpeg = NULL;
static long  g_buffer_size = 0;
static long  g_jpeg_size   = 0;
static int   g_got_frame   = 0;

static LRESULT CALLBACK frame_callback(HWND hwnd, LPVIDEOHDR hdr) {
    (void)hwnd;
    if (hdr == NULL || hdr->lpData == NULL) return 0;

    if (g_jpeg != NULL && hdr->dwBytesUsed <= (DWORD)g_buffer_size) {
        memcpy(g_jpeg, hdr->lpData, hdr->dwBytesUsed);
        g_jpeg_size = hdr->dwBytesUsed;
        g_got_frame = 1;
    }
    return 0;
}

/* Brightness of pixel i in an RGB array: average of the three channels. */
static int pixel_brightness(const unsigned char *pixels, long i) {
    long o = i * 3;
    return (pixels[o] + pixels[o + 1] + pixels[o + 2]) / 3;
}

/* Compares two frames of the same size.
   Returns the percentage of sampled pixels that changed. */
static double compare_frames(const unsigned char *prev,
                             const unsigned char *curr,
                             int width, int height) {
    long total_pixels = (long)width * height;
    long checked = 0;
    long changed = 0;

    for (long i = 0; i < total_pixels; i += SAMPLE_STEP) {
        int b1 = pixel_brightness(prev, i);
        int b2 = pixel_brightness(curr, i);

        int diff = b1 - b2;
        if (diff < 0) diff = -diff;        /* absolute value */

        if (diff > PIXEL_THRESHOLD) changed++;
        checked++;
    }

    if (checked == 0) return 0.0;
    return (100.0 * changed) / checked;
}

/* Grabs one frame and decodes it. Returns pixel buffer (free with
   stbi_image_free) or NULL. Writes dimensions into *w and *h. */
static unsigned char *grab_and_decode(HWND hwnd, int *w, int *h) {
    g_got_frame = 0;
    if (!capGrabFrameNoStop(hwnd)) return NULL;

    int waited = 0;
    while (!g_got_frame && waited < 2000) {
        Sleep(1);
        waited++;
    }
    if (!g_got_frame) return NULL;

    int channels = 0;
    return stbi_load_from_memory(g_jpeg, (int)g_jpeg_size,
                                 w, h, &channels, 3);
}

int main(void) {
    /* --- Setup --- */
    HWND hwnd = capCreateCaptureWindowA("cap", WS_POPUP, 0, 0, 640, 480, NULL, 0);
    if (hwnd == NULL) {
        printf("capCreateCaptureWindow failed\n");
        return 1;
    }

    if (!capDriverConnect(hwnd, 0)) {
        printf("capDriverConnect failed\n");
        DestroyWindow(hwnd);
        return 1;
    }

    g_buffer_size = 4 * 1024 * 1024;
    g_jpeg = malloc(g_buffer_size);
    if (g_jpeg == NULL) {
        printf("Out of memory\n");
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }

    if (!capSetCallbackOnFrame(hwnd, frame_callback)) {
        printf("capSetCallbackOnFrame failed\n");
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }

    printf("Motion detector running. Press Ctrl+C to stop.\n");
    printf("Settings: pixel threshold %d, motion threshold %.1f%%, sampling 1 in %d\n\n",
           PIXEL_THRESHOLD, MOTION_PERCENT, SAMPLE_STEP);

    /* --- First frame becomes our reference --- */
    int width = 0, height = 0;
    unsigned char *prev = grab_and_decode(hwnd, &width, &height);
    if (prev == NULL) {
        printf("Could not get first frame\n");
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }
    printf("Reference frame: %dx%d\n\n", width, height);

    /* --- Main loop --- */
    int frame_number = 0;

    for (;;) {
        Sleep(200);                        /* ~5 checks per second */

        int w = 0, h = 0;
        unsigned char *curr = grab_and_decode(hwnd, &w, &h);
        if (curr == NULL) {
            printf("Frame grab failed, retrying\n");
            continue;
        }

        /* Size change would break the comparison */
        if (w != width || h != height) {
            printf("Resolution changed, resetting reference\n");
            stbi_image_free(prev);
            prev = curr;
            width = w;
            height = h;
            continue;
        }

        /* Time the comparison */
        LARGE_INTEGER freq, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);

        double percent = compare_frames(prev, curr, width, height);

        QueryPerformanceCounter(&t2);
        double ms = 1000.0 * (t2.QuadPart - t1.QuadPart) / freq.QuadPart;

        frame_number++;

        if (percent > MOTION_PERCENT) {
            printf("Frame %4d: %6.2f%% changed  (%.2f ms)  <<< MOTION\n",
                   frame_number, percent, ms);
        } else {
            printf("Frame %4d: %6.2f%% changed  (%.2f ms)\n",
                   frame_number, percent, ms);
        }

        /* Current frame becomes the reference for next time */
        stbi_image_free(prev);
        prev = curr;
    }

    /* Unreachable in this version, but correct for later */
    stbi_image_free(prev);
    free(g_jpeg);
    capDriverDisconnect(hwnd);
    DestroyWindow(hwnd);
    return 0;
}