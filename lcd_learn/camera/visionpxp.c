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

#include "./pxp/pxp_device.h"
#include "./pxp/pxp_lib.h"
#include "./script/image_data.h"

#define DBG_DEBUG		3
#define DBG_INFO		2
#define DBG_WARNING		1
#define DBG_ERR			0

static int debug_level = DBG_INFO;
#define dbg(flag, fmt, args...)	{ if(flag <= debug_level)  printf("%s:%d "fmt, __FILE__, __LINE__,##args); }


#define CAM_DEV "/dev/video1"
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

static int pxp_engine_init(void);
static int camera_init(void);
static int framebuffer_init(void);
static void copy_image_to_fb(int left, int top, int width, int height, uint *img_ptr, struct fb_var_screeninfo *screen_info);
static void camera_enable(void);
static void camera_disable(void);

int main(int argc, char *argv[])
{
    framebuffer_init();
    camera_init();

    pxp_engine_init();

    camera_enable();
    printf("Starting PXP-accelerated Preview...\n");
    
    camera_disable();
    close(fd_cam);
    close(fd_pxp);
    close(fd_fb);
    return 0;

    while (1)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(fd_cam, VIDIOC_DQBUF, &buf) < 0) {
            break;
        }
        
        /* Start PXP Converting Process */
        struct v4l2_buffer pxp_in = {0};
        pxp_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        pxp_in.memory = V4L2_MEMORY_MMAP;
        pxp_in.index = buf.index; 
        ioctl(fd_pxp, VIDIOC_QBUF, &pxp_in);

        struct v4l2_buffer pxp_out = {0};
        pxp_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        pxp_out.memory = V4L2_MEMORY_USERPTR;
        pxp_out.m.userptr = (unsigned long)map_base; 
        pxp_out.length = screen_size;
        ioctl(fd_pxp, VIDIOC_QBUF, &pxp_out);

        ioctl(fd_pxp, VIDIOC_DQBUF, &pxp_in);  
        ioctl(fd_pxp, VIDIOC_DQBUF, &pxp_out); 

        if (ioctl(fd_cam, VIDIOC_QBUF, &buf) < 0) {
            perror("Camera QBUF Fail");
            break;
        }

    }

    camera_disable();
    close(fd_cam);
    close(fd_pxp);
    close(fd_fb);
     
    return 0;
}

static int camera_init(void)
{

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

    return 0;
}


static void camera_enable(void)
{
     /**
      * Start Camera Streaming
      */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_cam, VIDIOC_STREAMON, &type);
}

static void camera_disable(void)
{
     /**
      * Disable Camera Streaming
      */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_cam, VIDIOC_STREAMOFF, &type);

}
static int framebuffer_init(void)
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

    return 0;
}
static int pxp_engine_init(void)
{
    /**
     * Initialize PXP Device
     * 
     */
	struct pxp_config_data *pxp_conf = NULL;
	struct pxp_proc_data *proc_data = NULL;
	int ret = 0, i;
	struct pxp_mem_desc mem;
	struct pxp_mem_desc mem_o;
	pxp_chan_handle_t pxp_chan;
	struct fb_var_screeninfo var;

	ret = pxp_init();
	if (ret < 0) {
		dbg(DBG_ERR, "pxp init err\n");
		return -1;
	}

	ret = pxp_request_channel(&pxp_chan);
	if (ret < 0) {
		dbg(DBG_ERR, "pxp request channel err\n");
		goto err0;
	}
	dbg(DBG_INFO, "requested chan handle %d\n", pxp_chan.handle);

	/* Prepare the channel parameters */
	memset(&mem, 0, sizeof(struct pxp_mem_desc));
	memset(&mem_o, 0, sizeof(struct pxp_mem_desc));
	mem.size = IMG_WIDTH * IMG_HEIGHT * 4;
	mem_o.size = screen_size;

	ret = pxp_get_mem(&mem);
	if (ret < 0) {
		dbg(DBG_DEBUG, "get mem err\n");
		goto err1;
	}
	dbg(DBG_DEBUG, "mem.virt_uaddr %08x, mem.phys_addr %08x, mem.size %d\n",
				mem.virt_uaddr, mem.phys_addr, mem.size);

	ret = pxp_get_mem(&mem_o);
	if (ret < 0) {
		dbg(DBG_ERR, "get mem_o err\n");
		goto err2;
	}

	dbg(DBG_DEBUG, "mem_o.virt_uaddr %08x, mem_o.phys_addr %08x, mem_o.size %d\n",
				mem_o.virt_uaddr, mem_o.phys_addr, mem_o.size);

	for (i = 0; i < (IMG_WIDTH * IMG_HEIGHT * 4 / 4); i++) {
		*((unsigned int*)mem.virt_uaddr + i) = image_data[i];
		if (i < 10)
			dbg(DBG_DEBUG, "[PxP In] 0x%08x 0x%08x\n",
				*((unsigned int *)mem.virt_uaddr + i),
				image_data[i]);
	}

	/* Configure the channel */
	pxp_conf = malloc(sizeof (*pxp_conf));
	memset(pxp_conf, 0, sizeof(*pxp_conf));
	proc_data = &pxp_conf->proc_data;

	/* Initialize non-channel-specific PxP parameters */
	proc_data->srect.left = 0;
	proc_data->srect.top = 0;
	proc_data->drect.left = 0;
	proc_data->drect.top = 0;
	proc_data->srect.width = IMG_WIDTH;
	proc_data->srect.height = IMG_HEIGHT;
	proc_data->drect.width =  320;
	proc_data->drect.height = 240;
	proc_data->scaling = 1;
	proc_data->hflip = 0;
	proc_data->vflip = 0;
	proc_data->rotate = 0;
	proc_data->bgcolor = 0;

	proc_data->overlay_state = 0;
	proc_data->lut_transform = PXP_LUT_NONE; //PXP_LUT_INVERT; //PXP_LUT_NONE;

	/*
	 * Initialize S0 parameters
	 */
	pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_XRGB32;
	pxp_conf->s0_param.width = IMG_WIDTH;
	pxp_conf->s0_param.height = IMG_HEIGHT;
	pxp_conf->s0_param.color_key = -1;
	pxp_conf->s0_param.color_key_enable = false;
	pxp_conf->s0_param.paddr = mem.phys_addr;

	dbg(DBG_DEBUG, "pxp_test s0 paddr %08x\n", pxp_conf->s0_param.paddr);
	/*
	 * Initialize OL parameters
	 * No overlay will be used for PxP operation
	 */
	 for (i=0; i < 8; i++) {
		pxp_conf->ol_param[i].combine_enable = false;
		pxp_conf->ol_param[i].width = 0;
		pxp_conf->ol_param[i].height = 0;
		pxp_conf->ol_param[i].pixel_fmt = PXP_PIX_FMT_XRGB32;
		pxp_conf->ol_param[i].color_key_enable = false;
		pxp_conf->ol_param[i].color_key = -1;
		pxp_conf->ol_param[i].global_alpha_enable = false;
		pxp_conf->ol_param[i].global_alpha = 0;
		pxp_conf->ol_param[i].local_alpha_enable = false;
	}

	/*
	 * Initialize Output channel parameters
	 * Output is Y-only greyscale
	 */
	pxp_conf->out_param.width = 1024;
	pxp_conf->out_param.height = 600;
	pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_XRGB32;
	if (proc_data->rotate % 180)
		pxp_conf->out_param.stride = 600;
	else
		pxp_conf->out_param.stride = 1024;

	pxp_conf->out_param.paddr = mem_o.phys_addr;
	dbg(DBG_DEBUG, "pxp_test out paddr %08x\n", pxp_conf->out_param.paddr);

	ret = pxp_config_channel(&pxp_chan, pxp_conf);
	if (ret < 0) {
		dbg(DBG_ERR, "pxp config channel err\n");
		goto err3;
	}

	ret = pxp_start_channel(&pxp_chan);
	if (ret < 0) {
		dbg(DBG_ERR, "pxp start channel err\n");
		goto err3;
	}

	ret = pxp_wait_for_completion(&pxp_chan, 3);
	if (ret < 0) {
		dbg(DBG_ERR, "pxp wait for completion err\n");
		goto err3;
	}

    /**
     * Show Display first and wait for 5s to display
     */
    copy_image_to_fb(0, 0, IMG_WIDTH, IMG_HEIGHT, (void *)mem.virt_uaddr, &vinfo);
    sleep(1);
    /**
     *  Reconfigure the Fb device
     * 
     */
    copy_image_to_fb(0, 0, 1024, 600, (void *)mem_o.virt_uaddr, &vinfo);

	dbg(DBG_INFO, "pxp_test instance finished!\n");
err4:
	close(fd_fb);
err3:
	pxp_put_mem(&mem_o);
err2:
	pxp_put_mem(&mem);
err1:
	free(pxp_conf);
	pxp_release_channel(&pxp_chan);
err0:
	pxp_uninit();

	return ret;
}
static void copy_image_to_fb(int left, int top, int width, int height, uint *img_ptr, struct fb_var_screeninfo *screen_info)
{
	int i;
	uint *fb_ptr =  (uint *)map_base;
	uint bytes_per_pixel;

	if ((width > screen_info->xres) || (height > screen_info->yres)) {
		dbg(DBG_ERR, "Bad image dimensions!\n");
		return;
	}

	bytes_per_pixel = screen_info->bits_per_pixel / 8;

	for (i = 0; i < height; i++) {
		memcpy(fb_ptr + ((i + top) * screen_info->xres_virtual + left) * bytes_per_pixel / 4,
			img_ptr + (i * width) * bytes_per_pixel /4,
			width * bytes_per_pixel);
	}
}