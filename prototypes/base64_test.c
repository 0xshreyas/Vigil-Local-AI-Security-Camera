#include<stdio.h>
#include<stdlib.h>
#include<string.h>
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t input_len) {
    size_t output_len = ((input_len + 2) / 3) * 4;
    char *output = malloc(output_len + 1);
    if (output == NULL) return NULL;

    size_t i = 0, j = 0;

    // Main calculation of the base64
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

    // Left over calculations

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
        output[j + 2] = b64_table[(b1 & 0x0F) << 2];
        output[j + 3] = '=';
        j += 4;
    }

    output[j] = '\0';
    return output;
}

int main(void) {
    const char *tests[] = {"Man", "Ma", "M", "ManMan"};
    for (int k = 0; k < 4; k++) {
        char *r = base64_encode((const unsigned char *)tests[k], strlen(tests[k]));
        printf("%-8s -> %s\n", tests[k], r);
        free(r);
    }
    return 0;
}