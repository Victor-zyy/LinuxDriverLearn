/**
 *  spioled1.c file simple driver for waveshare
 *  transparent oled display
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
#include <gpiod.h>

/* sub dir include file */
#include <oledgui/GUI_Paint.h>

#define SPI3_DEV_PATH "/dev/spidev2.0"
#define SPI_DC_LINE_NUMBER  2
#define SPI_RST_LINE_NUMBER 8
#define SPI_GPIO_CONTROLLER "/dev/gpiochip0"
#define OLED_WIDTH    64
#define OLED_HEIGHT   128

static int fd;
static uint32_t mode = SPI_MODE_3; /* CPOL = 1 CPHA = 1*/
static uint8_t bits = 8; /* send and recv data bits */
static uint32_t speed = 500000; /* send speed HZ */
static uint16_t delay = 5000; /* usecs */
static struct gpiod_chip *chip;         /*gpio1 -- but gpiochip0 */
static struct gpiod_line *spi_dc_line; /* gpio1_06 */
static struct gpiod_line *spi_rst_line; /* gpio1_08 */

static int spi_gpiod_init(void)
{
 
    int ret;
    /* get the GPIO controller */
    chip = gpiod_chip_open(SPI_GPIO_CONTROLLER);

    if (chip == NULL){
        perror("get the gpiochip0 err!");
        return -1;
    }

    /* get the gpio pin */
    spi_dc_line = gpiod_chip_get_line(chip, SPI_DC_LINE_NUMBER);
    if (spi_dc_line == NULL ) {
        perror("get the spi_dc_line err!");
        goto releasechip;
    }

    /* set gpio output direction */
    ret = gpiod_line_request_output(spi_dc_line, "spi_dc", 0);
    if (ret < 0) {
        perror("set the spi_dc_pin output err!");
        goto releasechip;
    }

    spi_rst_line = gpiod_chip_get_line(chip, SPI_RST_LINE_NUMBER);
    if (spi_rst_line == NULL ) {
        perror("get the spi_rst_line err!");
        goto release2;
    }

    /* set gpio output direction */
    ret = gpiod_line_request_output(spi_rst_line, "spi_rst", 0);
    if (ret < 0) {
        perror("set the spi_rst_line output err!");
        goto release2;
    }
    printf("spi gpiod init finished!\n");

    return 0;
    
    gpiod_line_release(spi_rst_line);
release2:
    gpiod_line_release(spi_dc_line);
releasechip:
    gpiod_chip_close(chip);

    return -1;
}

static void  spi_gpiod_deinit(void)
{
    gpiod_line_release(spi_rst_line);
    gpiod_line_release(spi_dc_line);
    gpiod_chip_close(chip);
}


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

static void msleep(uint32_t ms)
{
    usleep(ms * 1000);
}
static void oled_reset(void)
{

    /* set gpio line low/high value */
    gpiod_line_set_value(spi_rst_line, 1);
    gpiod_line_set_value(spi_rst_line, 0);
    msleep(100); /*100ms = 100000 */
    gpiod_line_set_value(spi_rst_line, 1);
}

static int oled_sig_cmd(uint8_t cmd)
{
    int ret;
    /* using spi bulk transfer */ /* need to be considered */
    gpiod_line_set_value(spi_dc_line, 0);
    
    ret = transfer(fd, &cmd, NULL, 1);
    if (ret < 0) {
        perror("transfer error!");
        return -1;
    }
    return 0;
}

static int oled_sig_data(uint8_t data)
{
    int ret;
    /* using spi bulk transfer */ /* need to be considered */
    gpiod_line_set_value(spi_dc_line, 1);
    
    ret = transfer(fd, &data, NULL, 1);
    if (ret < 0) {
        perror("transfer error!");
        return -1;
    }
    return 0;
}

/**
 * 
 * Bulk transfer
 */
const uint8_t tx_cmd_bulk[] = {0xAE, 0x00, 0x10, 
    0x20, 0x00,  0xA6, // 0xFF unknown
    0xA8, 0x3F,  0xD3, 0x00,
    0xD5, 0x80,  0xD9, 0x22, 0xA0, 0xC8,
    0xDA, 0x12, 0xDB, 0x40};
static int oled_init_reg(void)
{
    int ret;
    /* using spi bulk transfer */ /* need to be considered */
    gpiod_line_set_value(spi_dc_line, 0);
    
    ret = transfer(fd, tx_cmd_bulk, NULL, sizeof(tx_cmd_bulk));
    if (ret < 0) {
        perror("transfer error!");
        return -1;
    }

    gpiod_line_set_value(spi_dc_line, 1);
    return 0;
}

static int oled_init(void)
{
    int ret;

    oled_reset();
    ret = oled_init_reg();
    if (ret < 0) {
        perror("oled_init reg error!");
        return -1;
    }
    msleep(200);
    ret = oled_sig_cmd(0xAF);
    if (ret < 0) {
        perror("oled_sig cmd open error!");
        return -1;
    }
    printf("oled init finished!\n");

    return 0;
}


void oled_fill(uint8_t byte)
{
    // UWORD Width, Height;
    UWORD i, j;
    // Width = (OLED_1IN3_WIDTH % 8 == 0)? (OLED_1IN3_WIDTH / 8 ): (OLED_1IN3_WIDTH / 8 + 1);
    // Height = OLED_1IN3_HEIGHT; 
    for (i=0; i<8; i++) {
        /* set page address */
        /* set low column address */
        /* set high column address */
        for(j=0; j<128; j++) {
            /* write data */
            oled_sig_data(byte);  
        }
    }
}

void oled_clear(void)
{
    // UWORD Width, Height;
    UWORD i, j;
    // Width = (OLED_1IN3_WIDTH % 8 == 0)? (OLED_1IN3_WIDTH / 8 ): (OLED_1IN3_WIDTH / 8 + 1);
    // Height = OLED_1IN3_HEIGHT; 
    for (i=0; i<8; i++) {
        /* set page address */
        /* set low column address */
        /* set high column address */
        for(j=0; j<128; j++) {
            /* write data */
            oled_sig_data(0x00);  
        }
    }
}

void oled_display(const UBYTE *Image)
{
    UWORD temp;
    UWORD page, column;
    for (page=0; page<8; page++) {
        for (column =0; column < 128; column++) {
            temp = Image[ (7 - page) + column * 8];
            oled_sig_data(temp);
        }
    }
}

static int oled_display_test(void)
{
   // 0.Create a new image cache
	UBYTE *BlackImage;
	UWORD Imagesize = ((OLED_WIDTH%8==0)? (OLED_WIDTH/8): (OLED_WIDTH/8+1)) * OLED_HEIGHT;
	if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
			printf("Failed to apply for black memory...\r\n");
			return -1;
	}
	printf("Paint_NewImage\r\n");
	Paint_NewImage(BlackImage, OLED_WIDTH, OLED_HEIGHT, 0, BLACK);	

	printf("Drawing\r\n");
	//1.Select Image
	Paint_SelectImage(BlackImage);
	msleep(500);
	Paint_Clear(BLACK);
    // Drawing on the image
    printf("Drawing:page 2\r\n");			
    Paint_DrawString_EN(30, 10, "hello world", &Font8, WHITE, BLACK);
    Paint_DrawNum(50, 30, 123.456789, &Font8, 4, WHITE, BLACK);
    // Show image on page2
    oled_display(BlackImage);
    msleep(2000);	
    Paint_Clear(BLACK);		
    
    free(BlackImage);
    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    /* do some configuration of spi like speed buffer size .etc. */
    ret = spi_init();
    if (ret < 0) {
        perror(" SPI init error for device !");
        return -1;
    } 
     
    /* gpiod setting up */
    ret = spi_gpiod_init();
    if (ret < 0) {
        perror(" SPI GPIO init error for device !");
        return -1;
    } 

    /* oled display set up for ssd1309 chip */
    ret = oled_init();
    if (ret < 0) {
        perror(" GPIO init error for device !");
        return -1;
    } 

    msleep(2000);		
    /* Now it is time to display oled */
    oled_fill(0xff);
    oled_clear();
    //msleep(5000);
    ret = oled_display_test();
    if (ret < 0) {
        perror("oled display error!");
        return -1;
    }
    msleep(1000);
    spi_gpiod_deinit();
    return 0;
}