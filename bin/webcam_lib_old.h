// Author: Nathan Otterness (otternes@cs.unc.edu)
//
// This library provides a basic wrapper around the V4L2 (Video for Linux 2)
// API, which is used, among other things, for interfacing with webcams on
// Linux.
//
// To use, first call OpenWebcam("/dev/<path>", ...). When the webcam is no
// longer needed, pass a pointer to the WebcamInfo struct populated by
// OpenWebcam() to CloseWebcam(...) to clean up resources.
#ifndef WEBCAM_LIB_H
#define WEBCAM_LIB_H
#include <linux/videodev2.h>
#include <stdint.h>

// Holds the resolution of a single discrete frame of video.
typedef struct {
  uint32_t width;
  uint32_t height;
} WebcamResolution;

// Holds information about the webcam, needed by the library functions. Do not
// directly modify the members of this struct.
typedef struct {
  int fd;
  struct v4l2_capability capabilities;
  struct v4l2_buffer buffer_info;
  void *image_buffer;
  WebcamResolution resolution;
} WebcamInfo;

// Takes a device path (e.g. /dev/video0) and a pointer to a WebcamInfo struct
// to populate. Returns 0 on error.
int OpenWebcam(char *path, WebcamInfo *webcam);

// Closes the webcam, freeing any allocated resources.
void CloseWebcam(WebcamInfo *webcam);

// Prints information about the open webcam to stdout. Returns 0 on error.
int PrintCapabilityDetails(WebcamInfo *webcam);

// Prints information about the webcam's supported video formats to stdout.
// Returns 0 on error.
int PrintVideoFormatDetails(WebcamInfo *webcam);

// Gets the supported YUYV (4:2:2) resolutions from the webcam. This will only
// provide resolutions for single-planar, discrete frames. This takes a pointer
// to an array of WebcamResolution structs, and will fill in up to
// resolutions_count members. Any entries in the list beyond the number of
// available resolutions will be set to 0. This returns 0 on error.
int GetSupportedResolutions(WebcamInfo *webcam, WebcamResolution *resolutions,
    int resolutions_count);

// Set the desired resolution for frame outputs. This must be called before
// BeginLoadingNextFrame or GetFrameBuffer. This returns 0 on error. It will
// fail if called more than once on a WebcamInfo struct. To change resolutions,
// first call CloseWebcam() and OpenWebcam() before this.
int SetResolution(WebcamInfo *webcam, uint32_t width, uint32_t height);

// Sets width and height to the current resolution of the webcam. They will
// both be 0 if SetResolution hasn't been called yet.
void GetResolution(WebcamInfo *webcam, uint32_t *width, uint32_t *height);

// Starts loading the next frame into a buffer to be retrieved using
// GetFrameBuffer. This function won't block, but it may return 0 on error,
// if, for example, SetResolution hasn't been called yet.
int BeginLoadingNextFrame(WebcamInfo *webcam);

// Sets buffer to point to the frame buffer containing YUYV pixel data, and
// size to the number of bytes used for pixel data in the buffer. Do not free
// the buffer from this function; it will be freed when CloseWebcam is called.
// Returns 0 on error, such as if SetResolution() hasn't been called yet.
// BeginLoadingNextFrame() must be called previously for this to succeed. This
// may block if the frame hasn't finished transferring yet.
int GetFrameBuffer(WebcamInfo *webcam, void **buffer, size_t *size);

// Converts the YUYV buffer pointed to by input to the 4-byte RGBA buffer
// pointed to by output. Each image is w pixels wide and h pixels tall. The row
// pitches are the number of bytes in a row for each image. Normally, this will
// just be 2 * w for the YUYV input pitch and 4 * w for the RGBA output pitch.
// The byte order for the RGBA colors will be A = output[0], B = output[1], ...
// Returns 0 on error.
int ConvertYUYVToRGBA(uint8_t *input, uint8_t *output, int w, int h,
    int input_pitch, int output_pitch);

#endif  // WEBCAM_LIB_H
