#include<stdio.h>
#include<stdlib.h>
#include<string.h>

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t input_len) {
    size_t output_len = ((input_len + 2) / 3) * 4;
    char *output = malloc(output_len + 1);
    if (output == NULL) return NULL;

    size_t i = 0, j = 0;

    while (i + 2 < input_len) {
        unsigned char b0 = data[i];
        unsigned char b1 = data[i + 1];
        unsigned char b2 = data[i + 2];

        output[j] = b64_table[b0 >> 2];
        output[j + 1] = b64_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        output[j + 2] = b64_table[((b1 & 0x0F) << 2) | (b2 >> 6)];
        output[j + 3] = b64_table[b2 & 0x3F];

        i += 3;
        j += 4;
    }
    size_t remaining = input_len - i;

    if (remaining == 1) {
        unsigned char b0 = data[i];
        output[j] = b64_table[b0 >> 2];
        output[j + 1] = b64_table[(b0 & 0x03) << 4];
        output[j + 2] = '=';
        output[j + 3] = '=';
        j += 4;
    }

    else if (remaining == 2) {
        unsigned char b0 = data[i];
        unsigned char b1 = data[i + 1];
        output[j] = b64_table[b0 >> 2];
        output[j + 1] = b64_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        output[j + 2] = b64_table[(b0 & 0x0F) << 2];
        output[j + 3] = '=';
        j += 4;
    }
    output[j] = '\0';
    return output;
}

unsigned char *read_file(const char *fileName, long *out_size) {
    FILE *f = fopen(fileName, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);

    if (bytes_read != (size_t) size) {
        free(buffer);
        return NULL;
    }

    *out_size = size;
    return buffer;
}

char *build_json(const char *model, const char *prompt, const char *b64image) {
    const char *format = "{\"model\":\"%s\",\"prompt\":\"%s\",\"images\":[\"%s\"],\"stream\":false}";
    size_t len = strlen(format) + strlen(model) + strlen(prompt) + strlen(b64image) + 1;
    char *json = malloc(len);
    if (json == NULL) return NULL;

    snprintf(json, len, format, model, prompt, b64image);
    return json;
}

int main(void) {
    long size = 0;
    unsigned char *image = read_file("images/image.jpeg", &size);
    if (image == NULL) {
        printf("ERROR! Could not read image");
        return 1;
    }

    printf("Image size: %ld bytes\n", size);
    char *encoded = base64_encode(image, (size_t)size);
    if (encoded == NULL) {
        printf("Encoding Failed\n");
        free(image);
        return 1;
    }

    printf("Encoded length: %zu characters\n", strlen(encoded));
    printf("First 60 characters: %.60s\n", encoded);

    // Wrap the data in json and send it to the model
    char *json = build_json("qwen2.5vl:3b", "What do you see?", encoded); // Prompt to the model
    if (json == NULL) {
        printf("JSON build failed\n");
        free(encoded);
        free(image);
        return 1;
    }

    printf("JSON Length: %zu\n", strlen(json));
    printf("JSON starts: %.80s\n", json);

    // Saving the JSON so that we can test with curl to verify the code works correctly
    FILE *out = fopen("build/request.json", "wb");
    if (out) {
        fwrite(json, 1, strlen(json), out);
        fclose(out);
        printf("Data successfully written to build/request.json\n");
    }
    else {
        printf("Write failed!\n");
    }

    free(json);
    free(encoded);
    free(image);
    return 0;
}