#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>
#include <vfw.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#define PIXEL_THRESHOLD  25
#define MOTION_PERCENT   2.0
#define SAMPLE_STEP      4
#define COOLDOWN_MS      10000
#define OLLAMA_MODEL     "qwen2.5vl:3b"
#define OLLAMA_PROMPT    "What do you see? Describe briefly."


static unsigned char *g_jpeg = NULL;
static long g_buffer_size = 0;
static long g_jpeg_size   = 0;
static int  g_got_frame   = 0;


static long g_frames_checked = 0;
static long g_motion_events  = 0;
static long g_ai_calls       = 0;


static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t input_len) {
    size_t output_len = ((input_len + 2) / 3) * 4;
    char *output = malloc(output_len + 1);
    if (output == NULL) return NULL;

    size_t i = 0, j = 0;

    while (i + 2 < input_len) {
        unsigned char b0 = data[i];
        unsigned char b1 = data[i + 1];
        unsigned char b2 = data[i + 2];

        output[j]     = b64_table[b0 >> 2];
        output[j + 1] = b64_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        output[j + 2] = b64_table[((b1 & 0x0F) << 2) | (b2 >> 6)];
        output[j + 3] = b64_table[b2 & 0x3F];

        i += 3;
        j += 4;
    }

    size_t remaining = input_len - i;

    if (remaining == 1) {
        unsigned char b0 = data[i];
        output[j]     = b64_table[b0 >> 2];
        output[j + 1] = b64_table[(b0 & 0x03) << 4];
        output[j + 2] = '=';
        output[j + 3] = '=';
        j += 4;
    } else if (remaining == 2) {
        unsigned char b0 = data[i];
        unsigned char b1 = data[i + 1];
        output[j]     = b64_table[b0 >> 2];
        output[j + 1] = b64_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        output[j + 2] = b64_table[(b1 & 0x0F) << 2];
        output[j + 3] = '=';
        j += 4;
    }

    output[j] = '\0';
    return output;
}



char *build_json(const char *model, const char *prompt, const char *b64image) {
    const char *fmt =
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"images\":[\"%s\"],\"stream\":false}";

    size_t len = strlen(fmt) + strlen(model) + strlen(prompt) + strlen(b64image) + 1;

    char *json = malloc(len);
    if (json == NULL) return NULL;

    snprintf(json, len, fmt, model, prompt, b64image);
    return json;
}

char *json_get_string(const char *json, const char *key) {
    size_t keylen = strlen(key);
    char *pattern = malloc(keylen + 5);
    if (!pattern) return NULL;
    sprintf(pattern, "\"%s\":\"", key);

    const char *start = strstr(json, pattern);
    free(pattern);
    if (!start) return NULL;

    start += keylen + 4;

    char *out = malloc(strlen(start) + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; start[i] != '\0'; i++) {
        char c = start[i];

        if (c == '\\') {
            char next = start[i + 1];
            if (next == '\0') break;
            switch (next) {
                case 'n':  out[j++] = '\n'; break;
                case 't':  out[j++] = '\t'; break;
                case 'r':  out[j++] = '\r'; break;
                case '"':  out[j++] = '"';  break;
                case '\\': out[j++] = '\\'; break;
                case '/':  out[j++] = '/';  break;
                default:   out[j++] = next; break;
            }
            i++;
        }
        else if (c == '"') break;
        else out[j++] = c;
    }

    out[j] = '\0';
    return out;
}



char *http_post_json(const wchar_t *host, int port, const wchar_t *path,
                     const char *body, size_t body_len) {

    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *response = NULL;
    size_t response_len = 0;

    hSession = WinHttpOpen(L"c-camera/1.0",
                           WINHTTP_ACCESS_TYPE_NO_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL) goto cleanup;

    hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (hConnect == NULL) goto cleanup;

    hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (hRequest == NULL) goto cleanup;

    const wchar_t *headers = L"Content-Type: application/json\r\n";

    if (!WinHttpSendRequest(hRequest, headers, (DWORD)-1,
                            (LPVOID)body, (DWORD)body_len,
                            (DWORD)body_len, 0)) goto cleanup;

    if (!WinHttpReceiveResponse(hRequest, NULL)) goto cleanup;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) {
            free(response); response = NULL; goto cleanup;
        }
        if (available == 0) break;

        char *grown = realloc(response, response_len + available + 1);
        if (grown == NULL) {
            free(response); response = NULL; goto cleanup;
        }
        response = grown;

        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, response + response_len,
                             available, &bytes_read)) {
            free(response); response = NULL; goto cleanup;
        }

        response_len += bytes_read;
        response[response_len] = '\0';
    }

    if (response == NULL) {
        response = malloc(1);
        if (response) response[0] = '\0';
    }

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return response;
}



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


static int pixel_brightness(const unsigned char *pixels, long i) {
    long o = i * 3;
    return (pixels[o] + pixels[o + 1] + pixels[o + 2]) / 3;
}

static double compare_frames(const unsigned char *prev, const unsigned char *curr,
                             int width, int height) {
    long total_pixels = (long)width * height;
    long checked = 0, changed = 0;

    for (long i = 0; i < total_pixels; i += SAMPLE_STEP) {
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


static void describe_frame(const unsigned char *jpeg, long jpeg_len) {
    printf("  Encoding %ld bytes...\n", jpeg_len);

    char *b64 = base64_encode(jpeg, (size_t)jpeg_len);
    if (b64 == NULL) { printf("  base64 failed\n"); return; }

    char *json = build_json(OLLAMA_MODEL, OLLAMA_PROMPT, b64);
    free(b64);
    if (json == NULL) { printf("  JSON build failed\n"); return; }

    printf("  Asking the model...\n");

    DWORD t0 = GetTickCount();
    char *reply = http_post_json(L"localhost", 11434, L"/api/generate",
                                 json, strlen(json));
    DWORD elapsed = GetTickCount() - t0;
    free(json);

    if (reply == NULL) {
        printf("  Request failed (is Ollama running?)\n\n");
        return;
    }

    char *description = json_get_string(reply, "response");
    if (description) {
        printf("  >>> %s\n", description);
        printf("  (model took %lu ms)\n\n", elapsed);
        free(description);
    } else {
        printf("  Could not parse reply\n\n");
    }
    free(reply);
}

int main(void) {
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
    printf("Connected to camera\n");

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

    printf("Motion-gated vision pipeline running. Ctrl+C to stop.\n");
    printf("Settings: pixel threshold %d, motion threshold %.1f%%, "
           "sampling 1 in %d, cooldown %d ms\n\n",
           PIXEL_THRESHOLD, MOTION_PERCENT, SAMPLE_STEP, COOLDOWN_MS);

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

    int frame_number = 0;
    DWORD last_ai_call = 0;

    for (;;) {
        Sleep(200);

        int w = 0, h = 0;
        unsigned char *curr = grab_and_decode(hwnd, &w, &h);
        if (curr == NULL) {
            printf("Frame grab failed, retrying\n");
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
        g_frames_checked++;

        if (percent > MOTION_PERCENT) {
            g_motion_events++;
            DWORD now = GetTickCount();
            if (now - last_ai_call >= COOLDOWN_MS) {
                printf("Frame %4d: %6.2f%% changed (%.2f ms) <<< MOTION\n",
                       frame_number, percent, ms);
                last_ai_call = now;
                g_ai_calls++;
                describe_frame(g_jpeg, g_jpeg_size);

                printf("  [stats] frames checked: %ld | motion events: %ld | "
                       "AI calls: %ld | reduction: %.2f%%\n\n",
                       g_frames_checked, g_motion_events, g_ai_calls,
                       100.0 * (1.0 - (double)g_ai_calls / (double)g_frames_checked));
            } else {
                DWORD remaining = (COOLDOWN_MS - (now - last_ai_call)) / 1000;
                printf("Frame %4d: %6.2f%% changed (%.2f ms) <<< MOTION "
                       "(cooldown %lus)\n", frame_number, percent, ms, remaining);
            }
        } else {
            printf("Frame %4d: %6.2f%% changed (%.2f ms)\n",
                   frame_number, percent, ms);
        }
        stbi_image_free(prev);
        prev = curr;
    }
    stbi_image_free(prev);
    free(g_jpeg);
    capDriverDisconnect(hwnd);
    DestroyWindow(hwnd);
    return 0;
}