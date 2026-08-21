#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <vfw.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "camera.h"

static HWND g_hwnd = NULL;
static unsigned char *g_jpeg = NULL; //Raw JPEG bytes from camera
static long g_buffer_size = 0; //capacity of g_jpeg
static long g_jpeg_size = 0; // bytes received
static int g_got_frame = 0; //set to 1 when a frame arrives

/* Vfw calls this function when a frame is captured. 
   lpData belongs to the driver and is only valid during this call
   Therefore, we will copy the bytes into our own buffer immediately*/
static LRESULT CALLBACK frame_callback(HWND hwnd, LPVIDEOHDR hdr) {
    (void) hwnd;
    if (hdr == NULL || hdr->lpData == NULL) return 0;
    if (g_jpeg != NULL && hdr->dwBytesUsed <= (DWORD)g_buffer_size) {
        memcpy(g_jpeg, hdr->lpData, hdr->dwBytesUsed);
        g_jpeg_size = hdr->dwBytesUsed;
        g_got_frame = 1;
    }
    return 0;
}

int camera_init(void) {
    g_hwnd = capCreateCaptureWindowA("cap", WS_POPUP, 0, 0, 640, 480, NULL, 0);
    if (g_hwnd == NULL) {
        printf("camera: capCreateCaptureWindow failed\n");
        return 0;
    }

    if (!capDriverConnect(g_hwnd, 0)) {
        printf("camera: capDriverConnect failed\n");
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        return 0;
    }

    g_buffer_size = 4 * 1024 * 1024;
    g_jpeg = malloc(g_buffer_size);
    if (g_jpeg == NULL) {
        printf("camera: out of memory\n");
        capDriverDisconnect(g_hwnd);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        return 0;
    }

    if (!capSetCallbackOnFrame(g_hwnd, frame_callback)) {
        printf("camera: capSetCallbackOnFrame failed\n");
        free(g_jpeg);
        g_jpeg = NULL;
        capDriverDisconnect(g_hwnd);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        return 0;
    }

    return 1;
}

unsigned char *camera_grab(int *width, int *height) {
    if (g_hwnd == NULL) return NULL;

    g_got_frame = 0;
    if (!capGrabFrameNoStop(g_hwnd)) return NULL;

    int waited = 0;
    while (!g_got_frame && waited < 2000) {
        Sleep(1);
        waited++;
    }

    if (!g_got_frame) return NULL;
    int channels = 0;
    return stbi_load_from_memory(g_jpeg, (int)g_jpeg_size, width, height, &channels, 3);
}

void camera_free_frame(unsigned char *pixels) {
    stbi_image_free(pixels);
}

const unsigned char *camera_last_jpeg(long *len) {
    if (len) *len = g_jpeg_size;
    return g_jpeg;
}

void camera_close(void) {
    if (g_jpeg) {
        free(g_jpeg);
        g_jpeg = NULL;
    }
    if (g_hwnd) {
        capDriverDisconnect(g_hwnd);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}