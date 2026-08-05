#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>

/* Sends a POST request with a JSON body to Ollama.
   Returns the response body as a malloc'd string (caller must free),
   or NULL on failure. */
char *http_post_json(const wchar_t *host, int port, const wchar_t *path, const char *body, size_t body_len) {

    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *response = NULL;
    size_t response_len = 0;

    /* 1. Open a session */
    hSession = WinHttpOpen(L"c-camera/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL) {
        printf("WinHttpOpen failed (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* 2. Connect to the host and port */
    hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (hConnect == NULL) {
        printf("WinHttpConnect failed (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* 3. Create the POST request for the given path */
    hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);   /* 0 = plain HTTP, not HTTPS */
    if (hRequest == NULL) {
        printf("WinHttpOpenRequest failed (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* 4. Send it, with the Content-Type header and the JSON body */
    const wchar_t *headers = L"Content-Type: application/json\r\n";

    BOOL ok = WinHttpSendRequest(hRequest,
                                 headers, (DWORD)-1,      /* -1 = measure length itself */
                                 (LPVOID)body,            /* the body bytes */
                                 (DWORD)body_len,         /* how many bytes to send */
                                 (DWORD)body_len,         /* total content length */
                                 0);
    if (!ok) {
        printf("WinHttpSendRequest failed (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* 5. Wait for the response headers */
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        printf("WinHttpReceiveResponse failed (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* 6. Read the body in chunks, growing our buffer as we go */
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) {
            printf("WinHttpQueryDataAvailable failed (%lu)\n", GetLastError());
            free(response);
            response = NULL;
            goto cleanup;
        }
        if (available == 0) break;          /* no more data: done */

        char *grown = realloc(response, response_len + available + 1);
        if (grown == NULL) {
            printf("Out of memory growing response\n");
            free(response);
            response = NULL;
            goto cleanup;
        }
        response = grown;

        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, response + response_len,
                             available, &bytes_read)) {
            printf("WinHttpReadData failed (%lu)\n", GetLastError());
            free(response);
            response = NULL;
            goto cleanup;
        }

        response_len += bytes_read;
        response[response_len] = '\0';      /* keep it a valid C string */
    }

    if (response == NULL) {
        /* Empty response: return an empty string rather than NULL */
        response = malloc(1);
        if (response) response[0] = '\0';
    }

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return response;
}

/* ---- Test: send the JSON we saved earlier ---- */

static char *read_text_file(const char *filename, size_t *out_len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

int main(void) {
    size_t body_len = 0;
    char *body = read_text_file("build/request.json", &body_len);
    if (!body) {
        printf("Could not read build/request.json\n");
        return 1;
    }
    printf("Sending %zu bytes to Ollama...\n", body_len);

    char *reply = http_post_json(L"localhost", 11434, L"/api/generate",
                                 body, body_len);
    if (!reply) {
        printf("Request failed\n");
        free(body);
        return 1;
    }

    printf("--- Response ---\n%s\n", reply);

    free(reply);
    free(body);
    return 0;
}