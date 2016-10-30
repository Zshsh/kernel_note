/*
 *  Copyright (c) 2013 Tang, Haifeng <tanghaifeng-gz@loongson.cn>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <asm/dma.h>

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <media/ls1b_camera.h>
#include "gc0307.h"

#define LS1B_CAMERA_MINOR 251
#define GC0307_DRIVER_NAME	"gc0307"

#define SENSOR_WIDTH	512
#define SENSOR_HEIGHT	480
#define DATA_BUFF_SIZE	(SENSOR_WIDTH * SENSOR_HEIGHT)
#define SENSOR_OSC_CLK	24000000	/* MHz */

#define DMA_ACCESS_ADDR	0x1fe78040	/* DMA对NAND操作的地址 */
#define ORDER_ADDR_IN	0x1fd01160	/* DMA配置寄存器 */
static void __iomem *order_addr_in;
#define DMA_DESC_NUM	28	/* DMA描述符占用的字节数 7x4 */
/* DMA描述符 */
#define DMA_ORDERED		0x00
#define DMA_SADDR		0x04
#define DMA_DADDR		0x08
#define DMA_LENGTH		0x0c
#define DMA_STEP_LENGTH		0x10
#define DMA_STEP_TIMES		0x14
#define	DMA_CMD			0x18

#define LS1B_NAND_BASE	0x1fe78000	/* NAND寄存器基地址 */
static void __iomem *ls1b_nand_base;
/* NAND寄存器 */
#define NAND_CMD		0x00
#define NAND_ADDR_L		0x04
#define NAND_ADDR_H		0x08
#define NAND_TIMING		0x0c
#define NAND_IDL		0x10
#define NAND_IDH		0x14
#define NAND_STATUS		0x14
#define NAND_PARAM		0x18
#define NAND_OPNUM		0x1c
#define NAND_CS_RDY		0x20

/* NAND_TIMING寄存器定义 */
#define HOLD_CYCLE	0x00
//#define WAIT_CYCLE	0x0a
#define WAIT_CYCLE	0x05

#undef THERM_USE_PROC
#ifdef THERM_USE_PROC
#include <linux/proc_fs.h>
#define GC0307_PROC_NAME	"gc0307"
static struct proc_dir_entry *s_proc = NULL;
static struct proc_dir_entry *proc_1bcamera_data;
#endif
static struct i2c_client *s_i2c_client = NULL;

static unsigned char *data_buff_base;

struct ls1b_camera_info {
	struct i2c_client *client;

	void __iomem	*dma_desc;
	dma_addr_t		dma_desc_phys;
	size_t			dma_desc_size;

	unsigned char	*data_buff;
	dma_addr_t		data_buff_phys;
	size_t			data_buff_size;
};

#ifdef THERM_USE_PROC
static void dump_i2c_regs(void)
{
	int i;

	if (!s_i2c_client) {
		printk("s_i2c_client not ready\n");
		return;
	}

	for (i = 0; i <= 0xFF; i++) {
		printk("dump_i2c_regs: 0x%02X=0x%02X\n", i, 
				i2c_smbus_read_byte_data(s_i2c_client, i) & 0xFF);
	}
}

static int ls1b_camera_writeproc(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
	return count;
}

static int ls1b_camera_readproc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	dump_i2c_regs();
	return 0;
}

static int
proc_camera_read(char *buf, char **start, off_t offset,
		       int len, int *eof, void *unused)
{
	if (offset >= DATA_BUFF_SIZE) {
		return 0;
	}
	if (len > DATA_BUFF_SIZE - offset) {
		len = DATA_BUFF_SIZE - offset;
	}
	memcpy(buf + offset, data_buff_base + offset, len);

	return offset + len;
}
#endif

static void dma_enable_trans(struct ls1b_camera_info *info)
{
	int timeout = 200000;

	writel((info->dma_desc_phys & ~0x1F) | 0x8, order_addr_in);
	while ((readl(order_addr_in) & 0x8) && (timeout-- > 0)) {
//		udelay(5);
	}
}

static int nand_done(void)
{
	int ret, timeout = 20000;

	do {
		ret = readl(ls1b_nand_base + NAND_CMD);
	} while (((ret & 0x400) != 0x400) && (timeout-- > 0));

	return timeout;
}

static int fp_capture(struct i2c_client *client)
{
	struct ls1b_camera_platform_data *pdata = client->dev.platform_data;
	struct ls1b_camera_info *info = i2c_get_clientdata(client);
	int height_counter = SENSOR_HEIGHT;
	int width_counter = 0;
	int ret, ret1;
	int timeout = 2000000;
	unsigned long irq_flags;

	/* 判断vsync信号是否是一帧的开始 gc0307为低电平有效 */
	while (timeout--) {
		ret = gpio_get_value(pdata->vsync);
		if (ret) {
			ret = gpio_get_value(pdata->vsync);
			if (!ret) {
				break;
			}
		}
	}
	if (timeout == 0) {
		dev_err(&client->dev, "vsync timeout\n");
		return -1;
	}

	local_irq_save(irq_flags);

	/* 采样一帧信号 */
	while (height_counter > 0) {
//		timeout = 2000000;
		do {
			ret = gpio_get_value(pdata->hsync);
			ret1 = gpio_get_value(pdata->vsync);
		} while (ret && (!ret1)/* && (timeout-- > 0)*/);

		do {
			ret = gpio_get_value(pdata->hsync);
			ret1 = gpio_get_value(pdata->vsync);
		} while ((!ret) && (!ret1)/* && (timeout-- > 0)*/);

		/* 使能nand flash读 */
		writel(0x00000102, ls1b_nand_base + NAND_CMD);
		writel(0x00000103, ls1b_nand_base + NAND_CMD);
		/* dam操作 */
		writel(info->data_buff_phys + width_counter, info->dma_desc + DMA_SADDR);
		writel(0x00000001, info->dma_desc + DMA_CMD);
		dma_enable_trans(info);
		/* 等待nand操作完成 */
		do {
			ret = readl(ls1b_nand_base + NAND_CMD);
		} while ((ret & 0x400) != 0x400);

		width_counter += SENSOR_WIDTH;
		height_counter--;
	}
	local_irq_restore(irq_flags);

	return 0;
}

static irqreturn_t camera_ts_interrupt(int irq, void *id)
{
	return IRQ_HANDLED;
}

static irqreturn_t camera_hsync_interrupt(int irq, void *id)
{
	return IRQ_HANDLED;
}

static irqreturn_t camera_vsync_interrupt(int irq, void *id)
{
	return IRQ_HANDLED;
}

static int camera_sensor_init(struct i2c_client *client)
{
	int i, err = -EINVAL;

	for (i = 0; i < GC0307_INIT_REGS; i++) {
		err = i2c_smbus_write_byte_data(client, gc0307_YCbCr8bit[i][0], gc0307_YCbCr8bit[i][1]);
		if (err < 0)
			v4l_info(client, "%s: %d register set failed\n",
					__func__, i);
	}

	if (err < 0) {
		v4l_err(client, "%s: camera initialization failed\n",
				__func__);
		return -EIO;
	}

	return 0;
}

static void camera_dma_init(struct ls1b_camera_info *info)
{
	writel(0, info->dma_desc + DMA_ORDERED);
	writel(info->data_buff_phys, info->dma_desc + DMA_SADDR);
	writel(DMA_ACCESS_ADDR, info->dma_desc + DMA_DADDR);
	writel(SENSOR_WIDTH / 4, info->dma_desc + DMA_LENGTH);
	writel(0, info->dma_desc + DMA_STEP_LENGTH);
	writel(1, info->dma_desc + DMA_STEP_TIMES);
	writel(0, info->dma_desc + DMA_CMD);
}

static int camera_nand_init(void)
{
	struct clk *clk;
	int prescale;

	clk = clk_get(NULL, "apb");
	prescale = clk_get_rate(clk);
	prescale = prescale / SENSOR_OSC_CLK;

	writel(0, ls1b_nand_base + NAND_CMD);
//	writel((HOLD_CYCLE << 8) | prescale, ls1b_nand_base + NAND_TIMING);
	writel((HOLD_CYCLE << 8) | WAIT_CYCLE, ls1b_nand_base + NAND_TIMING);
	writel(SENSOR_WIDTH, ls1b_nand_base + NAND_OPNUM);
	writel(0, ls1b_nand_base + NAND_CS_RDY);

	/* reaset */
	writel(0x00000040, ls1b_nand_base + NAND_CMD);
	writel(0x00000041, ls1b_nand_base + NAND_CMD);
	if (!nand_done()) {
		printk(KERN_ERR "Wait time out!!!\n");
		return -1;
	}

	writel(0x1f000000, ls1b_nand_base + NAND_ADDR_L);
	writel(0x00000008, ls1b_nand_base + NAND_ADDR_H);	/* 设置访问NAND Falsh的地址，目的是使片选CS0无效 */
	writel(0x00000100, ls1b_nand_base + NAND_PARAM);	/* 设置外部颗粒大小，目的是使片选CS0无效 */
	writel(0x18141211, ls1b_nand_base + NAND_CS_RDY);	/* 重映射rdy1/2/3信号到rdy0 rdy用于判断是否忙 */

	/* read id */
	writel(0x00000020, ls1b_nand_base + NAND_CMD);
	writel(0x00000021, ls1b_nand_base + NAND_CMD);
	if (!nand_done()) {
		printk(KERN_ERR "Wait time out!!!\n");
		return -1;
	}

	/* read state */
	writel(0x00000080, ls1b_nand_base + NAND_CMD);
	writel(0x00000081, ls1b_nand_base + NAND_CMD);
	if (!nand_done()) {
		printk(KERN_ERR "Wait time out!!!\n");
		return -1;
	}

	return 0;
}

static int ls1b_camera_init(struct i2c_client *client)
{
	struct ls1b_camera_platform_data *pdata = client->dev.platform_data;
	struct ls1b_camera_info *info = i2c_get_clientdata(client);
	int err = -EINVAL;

/*	err = gpio_request(pdata->bl, "ls1b_camera_bl");
	if (err) {
		dev_err(&client->dev,
			"failed to request GPIO%d for BL\n", pdata->bl);
		goto err_free_bl;
	}
	gpio_direction_output(pdata->bl, 1);
	
	err = gpio_request(pdata->ts, "ls1b_camera_ts");
	if (err) {
		dev_err(&client->dev,
			"failed to request GPIO%d for TS\n", pdata->ts);
		goto err_free_ts;
	}
	gpio_direction_input(pdata->ts);*/

	err = gpio_request(pdata->hsync, "ls1b_camera_hsync");
	if (err) {
		dev_err(&client->dev,
			"failed to request GPIO%d for hsync\n", pdata->hsync);
		goto err_free_hsync;
	}
	gpio_direction_input(pdata->hsync);

	err = gpio_request(pdata->vsync, "ls1b_camera_vsync");
	if (err) {
		dev_err(&client->dev,
			"failed to request GPIO%d for vsync\n", pdata->vsync);
		goto err_free_vsync;
	}
	gpio_direction_input(pdata->vsync);

	/* request irqs */
/*	err = request_irq(gpio_to_irq(pdata->ts), camera_ts_interrupt,
				IRQF_TRIGGER_LOW | IRQF_DISABLED,
				"camera_ts_int", pdata);
	if (err) {
		dev_err(&client->dev, "Unable to acquire camera_ts interrupt\n");
		goto err_ts_int;
	}*/
/*
	err = request_irq(gpio_to_irq(pdata->hsync), camera_hsync_interrupt,
				IRQF_TRIGGER_LOW | IRQF_DISABLED,
				"camera_hsync_int", pdata);
	if (err) {
		dev_err(&client->dev, "Unable to acquire camera_hsync interrupt\n");
		goto err_hsync_int;
	}

	err = request_irq(gpio_to_irq(pdata->vsync), camera_vsync_interrupt,
				IRQF_TRIGGER_LOW | IRQF_DISABLED,
				"camera_vsync_int", pdata);
	if (err) {
		dev_err(&client->dev, "Unable to acquire camera_vsync interrupt\n");
		goto err_vsync_int;
	}
*/
	err = camera_sensor_init(client);
	if (err != 0) {
		dev_err(&client->dev, "Initialize camera sensor fail\n");
		goto err_vsync_int;
	}

	camera_dma_init(info);

	err = camera_nand_init();
	if (err != 0) {
		dev_err(&client->dev, "NAND Controller setup fail\n");
		goto err_vsync_int;
	}

	return 0;

err_vsync_int:
//	free_irq(gpio_to_irq(pdata->hsync), pdata);
err_hsync_int:
//	free_irq(gpio_to_irq(pdata->ts), pdata);
err_ts_int:
	gpio_free(pdata->vsync);
err_free_vsync:
	gpio_free(pdata->hsync);
err_free_hsync:
//	gpio_free(pdata->ts);
err_free_ts:
//	gpio_free(pdata->bl);
err_free_bl:
	return err;
}

static int ls1b_camera_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
//	struct ls1b_camera_platform_data *pdata = client->dev.platform_data;
	struct ls1b_camera_info *info;
	int ret;

	if (!(info = kzalloc(sizeof(struct ls1b_camera_info), GFP_KERNEL))) {
		dev_err(&client->dev, "fialed to allocate memory!\n");
		return -ENOMEM;
	}

	info->dma_desc_size = PAGE_ALIGN(DMA_DESC_NUM);
	info->dma_desc = dma_alloc_coherent(&client->dev, info->dma_desc_size,
						&info->dma_desc_phys, GFP_KERNEL);
	if (!info->dma_desc) {
		dev_err(&client->dev, "fialed to allocate dma memory!\n");
		goto err1;
	}

	info->data_buff_size = PAGE_ALIGN(DATA_BUFF_SIZE);
	info->data_buff = dma_alloc_coherent(&client->dev, info->data_buff_size,
					&info->data_buff_phys, GFP_KERNEL);
	if (!info->data_buff) {
		dev_err(&client->dev, "failed to allocate data buffer\n");
		goto err2;
	}
	data_buff_base = info->data_buff;

	order_addr_in = ioremap(ORDER_ADDR_IN, 0x4);
	ls1b_nand_base = ioremap(LS1B_NAND_BASE, 0x4000);

	info->client = client;
	i2c_set_clientdata(client, info);

	ret = ls1b_camera_init(client);
	if (ret != 0) {
		dev_err(&client->dev, "failed to allocate data buffer\n");
		goto err3;
	}

	s_i2c_client = client;

#ifdef THERM_USE_PROC
	s_proc = create_proc_entry(GC0307_PROC_NAME, 0666, NULL);
	if (s_proc != NULL) {
		s_proc->write_proc = ls1b_camera_writeproc;
		s_proc->read_proc = ls1b_camera_readproc;
	}

	proc_1bcamera_data = create_proc_entry("1bcamera_data", 0, NULL);
	if (proc_1bcamera_data)
		proc_1bcamera_data->read_proc = proc_camera_read;
	else
		printk(KERN_ERR "1bcamera_data: unable to register /proc/1bcamera_data\n");
#endif

//	fp_capture(client);

	return 0;

err3:
	iounmap(ls1b_nand_base);
	iounmap(order_addr_in);
	dma_free_coherent(&client->dev, info->data_buff_size,
			info->data_buff, info->data_buff_phys);
err2:
	dma_free_coherent(&client->dev, info->dma_desc_size,
			info->dma_desc, info->dma_desc_phys);
err1:
	kfree(info);
	return -ENOMEM;
}

static int ls1b_camera_remove(struct i2c_client *client)
{
	struct ls1b_camera_platform_data *pdata = client->dev.platform_data;
	struct ls1b_camera_info *info = i2c_get_clientdata(client);

//	free_irq(gpio_to_irq(pdata->hsync), pdata);
//	free_irq(gpio_to_irq(pdata->ts), pdata);
	gpio_free(pdata->vsync);
	gpio_free(pdata->hsync);
//	gpio_free(pdata->ts);
//	gpio_free(pdata->bl);

	iounmap(ls1b_nand_base);
	iounmap(order_addr_in);
	dma_free_coherent(&client->dev, info->data_buff_size,
			info->data_buff, info->data_buff_phys);
	dma_free_coherent(&client->dev, info->dma_desc_size,
			info->dma_desc, info->dma_desc_phys);
	kfree(info);

	return 0;
}

static const struct i2c_device_id ls1b_camera_id[] = {
	{ GC0307_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ls1b_camera_id);

static struct i2c_driver ls1b_camera_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= GC0307_DRIVER_NAME,
	},
	.probe		= ls1b_camera_probe,
	.remove		= ls1b_camera_remove,
	.id_table	= ls1b_camera_id,
};

static int ls1b_camera_open(struct inode *inode, struct file *file)
{
	printk("wwj open256480\n");
	return nonseekable_open(inode, file);
}

static ssize_t
ls1b_camera_read(struct file *file, char __user *buf, size_t count, loff_t *ptr)
{
	unsigned long offset = *ptr;
	unsigned int len = count;

	if (offset >= DATA_BUFF_SIZE) {
		return count ? -ENXIO : 0;
	}
	if (len > DATA_BUFF_SIZE - offset) {
		len = DATA_BUFF_SIZE - offset;
	}

	fp_capture(s_i2c_client);

	if (copy_to_user(buf, data_buff_base + offset, len)) {
		return -EFAULT;
	}
	else {
//		*ptr += len;
//		printk(KERN_INFO "read %d bytes(s) from %ld\n", len, offset);
		return len;
	}
}

static long
ls1b_camera_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations ls1b_camera_fops = {
	.owner		= THIS_MODULE,
	.open		= ls1b_camera_open,
	.read		= ls1b_camera_read,
	.unlocked_ioctl	= ls1b_camera_unlocked_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice ls1b_camera_miscdev = {
	.minor = LS1B_CAMERA_MINOR,
	.name = "ls1b_camera",
	.fops = &ls1b_camera_fops,
};

static __init int init_ls1b_camera(void)
{
	int ret;

	ret = misc_register(&ls1b_camera_miscdev);
	if (ret < 0)
		return ret;
	return i2c_add_driver(&ls1b_camera_driver);
}

static __exit void exit_ls1b_camera(void)
{
	misc_deregister(&ls1b_camera_miscdev);
	i2c_del_driver(&ls1b_camera_driver);
}

module_init(init_ls1b_camera);
module_exit(exit_ls1b_camera);

MODULE_AUTHOR("loongson-gz tang <tanghaifeng-gz@loongson.cn>");
MODULE_DESCRIPTION("GC0307 camera driver");
MODULE_LICENSE("GPL");
