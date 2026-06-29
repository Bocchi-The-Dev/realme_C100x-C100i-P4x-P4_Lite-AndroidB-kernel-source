/**
 * SPDX-FileContributor: vdsp driver uart to implement serial communication.
 */
#ifndef _VDSP_UART_H_
#define _VDSP_UART_H_

struct vdsp_uart_ops;
struct vdsp_uart_desc {
	struct vdsp_uart_ops *ops;
};

struct vdsp_uart_ops {
	int (*enable) (struct vdsp_uart_desc * ctx);
	int (*disable) (struct vdsp_uart_desc * ctx);
};

struct vdsp_uart_desc *get_vdsp_uart_desc(void);
#endif