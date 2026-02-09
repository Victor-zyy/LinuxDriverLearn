
/**
 *  spifbapp.c  using spifbdev /dev/fb0 to display
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h> /*uint8_t .etc*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

#define WIDTH   128
#define HEIGHT  64
#define BYTES_PER_ROW (WIDTH / 8)
#define BUF_SIZE (BYTES_PER_ROW * HEIGHT)

uint8_t fb_buf[BUF_SIZE];

static void set_pixel(int x, int y)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;

    int byte_index = y * BYTES_PER_ROW + (x / 8);
    int bit_in_byte = 7 - (x % 8);  // bit7 = 左边像素

    // the highest is the master bit
    fb_buf[byte_index] |= (1 << bit_in_byte);
}


void print_fb_buf(void)
{
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {

            int byte_index = y * BYTES_PER_ROW + (x / 8);
            int bit_index  = 7 - (x % 8);   // bit7 = 左边像素

            int pixel = (fb_buf[byte_index] >> bit_index) & 0x01;

            putchar(pixel ? '*' : '-');
        }
        putchar('\n');
    }
}

void fill_test_pattern(void)
{
    memset(fb_buf, 0x00, sizeof(fb_buf));

    // 1) 画一个矩形边框
    for (int x = 0; x < WIDTH; x++) {
        set_pixel(x, 0);             // 上边
        set_pixel(x, HEIGHT - 1);    // 下边
    }
    for (int y = 0; y < HEIGHT; y++) {
        set_pixel(0, y);             // 左边
        set_pixel(WIDTH - 1, y);     // 右边
    }

    // 2) 画一条左上到右下的对角线
    for (int i = 0; i < HEIGHT && i < WIDTH; i++) {
        set_pixel(i, i);
    }
}

int main(int argc, char *argv[])
{
    /* generate test buf pattern */
    fill_test_pattern();
    print_fb_buf();

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open fb error!");
        return -1;
    }

    ssize_t n = write(fd, fb_buf, BUF_SIZE);
    if (n != BUF_SIZE) {
        perror("write fb error!");
    }

    close(fd);
    return 0;
}
