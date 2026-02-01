/**
 *  spiapp.c file simple driver for waveshare
 *  transparent oled display
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h> /*uint8_t .etc*/
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

/* sub dir include file */
#include <oledgui/GUI_Paint.h>

#define SPI_OLED_PATH   "/dev/spioled"

static int fd;

#define OLED_WIDTH  64
#define OLED_HEIGHT 128

static void msleep(uint32_t ms)
{
    usleep(ms * 1000);
}
static int oled_display(const UBYTE *Image)
{
    int num;
    num = write(fd, Image, 8 * 128);
    assert( num == 8 * 128 );
    return 0;
}

static int oled_display_test(void) {
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
    Paint_DrawString_EN(50, 10, "EmbeddedZynex", &Font8, WHITE, BLACK);
    oled_display(BlackImage);
    
    free(BlackImage);
    return 0;
}

int main(int argc, char *argv[])
{

    int ret;
    fd = open(SPI_OLED_PATH, O_RDWR); 
    if (fd < 0) {
        perror(" open file error !\n");
        return -1;
    }

    ret = oled_display_test();
    if (ret < 0) {
        perror(" oled display error!\n");
        return -1;
    }
    sleep(1);
    printf("oled display device model test ok\n");

    return 0;
}