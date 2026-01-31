/**
 * 
 *  libgpiod API to control GPIO signal
 *  We wanna control spi-slave device waveshare oled 
 *  only two gpio pins are selected --  that is RST and DC 
 *  
 */
#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static struct gpiod_chip *chip;         /*gpio1 -- but gpiochip0 */
static struct gpiod_line *spi_dc_line; /* gpio1_06 */
static struct gpiod_line *spi_rst_line; /* gpio1_08 */

#define SPI_DC_LINE_NUMBER  2
#define SPI_RST_LINE_NUMBER 8
#define SPI_GPIO_CONTROLLER "/dev/gpiochip0"
int main(int argc, char *argv[])
{
 
    int ret;
    int i;
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

    /* set gpio line low/high value */
    for (i = 0; i < 10; i++)
    {
        gpiod_line_set_value(spi_dc_line, i % 2 == 0);
        printf("spi_dc_line set to value %d\n", i % 2 == 0);
        usleep(100); 
        gpiod_line_set_value(spi_rst_line, i % 2 == 0);
        printf("spi_rst_line set to value %d\n", i % 2 == 0);
        usleep(100); 

    }
     
    
    
release1:
    gpiod_line_release(spi_rst_line);
release2:
    gpiod_line_release(spi_dc_line);
releasechip:
    gpiod_chip_close(chip);

    return 0;
}