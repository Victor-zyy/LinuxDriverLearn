#ifndef _SPI_TRANSPARENT_OLED_H_
#define _SPI_TRANSPARENT_OLED_H_
#include <linux/types.h>


#define X_WIDTH                 64
#define Y_WIDTH                 128   

typedef struct _oled_display_struct
{
    u8  x;
    u8  y;
    u32 length;
    u8 display_buffer[];
}waveshare_oled_display_struct;

#endif
