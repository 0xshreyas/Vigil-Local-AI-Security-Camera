// Code picked up from the concept of WinHTTP
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<windows.h>
#include<winhttp.h>

// Sends a POST request with a JSON body to Ollama.
// Returns the response body as a malloc'd string (caller must free), or NULL on failure.
char *http_post_json(const wchar_t *host, int port, const wchar_t *path, const char *body, size_t body_len) {
    
    // Default variables called upon using HINTERNET
    
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *response = NULL;
    size_t response_len = 0;

    // Opening a session
    hSession = WinHttpOpen(L"c-camera/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL) {
        printf("WinHttpOpen failed {%zu}\n", GetLastError());
        goto cleanup;
    }

    // Connect the host and port
    hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (hConnect == NULL) {
        printf("WinHttpConnect failed {%zu}\n", GetLastError());
        goto cleanup;
    }

    //Creating the POST Request for the given path

    hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (hRequest == NULL) {
        printf("WinHttpOpenRequest failed {%zu}\n", GetLastError());
        goto cleanup;
    }

    // Send it with Content-Type header and JSON body

    const wchar_t *headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(hRequest, headers, (DWORD)-1, (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);

    if(!ok) {
        printf("WinHttpSendRequest failed {%zu}\n", GetLastError());
        goto cleanup;
    }

    // Wait for the response header

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        printf("WinHttpReceiveResponse failed {%zu}\n", GetLastError());
        goto cleanup;
    }

    // Reading the data

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) {
            printf("WinHttpQueryDataAvailable failed {%zu}\n", GetLastError());
            free(response);
            response = NULL;
            goto cleanup;
        }

        if (available == 0) break;

        char *grown = realloc(response, response_len + available + 1);
        if (grown == NULL) {
            printf("Out of memory growing response\n");
            free(response);
            response = NULL;
            goto cleanup;
        }
        response = grown;

        DWORD bytes_read = 0;
        if (!WinHttpReadData(hRequest, response + response_len, available, &bytes_read)) {
            printf("WinHttpReadData failed {%zu}\n", GetLastError());
            free(response);
            response = NULL;
            goto cleanup;
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

// Testing

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
    size_t j = 0;
    for (size_t i = 0; start[i] != '\0'; i++) {
        char c = start[i];
        if (c == '\\') {
            char next = start[i + 1];
            if (next == '\0') break;
            switch (next) {
                case 'n': out[j++] = '\n'; break;
                case 't': out[j++] = '\t'; break;
                case 'r': out[j++] = '\r'; break;
                case '"': out[j++] = '"'; break;
                case '\\': out[j++] = '\\'; break;
                case '/': out[j++] = '/'; break;
                default: out[j++] = next; break;
            }
            i++;
        }
        else if (c == '"') {
            break;
        }
        else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
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

    char *description = json_get_string(reply, "response");
    if (description) {
        printf("\n-----Camera1.0 says-----\n%s\n\n", description);
        free(description);
    }
    else {
        printf("\nCamera1.0 could not find the 'response' field. Here is the raw reply: \n%s\n\n", reply);
    }

    free(reply);
    free(body);
    return 0;
}