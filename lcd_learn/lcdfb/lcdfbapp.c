#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include <linux/fb.h>


#define FB_DEV_PATH "/dev/fb0"
static int fd;
static int screen_size;
static unsigned char *map_base;
static struct fb_var_screeninfo var;

static void lcd_clear(uint32_t *vmem_base);
static void lcd_draw_line(uint32_t *vmem_base, int y);
static void lcd_draw_line_alpha(uint32_t *vmem_base, int y, uint8_t alpha);
static void lcd_draw_area(uint32_t *vmem_base, int y1, int y2, uint8_t alpha);

int main(int argc, char *argv[])
{
    int ret;
    fd = open(FB_DEV_PATH, O_RDWR);
    
    if(fd < 0)
    {
        perror(" Open dev error!");
        return -1;
    }

    /* get var information from the lcd device */
    ret = ioctl(fd, FBIOGET_VSCREENINFO, &var);
    if (ret < 0) {
        perror(" Get Fb var info error");
        goto err1;
    }

    /* mmap device to user buffer */
    printf("X:%d Y:%d bbp:%d\n", var.xres, var.yres, var.bits_per_pixel);
    screen_size = var.xres * var.yres * var.bits_per_pixel / 8;

    map_base = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(map_base == NULL) {
        perror("map device to user memory error");
        goto err1;
    }

    /* do some display */
    /**
     * 1. clear the screen display
     * 2. show display rgb one line
     */
    lcd_clear((uint32_t *)map_base);
    lcd_draw_area((uint32_t *)map_base, 0, 60, 0);
    lcd_draw_area((uint32_t *)map_base, 100, 160, 128);
    lcd_draw_area((uint32_t *)map_base, 250, 300, 255);
    sleep(1);
    ret = munmap(map_base, screen_size);
    if (ret < 0) {
        perror("unmap error please check");
    }

err1:
    close(fd);
}


/**
 * RGB888 --- 24bits == 3bytes
 */
static void lcd_clear(uint32_t *vmem_base) {
    int i,j;
    for (i = 0; i < var.yres; i++) {
        for (j = 0; j < var.xres; j++ ){
            vmem_base[i * 1024 + j] = 0x00000000;
        }
    }
}


static void lcd_draw_line(uint32_t *vmem_base, int y)
{
    for (int i = 0; i < 1024; i++ ) {
        vmem_base[y * 1024 + i] = 0x000000FF; // blue
    }
}
static void lcd_draw_line_alpha(uint32_t *vmem_base, int y, uint8_t alpha)
{
    for (int i = 0; i < 1024; i++ ) {
        vmem_base[y * 1024 + i] = alpha << 24 | 0x000000FF; // blue
    }
}

static void lcd_draw_area(uint32_t *vmem_base, int y1, int y2, uint8_t alpha)
{
    for (int i = y1; i < y2; i++) {
        lcd_draw_line_alpha(vmem_base, i, alpha);
    }
}