/*
 * Driver for the SPI SSD1309 OLED controller
 *
 * Copyright 2026 Zynex 
 *
 * Licensed under the GPLv2 or later.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define SSD1309FB_COMMAND   0x01
#define SSD1309FB_DATA      0x02

#define SSD1309FB_SET_ADDRESS_MODE	0x20
#define SSD1309FB_SET_ADDRESS_MODE_HORIZONTAL	(0x00)
#define SSD1309FB_SET_ADDRESS_MODE_VERTICAL	(0x01)
#define SSD1309FB_SET_ADDRESS_MODE_PAGE		(0x02)
#define SSD1309FB_SET_COL_RANGE		0x21
#define SSD1309FB_SET_PAGE_RANGE	0x22
#define SSD1309FB_CONTRAST		0x81
#define	SSD1309FB_CHARGE_PUMP		0x8d
#define SSD1309FB_SEG_REMAP_ON		0xa1
#define SSD1309FB_DISPLAY_OFF		0xae
#define SSD1309FB_SET_MULTIPLEX_RATIO	0xa8
#define SSD1309FB_DISPLAY_ON		0xaf
#define SSD1309FB_START_PAGE_ADDRESS	0xb0
#define SSD1309FB_SET_DISPLAY_OFFSET	0xd3
#define	SSD1309FB_SET_CLOCK_FREQ	0xd5
#define	SSD1309FB_SET_PRECHARGE_PERIOD	0xd9
#define	SSD1309FB_SET_COM_PINS_CONFIG	0xda
#define	SSD1309FB_SET_VCOMH		0xdb

#define MAX_CONTRAST 255

#define REFRESHRATE 1

static u_int refreshrate = REFRESHRATE;
module_param(refreshrate, uint, 0);

struct ssd1309fb_par;

struct ssd1309fb_deviceinfo {
	u32 default_vcomh;
	u32 default_dclk_div;
	u32 default_dclk_frq;
	int need_pwm;
	int need_chargepump;
};

struct ssd1309fb_par {
	u32 com_invdir;
	u32 com_lrremap;
	u32 com_offset;
	u32 com_seq;
	u32 contrast;
	u32 dclk_div;
	u32 dclk_frq;
	const struct ssd1309fb_deviceinfo *device_info;
    struct spi_device *spidev;
	u32 height;
	struct fb_info *info;
	u32 page_offset;
	u32 prechargep1;
	u32 prechargep2;
	struct pwm_device *pwm;
	u32 pwm_period;
	struct gpio_desc *reset;
	struct gpio_desc *dc;
	struct regulator *vbat_reg;
	u32 seg_remap;
	u32 vcomh;
	u32 width;
};

struct ssd1309fb_array {
	u8	type;
	u8	data[0];
};

static const struct fb_fix_screeninfo ssd1309fb_fix = {
	.id		    = "Zynex SSD1309",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo ssd1309fb_var = {
	.bits_per_pixel	= 1,
};

static struct ssd1309fb_array *ssd1309fb_alloc_array(u32 len, u8 type)
{
	struct ssd1309fb_array *array;

	array = kzalloc(sizeof(struct ssd1309fb_array) + len, GFP_KERNEL);
	if (!array)
		return NULL;

	array->type = type;

	return array;
}

static int spi_master_send(struct spi_device *spidev, struct ssd1309fb_array *array, u32 len)
{
    // spidev->dev->data => struct ssd1309fb_par  is that ok?
	struct fb_info *info = spi_get_drvdata(spidev);
	struct ssd1309fb_par *par = info->par;
    struct spi_message *message;
    struct spi_transfer *transfer;
    int error;

    if (array->type == SSD1309FB_COMMAND) {
		gpiod_set_value(par->dc, 0);
    } else if (array->type == SSD1309FB_DATA) {
		gpiod_set_value(par->dc, 1);
    } else {
        return 0;
    }

    // kzalloc space
    message  = kzalloc(sizeof(struct spi_message), GFP_KERNEL);
    transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    // Fullfill the two structs
    transfer->tx_buf = &(array->data);
    transfer->len = len; 
    spi_message_init(message);
    spi_message_add_tail(transfer, message);

    // Ready to Send Message
    error = spi_sync(spidev, message);
    kfree(message);
    kfree(transfer);
    if( 0 != error ){
        pr_err("spi_sync error please check!\r\n");
    }
    return len;
}

static int ssd1309fb_write_array(struct spi_device *spidev,
				 struct ssd1309fb_array *array, u32 len)
{
	int ret;

	//len += sizeof(struct ssd1309fb_array);

	ret = spi_master_send(spidev, array, len);
	if (ret != len) {
		dev_err(&spidev->dev, "Couldn't send SPI command or Data.\n");
		return ret;
	}

	return 0;
}

static inline int ssd1309fb_write_cmd(struct spi_device *spidev, u8 cmd)
{
	struct ssd1309fb_array *array;
	int ret;

	array = ssd1309fb_alloc_array(1, SSD1309FB_COMMAND);
	if (!array)
		return -ENOMEM;

	array->data[0] = cmd;

	ret = ssd1309fb_write_array(spidev, array, 1);
	kfree(array);

	return ret;
}

static inline int ssd1309fb_write_cmd2byte(struct spi_device *spidev, u8 cmd, u8 dat)
{
	struct ssd1309fb_array *array;
	int ret;

	array = ssd1309fb_alloc_array(2, SSD1309FB_COMMAND);
	if (!array)
		return -ENOMEM;

	array->data[0] = cmd;
	array->data[1] = dat;
	ret = ssd1309fb_write_array(spidev, array, 2);
	kfree(array);

	return ret;
}

static inline int ssd1309fb_write_cmd3byte(struct spi_device *spidev, u8 cmd, u8 dat1, u8 dat2)
{
	struct ssd1309fb_array *array;
	int ret;

	array = ssd1309fb_alloc_array(3, SSD1309FB_COMMAND);
	if (!array)
		return -ENOMEM;

	array->data[0] = cmd;
	array->data[1] = dat1;
	array->data[2] = dat2;

	ret = ssd1309fb_write_array(spidev, array, 3);
	kfree(array);

	return ret;
}

static void ssd1309fb_update_display(struct ssd1309fb_par *par)
{
	struct ssd1309fb_array *array;
	u8 *vmem = par->info->screen_base;
	int i, j, k;

	array = ssd1309fb_alloc_array(par->width * par->height / 8,
				      SSD1309FB_DATA);
	if (!array)
		return;

	/*
	 * The screen is divided in pages, each having a height of 8
	 * pixels, and the width of the screen. When sending a byte of
	 * data to the controller, it gives the 8 bits for the current
	 * column. I.e, the first byte are the 8 bits of the first
	 * column, then the 8 bits for the second column, etc.
	 *
	 *
	 * Representation of the screen, assuming it is 5 bits
	 * wide. Each letter-number combination is a bit that controls
	 * one pixel.
	 *
	 * A0 A1 A2 A3 A4
	 * B0 B1 B2 B3 B4
	 * C0 C1 C2 C3 C4
	 * D0 D1 D2 D3 D4
	 * E0 E1 E2 E3 E4
	 * F0 F1 F2 F3 F4
	 * G0 G1 G2 G3 G4
	 * H0 H1 H2 H3 H4
	 *
	 * If you want to update this screen, you need to send 5 bytes:
	 *  (1) A0 B0 C0 D0 E0 F0 G0 H0
	 *  (2) A1 B1 C1 D1 E1 F1 G1 H1
	 *  (3) A2 B2 C2 D2 E2 F2 G2 H2
	 *  (4) A3 B3 C3 D3 E3 F3 G3 H3
	 *  (5) A4 B4 C4 D4 E4 F4 G4 H4
	 */

	for (i = 0; i < (par->height / 8); i++) {
		for (j = 0; j < par->width; j++) {
			u32 array_idx = i * par->width + j;
			array->data[array_idx] = 0;
			for (k = 0; k < 8; k++) {
				u32 page_length = par->width * i; 					// page_length = 0 128 256
				u32 index = page_length + (par->width * k + j) / 8; // index = 0 , 16,  32, 48 .etc.
				u8 byte = *(vmem + index);
				//u8 bit = byte & (1 << (j % 8));  					// it assumes high is low
				u8 bit = byte & (1 << (7 - j % 8));  			
				//bit = bit >> (j % 8);
				bit = bit >> (7 - j % 8);
				array->data[array_idx] |= bit << k;					// oled display 8pixel MSB or LSB
			}
		}
	}

	ssd1309fb_write_array(par->spidev, array, par->width * par->height / 8);
	kfree(array);
}


static ssize_t ssd1309fb_write(struct fb_info *info, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct ssd1309fb_par *par = info->par;
	unsigned long total_size;
	unsigned long p = *ppos;
	u8 __iomem *dst;

	total_size = info->fix.smem_len;

	if (p > total_size)
		return -EINVAL;

	if (count + p > total_size)
		count = total_size - p;

	if (!count)
		return -EINVAL;

	dst = (void __force *) (info->screen_base + p);

	if (copy_from_user(dst, buf, count))
		return -EFAULT;

	ssd1309fb_update_display(par);

	*ppos += count;

	return count;
}

static int ssd1309fb_blank(int blank_mode, struct fb_info *info)
{
	struct ssd1309fb_par *par = info->par;

	if (blank_mode != FB_BLANK_UNBLANK)
		return ssd1309fb_write_cmd(par->spidev, SSD1309FB_DISPLAY_OFF);
	else
		return ssd1309fb_write_cmd(par->spidev, SSD1309FB_DISPLAY_ON);
}

static void ssd1309fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct ssd1309fb_par *par = info->par;
	sys_fillrect(info, rect);
	ssd1309fb_update_display(par);
}

static void ssd1309fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct ssd1309fb_par *par = info->par;
	sys_copyarea(info, area);
	ssd1309fb_update_display(par);
}

static void ssd1309fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct ssd1309fb_par *par = info->par;
	sys_imageblit(info, image);
	ssd1309fb_update_display(par);
}

static struct fb_ops ssd1309fb_ops = {
	.owner		    = THIS_MODULE,
	.fb_read	    = fb_sys_read,
	.fb_write	    = ssd1309fb_write,
	.fb_blank	    = ssd1309fb_blank,
	.fb_fillrect	= ssd1309fb_fillrect,
	.fb_copyarea	= ssd1309fb_copyarea,
	.fb_imageblit	= ssd1309fb_imageblit,
};

static void ssd1309fb_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	ssd1309fb_update_display(info->par);
}

static void ssd1309fb_fill(struct ssd1309fb_par *par, u8 data)
{

	struct ssd1309fb_array *array;
	int i, j;
	array = ssd1309fb_alloc_array(par->width * par->height / 8,
				      SSD1309FB_DATA);
	if (!array)
		return;
	
	for (i = 0; i < (par->height / 8); i++) {
		for (j = 0; j < par->width; j++) {
			u32 array_idx = i * par->width + j;
			array->data[array_idx] = data;
		}
	}

	ssd1309fb_write_array(par->spidev, array, par->width * par->height / 8);
	kfree(array);
}

static int ssd1309fb_init(struct ssd1309fb_par *par)
{
	int ret;
	u32 precharge, dclk, com_invdir, compins;
	struct pwm_args pargs;

	if (par->device_info->need_pwm) {
		par->pwm = pwm_get(&par->spidev->dev, NULL);
		if (IS_ERR(par->pwm)) {
			dev_err(&par->spidev->dev, "Could not get PWM from device tree!\n");
			return PTR_ERR(par->pwm);
		}

		/*
		 * FIXME: pwm_apply_args() should be removed when switching to
		 * the atomic PWM API.
		 */
		pwm_apply_args(par->pwm);

		pwm_get_args(par->pwm, &pargs);

		par->pwm_period = pargs.period;
		/* Enable the PWM */
		pwm_config(par->pwm, par->pwm_period / 2, par->pwm_period);
		pwm_enable(par->pwm);

		dev_dbg(&par->spidev->dev, "Using PWM%d with a %dns period.\n",
			par->pwm->pwm, par->pwm_period);
	};

	/* Set initial contrast */
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_CONTRAST, par->contrast);
	if (ret < 0)
		return ret;

	/* Set segment re-map */
	if (par->seg_remap) {
		ret = ssd1309fb_write_cmd(par->spidev, SSD1309FB_SEG_REMAP_ON);
		if (ret < 0)
			return ret;
	};

	/* Set COM direction */
	com_invdir = 0xc0 | (par->com_invdir & 0x1) << 3;
	ret = ssd1309fb_write_cmd(par->spidev,  com_invdir);
	if (ret < 0)
		return ret;

	/* Set multiplex ratio value */
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_MULTIPLEX_RATIO, par->height - 1);
	if (ret < 0)
		return ret;

	/* set display offset value */
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_DISPLAY_OFFSET, par->com_offset);
	if (ret < 0)
		return ret;

	/* Set clock frequency */
	dclk = ((par->dclk_div - 1) & 0xf) | (par->dclk_frq & 0xf) << 4;
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_CLOCK_FREQ, dclk);
	if (ret < 0)
		return ret;

	/* Set precharge period in number of ticks from the internal clock */
	precharge = (par->prechargep1 & 0xf) | (par->prechargep2 & 0xf) << 4;
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_PRECHARGE_PERIOD, precharge);
	if (ret < 0)
		return ret;

	/* Set COM pins configuration */
	compins = 0x02 | !(par->com_seq & 0x1) << 4
				   | (par->com_lrremap & 0x1) << 5;
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_COM_PINS_CONFIG, compins);
	if (ret < 0)
		return ret;

	/* Set VCOMH */
	ret = ssd1309fb_write_cmd2byte(par->spidev, SSD1309FB_SET_VCOMH, par->vcomh);
	if (ret < 0)
		return ret;

	/* Turn on the DC-DC Charge Pump */
#if 0
	ret = ssd1309fb_write_cmd(par->spidev, SSD1309FB_CHARGE_PUMP);
	if (ret < 0)
		return ret;

	ret = ssd1309fb_write_cmd(par->spidev,
		BIT(4) | (par->device_info->need_chargepump ? BIT(2) : 0));
	if (ret < 0)
		return ret;
#endif
	/* Switch to horizontal addressing mode */
	ret = ssd1309fb_write_cmd(par->spidev, SSD1309FB_SET_ADDRESS_MODE);
	if (ret < 0)
		return ret;

	ret = ssd1309fb_write_cmd(par->spidev,
				  SSD1309FB_SET_ADDRESS_MODE_HORIZONTAL);
	if (ret < 0)
		return ret;

	/* Set column range */
	ret = ssd1309fb_write_cmd3byte(par->spidev, SSD1309FB_SET_COL_RANGE, 0x0, par->width - 1);

	/* Set page range */
	ret = ssd1309fb_write_cmd3byte(par->spidev, SSD1309FB_SET_PAGE_RANGE, 0x0, par->page_offset + (par->height / 8) - 1);
	if (ret < 0)
		return ret;

	/* Clear the screen */
	ssd1309fb_update_display(par);

	/* Turn on the display */
	ret = ssd1309fb_write_cmd(par->spidev, SSD1309FB_DISPLAY_ON);
	if (ret < 0)
		return ret;

	/* just for test */
	mdelay(500);
	ssd1309fb_fill(par, 0xff);
	return 0;
}

static int ssd1309fb_update_bl(struct backlight_device *bdev)
{
	struct ssd1309fb_par *par = bl_get_data(bdev);
	int ret;
	int brightness = bdev->props.brightness;

	par->contrast = brightness;

	ret = ssd1309fb_write_cmd(par->spidev, SSD1309FB_CONTRAST);
	if (ret < 0)
		return ret;
	ret = ssd1309fb_write_cmd(par->spidev, par->contrast);
	if (ret < 0)
		return ret;
	return 0;
}

static int ssd1309fb_get_brightness(struct backlight_device *bdev)
{
	struct ssd1309fb_par *par = bl_get_data(bdev);

	return par->contrast;
}

static int ssd1309fb_check_fb(struct backlight_device *bdev,
				   struct fb_info *info)
{
	return (info->bl_dev == bdev);
}

static const struct backlight_ops ssd1309fb_bl_ops = {
	.options	    = BL_CORE_SUSPENDRESUME,
	.update_status	= ssd1309fb_update_bl,
	.get_brightness	= ssd1309fb_get_brightness,
	.check_fb	    = ssd1309fb_check_fb,
};

static struct ssd1309fb_deviceinfo ssd1309fb_ssd1309_deviceinfo = {
	.default_vcomh = 0x40,
	.default_dclk_div = 1, //0000 + 1
	.default_dclk_frq = 8, //1000b
};

static const struct of_device_id spi_ssd1309fb_of_match[] = {
	{
		.compatible = "zynex,ssd1309fb-spi",
		.data = (void *)&ssd1309fb_ssd1309_deviceinfo,
	},
	{},
};
MODULE_DEVICE_TABLE(of, spi_ssd1309fb_of_match);

static int spi_ssd1309fb_probe(struct spi_device *spidev)
{
	struct backlight_device *bl;
	char bl_name[12];
	struct fb_info *info;
	struct device_node *node = spidev->dev.of_node;
	struct fb_deferred_io *ssd1309fb_defio;
	u32 vmem_size;
	struct ssd1309fb_par *par;
	u8 *vmem;
	int ret;

	if (!node) {
		dev_err(&spidev->dev, "No device tree data found!\n");
		return -EINVAL;
	}

	info = framebuffer_alloc(sizeof(struct ssd1309fb_par), &spidev->dev);
	if (!info) {
		dev_err(&spidev->dev, "Couldn't allocate framebuffer.\n");
		return -ENOMEM;
	}

	par = info->par;
	par->info = info;
	par->spidev = spidev;

	par->device_info = of_device_get_match_data(&spidev->dev);

	par->reset = devm_gpiod_get_optional(&spidev->dev, "reset",
					     GPIOD_OUT_LOW);

	if (IS_ERR(par->reset)) {
		dev_err(&spidev->dev, "failed to get reset gpio: %ld\n",
			PTR_ERR(par->reset));
		ret = PTR_ERR(par->reset);
		goto fb_alloc_error;
	}

	par->dc = devm_gpiod_get_optional(&spidev->dev, "dc",
					     GPIOD_OUT_LOW);

	if (IS_ERR(par->dc)) {
		dev_err(&spidev->dev, "failed to get reset gpio: %ld\n",
			PTR_ERR(par->reset));
		ret = PTR_ERR(par->reset);
		goto fb_alloc_error;
	}
	par->vbat_reg = devm_regulator_get_optional(&spidev->dev, "vbat");
	if (IS_ERR(par->vbat_reg)) {
		ret = PTR_ERR(par->vbat_reg);
		if (ret == -ENODEV) {
			par->vbat_reg = NULL;
		} else {
			dev_err(&spidev->dev, "failed to get VBAT regulator: %d\n",
				ret);
			goto fb_alloc_error;
		}
	}

	if (of_property_read_u32(node, "zynex,width", &par->width))	// oled width when setting 128
		par->width = 96;

	if (of_property_read_u32(node, "zynex,height", &par->height)) // oled height when setting 64
		par->height = 16;

	if (of_property_read_u32(node, "zynex,page-offset", &par->page_offset)) //
		par->page_offset = 1;

	if (of_property_read_u32(node, "zynex,com-offset", &par->com_offset))
		par->com_offset = 0;

	if (of_property_read_u32(node, "zynex,prechargep1", &par->prechargep1))
		par->prechargep1 = 2;

	if (of_property_read_u32(node, "zynex,prechargep2", &par->prechargep2))
		par->prechargep2 = 2;

	par->seg_remap = !of_property_read_bool(node, "zynex,segment-no-remap");
	par->com_seq = of_property_read_bool(node, "zynex,com-seq");
	par->com_lrremap = of_property_read_bool(node, "zynex,com-lrremap");
	par->com_invdir = of_property_read_bool(node, "zynex,com-invdir");

	par->contrast = 127;
	par->vcomh = par->device_info->default_vcomh;

	/* Setup display timing */
	par->dclk_div = par->device_info->default_dclk_div;
	par->dclk_frq = par->device_info->default_dclk_frq;

	vmem_size = par->width * par->height / 8;

	vmem = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(vmem_size));
	if (!vmem) {
		dev_err(&spidev->dev, "Couldn't allocate graphical memory.\n");
		ret = -ENOMEM;
		goto fb_alloc_error;
	}

	ssd1309fb_defio = devm_kzalloc(&spidev->dev, sizeof(*ssd1309fb_defio),
				       GFP_KERNEL);
	if (!ssd1309fb_defio) {
		dev_err(&spidev->dev, "Couldn't allocate deferred io.\n");
		ret = -ENOMEM;
		goto fb_alloc_error;
	}

	ssd1309fb_defio->delay = HZ / refreshrate;
	ssd1309fb_defio->deferred_io = ssd1309fb_deferred_io;

	info->fbops = &ssd1309fb_ops;
	info->fix = ssd1309fb_fix;
	info->fix.line_length = par->width / 8;
	info->fbdefio = ssd1309fb_defio;

	info->var = ssd1309fb_var;
	info->var.xres = par->width;
	info->var.xres_virtual = par->width;
	info->var.yres = par->height;
	info->var.yres_virtual = par->height;

	info->var.red.length = 1;
	info->var.red.offset = 0;
	info->var.green.length = 1;
	info->var.green.offset = 0;
	info->var.blue.length = 1;
	info->var.blue.offset = 0;

	info->screen_base = (u8 __force __iomem *)vmem;
	info->fix.smem_start = __pa(vmem);
	info->fix.smem_len = vmem_size;

	fb_deferred_io_init(info);

	//i2c_set_clientdata(client, info);
	par->spidev = spidev;	
	par->spidev->mode = SPI_MODE_3;
	par->spidev->max_speed_hz = 500000; // 50khz
	par->spidev->bits_per_word = 8;
	spi_setup(spidev);
    spi_set_drvdata(spidev, info);

	if (par->reset) {
		/* Reset the screen */
		gpiod_set_value(par->reset, 1);
		gpiod_set_value(par->reset, 0);
		mdelay(100);
		gpiod_set_value(par->reset, 1);
		mdelay(100);
	}

	if (par->vbat_reg) {
		ret = regulator_enable(par->vbat_reg);
		if (ret) {
			dev_err(&spidev->dev, "failed to enable VBAT: %d\n",
				ret);
			goto reset_oled_error;
		}
	}

	ret = ssd1309fb_init(par);
	par->contrast = 127;
	if (ret)
		goto regulator_enable_error;

	ret = register_framebuffer(info);
	if (ret) {
		dev_err(&spidev->dev, "Couldn't register the framebuffer\n");
		goto panel_init_error;
	}

	snprintf(bl_name, sizeof(bl_name), "ssd1309fb%d", info->node);
	bl = backlight_device_register(bl_name, &spidev->dev, par,
				       &ssd1309fb_bl_ops, NULL);
	if (IS_ERR(bl)) {
		ret = PTR_ERR(bl);
		dev_err(&spidev->dev, "unable to register backlight device: %d\n",
			ret);
		goto bl_init_error;
	}

	bl->props.brightness = par->contrast;
	bl->props.max_brightness = MAX_CONTRAST;
	info->bl_dev = bl;

	dev_info(&spidev->dev, "fb%d: %s framebuffer device registered, using %d bytes of video memory\n", info->node, info->fix.id, vmem_size);

	return 0;

bl_init_error:
	unregister_framebuffer(info);
panel_init_error:
	if (par->device_info->need_pwm) {
		pwm_disable(par->pwm);
		pwm_put(par->pwm);
	};
regulator_enable_error:
	if (par->vbat_reg)
		regulator_disable(par->vbat_reg);
reset_oled_error:
	fb_deferred_io_cleanup(info);
fb_alloc_error:
	framebuffer_release(info);
	return ret;
}

static int spi_ssd1309fb_remove(struct spi_device *spidev)
{
	struct fb_info *info = spi_get_drvdata(spidev);
	struct ssd1309fb_par *par = info->par;

	ssd1309fb_write_cmd(par->spidev, SSD1309FB_DISPLAY_OFF);

	backlight_device_unregister(info->bl_dev);

	unregister_framebuffer(info);
	if (par->device_info->need_pwm) {
		pwm_disable(par->pwm);
		pwm_put(par->pwm);
	};
	fb_deferred_io_cleanup(info);
	__free_pages(__va(info->fix.smem_start), get_order(info->fix.smem_len));
	framebuffer_release(info);

	return 0;
}

/**
 *  SPI Driver and SPI Device ID Match Table
 */
static const struct spi_device_id spi_ssd1309fb_id[] = {
    {"zynex,ssd1309fb", 0},
    {}
};

MODULE_DEVICE_TABLE(spi, spi_ssd1309fb_id);

/**
 * SPI Driver
 */
static struct spi_driver spi_ssd1309fb_driver= {
    .probe = spi_ssd1309fb_probe,
    .remove = spi_ssd1309fb_remove,
    .id_table = spi_ssd1309fb_id,
    .driver = {
        .name = "spi_ssd1309fb",
        .owner = THIS_MODULE,
        .of_match_table = spi_ssd1309fb_of_match,
    },
};


module_spi_driver(spi_ssd1309fb_driver);

MODULE_DESCRIPTION("FB driver for the SPI SSD1309 OLED controller");
MODULE_AUTHOR("Victor zyy<gt7591665@gmail.com>");
MODULE_LICENSE("GPL");
