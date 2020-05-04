// Author: Nathan Otterness (otternes@cs.unc.edu)
//
// This file implements the API defined in webcam_lib.h.
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "webcam_lib_old.h"

// The specifier used when requesting the YUYV format from V4L2.
#define YUYV_FORMAT_CODE (v4l2_fourcc('Y', 'U', 'Y', 'V'))

// Prints the meaning of the set flags in the v4l2_capability struct's flag
// fields.
static void PrintCapabilityFlagDetails(uint32_t flags) {
  if (flags == 0) {
    printf("  <none>\n");
    return;
  }
  if (flags & 1) {
    printf("  Single-planar video capture\n");
  }
  if (flags & 0x1000) {
    printf("  Multi-planar video capture\n");
  }
  if (flags & 2) {
    printf("  Single-planar video output\n");
  }
  if (flags & 0x2000) {
    printf("  Multi-planar video output\n");
  }
  if (flags & 0x4000) {
    printf("  Single-planar video-to-memory API\n");
  }
  if (flags & 0x8000) {
    printf("  Multi-planar video-to-memory API\n");
  }
  if (flags & 4) {
    printf("  Video overlay interface\n");
  }
  if (flags & 0x10) {
    printf("  Raw VBI capture interface\n");
  }
  if (flags & 0x20) {
    printf("  Raw VBI output interface\n");
  }
  if (flags & 0x40) {
    printf("  Sliced VBI capture interface\n");
  }
  if (flags & 0x80) {
    printf("  Sliced VBI output interface\n");
  }
  if (flags & 0x100) {
    printf("  RDS capture interface\n");
  }
  if (flags & 0x200) {
    printf("  Video output overlay (OSD) interface\n");
  }
  if (flags & 0x400) {
    printf("  Hardware frequency seeking\n");
  }
  if (flags & 0x800) {
    printf("  RDS output interface\n");
  }
  if (flags & 0x10000) {
    printf("  Receive RF-modulated video signals\n");
  }
  if (flags & 0x20000) {
    printf("  Audio inputs or outputs\n");
  }
  if (flags & 0x40000) {
    printf("  Radio receiver\n");
  }
  if (flags & 0x80000) {
    printf("  Emit RF-modulated audio or video signals\n");
  }
  if (flags & 0x100000) {
    printf("  SDR capture interface\n");
  }
  if (flags & 0x200000) {
    printf("  v4l2_pix_format fields available\n");
  }
  if (flags & 0x400000) {
    printf("  SDR output interface\n");
  }
  if (flags & 0x1000000) {
    printf("  read() or write() I/O\n");
  }
  if (flags & 0x4000000) {
    printf("  Streaming I/O\n");
  }
  if (flags & 0x10000000) {
    printf("  Touch device\n");
  }
  if (flags & 0x80000000) {
    printf("  device_caps provided\n");
  }
}

// Takes a file descriptor for a device and a pixel format from the
// v4l2_fmtdesc struct and prints a list of frame sizes supported by the
// format. Returns 0 on error, 1 on success.
static int PrintFormatFrameSizes(int fd, uint32_t pixel_format) {
  struct v4l2_frmsizeenum info;
  int result;
  uint32_t current_index;
  info.pixel_format = pixel_format;
  for (current_index = 0; ; current_index++) {
    info.index = current_index;
    result = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &info);
    if (result < 0) {
      if (errno == EINVAL) break;
      return 0;
    }
    if (info.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
      printf("    Continuous frame size.\n");
      continue;
    }
    if (info.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      printf("    Discrete %dx%d frames\n", info.discrete.width,
        info.discrete.height);
      continue;
    }
    printf("    Stepwise %d-%dx%d-%d frames\n", info.stepwise.min_width,
      info.stepwise.max_width, info.stepwise.min_height,
      info.stepwise.max_height);
  }
  return 1;
}

// Takes a file descriptor for the device, and prints information about the
// image formats it supports to stdout. Returns 0 on error, 1 on success.
int PrintVideoFormatDetails(WebcamInfo *webcam) {
  struct v4l2_fmtdesc info;
  uint32_t current_index = 0;
  int result = 0;
  info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  printf("Available image formats:\n");
  while (1) {
    info.index = current_index;
    result = ioctl(webcam->fd, VIDIOC_ENUM_FMT, &info);
    if (result < 0) {
      if (errno == EINVAL) break;
      return 0;
    }
    printf("  %s", info.description);
    if (info.flags != 0) {
      printf(" (");
      if (info.flags & 1) {
        printf("compressed");
        if (info.flags != 1) {
          printf(", ");
        }
      }
      if (info.flags & 2) {
        printf("emulated");
      }
      printf(")");
    }
    printf("\n");
    printf("  Supported frame sizes:\n");
    if (!PrintFormatFrameSizes(webcam->fd, info.pixelformat)) return 0;
    current_index++;
  }
  return 1;
}

int PrintCapabilityDetails(WebcamInfo *webcam) {
  struct v4l2_capability *caps = &(webcam->capabilities);
  printf("Device %s on %s, driver %s:\n", caps->card, caps->bus_info,
    caps->driver);
  printf("Device features:\n");
  PrintCapabilityFlagDetails(caps->capabilities);
  if (caps->capabilities & 0x80000000) {
    // Print features in info.device_caps but not info.capabilities, ignoring
    // the device_caps_provided field.
    printf("Device features via a different interface:\n");
    PrintCapabilityFlagDetails((caps->device_caps ^ caps->capabilities) &
      0x7fffffff);
  }
  return 1;
}

// Verifies that the flags for video capture and streaming are set in the
// webcam's capabilities structure, otherwise we can't use it to get images.
// This doesn't require the fd field to be set.
static int VerifyCaptureAndStreaming(WebcamInfo *webcam) {
  uint32_t flags = webcam->capabilities.capabilities;
  // Has both the single-planar capture (1) and streaming (0x4000000) bits set.
  uint32_t desired = 0x4000001;
  return (flags & desired) == desired;
}

int OpenWebcam(char *path, WebcamInfo *webcam) {
  int fd = open(path, O_RDWR);
  memset(webcam, 0, sizeof(*webcam));
  if (fd < 0) return 0;
  if (ioctl(fd, VIDIOC_QUERYCAP, &(webcam->capabilities)) < 0) {
    close(fd);
    return 0;
  }
  if (!VerifyCaptureAndStreaming(webcam)) {
    close(fd);
    memset(webcam, 0, sizeof(*webcam));
    errno = ENOTSUP;
    return 0;
  }
  webcam->fd = fd;
  // Set these once, since they're used pretty often.
  webcam->buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  webcam->buffer_info.memory = V4L2_MEMORY_MMAP;
  webcam->buffer_info.index = 0;
  return 1;
}

void CloseWebcam(WebcamInfo *webcam) {
  close(webcam->fd);
  if (webcam->image_buffer) {
    munmap(webcam->image_buffer, webcam->buffer_info.length);
  }
  memset(webcam, 0, sizeof(*webcam));
}

int GetSupportedResolutions(WebcamInfo *webcam, WebcamResolution *resolutions,
    int resolutions_count) {
  struct v4l2_frmsizeenum info;
  int result;
  uint32_t api_index;
  int output_index = 0;
  info.pixel_format = YUYV_FORMAT_CODE;
  // Ensure that all resolutions are zeroed-out so if the array isn't totally
  // full, the unset resolutions will simply be 0x0.
  memset(resolutions, 0, sizeof(WebcamResolution) * resolutions_count);
  // The API index tracks all of the frame sizes, but the output index only
  // tracks discrete frame sizes.
  for (api_index = 0; ; api_index++) {
    if (output_index >= resolutions_count) break;
    info.index = api_index;
    result = ioctl(webcam->fd, VIDIOC_ENUM_FRAMESIZES, &info);
    if (result < 0) {
      if (errno == EINVAL) break;
      return 0;
    }
    // Skip continuous frame types.
    if (info.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) continue;
    // Skip stepwise frame types.
    if (info.type == V4L2_FRMSIZE_TYPE_STEPWISE) continue;
    // This is an error--encountered a frame size not defined by the v4l2
    // spec (should never happen).
    if (info.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
      // This is probably not quite the right thing to put here, but it should
      // be unique enough to trace back to this cause.
      errno = EPFNOSUPPORT;
      return 0;
    }
    resolutions[output_index].width = info.discrete.width;
    resolutions[output_index].height = info.discrete.height;
    output_index++;
  }
  return 1;
}

int SetResolution(WebcamInfo *webcam, uint32_t width, uint32_t height) {
  struct v4l2_format format;
  struct v4l2_requestbuffers buffer_request;
  struct v4l2_buffer *buffer_info;
  memset(&format, 0, sizeof(format));
  memset(&buffer_request, 0, sizeof(buffer_request));
  buffer_info = &(webcam->buffer_info);

  // Ensure that SetResolution hasn't been called before.
  if (webcam->image_buffer) return 0;

  // First, notify the device which video format and resolution we want.
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = width;
  format.fmt.pix.height = height;
  format.fmt.pix.pixelformat = YUYV_FORMAT_CODE;
  if (ioctl(webcam->fd, VIDIOC_S_FMT, &format) < 0) {
    return 0;
  }

  // Next, tell the device we want to use a single mmap'd buffer.
  buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer_request.memory = V4L2_MEMORY_MMAP;
  buffer_request.count = 1;
  if (ioctl(webcam->fd, VIDIOC_REQBUFS, &buffer_request) < 0) {
    return 0;
  }

  // Next, get the device to tell us how much memory to allocate for the frame
  // buffer.
  if (ioctl(webcam->fd, VIDIOC_QUERYBUF, buffer_info) < 0) {
    return 0;
  }

  // Allocate the buffer which the video frame will get written to.
  webcam->image_buffer = mmap(NULL, buffer_info->length,
    PROT_READ | PROT_WRITE, MAP_SHARED, webcam->fd, buffer_info->m.offset);
  if (webcam->image_buffer == MAP_FAILED) {
    printf("MAP_FAILED\n");
    return 0;
  }

  // Activate streaming mode.
  if (ioctl(webcam->fd, VIDIOC_STREAMON, &(buffer_info->type)) < 0) {
    return 0;
  }

  // Now we're ready to receive image data. Yay.
  memset(webcam->image_buffer, 0, buffer_info->length);
  webcam->resolution.width = width;
  webcam->resolution.height = height;
  return 1;
}

void GetResolution(WebcamInfo *webcam, uint32_t *width, uint32_t *height) {
  *width = webcam->resolution.width;
  *height = webcam->resolution.height;
}

int BeginLoadingNextFrame(WebcamInfo *webcam) {
  if (ioctl(webcam->fd, VIDIOC_QBUF, &(webcam->buffer_info)) < 0) {
    return 0;
  }
  return 1;
}

int GetFrameBuffer(WebcamInfo *webcam, void **buffer, size_t *size) {
  if (ioctl(webcam->fd, VIDIOC_DQBUF, &(webcam->buffer_info)) < 0) {
    return 0;
  }
  *buffer = webcam->image_buffer;
  // The API allows bytesused to remain unset, in which case the length field
  // (the full size of the buffer) is used.
  if (webcam->buffer_info.bytesused) {
    *size = webcam->buffer_info.bytesused;
  } else {
    *size = webcam->buffer_info.length;
  }
  return 1;
}

// Converts v to a byte, clamping it between 0 and 255.
static uint8_t Clamp(float v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t) v;
}

// Reads the two pixels contained in the first four bytes of the input buffer
// and writes the two pixels located in the first 8 bytes of the output vuffer.
static void ConvertTwoPixels(uint8_t *input, uint8_t *output) {
  float y_1, y_2, u, v;
  uint8_t r1, g1, b1, r2, g2, b2;
  y_1 = input[0];
  u = input[1];
  y_2 = input[2];
  v = input[3];
  r1 = Clamp(1.164 * (y_1 - 16) + 1.596 * (v - 128));
  g1 = Clamp(1.164 * (y_1 - 16) - 0.813 * (v - 128) - 0.391 * (u - 128));
  b1 = Clamp(1.164 * (y_1 - 16) + 2.018 * (u - 128));
  r2 = Clamp(1.164 * (y_2 - 16) + 1.596 * (v - 128));
  g2 = Clamp(1.164 * (y_2 - 16) - 0.813 * (v - 128) - 0.391 * (u - 128));
  b2 = Clamp(1.164 * (y_2 - 16) + 2.018 * (u - 128));
  output[0] = 0xff;
  output[1] = b1;
  output[2] = g1;
  output[3] = r1;
  output[4] = 0xff;
  output[5] = b2;
  output[6] = g2;
  output[7] = r2;
}

int ConvertYUYVToRGBA(uint8_t *input, uint8_t *output, int w, int h,
    int input_pitch, int output_pitch) {
  int unused_input_row_bytes, unused_output_row_bytes;
  int x, y;
  if (input_pitch < (w * 2)) return 0;
  if (output_pitch < (w * 4)) return 0;
  // Since the pitch contains the number of bytes per row, calculate the number
  // of bytes to skip after each row of meaningful pixels.
  unused_input_row_bytes = input_pitch - (w * 2);
  unused_output_row_bytes = output_pitch - (w * 4);
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x += 2) {
      ConvertTwoPixels(input, output);
      input += 4;
      output += 8;
    }
    input += unused_input_row_bytes;
    output += unused_output_row_bytes;
  }
  return 1;
}
