#include "camera.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<windows.h>
#include<time.h>
#include<curl/curl.h>

#define COOLDOWN_MS   10000   /* minimum gap between AI calls */
#define OLLAMA_MODEL  "qwen2.5vl:3b"
#define OLLAMA_PROMPT "What do you see? Describe briefly."

static long g_frames_checked = 0;
static long g_motion_events = 0;
static long g_ai_calls = 0;

#define PIXEL_THRESHOLD 25 // Change in brightness in order to call a frame changed
#define MOTION_PERCENT 2.0 // Percentage of changed pixels in order to call motion
#define SAMPLE_STEP 4 // To check every 4th pixel which is speed vs accuracy

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
    const char *fmt = "{\"model\":\"%s\",\"prompt\":\"%s\",\"images\":[\"%s\"],\"stream\":false}";

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

// Accumulating all the response as libcurl delivers it in chunks
struct response_buffer {
    char *data;
    size_t len;
};

//libcurl calls this each time a chunk of the response arrives
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    struct response_buffer *b = (struct response_buffer *)userdata;
    char *grown = realloc(b->data, b->len + total + 1);
    if (grown == NULL) return 0;

    b->data = grown;
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

char *http_post_json(const char *url, const char *body, size_t body_len) {
    CURL *curl = curl_easy_init();
    if (curl == NULL) return NULL;

    struct response_buffer buf = {NULL, 0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,(long)body_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK) {
        printf("   curl failed: %s\n", curl_easy_strerror(res));
        free(buf.data);
        return NULL;
    }
    return buf.data;
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



static int save_captures(const unsigned char *jpeg, long jpeg_len, char *out_path, size_t path_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    snprintf(out_path, path_size, "detections/%04d%02d%02d_%02d%02d%02d.jpg", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

    FILE *f = fopen(out_path, "wb");
    if (f == NULL) return 0;

    size_t write = fwrite(jpeg, 1, (size_t)jpeg_len, f);
    fclose(f);

    return (write == (size_t)jpeg_len);
}

/* Sends the current JPEG to Ollama and prints the description. */
static void describe_frame(void) {
    long jpeg_len = 0;
    const unsigned char *jpeg = camera_last_jpeg(&jpeg_len);
    if (jpeg == NULL || jpeg_len == 0) {
        printf("   No frame available\n");
        return;
    }
    char path[256];
    if (save_captures(jpeg, jpeg_len, path, sizeof(path))) {
        printf("   Saved in %s\n", path);
    } else {
        printf("Could not save the capture (does detections/ exist?)\n");
    }
    printf("  Encoding %ld bytes...\n", jpeg_len);

    char *b64 = base64_encode(jpeg, (size_t)jpeg_len);
    if (b64 == NULL) { printf("  base64 failed\n"); return; }

    char *json = build_json(OLLAMA_MODEL, OLLAMA_PROMPT, b64);
    free(b64);
    if (json == NULL) { printf("  JSON build failed\n"); return; }

    printf("  Asking the model...\n");

    DWORD t0 = GetTickCount();
    char *reply = http_post_json("http://localhost:11434/api/generate", json, strlen(json));
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

// Creating a hidden capture window and connect to the camera
    
    if (!camera_init()) {
        printf("Vigil could not open your camera\n");
        return 1;
    }
    printf("Vigil successfully connected to your camera\n");

    printf("Motion detector is running. Please Ctrl+C to stop\n");
    printf("Settings: Pixel Threshold: %d, Motion Threshold: %.1f%%, sampling 1 in %d\n\n", PIXEL_THRESHOLD, MOTION_PERCENT, SAMPLE_STEP);

    int width = 0, height = 0;
    unsigned char *prev = camera_grab(&width, &height);
    if (prev == NULL) {
        printf("Could not get the first frame from stream\n");
        camera_close();
        return 1;
    }
    printf("Reference frame: %dx%d\n\n", width, height);

    int frame_number = 0;
    DWORD last_ai_call = 0;
    for (;;) {
        Sleep(200);
        int w = 0, h = 0;
        unsigned char *curr = camera_grab(&w, &h);
        if (curr == NULL) {
            printf("Frame grabbing failed, retrying....\n");
            continue;
        }

        if (w != width || h != height) {
            printf("Resolution changed, resetting reference\n");
            camera_free_frame(prev);
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
                describe_frame();

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
        camera_free_frame(prev);
        prev = curr;
    }

    camera_free_frame(prev);
    camera_close();
    return 0;
}