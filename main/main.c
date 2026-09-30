/*
 * cam_tracker_s3 -- step 1: measure what the camera pipeline really delivers.
 *
 * Camera bring-up is reused unchanged from Waveshare's example_video_common
 * component. This file (1) opens the video device the way the original
 * main.c did, (2) pulls raw frames, (3) times them.
 *
 * Frame layout: 640x480, 2 bytes per pixel, packed 4:2:2 (YUYV-style), so the
 * brightness (Y) byte of pixel x sits at byte offset 2*x. Checked by eye on a
 * captured frame; the driver's "422P" label does not match the bytes.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "example_video_common.h"

#define NUM_BUFFERS   CONFIG_EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER
#define PHASE_FRAMES  100

static const char *TAG = "tracker";

typedef struct {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    size_t buf_len;
    void *buf[NUM_BUFFERS];
} cam_t;

static esp_err_t cam_open(cam_t *cam)
{
    memset(cam, 0, sizeof(*cam));

    cam->fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    ESP_RETURN_ON_FALSE(cam->fd >= 0, ESP_ERR_NOT_FOUND, TAG, "open %s failed", ESP_VIDEO_DVP_DEVICE_NAME);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_RETURN_ON_FALSE(ioctl(cam->fd, VIDIOC_REQBUFS, &req) == 0, ESP_FAIL, TAG, "REQBUFS failed");

    for (int i = 0; i < NUM_BUFFERS; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        ESP_RETURN_ON_FALSE(ioctl(cam->fd, VIDIOC_QUERYBUF, &b) == 0, ESP_FAIL, TAG, "QUERYBUF failed");

        cam->buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam->fd, b.m.offset);
        ESP_RETURN_ON_FALSE(cam->buf[i] != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "mmap failed");
        cam->buf_len = b.length;

        ESP_RETURN_ON_FALSE(ioctl(cam->fd, VIDIOC_QBUF, &b) == 0, ESP_FAIL, TAG, "QBUF failed");
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_FALSE(ioctl(cam->fd, VIDIOC_G_FMT, &fmt) == 0, ESP_FAIL, TAG, "G_FMT failed");
    cam->width = fmt.fmt.pix.width;
    cam->height = fmt.fmt.pix.height;
    cam->pixel_format = fmt.fmt.pix.pixelformat;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_FALSE(ioctl(cam->fd, VIDIOC_STREAMON, &type) == 0, ESP_FAIL, TAG, "STREAMON failed");
    return ESP_OK;
}

/* 640x480 packed 4:2:2 -> 320x240 grayscale: Y of every 2nd pixel of every 2nd row. */
static void extract_gray_half(const uint8_t *src, uint32_t w, uint32_t h, uint8_t *dst)
{
    const uint32_t ow = w / 2;
    const uint32_t oh = h / 2;
    for (uint32_t y = 0; y < oh; y++) {
        const uint8_t *row = src + (size_t)(y * 2) * w * 2;
        uint8_t *out = dst + (size_t)y * ow;
        for (uint32_t x = 0; x < ow; x++) {
            out[x] = row[x * 4];
        }
    }
}

static uint32_t mean_u8(const uint8_t *p, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) {
        s += p[i];
    }
    return (uint32_t)(s / n);
}

static void run_phase(cam_t *cam, uint8_t *gray, int frames, bool do_extract, const char *label)
{
    static bool size_checked = false;
    int64_t t_first = 0;
    int64_t t_last = 0;
    int64_t extract_us = 0;

    for (int n = 0; n < frames;) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;

        if (ioctl(cam->fd, VIDIOC_DQBUF, &b) != 0) {
            ESP_LOGE(TAG, "DQBUF failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!(b.flags & V4L2_BUF_FLAG_DONE)) {
            ioctl(cam->fd, VIDIOC_QBUF, &b);
            continue;
        }

        t_last = esp_timer_get_time();
        if (n == 0) {
            t_first = t_last;
        }
        if (!size_checked) {
            size_checked = true;
            uint32_t expect = cam->width * cam->height * 2;
            if ((uint32_t)b.bytesused != expect) {
                ESP_LOGW(TAG, "frame is %" PRIu32 " bytes, expected %" PRIu32 " for packed 4:2:2",
                         (uint32_t)b.bytesused, expect);
            }
        }

        if (do_extract) {
            int64_t t0 = esp_timer_get_time();
            extract_gray_half(cam->buf[b.index], cam->width, cam->height, gray);
            extract_us += esp_timer_get_time() - t0;
        }

        if (ioctl(cam->fd, VIDIOC_QBUF, &b) != 0) {
            ESP_LOGE(TAG, "QBUF failed");
        }
        n++;
    }

    double secs = (double)(t_last - t_first) / 1e6;
    double fps = (frames > 1 && secs > 0) ? (frames - 1) / secs : 0;
    if (do_extract) {
        uint32_t mean = mean_u8(gray, (cam->width / 2) * (cam->height / 2));
        ESP_LOGI(TAG, "[%s] %d frames, %.1f fps, extract %.2f ms/frame, mean gray %" PRIu32,
                 label, frames, fps, extract_us / 1000.0 / frames, mean);
    } else {
        ESP_LOGI(TAG, "[%s] %d frames, %.1f fps", label, frames, fps);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Same order as the Waveshare example: the sensor needs its XCLK right after boot. */
    ESP_ERROR_CHECK(example_video_init());

    cam_t cam;
    ESP_ERROR_CHECK(cam_open(&cam));
    ESP_LOGI(TAG, "video: %" PRIu32 "x%" PRIu32 " format=" V4L2_FMT_STR ", %u buffers of %u bytes",
             cam.width, cam.height, V4L2_FMT_STR_ARG(cam.pixel_format),
             (unsigned)NUM_BUFFERS, (unsigned)cam.buf_len);

    const size_t gray_len = (cam.width / 2) * (cam.height / 2);
    uint8_t *gray = heap_caps_malloc(gray_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (gray == NULL) {
        ESP_LOGW(TAG, "no internal RAM for the gray buffer, using the default heap");
        gray = malloc(gray_len);
    }
    ESP_ERROR_CHECK(gray ? ESP_OK : ESP_ERR_NO_MEM);

    run_phase(&cam, gray, PHASE_FRAMES, false, "capture only");
    while (1) {
        run_phase(&cam, gray, PHASE_FRAMES, true, "capture + gray 320x240");
    }
}
