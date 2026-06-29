/**
 * SPDX-FileContributor: vdsp driver qos to implement access ddr priority config.
 */

#ifndef _VDSP_QOS_H_
#define _VDSP_QOS_H_

void parse_qos(void *hw_arg, void *of_node);
int set_qos(void *hw_arg);

#endif /* #ifndef _VDSP_QOS_H_ */

