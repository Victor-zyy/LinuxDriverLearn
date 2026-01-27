/**
 *  spidev1.c  using spi_controller /dev/spidev2.0 to control
 *  spi slave device like oled waveshare .etc.
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
#include <linux/spi/spidev.h>

#define SPI3_DEV_PATH "/dev/spidev2.0"

static int fd;
static uint32_t mode = SPI_MODE_3; /* CPOL = 1 CPHA = 1*/
static uint8_t bits = 8; /* send and recv data bits */
static uint32_t speed = 500000; /* send speed HZ */
static uint16_t delay = 10;

static int spi_init(void)
{
    int ret;
    fd = open(SPI3_DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open /dev/spidev2.0 fail!");
        return -1;
    }
    /* set SPI mode */
    ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret < 0){
        perror("SPI_IOC_WR_MODE err");
        goto fd_close;
    }
    /* set spi write data bits per words */
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret < 0){
        perror("SPI_IOC_WR_BITS_PER_WORD err");
        goto fd_close;
    }
    /* set spi operate working max speed */
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret < 0){
        perror("SPI_IOC_WR_MAX_SPEED_HZ err");
        goto fd_close;
    }

    /* print some information like that */
    printf("spi mode: 0x%x\n", mode);
    printf("bits per word: %d\n", bits);
    printf("max speed: %d Hz (%d KHz)\n", speed, speed / 1000);

    return 0;

fd_close:
    close(fd);
    return -1;

}

static int transfer(int fd, uint8_t const *tx, uint8_t const *rx, size_t len) {
    int ret;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = len,
        .delay_usecs = delay,
        .speed_hz = speed,
        .bits_per_word = bits,
    };

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        return -1;
    }
    
    return 0;
}

const uint8_t tx_cmd[] = {0x25, 0x45};
int main(int argc, char *argv[])
{
    int ret;

    /* do some configuration of spi like speed buffer size .etc. */
    ret = spi_init();
    if (ret < 0) {
        perror(" SPI init error for device !");
        return -1;
    } 
    
    ret = transfer(fd, tx_cmd, NULL, 2);
    if (ret < 0) {
        perror("transfer error!");
        return -1;
    }

    return 0;
}