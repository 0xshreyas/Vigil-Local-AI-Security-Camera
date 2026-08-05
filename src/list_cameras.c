#include<stdio.h>
#include "escapi.h"

int main(void) {
    int devices = setupESCAPI();
    if (devices == 0) {
        printf("ESCAPI failed --- No Devices or escapi.dll not found\n");
        return 1;
    }

    printf("Found %d capture device(s)\n", devices);
    for (int i = 0; i < devices; i++) {
        char name[256];
        getCaptureDeviceName(i, name, sizeof(name));
        printf("   Device %d: %s\n", i, name);
    }
    return 0;
}