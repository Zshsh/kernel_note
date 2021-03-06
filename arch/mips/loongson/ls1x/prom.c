/*
 * Copyright (c) 2012 Tang, Haifeng <tanghaifeng-gz@loongson.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/serial_reg.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <video/ls1xfb.h>

#include <asm/bootinfo.h>
#include <loongson1.h>
#include <prom.h>

#define DEFAULT_MEMSIZE			64	/* If no memsize provided */
#define DEFAULT_BUSCLOCK		133000000
#define DEFAULT_CPUCLOCK		266000000

#ifdef CONFIG_STMMAC_ETH
extern unsigned char *hwaddr;
char *tmp;
#endif

unsigned long cpu_clock_freq;
unsigned long ls1x_bus_clock;
EXPORT_SYMBOL(cpu_clock_freq);
EXPORT_SYMBOL(ls1x_bus_clock);

int prom_argc;
char **prom_argv, **prom_envp;
unsigned long memsize, highmemsize;

const char *get_system_type(void)
{
	return "LS232 Evaluation board-V1.0";
}

char *prom_getenv(char *envname) //从prom_envp中获取制定参数
{
	char **env = prom_envp;
	int i;

	i = strlen(envname);

	while (*env) {
		if (strncmp(envname, *env, i) == 0 && *(*env+i) == '=')
			return *env + i + 1;
		env++;
	}

	return 0;
}

static inline unsigned long env_or_default(char *env, unsigned long dfl)
{ //查找env输出的环境变量,查找对应的变量，找不到则用默认的dfl
	char *str = prom_getenv(env);
	return str ? simple_strtol(str, 0, 0) : dfl; //string to long
}

void __init prom_init_cmdline(void)
{
	char *c = &(arcs_cmdline[0]);
	int i;

	for (i = 1; i < prom_argc; i++) { //第一个为g,见下面函数
		strcpy(c, prom_argv[i]);
		c += strlen(prom_argv[i]);
		if (i < prom_argc-1)
			*c++ = ' ';
	}
	*c = 0;
}
//prom函数是和硬件相关的，做一些底层的初始化，接受引导装载函数传给内核的参数
void __init prom_init(void)//ls1c go here
{
	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1; //pmon显示:g root=/dev/mtdblock1 console=ttyS2,115200 noinitrd init=/linuxrc rw rootfstype=yaffs2 video=ls1bfb:320x240-16@60 fbcon=rotate:1 consoleblank=1
	prom_envp = (char **)fw_arg2; //pmon下执行env输出的内容

	mips_machtype = MACH_LS232;
	system_state = SYSTEM_BOOTING;

	prom_init_cmdline(); //prom_argv内容拷贝到arcs_cmdline中,规范化

	ls1x_bus_clock = env_or_default("busclock", DEFAULT_BUSCLOCK);
	cpu_clock_freq = env_or_default("cpuclock", DEFAULT_CPUCLOCK);
	memsize = env_or_default("memsize", DEFAULT_MEMSIZE);
	highmemsize = env_or_default("highmemsize", 0x0);

#ifdef	CONFIG_LS1A_MACH //ls1c no use
	if (ls1x_bus_clock == 0)
		ls1x_bus_clock = 100  * 1000000;
	if (cpu_clock_freq == 0)
		cpu_clock_freq = 200 * 1000000;
#endif

#if defined(CONFIG_LS1C_MACH) //ls1c go here,复位usb控制器，防止启动时无故死机,暂不看
	__raw_writel(__raw_readl(LS1X_MUX_CTRL0) & (~USBHOST_SHUT), LS1X_MUX_CTRL0);
	__raw_writel(__raw_readl(LS1X_MUX_CTRL1) & (~USBHOST_RSTN), LS1X_MUX_CTRL1);
	mdelay(60);
	/* reset stop */
	__raw_writel(__raw_readl(LS1X_MUX_CTRL1) | USBHOST_RSTN, LS1X_MUX_CTRL1);
#else
	/* 闇�瑕佸浣嶄竴娆SB鎺у埗鍣紝涓斿浣嶆椂闂磋瓒冲闀匡紝鍚﹀垯鍚姩鏃惰帿鍚嶅叾濡欑殑姝绘満 */
	#if defined(CONFIG_LS1A_MACH)
	#define MUX_CTRL0 LS1X_MUX_CTRL0
	#define MUX_CTRL1 LS1X_MUX_CTRL1
	#elif defined(CONFIG_LS1B_MACH)
	#define MUX_CTRL0 LS1X_MUX_CTRL1
	#define MUX_CTRL1 LS1X_MUX_CTRL1
	#endif
//	__raw_writel(__raw_readl(MUX_CTRL0) | USB_SHUT, MUX_CTRL0);
//	__raw_writel(__raw_readl(MUX_CTRL1) & (~USB_RESET), MUX_CTRL1);
//	mdelay(10);
//	#if defined(CONFIG_USB_EHCI_HCD_LS1X) || defined(CONFIG_USB_OHCI_HCD_LS1X)
	/* USB controller enable and reset */
	__raw_writel(__raw_readl(MUX_CTRL0) & (~USB_SHUT), MUX_CTRL0);
	__raw_writel(__raw_readl(MUX_CTRL1) & (~USB_RESET), MUX_CTRL1);
	mdelay(60);
	/* reset stop */
	__raw_writel(__raw_readl(MUX_CTRL1) | USB_RESET, MUX_CTRL1);
//	#endif
#endif

#ifdef CONFIG_STMMAC_ETH
	tmp = prom_getenv("ethaddr");
	if (tmp) {
		sscanf(tmp, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
			&hwaddr[0], &hwaddr[1], &hwaddr[2], &hwaddr[3], &hwaddr[4], &hwaddr[5]);
	}
#endif

#if defined(CONFIG_LS1B_MACH) && defined(CONFIG_FB_LOONGSON1) //ls1c no use
	/* 鍙敤浜巐s1b鐨刲cd鎺ュ彛浼爒ga鎺ュ彛鏃朵娇鐢紝濡備簯缁堢锛�
	 鍥犱负浣跨敤vga鏃讹紝pll閫氳繃璁＄畻闅句互寰楀埌鍚堥�傜殑鍒嗛;缁橪S1X_CLK_PLL_FREQ鍜孡S1X_CLK_PLL_DIV
	 瀵勫瓨鍣ㄤ竴涓浐瀹氱殑鍊� */
	{
	int i;
	int default_xres = 800;
	int default_yres = 600;
	int default_refresh = 75;
	struct ls1b_vga *input_vga;
	extern struct ls1b_vga ls1b_vga_modes[];
	char *strp, *options, *end;

	options = strstr(arcs_cmdline, "video=ls1bvga:");
	if (options) {
		
		options += 14;
		/* ls1bvga:1920x1080-16@60 */
		for (i=0; i<strlen(options); i++)
			if (isdigit(*(options+i)))
				break;	/* 鏌ユ壘options瀛椾覆涓涓�涓暟瀛楁椂i鐨勫�� */
		if (i < 4) {
			default_xres = simple_strtoul(options+i, &end, 10);
			default_yres = simple_strtoul(end+1, NULL, 10);
			/* refresh */
			strp = strchr((const char *)options, '@');
			if (strp) {
				default_refresh = simple_strtoul(strp+1, NULL, 0);
			}
			if ((default_xres<=0 || default_xres>1920) || 
				(default_yres<=0 || default_yres>1080)) {
				pr_info("Warning: Resolution is out of range."
					"MAX resolution is 1920x1080@60Hz\n");
				default_xres = 800;
				default_yres = 600;
			}
		}
		for (input_vga=ls1b_vga_modes; input_vga->ls1b_pll_freq !=0; ++input_vga) {
//			if((input_vga->xres == default_xres) && (input_vga->yres == default_yres) && 
//				(input_vga->refresh == default_refresh)) {
			if ((input_vga->xres == default_xres) && (input_vga->yres == default_yres)) {
				break;
			}
		}
		if (input_vga->ls1b_pll_freq) {
			u32 pll, ctrl;
			u32 x, divisor;

			writel(input_vga->ls1b_pll_freq, LS1X_CLK_PLL_FREQ);
			writel(input_vga->ls1b_pll_div, LS1X_CLK_PLL_DIV);
			/* 璁＄畻ddr棰戠巼锛屾洿鏂颁覆鍙ｅ垎棰� */
			pll = input_vga->ls1b_pll_freq;
			ctrl = input_vga->ls1b_pll_div & DIV_DDR;
			divisor = (12 + (pll & 0x3f)) * APB_CLK / 2
					+ ((pll >> 8) & 0x3ff) * APB_CLK / 1024 / 2;
			divisor = divisor / (ctrl >> DIV_DDR_SHIFT);
			divisor = divisor / 2 / (16*115200);
			x = readb(PORT(UART_LCR));
			writeb(x | UART_LCR_DLAB, PORT(UART_LCR));
			writeb(divisor & 0xff, PORT(UART_DLL));
			writeb((divisor>>8) & 0xff, PORT(UART_DLM));
			writeb(x & ~UART_LCR_DLAB, PORT(UART_LCR));
		}
	}
	}
#endif
	
	pr_info("memsize=%ldMB, highmemsize=%ldMB\n", memsize, highmemsize);
}

void __init prom_free_prom_memory(void)
{
}

void __init prom_putchar(char c) //ls1c go here
{
	int timeout;

	timeout = 1024;

	while (((readb(PORT(UART_LSR)) & UART_LSR_THRE) == 0)
			&& (timeout-- > 0))
		;

	writeb(c, PORT(UART_TX));
}

