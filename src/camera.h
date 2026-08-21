#ifndef CAMERA_H
#define CAMERA_H

/* This is meant to make Vigil's interface platform independent. 
   Implementation in camera_win32.c*/

// Opens the camera, returns 1 in success and 0 on failure.
int camera_init(void);

/* Grabs a frame from the camera and decodes it into RGB pixels.
   Writes the dimensions into *width and *height and returns the buffer,
   Can be released by calling camera_free_frame(), NULL on failure.*/
unsigned char *camera_grab(int *width, int *height);

//Releases the buffer returned by the function camera_grab()
void camera_free_frame(unsigned char *pixels);

/* Returns the compressed JPEG of the most recent frame (earlier solved by STB)
   Length written into *len*/

const unsigned char *camera_last_jpeg(long *len);

// Closes the camera
void camera_close(void);

#endif
