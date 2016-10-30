/*
 *  Copyright (c) 2013 Tang, Haifeng <tanghaifeng-gz@loongson.cn>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/ch372.h>

#include "ch372_usb_dev.h"

MODULE_DESCRIPTION("CH372 USB device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tang Hai <tanghaifeng-gz@loongson.cn>");
MODULE_ALIAS("platform:ch372_usb");

#define CH372_MINOR 250
#define EPDATA_LEN_MAX 64
#define DATA_LEN_MAX 2048
#define MODE_DEFINE 0

#define  GPIO_CONFIG0  (*(volatile int *)0xbfd010c0)
#define  GPIO_CONFIG1  (*(volatile int *)0xbfd010c4)
#define  GPIO_OE0      (*(volatile int *)0xbfd010d0)
#define  GPIO_OE1      (*(volatile int *)0xbfd010d4)
#define  GPIO_IN0      (*(volatile int *)0xbfd010e0)
#define  GPIO_IN1      (*(volatile int *)0xbfd010e4)
#define  GPIO_OUT0     (*(volatile int *)0xbfd010f0)
#define  GPIO_OUT1     (*(volatile int *)0xbfd010f4)


/*
struct ch372_dev {
	unsigned int current_len;
	struct semaphore sem;
	wait_queue_head_t rx_wait;
	wait_queue_head_t wx_wait;
};*/

static unsigned int current_len = 0;
static unsigned int read_len = 0;
wait_queue_head_t rx_wait;
wait_queue_head_t tx_wait;

struct ch372_platform_data *pdata;
void __iomem *reg_addr;
struct completion com;
int rx_flag = 0;
int tx_flag = 0;

static unsigned char tx_buf[DATA_LEN_MAX] = {};
static unsigned char rx_buf[DATA_LEN_MAX] = {};
static unsigned char *tx_bufp = tx_buf;
static unsigned char *rx_bufp = rx_buf;

#if MODE_DEFINE
#else
static void set_cs_value(int value)
{
     GPIO_OUT1 = (value? (GPIO_OUT1|(1<<18)) : GPIO_OUT1 & ( ~(1<<18)));  //ouput
}

static void set_a0_value(int value)
{
    GPIO_OUT1 = (value? (GPIO_OUT1|(1<<19)) : GPIO_OUT1 & ( ~(1<<19)));  //ouput
}

static void set_wr_value(int value)
{
    GPIO_OUT1 = (value? (GPIO_OUT1|(1<<21)) : GPIO_OUT1 & ( ~(1<<21)));  //ouput
}

static void set_rd_value(int value)
{
    GPIO_OUT1 = (value? (GPIO_OUT1|(1<<20)) : GPIO_OUT1 & ( ~(1<<20)));  //ouput
}

static void ch372_wr_dat_hw(u8 dat)
{
    unsigned int  value = dat;
    
    GPIO_OE1 &=  0xfffc03ff;  //output mode
    value = value << 10;
    GPIO_OUT1 = (GPIO_OUT1 & 0xfffc03ff) | value;  //ouput hight
}

static u8 ch372_rd_dat_hw(void)
{
    unsigned char  value = 0;

    GPIO_OE1 |=  ~0xfffc03ff;  //input mode

    value = (GPIO_IN1 >> 10) & 0xff ;
    return value;
}


#endif

static void ch372_wr_cmd(u8 cmd)
{
#if MODE_DEFINE
//	u32 ret;
//	udelay(2);
	gpio_set_value(pdata->gpios_d0, (cmd>>0) & 0x01);
	gpio_set_value(pdata->gpios_d1, (cmd>>1) & 0x01);
	gpio_set_value(pdata->gpios_d2, (cmd>>2) & 0x01);
	gpio_set_value(pdata->gpios_d3, (cmd>>3) & 0x01);
	gpio_set_value(pdata->gpios_d4, (cmd>>4) & 0x01);
	gpio_set_value(pdata->gpios_d5, (cmd>>5) & 0x01);
	gpio_set_value(pdata->gpios_d6, (cmd>>6) & 0x01);
	gpio_set_value(pdata->gpios_d7, (cmd>>7) & 0x01);

	gpio_set_value(pdata->gpios_cs, 0);
	gpio_set_value(pdata->gpios_dc, 1);
	gpio_set_value(pdata->gpios_rd, 1);
	gpio_set_value(pdata->gpios_wr, 0);

//	ret = readl(reg_addr);
//	ret &= ~(0xFF << pdata->datas_offset);
//	writel(ret | (cmd << pdata->datas_offset), reg_addr);

	gpio_set_value(pdata->gpios_wr, 1);
	gpio_set_value(pdata->gpios_cs, 1);
	gpio_set_value(pdata->gpios_dc, 1);
#else
    ch372_wr_dat_hw(cmd);
    set_cs_value(0);
  	set_a0_value(1);
    set_rd_value(1);
    set_wr_value(0);
    
    set_wr_value(1);
    set_cs_value(1);
    set_a0_value(1);

#endif

}

static void ch372_wr_dat(u8 dat)
{
#if MODE_DEFINE
//	u32 ret;
//	udelay(2);
	gpio_set_value(pdata->gpios_d0, (dat>>0) & 0x01);
	gpio_set_value(pdata->gpios_d1, (dat>>1) & 0x01);
	gpio_set_value(pdata->gpios_d2, (dat>>2) & 0x01);
	gpio_set_value(pdata->gpios_d3, (dat>>3) & 0x01);
	gpio_set_value(pdata->gpios_d4, (dat>>4) & 0x01);
	gpio_set_value(pdata->gpios_d5, (dat>>5) & 0x01);
	gpio_set_value(pdata->gpios_d6, (dat>>6) & 0x01);
	gpio_set_value(pdata->gpios_d7, (dat>>7) & 0x01);

	gpio_set_value(pdata->gpios_cs, 0);
	gpio_set_value(pdata->gpios_dc, 0);
	gpio_set_value(pdata->gpios_rd, 1);
	gpio_set_value(pdata->gpios_wr, 0);

//	ret = readl(reg_addr);
//	ret &= ~(0xFF << pdata->datas_offset);
//	writel(ret | (dat << pdata->datas_offset), reg_addr);

	gpio_set_value(pdata->gpios_wr, 1);
	gpio_set_value(pdata->gpios_cs, 1);
	gpio_set_value(pdata->gpios_dc, 1);
#else
    ch372_wr_dat_hw(dat);
    set_cs_value(0);
    set_a0_value(0);
    set_rd_value(1);
    set_wr_value(0);
    
    set_wr_value(1);
    set_cs_value(1);
    set_a0_value(1);

#endif
}

static u8 ch372_rd_dat(void)
{
#if MODE_DEFINE
//	u32 ret;
	u8 val = 0;
//	udelay(2);
	gpio_direction_input(pdata->gpios_d0);
	gpio_direction_input(pdata->gpios_d1);
	gpio_direction_input(pdata->gpios_d2);
	gpio_direction_input(pdata->gpios_d3);
	gpio_direction_input(pdata->gpios_d4);
	gpio_direction_input(pdata->gpios_d5);
	gpio_direction_input(pdata->gpios_d6);
	gpio_direction_input(pdata->gpios_d7);

	gpio_set_value(pdata->gpios_cs, 0);
	gpio_set_value(pdata->gpios_dc, 0);
	gpio_set_value(pdata->gpios_rd, 0);
	gpio_set_value(pdata->gpios_wr, 1);

//	ret = readl(reg_addr - 0x20);
//	ret |= (0xFF << pdata->datas_offset);
//	writel(ret, reg_addr - 0x20);

//	val = readl(reg_addr - 0x10) >> pdata->datas_offset;
	val = (gpio_get_value(pdata->gpios_d0) & 0x01) << 0;
	val |= (gpio_get_value(pdata->gpios_d1) & 0x01) << 1;
	val |= (gpio_get_value(pdata->gpios_d2) & 0x01) << 2;
	val |= (gpio_get_value(pdata->gpios_d3) & 0x01) << 3;
	val |= (gpio_get_value(pdata->gpios_d4) & 0x01) << 4;
	val |= (gpio_get_value(pdata->gpios_d5) & 0x01) << 5;
	val |= (gpio_get_value(pdata->gpios_d6) & 0x01) << 6;
	val |= (gpio_get_value(pdata->gpios_d7) & 0x01) << 7;

	gpio_set_value(pdata->gpios_rd, 1);
	gpio_set_value(pdata->gpios_cs, 1);
	gpio_set_value(pdata->gpios_dc, 1);
	gpio_set_value(pdata->gpios_wr, 1);

//	ret = readl(reg_addr);
//	ret &= ~(0xFF << pdata->datas_offset);
//	writel(ret, reg_addr - 0x20);

	gpio_direction_output(pdata->gpios_d0, 1);
	gpio_direction_output(pdata->gpios_d1, 1);
	gpio_direction_output(pdata->gpios_d2, 1);
	gpio_direction_output(pdata->gpios_d3, 1);
	gpio_direction_output(pdata->gpios_d4, 1);
	gpio_direction_output(pdata->gpios_d5, 1);
	gpio_direction_output(pdata->gpios_d6, 1);
	gpio_direction_output(pdata->gpios_d7, 1);
	return val;
#else
    unsigned char temp;
    set_a0_value(0);
    set_cs_value(0);
    set_rd_value(0);
    set_wr_value(1);
    temp =  ch372_rd_dat_hw();
    set_rd_value(1);
    set_cs_value(1);
    set_a0_value(1);
    set_wr_value(1);
    return temp;
#endif
}

static int init_controller(void)
{
	int err = 0;
	u8 reg;
	u32 i = 0;

	err += gpio_request(pdata->gpios_cs, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_dc, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_rd, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_wr, "st7565fb_gpio");
	
	err += gpio_request(pdata->gpios_d0, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d1, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d2, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d3, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d4, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d5, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d6, "st7565fb_gpio");
	err += gpio_request(pdata->gpios_d7, "st7565fb_gpio");
	if (err) {
		printk(KERN_ERR "failed to request GPIO for st7565\n");
		return -EINVAL;
	}
	gpio_direction_output(pdata->gpios_cs, 1);
	gpio_direction_output(pdata->gpios_dc, 1);
	gpio_direction_output(pdata->gpios_rd, 1);
	gpio_direction_output(pdata->gpios_wr, 1);
	gpio_direction_output(pdata->gpios_d0, 1);
	gpio_direction_output(pdata->gpios_d1, 1);
	gpio_direction_output(pdata->gpios_d2, 1);
	gpio_direction_output(pdata->gpios_d3, 1);
	gpio_direction_output(pdata->gpios_d4, 1);
	gpio_direction_output(pdata->gpios_d5, 1);
	gpio_direction_output(pdata->gpios_d6, 1);
	gpio_direction_output(pdata->gpios_d7, 1);

	ch372_wr_cmd(CH372_RESET_ALL);
	mdelay(40);

	ch372_wr_cmd(CH372_CHECK_EXIST);
	ch372_wr_dat(3);
	reg = ch372_rd_dat();
	printk("CH372_CHECK_EXIST: 0x%x\n", reg);

	ch372_wr_cmd(CH372_SET_USB_MODE);
	ch372_wr_dat(2);
	reg = ch372_rd_dat();
	printk("CH372_SET_USB_MODE: 0x%x\n", reg);

	while (i++ < 10000) {
		if ((reg = ch372_rd_dat()) == CMD_RET_SUCCESS)
			break;
		udelay(20);
	}
	ch372_wr_cmd(CH372_GET_IC_VER);
	reg = ch372_rd_dat();
	printk("CH372_GET_IC_VER: 0x%x\n", reg & 0x7F);

	return 0;
}

static void out_ep_fifo_handler(void)
{
	int i, length;

	if (rx_bufp >= (rx_buf + DATA_LEN_MAX)) {
		rx_bufp = rx_buf;
	}

	ch372_wr_cmd(CH372_RD_USB_DATA);

	length = ch372_rd_dat();
	for (i = 0; i < length; i += 1) {
		*rx_bufp = ch372_rd_dat();
		rx_bufp++;
	}
	//printk("data length : %d \n",length);
	current_len += length;
	if (current_len >= read_len) {
		current_len = read_len;
		wake_up_interruptible(&rx_wait);
	}
}

static irqreturn_t ch372_irq(int irq, void *_ch372)
{
	u8 interrupt_status;

	ch372_wr_cmd(CH372_GET_STATUS);
	interrupt_status = ch372_rd_dat();

	switch (interrupt_status) {
		case USB_INT_EP1_OUT:
			ch372_wr_cmd(CH372_UNLOCK_USB);
		break;
		case USB_INT_EP1_IN:
			ch372_wr_cmd(CH372_UNLOCK_USB);
		break;
		case USB_INT_EP2_OUT:
//			if (rx_flag) {
				out_ep_fifo_handler();
//				complete(&com);
//			}
			ch372_wr_cmd(CH372_UNLOCK_USB);
			wake_up_interruptible(&rx_wait);
		break;
		case USB_INT_EP2_IN:
			ch372_wr_cmd(CH372_UNLOCK_USB);
			if (tx_flag) {
				complete(&com);
			}
		break;
		case USB_INT_USB_SUSPEND:
			printk(KERN_INFO "ch372_suspend\n");
		break;
		case USB_INT_WAKE_UP:
			printk(KERN_INFO "ch372_wakeup\n");
		break;
		default:
			ch372_wr_cmd(CH372_UNLOCK_USB);
		break;
	}

	return IRQ_HANDLED;
}

static int __init ch372_probe(struct platform_device *pdev)
{
	int ret = 0;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -1;
	}
/*	reg_addr = ioremap(pdata->gpio_outpu, 0x4);
	if (reg_addr == NULL) {
		ret = -ENOMEM;
		pr_err("ioremap error.\n");
		goto clean_up;
	}*/

	ret = request_irq(pdata->irq, ch372_irq, IRQF_TRIGGER_LOW | IRQF_DISABLED,
			  "ch372_usb", NULL);
	if (ret < 0) {
		pr_err("request_irq error (%d)\n", ret);
		return ret;
	}

	init_waitqueue_head(&rx_wait);
	init_waitqueue_head(&tx_wait);

	ret = init_controller();

	return ret;
}

static int __exit ch372_remove(struct platform_device *pdev)
{
//	struct ch372 *ch372 = dev_get_drvdata(&pdev->dev);

	free_irq(platform_get_irq(pdev, 0), NULL);

	gpio_free(pdata->gpios_cs);
	gpio_free(pdata->gpios_dc);
	gpio_free(pdata->gpios_rd);
	gpio_free(pdata->gpios_wr);

	gpio_free(pdata->gpios_d0);
	gpio_free(pdata->gpios_d1);
	gpio_free(pdata->gpios_d2);
	gpio_free(pdata->gpios_d3);
	gpio_free(pdata->gpios_d4);
	gpio_free(pdata->gpios_d5);
	gpio_free(pdata->gpios_d6);
	gpio_free(pdata->gpios_d7);

	return 0;
}

static struct platform_driver ch372_driver = {
	.remove =	__exit_p(ch372_remove),
	.driver		= {
		.name =	"ch372_usb",
		.owner	= THIS_MODULE,
	},
};

static int ch372_dev_open(struct inode *inode, struct file *filep)
{
	rx_bufp = rx_buf;
	current_len = 0;
	return nonseekable_open(inode, filep);
}

static ssize_t
ch372_dev_read(struct file *filep, char __user *buf, size_t count, loff_t *ptr)
{
	unsigned long offset = *ptr;
	read_len = count;

	if (offset >= DATA_LEN_MAX) {
		return count ? -ENXIO : 0;
	}
	if (read_len > DATA_LEN_MAX - offset) {
		read_len = DATA_LEN_MAX - offset;
	}
/*
	init_completion(&com);
	rx_flag = 1;

	if (!wait_for_completion_timeout(&com, 2 * HZ)) {
		rx_flag = 0;
		printk(KERN_INFO "RX: timeout");
		return -EFAULT;
	}

//	wait_for_completion(&com);
	rx_flag = 0;*/

	if (rx_bufp == rx_buf) {
		if (filep->f_flags & O_NONBLOCK) {
			return -EFAULT;
		}
		wait_event_interruptible(rx_wait, rx_bufp != rx_buf);
	}

	if (read_len > EPDATA_LEN_MAX) {
		wait_event_interruptible_timeout(rx_wait, current_len >= read_len, 1*HZ);
	}

	if (copy_to_user(buf, (void *)(rx_buf + offset), current_len)) {
		rx_bufp = rx_buf;
		current_len = 0;
		return -EFAULT;
	}
	else {
//		*ptr += len;
//		printk(KERN_INFO "read %d bytes(s) from %ld\n", len, offset);
		read_len = current_len;
		rx_bufp = rx_buf;
		current_len = 0;
		return read_len;
	}
}

static ssize_t ch372_dev_write(struct file *filep, const char __user *buf,
			    size_t count, loff_t *ptr)
{
	unsigned long offset = *ptr;
	unsigned int len = count;
	int i;

	if (offset >= EPDATA_LEN_MAX) {
		return count ? -ENXIO : 0;
	}
	if (len > EPDATA_LEN_MAX - offset) {
		len = EPDATA_LEN_MAX - offset;
	}

	if (copy_from_user((void *)(tx_buf + offset), buf, len)) {
		return -EFAULT;
	}
	else {
		while (len > EPDATA_LEN_MAX) {
			init_completion(&com);
			tx_flag = 1;
			ch372_wr_cmd(CH372_WR_USB_DATA7);
			ch372_wr_dat(EPDATA_LEN_MAX);
			for (i = 0; i < EPDATA_LEN_MAX; i += 1) {
				ch372_wr_dat(*(tx_buf + offset + i));
			}
	//		*ptr += len;
			if (!wait_for_completion_timeout(&com, 2 * HZ)) {
				tx_flag = 0;
				printk(KERN_INFO "len>len_max TX: timeout");
				return -EFAULT;
			}
			tx_flag = 0;
			len -= EPDATA_LEN_MAX;
			offset += EPDATA_LEN_MAX;
		}
		if (len > 0) {
			init_completion(&com);
			tx_flag = 1;
			ch372_wr_cmd(CH372_WR_USB_DATA7);
			ch372_wr_dat(len);
			for (i = 0; i < len; i += 1) {
				ch372_wr_dat(*(tx_buf + offset + i));
			}
	//		*ptr += len;
			if (!wait_for_completion_timeout(&com, 2 * HZ)) {
				tx_flag = 0;
				printk(KERN_INFO "len > 0 TX: timeout");
				return -EFAULT;
			}
			tx_flag = 0;
		}
	}

//	printk(KERN_INFO "write %d bytes(s) to %ld\n", len, offset);

	return len;
}

static long
ch372_dev_unlocked_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static unsigned int ch372_poll(struct file *filep, poll_table *wait)
{
	unsigned int mask = 0;
	poll_wait(filep, &rx_wait, wait);
	poll_wait(filep, &tx_wait, wait);
	
	if (rx_bufp != rx_buf) {
		mask |= POLLIN | POLLRDNORM; /*标示数据可获*/
	}
	return mask;
}

static const struct file_operations ch372_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= ch372_dev_open,
	.read		= ch372_dev_read,
	.write		= ch372_dev_write,
	.unlocked_ioctl	= ch372_dev_unlocked_ioctl,
	.llseek		= no_llseek,
	.poll = ch372_poll,
};

static struct miscdevice ch372_dev_miscdev = {
	.minor = CH372_MINOR,
	.name = "ch372_dev",
	.fops = &ch372_dev_fops
};

static int __init ch372_dev_init(void)
{
	int ret;

	ret = misc_register(&ch372_dev_miscdev);
	if (ret < 0)
		return ret;
	return platform_driver_probe(&ch372_driver, ch372_probe);
}

module_init(ch372_dev_init);

static void __exit ch372_dev_cleanup(void)
{
	misc_deregister(&ch372_dev_miscdev);
	platform_driver_unregister(&ch372_driver);
}
module_exit(ch372_dev_cleanup);
