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
#include <pthread.h>
#include <signal.h>
#include <getopt.h>

#include "../pxp/pxp_device.h"
#include "../pxp/pxp_lib.h"


#include "infer.h"

#define DEFAULT_WIDTH	320
#define DEFAULT_HEIGHT	240


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

#define MAX_V4L2_DEVICE_NR     64
#define	V4L2_BUF_NUM	6

int g_num_buffers;

struct buffer {
	void *start;
	struct v4l2_buffer buf;
};

struct pxp_control {
	int fmt_idx;
	int vfd;
	char *s0_infile;
	char *s1_infile;
	unsigned int out_addr;
	char *outfile;
	int outfile_state;
	struct buffer *buffers;
	int global_alpha;
	int global_alpha_val;
	int colorkey;
	unsigned int colorkey_val;
	int hflip;
	int vflip;
	int rotate;
	int rotate_pass;
	struct v4l2_rect s0;
	int dst_state;
	struct v4l2_rect dst;
	int wait;
	int screen_w, screen_h;
};

struct pxp_video_format {
	char *name;
	unsigned int bpp;
	unsigned int fourcc;
	enum v4l2_colorspace colorspace;
};

static struct pxp_video_format pxp_video_formats[] = {
	{
	 .name = "24-bit RGB",
	 .bpp = 4,
	 .fourcc = V4L2_PIX_FMT_RGB24,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },
	{
	 .name = "16-bit RGB 5:6:5",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_RGB565,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },
	{
	 .name = "16-bit RGB 5:5:5",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_RGB555,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 },
	{
	 .name = "YUV 4:2:0 Planar",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_YUV420,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
	{
	 .name = "YUV 4:2:2 Planar",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_YUV422P,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
	{
	 .name = "UYVY",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_UYVY,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
	{
	 .name = "YUYV",
	 .bpp = 2,
	 .fourcc = V4L2_PIX_FMT_YUYV,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
	{
	 .name = "YUV32",
	 .bpp = 4,
	 .fourcc = V4L2_PIX_FMT_YUV32,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 },
};

#define VERSION	"1.0"
#define MAX_LEN 512
#define DEFAULT_OUTFILE "out.pxp"
      
#define PXP_RES		0
#define PXP_DST		1
#define PXP_HFLIP	2
#define PXP_VFLIP	3
#define PXP_WIDTH	4
#define PXP_HEIGHT	5


static int fd_cam;
static struct buffer *bufs;
static struct v4l2_format fmt;
static struct v4l2_requestbuffers req;

static struct pxp_mem_desc mem;
static struct pxp_mem_desc mem_o;
static struct pxp_config_data *pxp_conf = NULL;
static struct pxp_proc_data *proc_data = NULL;
static pxp_chan_handle_t pxp_chan;

struct worker_args {
    int thread_id;

    struct v4l2_buffer dbuf;        
    int width, height;      
    int quit;               

    pthread_mutex_t lock;
    pthread_cond_t  cond;

    int has_task;
};

static int device_pxp_run(void);
static void copy_camera_to_buf(int left, int top, int width, int height, uint *img_ptr, uint* uaddr);

static struct worker_args ai_worker;
static pthread_t pxptid;

static void usage(char *bin)
{
	printf
	    ("Usage: %s [-a <n>] [-k 0xHHHHHHHH] [-o <outfile>] [-sx <width>] [-sy <height>] [-hf] [-vf] [-r <D>] [-res <x>:<y>] [-w <n>] [-dst ...] [-f <fmt>] <s0_in> <s1_in>\n",
	     bin);
}

static void help(char *bin)
{
	printf("pxp_qa - PxP QA test, v%s\n", VERSION);

	usage(bin);

	printf("\nPossible options:\n");
	printf("\t-a n\t\tset global alpha\n");
	printf("\t-h    \tprint help information\n");
	printf("\t-hf   \tflip image horizontally\n");
	printf("\t-k 0xHHHHHHHH   \tSet colorkey\n");
	printf("\t-dst <x>:<y>:<w>:<h>  \tset destination window\n");
	printf("\t-sx <width> \twidth of the LCD screen\n");
	printf("\t-sy <height> \theight of the LCD screen\n");
	printf("\t-o <outfile>  \tset outfile for virtual buffer\n");
	printf("\t-r   \trotate image\n");
	printf("\t-f <x>   \timage format\t0-RGB24  1-RGB565  2-RGB555  3-YUV420  4-YUV422  5-UYVY  6-YUYV 7-YUV444\n");
	printf("\t-res <w>:<h>  \tinput resolution\n");
	printf("\t-vf   \tflip image vertically\n");
	printf("\t-w n   \twait n seconds before exiting\n");
	printf("\t-?    \tprint this usage information\n");
}

static sigset_t sigset;
static int quitflag = 0;
static int signal_thread(void *arg)
{   
    int sig, err;
    
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
    
    while (1) {
        err = sigwait(&sigset, &sig);
        if (sig == SIGINT) {
            dbg(DBG_INFO, "Ctrl-C received\n");
        } else {
            dbg(DBG_ERR, "Unknown signal. Still exiting\n");
        }
        quitflag = 1;
        break;
    }
    
    return 0;
}

/**
 * device pxp thread
 */
int fd_file[6];

static int file_init(int fd[], size_t cnt)
{
	int ret = 0;
	int i ;
	char file_name[10];
	for (i = 0; i < cnt; i++) {	
		snprintf(file_name, sizeof(file_name), "fileimg%d", i);
		fd[i] = open(file_name, O_CREAT | O_RDWR, 0666);
		if (fd[i] < 0) {
			dbg(DBG_ERR, "Open %s%d file fail\n", file_name, i);
			ret = -1;
			break;
		}
	}

	return ret;
}
static void device_pxp_write_file(int fd)
{
	write(fd_file[fd], mem_o.virt_uaddr, 224 * 224 * 4);
}
static int device_pxp_thread(void *arg)
{   
    struct worker_args *p = (struct worker_args*)arg;

    dbg(DBG_INFO, "Worker %d started\n", p->thread_id);
    while (1) {
        pthread_mutex_lock(&p->lock);

        while (!p->has_task && !p->quit) {
            pthread_cond_wait(&p->cond, &p->lock);
        }

        if (p->quit) {
            pthread_mutex_unlock(&p->lock);
            break;
        }

        p->has_task = 0;
        pthread_mutex_unlock(&p->lock);

        dbg(DBG_DEBUG, "Worker %d \n", p->thread_id);
		device_pxp_run();
		infer_run(mem_o.virt_uaddr);	
		if (ioctl(fd_cam, VIDIOC_QBUF, &p->dbuf) < 0) {
			printf("VIDIOC_QBUF Cam PXP Device failed\n");
			break;
		}
    }

    printf("Worker %d exit\n", p->thread_id);
    return NULL;
}

static int camera_init(struct pxp_control *pxp)
{

	struct v4l2_buffer buf;
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
    req.count = V4L2_BUF_NUM + 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(fd_cam, VIDIOC_REQBUFS, &req) < 0) {
		printf("VIDIOC_QBUF error\n");
		return -1;
	}
    bufs = calloc(req.count, sizeof(*bufs));

    /**
     * Query Buf
     */
    for (int i = 0; i < V4L2_BUF_NUM; i++)
    {
		memset(&buf, 0, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;
		buf.index = i;
		buf.length = pxp->buffers[i].buf.length;
		buf.m.userptr = (unsigned long) pxp->buffers[i].start;
		if (ioctl (fd_cam, VIDIOC_QBUF, &buf) < 0) {
			printf("VIDIOC_QBUF error\n");
			return -1;
		}
    }

	/** Another buffer to map device */
	memset(&buf, 0, sizeof (buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_USERPTR;
	buf.index = V4L2_BUF_NUM;
	buf.length = mem.size;
	buf.m.userptr = (unsigned long) mem.virt_uaddr;
	if (ioctl (fd_cam, VIDIOC_QBUF, &buf) < 0) {
		printf("VIDIOC_QBUF For /dev/pxp_device error\n");
		return -1;
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

static int find_video_device(void)
{
	char v4l_devname[20] = "/dev/video";
	char index[3];
	char v4l_device[20];
	struct v4l2_capability cap;
	int fd_v4l;
	int i = 0;

	if ((fd_v4l = open(v4l_devname, O_RDWR, 0)) < 0) {
		printf("unable to open %s for output, continue searching "
			"device.\n", v4l_devname);
	}
	if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
		if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) {
			printf("Found v4l2 output device %s.\n", v4l_devname);
			return fd_v4l;
		}
	} else {
		close(fd_v4l);
	}

	/* continue to search */
	while (i < MAX_V4L2_DEVICE_NR) {
		strcpy(v4l_device, v4l_devname);
		sprintf(index, "%d", i);
		strcat(v4l_device, index);

		if ((fd_v4l = open(v4l_device, O_RDWR, 0)) < 0) {
			i++;
			continue;
		}
		if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap)) {
			close(fd_v4l);
			i++;
			continue;
		}
		if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) {
			printf("Found v4l2 output device %s.\n", v4l_device);
			break;
		}

		i++;
	}

	if (i == MAX_V4L2_DEVICE_NR)
		return -1;
	else
		return fd_v4l;
}

static struct pxp_control *video_pxp_init(int argc, char **argv)
{
	struct pxp_control *pxp;
	int opt;
	int long_index = 0;
	int tfd, fd_v4l;
	char buf[8];

	if ((fd_v4l = find_video_device()) < 0) {
		printf("Unable open v4l2 output device.\n");
		return NULL;
	}

		/* Disable screen blanking */
	tfd = open("/dev/tty0", O_RDWR);
	sprintf(buf, "%s", "\033[9;0]");
	write(tfd, buf, 7);
	close(tfd);

	pxp = calloc(1, sizeof(struct pxp_control));
	if (!pxp) {
		perror("failed to allocate PxP control object");
		return NULL;
	}

	pxp->vfd = fd_v4l;

	pxp->buffers = calloc(V4L2_BUF_NUM, sizeof(*pxp->buffers));

	if (!pxp->buffers) {
		perror("insufficient buffer memory");
		return NULL;
	}

		/* Init pxp control struct */
	pxp->s0_infile = argv[argc - 2];
	pxp->s1_infile =
	    strcmp(argv[argc - 1], "BLANK") == 0 ? NULL : argv[argc - 1];
	pxp->outfile = calloc(1, MAX_LEN);
	pxp->outfile_state = 0;
	strcpy(pxp->outfile, DEFAULT_OUTFILE);
	pxp->screen_w = pxp->s0.width = DEFAULT_WIDTH;
	pxp->screen_h = pxp->s0.height = DEFAULT_HEIGHT;
	pxp->fmt_idx = 6;	/* YUV420 */
	pxp->wait = 1;

	static const char *opt_string = "a:hk:o:ir:w:f:?";

	static const struct option long_opts[] = {
		{"dst", required_argument, NULL, PXP_DST},
		{"hf", no_argument, NULL, PXP_HFLIP}, 		// horizontal flip
		{"res", required_argument, NULL, PXP_RES},
		{"vf", no_argument, NULL, PXP_VFLIP},		// vertical flip
		{"sx", required_argument, NULL, PXP_WIDTH}, // input width
		{"sy", required_argument, NULL, PXP_HEIGHT},// input height
		{NULL, no_argument, NULL, 0}
	};

	for (;;) {
		opt = getopt_long_only(argc, argv, opt_string,
				       long_opts, &long_index);
		if (opt == -1)
			break;
		switch (opt) {
		case PXP_WIDTH:
			pxp->screen_w = pxp->s0.width = atoi(optarg);
			break;
		case PXP_HEIGHT:
			pxp->screen_h = pxp->s0.height = atoi(optarg);
			break;
		case PXP_RES:
			pxp->s0.width = atoi(strtok(optarg, ":"));
			pxp->s0.height = atoi(strtok(NULL, ":"));
			break;
		case PXP_DST:
			pxp->dst_state = 1;
			pxp->dst.left = atoi(strtok(optarg, ":"));
			pxp->dst.top = atoi(strtok(NULL, ":"));
			pxp->dst.width = atoi(strtok(NULL, ":"));
			pxp->dst.height = atoi(strtok(NULL, ":"));
			break;
		case PXP_HFLIP:
			pxp->hflip = 1;
			break;
		case PXP_VFLIP:
			pxp->vflip = 1;
			break;
		case 'a':
			pxp->global_alpha = 1;
			pxp->global_alpha_val = atoi(optarg);
			break;
		case 'h':
			usage(argv[0]);
			goto error;
		case 'k':
			pxp->colorkey = 1;
			pxp->colorkey_val = strtoul(optarg, NULL, 16);
			break;
		case 'o':
			pxp->outfile_state = 1;
			strncpy(pxp->outfile, optarg, MAX_LEN);
			break;
		case 'r':
			pxp->rotate = atoi(optarg);
			if ((pxp->rotate == 90) || (pxp->rotate == 270))
				pxp->rotate_pass = 1;
			break;
		case 'w':
			pxp->wait = atoi(optarg);
			break;
		case 'f':
			pxp->fmt_idx = atoi(optarg);
			break;
		case '?':
			help(argv[0]);
			goto error;
		default:
			usage(argv[0]);
		}
	}

	if ((optind == argc) || (2 != argc - optind)) {
		usage(argv[0]);
		goto error;
	}

	if ((pxp->rotate != 0) && (pxp->rotate != 90) &&
	    (pxp->rotate != 180) && (pxp->rotate != 270)) {
		printf("Rotation must be 0, 90, 180, or 270 degrees\n");
		goto error;
	}

	return pxp;

error:
	if (pxp)
		free(pxp);
	return NULL;
}

static int pxp_check_capabilities(struct pxp_control *pxp)
{
	struct v4l2_capability cap;

	if (ioctl(pxp->vfd, VIDIOC_QUERYCAP, &cap) < 0) {
		perror("VIDIOC_QUERYCAP");
		return 1;
	}

	if (!(cap.capabilities &
	      (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_OVERLAY))) {
		perror("video output overlay not detected\n");
		return 1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		perror("streaming support not detected\n");
		return 1;
	}

	return 0;
}

static int pxp_config_output(struct pxp_control *pxp)
{
	struct v4l2_output output;
	struct v4l2_format format;
	int out_idx = 1;

	/**
	 * VIDIOC_S_OUTPUT --> pxp_s_output --> fb information
	 */
	if (ioctl(pxp->vfd, VIDIOC_S_OUTPUT, &out_idx) < 0) {
		perror("failed to set output");
		return 1;
	}

	output.index = out_idx;

	if (ioctl(pxp->vfd, VIDIOC_ENUMOUTPUT, &output) >= 0) {
		pxp->out_addr = output.reserved[0];
		printf("V4L output %d (0x%08x): %s\n",
		       output.index, output.reserved[0], output.name);
	} else {
		perror("VIDIOC_ENUMOUTPUT");
		return 1;
	}

	/**
	 * VIDIOC_S_FMT --> video out format --> pxp_s_fmt_video_output
	 */
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format.fmt.pix.width = pxp->s0.width;
	format.fmt.pix.height = pxp->s0.height;
	format.fmt.pix.pixelformat = pxp_video_formats[pxp->fmt_idx].fourcc;
	if (ioctl(pxp->vfd, VIDIOC_S_FMT, &format) < 0) {
		perror("VIDIOC_S_FMT output");
		return 1;
	}

	printf("Video input format: %dx%d %s\n", pxp->s0.width, pxp->s0.height,
	       pxp_video_formats[pxp->fmt_idx].name);

	return 0;
}

static int pxp_config_buffer(struct pxp_control *pxp)
{
	struct v4l2_requestbuffers req;
	int ibcnt = V4L2_BUF_NUM;
	int i = 0;

	req.count = ibcnt;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_MMAP;
	printf("request count %d\n", req.count);
	g_num_buffers = req.count;

	if (ioctl(pxp->vfd, VIDIOC_REQBUFS, &req) < 0) {
		perror("VIDIOC_REQBUFS");
		return 1;
	}

	if (req.count < ibcnt) {
		perror("insufficient buffer control memory");
		return 1;
	}

	for (i = 0; i < ibcnt; i++) {
		pxp->buffers[i].buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		pxp->buffers[i].buf.memory = V4L2_MEMORY_MMAP;
		pxp->buffers[i].buf.index = i;

		if (ioctl(pxp->vfd, VIDIOC_QUERYBUF, &pxp->buffers[i].buf) < 0) {
			perror("VIDIOC_QUERYBUF");
			return 1;
		}

		pxp->buffers[i].start = mmap(NULL /* start anywhere */ ,
					     pxp->buffers[i].buf.length,
					     PROT_READ | PROT_WRITE,
					     MAP_SHARED,
					     pxp->vfd,
					     pxp->buffers[i].buf.m.offset);
		dbg(DBG_INFO, "pxp->buffers[%d].start 0x%08x length 0x%08x\n", i, pxp->buffers[i].start, pxp->buffers[i].buf.length);
		if (pxp->buffers[i].start == MAP_FAILED) {
			perror("failed to mmap pxp buffer");
			return 1;
		}
		/**
		 * 
		 */
		if (ioctl(pxp->vfd, VIDIOC_QBUF, &pxp->buffers[i].buf) < 0) {
			printf("VIDIOC_QBUF failed\n");
			return 1;
		}
	}

	return 0;
}

static int pxp_config_windows(struct pxp_control *pxp)
{
	struct v4l2_framebuffer fb;
	struct v4l2_format format;
	struct v4l2_crop crop;

	/* Set FB overlay options */
	fb.flags = V4L2_FBUF_FLAG_OVERLAY;

	if (pxp->global_alpha)
		fb.flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;
	if (pxp->colorkey)
		fb.flags |= V4L2_FBUF_FLAG_CHROMAKEY;
	if (ioctl(pxp->vfd, VIDIOC_S_FBUF, &fb) < 0) {
		perror("VIDIOC_S_FBUF");
		return 1;
	}

	/* Set overlay source window */
	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
	format.fmt.win.global_alpha = pxp->global_alpha_val;
	format.fmt.win.chromakey = pxp->colorkey_val;
	format.fmt.win.w.left = 0;
	format.fmt.win.w.top = 0;
	format.fmt.win.w.width = pxp->s0.width;
	format.fmt.win.w.height = pxp->s0.height;
	printf("win.w.l/t/w/h = %d/%d/%d/%d\n", format.fmt.win.w.left,
	       format.fmt.win.w.top,
	       format.fmt.win.w.width, format.fmt.win.w.height);
	if (ioctl(pxp->vfd, VIDIOC_S_FMT, &format) < 0) {
		perror("VIDIOC_S_FMT output overlay");
		return 1;
	}

	/* Set cropping window */
	crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
	if (pxp->dst_state) {
		crop.c.left = pxp->dst.left;
		crop.c.top = pxp->dst.top;
		crop.c.width = pxp->dst.width;
		crop.c.height = pxp->dst.height;
	} else {
		if (pxp->rotate_pass) {
			int scale = 16 * pxp->screen_h / pxp->screen_w;
			if (pxp->rotate == 90 || pxp->rotate == 270) {
				crop.c.left = 0;
				crop.c.top = 0;
			}
			crop.c.width = pxp->screen_w * scale / 16;
			crop.c.height = pxp->screen_h * scale / 16;

			crop.c.width = (crop.c.width >> 3) << 3;
			crop.c.height = (crop.c.height >> 3) << 3;
		} else {
			crop.c.left = 0;
			crop.c.top = 0;
			crop.c.width = pxp->s0.width;
			crop.c.height = pxp->s0.height;
		}
	}
	printf("crop.c.l/t/w/h = %d/%d/%d/%d\n", crop.c.left,
	       crop.c.top, crop.c.width, crop.c.height);
	if (ioctl(pxp->vfd, VIDIOC_S_CROP, &crop) < 0) {
		perror("VIDIOC_S_CROP");
		return 1;
	}

	return 0;
}

static int pxp_config_controls(struct pxp_control *pxp)
{
	struct v4l2_control vc;

	/* Horizontal flip */
	if (pxp->hflip)
		vc.value = 1;
	else
		vc.value = 0;
	vc.id = V4L2_CID_HFLIP;
	if (ioctl(pxp->vfd, VIDIOC_S_CTRL, &vc) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	/* Vertical flip */
	if (pxp->vflip)
		vc.value = 1;
	else
		vc.value = 0;
	vc.id = V4L2_CID_VFLIP;
	if (ioctl(pxp->vfd, VIDIOC_S_CTRL, &vc) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	/* Rotation */
	vc.id = V4L2_CID_PRIVATE_BASE;
	vc.value = pxp->rotate;
	if (ioctl(pxp->vfd, VIDIOC_S_CTRL, &vc) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	/* Set background color */
	vc.id = V4L2_CID_PRIVATE_BASE + 1;
	vc.value = 0x0;
	if (ioctl(pxp->vfd, VIDIOC_S_CTRL, &vc) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	/* Set s0 color key */
	vc.id = V4L2_CID_PRIVATE_BASE + 2;
	vc.value = 0xFFFFEE;
	if (ioctl(pxp->vfd, VIDIOC_S_CTRL, &vc) < 0) {
		perror("VIDIOC_S_CTRL");
		return 1;
	}

	return 0;
}

static int pxp_start(struct pxp_control *pxp)
{
	int i = 0;
	int fd;
	int s0_size;
	unsigned int total_time;
	struct timeval tv_start, tv_current;
	struct v4l2_buffer buf_cam;
	struct v4l2_buffer buf_pxp;
	buf_pxp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf_pxp.memory = V4L2_MEMORY_MMAP;
	buf_cam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf_cam.memory = V4L2_MEMORY_USERPTR;
	int ret = 0;

	if (pxp_video_formats[pxp->fmt_idx].fourcc ==  V4L2_PIX_FMT_YUV420)
		s0_size = pxp->s0.width * pxp->s0.height * 3 / 2;
	else
		s0_size = pxp->s0.width * pxp->s0.height
			* pxp_video_formats[pxp->fmt_idx].bpp;

	printf("pxp->s0.width %d pxp->s0.height %d bpp %d\n", pxp->s0.width, pxp->s0.height, pxp_video_formats[pxp->fmt_idx].bpp);

	camera_enable();

	printf("PxP processing: start...\n");

	gettimeofday(&tv_start, NULL);

	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (ioctl(pxp->vfd, VIDIOC_STREAMON, &type) < 0) {
		printf("Can't stream on\n");
		ret = -1;
		return ret;
	}
//goto end;

	/**
	 * Starting Capture and Show Display
	 */
	for (i = 0;; i++) {
		if (quitflag == 1)
			break;
		if (ioctl(fd_cam, VIDIOC_DQBUF, &buf_cam) < 0) {
			printf("VIDIOC_DQBUF Cam failed\n");
			ret = -1;
			break;
		}
		if (buf_cam.index == V4L2_BUF_NUM) {
			pthread_mutex_lock(&ai_worker.lock);
			ai_worker.dbuf 		 = buf_cam;
			ai_worker.width      = 224;
			ai_worker.height     = 224;
			ai_worker.has_task = 1;
			pthread_cond_signal(&ai_worker.cond);
			pthread_mutex_unlock(&ai_worker.lock);
		}else {
			if (ioctl(pxp->vfd, VIDIOC_DQBUF, &buf_pxp) < 0) {
				printf("VIDIOC_DQBUF Pxp failed\n");
				ret = -1;
				break;
			}
			if (ioctl(fd_cam, VIDIOC_QBUF, &buf_cam) < 0) {
				printf("VIDIOC_QBUF Cam failed\n");
				ret = -1;
				break;
			}
			if (ioctl(pxp->vfd, VIDIOC_QBUF, &buf_pxp) < 0) {
				printf("VIDIOC_QBUF Pxp failed\n");
				ret = -1;
				break;
			}
		}
	}

	pthread_mutex_lock(&ai_worker.lock);
	ai_worker.quit = 1;
	pthread_cond_signal(&ai_worker.cond);
	pthread_mutex_unlock(&ai_worker.lock);

	pthread_join(pxptid, NULL);

end:
	gettimeofday(&tv_current, NULL);
	total_time = (tv_current.tv_sec - tv_start.tv_sec) * 1000000L;
	total_time += tv_current.tv_usec - tv_start.tv_usec;
	printf("total time for %u frames = %u us, %lld fps\n", i, total_time,
	       (i * 1000000ULL) / total_time);

	close(fd);
	return ret;
}


static int camera_stop(void)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int i;

	printf("complete\n");

	/* Disable PxP */
	if (ioctl(fd_cam, VIDIOC_STREAMOFF, &type) < 0) {
		perror("Cam VIDIOC_STREAMOFF");
		return 1;
	}

	return 0;

}

static void camera_cleanup(void)
{
	close(fd_cam);
	free(bufs);
}

static int pxp_stop(struct pxp_control *pxp)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	int i;

	sleep(pxp->wait);
	printf("complete\n");

	/* Disable PxP */
	if (ioctl(pxp->vfd, VIDIOC_STREAMOFF, &type) < 0) {
		perror("VIDIOC_STREAMOFF");
		return 1;
	}

	for (i = 0; i < g_num_buffers; i++)
		munmap(pxp->buffers[i].start, pxp->buffers[i].buf.length);

	return 0;
}

static void pxp_cleanup(struct pxp_control *pxp)
{
	close(pxp->vfd);
	if (pxp->outfile)
		free(pxp->outfile);
	free(pxp->buffers);
	free(pxp);
}


static int device_pxp_init(void)
{
    /**
     * Initialize PXP Device
     * 
     */
	int ret = 0, i;

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
	mem.size = IMG_WIDTH * IMG_HEIGHT * 2; /** YUYV */
	mem_o.size = 224 * 224 * 4 ;

	ret = pxp_get_mem(&mem);
	if (ret < 0) {
		dbg(DBG_DEBUG, "get mem err\n");
		goto err1;
	}
	dbg(DBG_DEBUG, "mem.virt_uaddr %08x, mem.phys_addr %08x, mem.size %d\n",
				mem.virt_uaddr, mem.phys_addr, mem.size);


	#if 0
	pxp->pxp_buf.start = mem.virt_uaddr;
	pxp->pxp_buf.buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	pxp->pxp_buf.buf.memory = V4L2_MEMORY_MMAP;
	pxp->pxp_buf.buf.index  = i;
	#endif

	ret = pxp_get_mem(&mem_o);
	if (ret < 0) {
		dbg(DBG_ERR, "get mem_o err\n");
		goto err2;
	}

	dbg(DBG_DEBUG, "mem_o.virt_uaddr %08x, mem_o.phys_addr %08x, mem_o.size %d\n",
				mem_o.virt_uaddr, mem_o.phys_addr, mem_o.size);

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
	proc_data->drect.width =  224;
	proc_data->drect.height = 224;
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
	//pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_XRGB32;
	pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_YUYV;
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
	pxp_conf->out_param.width = 224;
	pxp_conf->out_param.height = 224;
	pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_XRGB32;
	if (proc_data->rotate % 180)
		pxp_conf->out_param.stride = 224;
	else
		pxp_conf->out_param.stride = 224;

	pxp_conf->out_param.paddr = mem_o.phys_addr;

	return ret;

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

static int device_pxp_run(void) {
	int ret;

	//copy_camera_to_buf(0, 0, IMG_WIDTH, IMG_HEIGHT, img_buf, mem.virt_uaddr);

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
	dbg(DBG_DEBUG, "pxp converted completed\n");

	return ret;

err3:
	pxp_put_mem(&mem_o);
	pxp_put_mem(&mem);
	free(pxp_conf);
	pxp_release_channel(&pxp_chan);
	pxp_uninit();
	return ret;
}


static void device_pxp_deinit(void)
{
	pxp_put_mem(&mem_o);
	pxp_put_mem(&mem);
	free(pxp_conf);
	pxp_release_channel(&pxp_chan);
	pxp_uninit();
}

static void copy_camera_to_buf(int left, int top, int width, int height, uint *img_ptr, uint* uaddr)
{
	memcpy(uaddr, img_ptr, IMG_WIDTH * IMG_HEIGHT * 2);
}

static int worker_thread_init(struct worker_args *w_arg)
{
	w_arg->width  = 224;
	w_arg->height = 224;
	w_arg->quit = 0;
	memset(&w_arg->dbuf, 0, sizeof(w_arg->dbuf));
    w_arg->has_task = 0;
	w_arg->thread_id = 1;
	pthread_mutex_init(&w_arg->lock, NULL);
	pthread_cond_init(&w_arg->cond, NULL);

	return 0;
}


int main(int argc, char *argv[])
{
	pthread_t sigtid;
	struct pxp_control *pxp;

	if (!(pxp = video_pxp_init(argc, argv)))
		return 1;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	pthread_create(&sigtid, NULL, (void *)&signal_thread, NULL);

	if (worker_thread_init(&ai_worker))
		return 1;

	if (device_pxp_init())
		return 1;

	/**
	 * Inference Init
	 */
	model_init();
	label_init();

	pthread_create(&pxptid, NULL, (void *)&device_pxp_thread, &ai_worker);


	if (pxp_check_capabilities(pxp))
		return 1;

	if (pxp_config_output(pxp))
		return 1;

	if (pxp_config_buffer(pxp))
		return 1;

    if (camera_init(pxp)) 
		return 1;

#if 0
	if (pxp_read_infiles(pxp))
		return 1;

#endif

	if (pxp_config_windows(pxp))
		return 1;

	if (pxp_config_controls(pxp))
		return 1;


	if (pxp_start(pxp))
		return 1;

	if (camera_stop()) 
		return 1;

	camera_cleanup();

	if (pxp_stop(pxp))
		return 1;

#if 0
	if (pxp->outfile_state)
		if (pxp_write_outfile(pxp))
			return 1;
#endif
	pxp_cleanup(pxp);
	device_pxp_deinit();

	return 0;
}
