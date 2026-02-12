#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/fb.h>

#define CAM_DEV "/dev/video1"
#define PXP_DEV "/dev/pxp_device"
#define FB_DEV  "/dev/fb0"

#define IMG_WIDTH   640
#define IMG_HEIGHT  480
#define BUF_COUNT   4

static int fd_cam;
static struct v4l2_format fmt;
static struct v4l2_requestbuffers req;

struct buffer
{
    void *start;
    size_t length;
    unsigned long paddr;
};

static struct buffer *bufs;


static int fd_pxp;
static int fd_fb;

static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int screen_size;
static void *map_base;

static inline uint32_t xrgb8888(int r, int g, int b);
static void yuyv_to_xrgb8888(uint8_t *yuyv, uint32_t *rgb, int width, int height);

int main(int argc, char *argv[])
{

    /**
     *  Framebuffer Device Info 
     * 
     */
    fd_fb = open(FB_DEV, O_RDWR);
    if (fd_fb < 0) {
        perror("Open FB");
        return -1;
    }
    ioctl(fd_fb, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo);

    screen_size = vinfo.xres * vinfo.yres * ( vinfo.bits_per_pixel >> 3 );
    map_base = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);

    /**
     * Clear the Screen
     */
    memset(map_base, 0, screen_size);

    /**
     * Initialize the Camera 
     */
    fd_cam = open(CAM_DEV, O_RDWR);
    if (fd_cam < 0) {
        perror("Open CAM");
        exit(-1);
    }
    /**
     * Set the camera fmt
     */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = IMG_WIDTH;
    fmt.fmt.pix.height = IMG_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if (ioctl(fd_cam, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Set CAM FMT");
        exit(-1);
    }

    /**
     * Requeset v4l2_requesetbuffers
     */
    req.count = BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd_cam, VIDIOC_REQBUFS, &req);
    bufs = calloc(req.count, sizeof(*bufs));

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //ioctl(fd_cam, VIDIOC_STREAMON, &type);
    /**
     * Query Buf
     */
    for (int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(fd_cam, VIDIOC_QUERYBUF, &buf);
        /** map to user layer */
        bufs[i].length = buf.length;
        bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_cam, buf.m.offset);
        printf("bufs[%d] offset 0x%08x start %p\n", i, buf.m.offset, bufs[i].start);
        bufs[i].paddr = buf.m.offset; // not sure of it

        /** put the buffer to video wait the image to flush  */
        ioctl(fd_cam, VIDIOC_QBUF, &buf); // different
    }

    printf("Starting Camera Preview...\n");
    
    while (1)
    {
        struct v4l2_buffer buf = {0}; 
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
        buf.memory = V4L2_MEMORY_MMAP; 
        ioctl(fd_cam, VIDIOC_QBUF, &buf);
        /** Get one frame from Video queue */
        ioctl(fd_cam, VIDIOC_DQBUF, &buf); 
        yuyv_to_xrgb8888(bufs[buf.index].start, map_base, IMG_WIDTH, IMG_HEIGHT); 
    }
    ioctl(fd_cam, VIDIOC_STREAMOFF, &type);
    close(fd_cam);
    close(fd_pxp);
    close(fd_fb);
     
    return 0;
}

static inline uint32_t xrgb8888(int r, int g, int b)
{
    return (0xFF << 24) |
           ((r & 0xFF) << 16) |
           ((g & 0xFF) << 8)  |
           (b & 0xFF);
}

static void yuyv_to_xrgb8888(uint8_t *yuyv, uint32_t *rgb, int width, int height)
{
    // 640 * 480
    int frame_size = width * height * 2;
    static uint32_t index = 0;
    uint32_t *addr = rgb;

    for (int i = 0; i < frame_size; i += 4) {
        int y0 = yuyv[i + 0];
        int u  = yuyv[i + 1] - 128;
        int y1 = yuyv[i + 2];
        int v  = yuyv[i + 3] - 128;

        int r0 = y0 + 1.402 * v;
        int g0 = y0 - 0.344 * u - 0.714 * v;
        int b0 = y0 + 1.772 * u;

        int r1 = y1 + 1.402 * v;
        int g1 = y1 - 0.344 * u - 0.714 * v;
        int b1 = y1 + 1.772 * u;

        rgb[0] = xrgb8888(r0, g0, b0);
        rgb[1] = xrgb8888(r1, g1, b1);

        // 640 * 480 * 2 
        
        // i 0 4 640
        if (i > 0 && i % 1280 == 0) {
            index++;
            rgb = addr + index * 1024;
        }
        else
            rgb += 2;
    }
    index = 0;
}
