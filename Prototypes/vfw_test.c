#include <stdio.h>
#include <windows.h>
#include <vfw.h>

int main(void) {
    char name[128], version[128];

    printf("Scanning for VfW capture drivers...\n");

    int found = 0;
    for (int i = 0; i < 10; i++) {
        if (capGetDriverDescriptionA(i, name, sizeof(name),
                                     version, sizeof(version))) {
            printf("  Driver %d: %s  (%s)\n", i, name, version);
            found++;
        }
    }

    if (found == 0) {
        printf("No VfW drivers found - your camera doesn't support this API.\n");
        return 1;
    }

    /* Try to open a capture window on driver 0 */
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
    printf("Connected to driver 0\n");

    /* Ask for the frame format so we know the real resolution */
    BITMAPINFO bi;
    capGetVideoFormat(hwnd, &bi, sizeof(bi));
    printf("Frame format: %ldx%ld, %d bits per pixel\n",
           bi.bmiHeader.biWidth, bi.bmiHeader.biHeight,
           bi.bmiHeader.biBitCount);

    capDriverDisconnect(hwnd);
    DestroyWindow(hwnd);
    return 0;
}