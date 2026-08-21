#include<stdio.h>
#include<stdlib.h>
int main(void) {
    // 1. This is the start of file handling, standard procedure
    FILE *f;
    const char *fileName = "images/image.jpeg";
    f = fopen(fileName, "rb");
    if (f == NULL) {
        printf("Sorry. File could not be opened\n");
        return 1;
    }

    // 2. In order to find out how much size we need to allocate, this is a trick
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    // 3. Request for needed memory
    unsigned char *buffer = malloc(size);
    if (buffer == NULL) {
        printf("Out of memory\n");
        fclose(f);
        return 1;
    }

    // 4. Read the file into the memory
    size_t bytes_read = fread(buffer, 1, size, f);

    // 5. You can use it, now you are just reporting it
    printf("File Size: %ld bytes\n", size);
    printf("Bytes actually read: %zu\n", bytes_read);
    printf("First byte value %d\n", buffer[0]);

    // 6. Clean Up
    free(buffer);
    fclose(f);
    return 0;
}