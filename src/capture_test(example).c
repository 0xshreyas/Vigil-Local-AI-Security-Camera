#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<windows.h>
#include<vfw.h>

#define COOLDOWN_MS   10000   /* minimum gap between AI calls */
#define OLLAMA_MODEL  "qwen2.5vl:3b"
#define OLLAMA_PROMPT "What do you see? Describe briefly."
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static unsigned char *g_jpeg = NULL; //Raw JPEG bytes from camera
static long g_buffer_size = 0; //capacity of g_jpeg
static long g_jpeg_size = 0; // bytes received
static int g_got_frame = 0; //set to 1 when a frame arrives

#define PIXEL_THRESHOLD 25 // Change in brightness in order to call a frame changed
#define MOTION_PERCENT 2.0 // Percentage of changed pixels in order to call motion
#define SAMPLE_STEP 4 // To check every 4th pixel which is speed vs accuracy

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

// Brightness of pixel i in an RGB array which is average of 3 channels
static int pixel_brightness(const unsigned char *pixels, long i) {
    long o = i * 3;
    return (pixels[o] + pixels[o + 1] + pixels[o + 2]) / 3;
}

// Compares two frames of same size and returns the percentage of pixels changed
static double compare_frames(const unsigned char *prev, const unsigned char *curr, int width, int height) {
    long total_pixels = (long)width * height;
    long checked = 0;
    long changed = 0;

    for (long i = 0; i < total_pixels; i+= SAMPLE_STEP) {
        int b1 = pixel_brightness(prev, i);
        int b2 = pixel_brightness(curr, i);
        int diff = b1 - b2;
        if (diff < 0) diff = -diff;
        if (diff > PIXEL_THRESHOLD) changed++;
        checked++;
    }
    if (checked == 0) return 0.0;
    return (100.0 * changed) / checked;
}

// Grabs one frame and decodes it and returns the pixel buffer or NULL. Writes the dimensions into *w and *h
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
    return stbi_load_from_memory(g_jpeg, (int)g_jpeg_size, w, h, &channels, 3);
}

/* Sends the current JPEG to Ollama and prints the description. */
static void describe_frame(const unsigned char *jpeg, long jpeg_len) {
    printf("  Encoding %ld bytes...\n", jpeg_len);

    char *b64 = base64_encode(jpeg, (size_t)jpeg_len);
    if (b64 == NULL) {
        printf("  base64 failed\n");
        return;
    }

    char *json = build_json(OLLAMA_MODEL, OLLAMA_PROMPT, b64);
    free(b64);
    if (json == NULL) {
        printf("  JSON build failed\n");
        return;
    }

    printf("  Asking the model...\n");

    char *reply = http_post_json(L"localhost", 11434, L"/api/generate",
                                 json, strlen(json));
    free(json);
    if (reply == NULL) {
        printf("  Request failed (is Ollama running?)\n");
        return;
    }

    char *description = json_get_string(reply, "response");
    if (description) {
        printf("  >>> %s\n\n", description);
        free(description);
    } else {
        printf("  Could not parse reply\n\n");
    }
    free(reply);
}

int main(void) {

// Creating a hidden capture window and connect to the camera
    

    // Creating capture window
    HWND hwnd = capCreateCaptureWindowA("cap", WS_POPUP, 0, 0, 640, 480, NULL, 0);
    if (hwnd == NULL) {
        printf("capCreateCaptureWindow failed\n");
        return 1;
    }
    

    // Checking if capture window connects to camera driver
    if (!capDriverConnect(hwnd, 0)) {
        printf("capDriverConnect failed\n");
        DestroyWindow(hwnd);
        return 1;
    }
    printf("Connected to camera\n");

// Reporting the format of the video

    /* BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    capGetVideoFormat(hwnd, &bi, sizeof(bi));
    printf("Camera format: %ldx%ld, compression 0x%lx (MJPEG)\n", bi.bmiHeader.biWidth, bi.bmiHeader.biHeight, bi.bmiHeader.biCompression); */
    
// Allocating buffer, extra
    g_buffer_size = 4 * 1024 * 1024;
    g_jpeg = malloc(g_buffer_size);
    if (g_jpeg == NULL) {
        printf("Out of memory\n");
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }

// Registering a callback
    if (!capSetCallbackOnFrame(hwnd, frame_callback)) {
        printf("capSetCallbackOnFrame failed\n");
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }

    printf("Motion detector is running. Please Ctrl+C to stop\n");
    printf("Settings: Pixel Threshold: %d, Motion Threshold: %.1f%%, sampling 1 in %d\n\n", PIXEL_THRESHOLD, MOTION_PERCENT, SAMPLE_STEP);

    int width = 0, height = 0;
    unsigned char *prev = grab_and_decode(hwnd, &width, &height);
    if (prev == NULL) {
        printf("Could not get the first frame from stream\n");
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }
    printf("Reference frame: %dx%d\n\n", width, height);

    int frame_number = 0;
    DWORD last_ai_call = 0;
    for (;;) {
        Sleep(200);
        int w = 0, h = 0;
        unsigned char *curr = grab_and_decode(hwnd, &w, &h);
        if (curr == NULL) {
            printf("Frame grabbing failed, retrying....\n");
            continue;
        }

        if (w != width || h != height) {
            printf("Resolution changed, resetting reference\n");
            stbi_image_free(prev);
            prev = curr;
            width = w;
            height = h;
            continue;
        }

        LARGE_INTEGER freq, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);

        double percent = compare_frames(prev, curr, width, height);
        QueryPerformanceCounter(&t2);
        double ms = 1000.0 * (t2.QuadPart - t1.QuadPart) / freq.QuadPart;

        frame_number++;

        if (percent > MOTION_PERCENT) {
            DWORD now = GetTickCount();

            if (now - last_ai_call >= COOLDOWN_MS) {
                printf("Frame %4d: %6.2f%% changed (%.2f ms) <<< MOTION\n", frame_number, percent, ms);
                last_ai_call = now;
                describe_frame(g_jpeg, g_jpeg_size);
            } else {
                DWORD remaining = (COOLDOWN_MS - (now - last_ai_call)) / 1000;
                printf("Frame %4d: %6.2f%% changed (%.2f ms) <<< MOTION (cooldown %lus)\n", frame_number, percent, ms, remaining);
            }
        }
        stbi_image_free(prev);
        prev = curr;
    }
// Grabbing a frame
    /*g_got_frame = 0;
    if (!capGrabFrameNoStop(hwnd)) {
        printf("capGrabFrameNoStop failed\n");
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }

// Waiting loop
    int waited = 0;
    while (!g_got_frame && waited < 5000) {
        Sleep(1);
        waited++;
    }

    if (!g_got_frame) {
        printf("No frame received after %d ms\n", waited);
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    } 

    printf("JPEG frame received: %ld bytes (%d ms)\n", g_jpeg_size, waited);

    // JPEG Checking bytes
    if (g_jpeg[0] == 0xFF && g_jpeg[1] == 0xD8) {
        printf("Valid JPEG signature (FF D8)\n");
    } else {
        printf("WARNING: not a JPEG (first bytes %02X %02X)\n", g_jpeg[0], g_jpeg[0]);
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(g_jpeg, (int)g_jpeg_size, &width, &height, &channels, 3);

    if (pixels == NULL) {
        printf("JPEG decode failed: %s\n", stbi_failure_reason());
        free(g_jpeg);
        capDriverDisconnect(hwnd);
        DestroyWindow(hwnd);
        return 1;
    }
    printf("Decoded to %dx%d, %d channels in source\n", width, height, channels);

// Inspecting some of the decoded pixels
    for (int i = 0; i < 3; i++) {
        int o = i * 3;
        printf("Pixel %d:  R=%3d  G=%3d  B=%3d\n",
               i, pixels[o], pixels[o + 1], pixels[o + 2]);
    }

// Average brightness across the decoded frames
    long total = 0;
    long count = (long)width * height;
    for (long i = 0; i < count; i++) {
        long o = i * 3;
        total += (pixels[o] + pixels[o + 1] + pixels[o + 2]) / 3;
    }
    printf("Average brightness: %ld (0=black, 255=white)\n", total / count);

    FILE *out = fopen("frame.jpg", "wb");
    if (out) {
        fwrite(g_jpeg, 1, g_jpeg_size, out);
        fclose(out);
        printf("Saved frame.jpg\n");
    } */

    //stbi_image_free(pixels);
    free(g_jpeg);
    capDriverDisconnect(hwnd);
    DestroyWindow(hwnd);
    return 0;
}

//https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/um/Vfw.h
//https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-capdriverconnect
//https://learn.microsoft.com/en-us/windows/win32/multimedia/adding-callback-functions-to-an-application
//https://learn.microsoft.com/en-us/windows/win32/api/vfw/ns-vfw-videohdr
//https://learn.microsoft.com/en-us/windows/win32/api/vfw/nc-vfw-capvideocallback