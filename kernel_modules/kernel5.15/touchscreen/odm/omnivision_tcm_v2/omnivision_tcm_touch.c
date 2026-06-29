/*
 * omnivision TCM touchscreen driver
 *
 * Copyright (C) 2017-2018 omnivision Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND omnivision
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL omnivision BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF omnivision WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, omnivision'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include "omnivision_tcm_core.h"

#ifdef MTK_PLATFORM
#include "../tpd.h"
extern struct tpd_device *tpd;
#endif

#define TYPE_B_PROTOCOL

#define REPORT_Z_MAJOR_VALUE
#define MAX_Z_VALUE 1000
#define MAX_MAJOR_VALUE 255
#define MAX_MINOR_VALUE 255

#define TOUCH_REPORT_CONFIG_SIZE 128

//#define SUPPORT_FACE_DETECT
//#define SUPPORT_KNUCKLE_DATA_REPORT
//#define SUPPORT_DEBUG_FRAME_DATA

//#define USE_DEFAULT_TOUCH_REPORT_CONFIG

enum touch_status {
	LIFT = 0,
	FINGER = 1,
	GLOVED_FINGER = 2,
	NOP = -1,
};

enum gesture_id {
	NO_GESTURE_DETECTED = 0,
	GESTURE_DOUBLE_TAP = 0X01,
	GESTURE_CIRCLE = 0x02,
	GESTURE_SWIPE = 0x04,
	GESTURE_UNICODE = 0x08,
	GESTURE_VEE = 0x10,
	GESTURE_TRIANGLE = 0x20,
};

enum touch_report_code {
	TOUCH_END = 0,
	TOUCH_FOREACH_ACTIVE_OBJECT,
	TOUCH_FOREACH_OBJECT,
	TOUCH_FOREACH_END,
	TOUCH_PAD_TO_NEXT_BYTE,
	TOUCH_TIMESTAMP,
	TOUCH_OBJECT_N_INDEX,
	TOUCH_OBJECT_N_CLASSIFICATION,
	TOUCH_OBJECT_N_X_POSITION,
	TOUCH_OBJECT_N_Y_POSITION,
	TOUCH_OBJECT_N_Z,
	TOUCH_OBJECT_N_X_WIDTH,
	TOUCH_OBJECT_N_Y_WIDTH,
	TOUCH_OBJECT_N_TX_POSITION_TIXELS,
	TOUCH_OBJECT_N_RX_POSITION_TIXELS,
	TOUCH_0D_BUTTONS_STATE,
	TOUCH_GESTURE_ID,
	TOUCH_FRAME_RATE,
	TOUCH_POWER_IM,
	TOUCH_CID_IM,
	TOUCH_RAIL_IM,
	TOUCH_CID_VARIANCE_IM,
	TOUCH_NSM_FREQUENCY,
	TOUCH_NSM_STATE,
	TOUCH_NUM_OF_ACTIVE_OBJECTS,
	TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME,
	TOUCH_FACE_DETECT,
	TOUCH_GESTURE_DATA,
	TOUCH_OBJECT_N_FORCE,
	TOUCH_FINGERPRINT_AREA_MEET,
	TOUCH_TUNING_GAUSSIAN_WIDTHS = 0x80,
	TOUCH_TUNING_SMALL_OBJECT_PARAMS,
	TOUCH_TUNING_0D_BUTTONS_VARIANCE,
#ifdef SUPPORT_KNUCKLE_DATA_REPORT
	TOUCH_KNUCKLE_DATA = 0xca,
#endif
	TOUCH_REPORT_GESTURE_SWIPE = 193,
	TOUCH_REPORT_GESTURE_CIRCLE = 194,
	TOUCH_REPORT_GESTURE_UNICODE = 195,
	TOUCH_REPORT_GESTURE_VEE = 196,
	TOUCH_REPORT_GESTURE_TRIANGLE = 197,
	TOUCH_REPORT_GESTURE_INFO = 198,  //gesture info direction, unicode value
	TOUCH_REPORT_GESTURE_COORDINATE = 199,  //gesture six points

	TOUCH_REPORT_PALM_DETECTED = 0xc8,  //200
	TOUCH_REPORT_WATERPROOF_INFO = 0xca, //202 moisture
	TOUCH_AOD_STATE = 0xCB,   //203
	TOUCH_GLOVE_STATE = 0xCC,  //204
#ifdef SUPPORT_VERIFICATION_REPORT_INFO
	TOUCH_REPORT_VERIFICATION_INFO = 205,
#endif
	TOUCH_FRAME_DATA = 0xCB,
	TOUCH_5_LINE_INFO = 0xCC,
};

enum face_status {
	FACE_FAR_FROM_1_2_3_CLOSE_WHEN_SCREEN_ON = 0x00,
	FACE_CLOSE_1_SMALL_SIGNAL = 0x01,
	FACE_CLOSE_2_ENOUGH_SIGNAL = 0x02,
	FACE_CLOSE_3_ENOUGH_SIGNAL = 0x03,
	FACE_CLOSE_FROM_1_2_3_CLOSE_WHEN_SCREEN_OFF = 0x04,
	FACE_FAR_FROM_1_2_3_CLOSE_WHEN_SCREEN_OFF = 0x05,
	FACE_STATUS_NONE = 0x0f,
};


struct object_data {
	unsigned char status;
	unsigned int x_pos;
	unsigned int y_pos;
	unsigned int x_width;
	unsigned int y_width;
	unsigned int z;
	unsigned int tx_pos;
	unsigned int rx_pos;
};

struct input_params {
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
};

#ifdef SUPPORT_KNUCKLE_DATA_REPORT
#define KNUCKLE_DATA_SIZE 102
#endif
struct touch_data {
	struct object_data *object_data;
	unsigned int timestamp;
	unsigned int buttons_state;
	unsigned int gesture_id;
	unsigned int frame_rate;
	unsigned int power_im;
	unsigned int cid_im;
	unsigned int rail_im;
	unsigned int cid_variance_im;
	unsigned int nsm_frequency;
	unsigned int nsm_state;
	unsigned int num_of_active_objects;
	unsigned int num_of_cpu_cycles;
	unsigned int fd_data;
	unsigned int force_data;
	unsigned int fingerprint_area_meet;
#ifdef SUPPORT_KNUCKLE_DATA_REPORT
	unsigned char knuckle_data[KNUCKLE_DATA_SIZE];
#endif
	unsigned char frame_data[40*20*2];
	unsigned char ifp_5_line_data[20*2*5];
	unsigned int palm_status;
	unsigned int aod_status;
	unsigned int moisture_status;
	unsigned int glove_status;
	unsigned int gesture_extra_info;
	unsigned short gesture_six_points[12];
};

struct touch_hcd {
	bool irq_wake;
	bool init_touch_ok;
	bool suspend_touch;
	bool suspend_touch_finger;
	unsigned char *prev_status;
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
	struct mutex report_mutex;
	struct input_dev *input_dev;
	struct touch_data touch_data;
	struct input_params input_params;
	struct ovt_tcm_buffer out;
	struct ovt_tcm_buffer resp;
	struct ovt_tcm_hcd *tcm_hcd;
};

static struct touch_hcd *touch_hcd;

/*
static void ovt_log_frame_data(unsigned char *data, unsigned int d_len, int use_signed, int col_cnt)
{
	unsigned int i = 0;
	short data_value;
	unsigned short u_data_value;
	unsigned int data_index = 0;
	unsigned char log_str[512] = {0};

	if (col_cnt == 0) {
		col_cnt = 18;
	}

	for (i = 0; i < d_len / 2; i++) {
		if (use_signed) {
			data_value = (signed short)le2_to_uint(&data[data_index * 2]);
			snprintf(log_str + strlen(log_str), sizeof(log_str) - strlen(log_str) - 1, "%6d, ", data_value);
		} else {
			u_data_value = (unsigned short)le2_to_uint(&data[data_index * 2]);
			snprintf(log_str + strlen(log_str), sizeof(log_str) - strlen(log_str) - 1, "%6d, ", u_data_value);
		}
		data_index++;
		if (data_index % col_cnt == 0) {
			//one line
			printk("ovt-debug-frame[%d]: %s\n", (data_index / col_cnt - 1),  log_str);
			memset(log_str, 0, sizeof(log_str));
		}
	}
	if (data_index % col_cnt) {
		printk("ovt-debug-frame[%d]: %s\n",(data_index / col_cnt), log_str);
		memset(log_str, 0, sizeof(log_str));
	}
}
*/
#ifdef SUPPORT_FACE_DETECT
static int ovt_check_face_state(int current_face_state)
{
	//static int pre_face_state = FACE_STATUS_NONE;
	if ((current_face_state == FACE_FAR_FROM_1_2_3_CLOSE_WHEN_SCREEN_ON) ||
		(current_face_state == FACE_FAR_FROM_1_2_3_CLOSE_WHEN_SCREEN_OFF)) {
		//report far event
		printk("tcm check face far\n");
	} else if ((current_face_state == FACE_CLOSE_1_SMALL_SIGNAL) ||
				(current_face_state == FACE_CLOSE_FROM_1_2_3_CLOSE_WHEN_SCREEN_OFF)) {

		//report close event
		printk("tcm check face close\n");
	}
	return 0;
}
#endif
/**
 * touch_free_objects() - Free all touch objects
 *
 * Report finger lift events to the input subsystem for all touch objects.
 */
static void touch_free_objects(void)
{
#ifdef TYPE_B_PROTOCOL
	unsigned int idx;
#endif

	if (touch_hcd->input_dev == NULL)
		return;

	mutex_lock(&touch_hcd->report_mutex);

#ifdef TYPE_B_PROTOCOL
	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		input_mt_slot(touch_hcd->input_dev, idx);
#ifdef REPORT_Z_MAJOR_VALUE
		input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE, 0);
		input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MINOR, 0);
#endif
		input_mt_report_slot_state(touch_hcd->input_dev,
				MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(touch_hcd->input_dev,
			BTN_TOUCH, 0);
	input_report_key(touch_hcd->input_dev,
			BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
	input_mt_sync(touch_hcd->input_dev);
#endif
	input_sync(touch_hcd->input_dev);

	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/**
 * touch_get_report_data() - Retrieve data from touch report
 *
 * Retrieve data from the touch report based on the bit offset and bit length
 * information from the touch report configuration.
 */
static int touch_get_report_data(unsigned int offset,
		unsigned int bits, unsigned int *data)
{
	unsigned char mask;
	unsigned char byte_data;
	unsigned int output_data;
	unsigned int bit_offset;
	unsigned int byte_offset;
	unsigned int data_bits;
	unsigned int available_bits;
	unsigned int remaining_bits;
	unsigned char *touch_report;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (bits == 0 || bits > 32) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Invalid number of bits\n");
		return -EINVAL;
	}

	if (offset + bits > tcm_hcd->report.buffer.data_length * 8) {
		*data = 0;
		return 0;
	}

	touch_report = tcm_hcd->report.buffer.buf;

	output_data = 0;
	remaining_bits = bits;

	bit_offset = offset % 8;
	byte_offset = offset / 8;

	while (remaining_bits) {
		byte_data = touch_report[byte_offset];
		byte_data >>= bit_offset;

		available_bits = 8 - bit_offset;
		data_bits = MIN(available_bits, remaining_bits);
		mask = 0xff >> (8 - data_bits);

		byte_data &= mask;

		output_data |= byte_data << (bits - remaining_bits);

		bit_offset = 0;
		byte_offset += 1;
		remaining_bits -= data_bits;
	}

	*data = output_data;

	return 0;
}

#ifdef SUPPORT_VERIFICATION_REPORT_INFO
static unsigned int verificationCrc32(unsigned char *data, unsigned short len)
{
    unsigned int crc = 0xa55a;
    unsigned char i = 0;
    while(len--)
    {
        crc ^= (unsigned int)(*data++) << 24;

        for(i = 0; i < 8; ++i)
        {
            if(crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11234L;
            else
                crc <<= 1;
        }
    }
    return crc;
}
#endif

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int touch_parse_report(void)
{
	int retval;
	bool active_only;
	bool num_of_active_objects;
	unsigned char code;
	unsigned int size;
	unsigned int idx;
	unsigned int obj;
	unsigned int next;
	unsigned int data;
	unsigned int bits;
	unsigned int offset;
	unsigned int objects;
	unsigned int active_objects;
	unsigned int report_size;
	unsigned int config_size;
	unsigned char *config_data;
	struct touch_data *touch_data;
	struct object_data *object_data;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	static unsigned int end_of_foreach;
	struct ovt_tcm_app_info *app_info;
	int rows, cols;

	app_info = &tcm_hcd->app_info;
	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;

	config_data = tcm_hcd->config.buf;
	config_size = tcm_hcd->config.data_length;

	report_size = tcm_hcd->report.buffer.data_length;

	size = sizeof(*object_data) * touch_hcd->max_objects;
	memset(touch_hcd->touch_data.object_data, 0x00, size);

	num_of_active_objects = false;

	idx = 0;
	obj = 0;
	next = 0;
	offset = 0;
	objects = 0;
	active_objects = 0;
	active_only = false;

	while (idx < config_size) {
		code = config_data[idx++];
		switch (code) {
		case TOUCH_END:
			goto exit;
		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;
		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;
		case TOUCH_FOREACH_END:
			end_of_foreach = idx;
			if (active_only) {
				if (num_of_active_objects) {
					objects++;
					if (objects < active_objects)
						idx = next;
				} else if (offset < report_size * 8) {
					idx = next;
				}
			} else {
				obj++;
				if (obj < touch_hcd->max_objects)
					idx = next;
			}
			break;
		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ceil_div(offset, 8) * 8;
			break;
		case TOUCH_TIMESTAMP:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get timestamp\n");
				return retval;
			}
			touch_data->timestamp = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_INDEX:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &obj);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object index\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_OBJECT_N_CLASSIFICATION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object classification\n");
				return retval;
			}
			object_data[obj].status = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object x position\n");
				return retval;
			}
			object_data[obj].x_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object y position\n");
				return retval;
			}
			object_data[obj].y_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Z:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object z\n");
				return retval;
			}
			object_data[obj].z = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object x width\n");
				return retval;
			}
			object_data[obj].x_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object y width\n");
				return retval;
			}
			object_data[obj].y_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object tx position\n");
				return retval;
			}
			object_data[obj].tx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object rx position\n");
				return retval;
			}
			object_data[obj].rx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_FORCE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object force\n");
				return retval;
			}
			touch_data->force_data = data;
			offset += bits;
			break;
		case TOUCH_REPORT_PALM_DETECTED:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get palm status\n");
				return retval;
			}
			touch_data->palm_status = data;
			offset += bits;
			break;
		case TOUCH_REPORT_WATERPROOF_INFO:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get palm status\n");
				return retval;
			}
			touch_data->moisture_status = data;
			offset += bits;
			break;
		case TOUCH_AOD_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get palm status\n");
				return retval;
			}
			touch_data->aod_status = data;
			if (touch_data->aod_status) {
				LOGN(tcm_hcd->pdev->dev.parent, "aod status value:%x\n", touch_data->aod_status);
			}
			offset += bits;
			break;
		case TOUCH_GLOVE_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get glove status\n");
				return retval;
			}
			touch_data->glove_status = data;
			offset += bits;
			break;
		case TOUCH_FINGERPRINT_AREA_MEET:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object force\n");
				return retval;
			}
			touch_data->fingerprint_area_meet = data;
			LOGN(tcm_hcd->pdev->dev.parent,
					"fingerprint_area_meet = %x\n",
					touch_data->fingerprint_area_meet);
			offset += bits;
			break;
		case TOUCH_0D_BUTTONS_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get 0D buttons state\n");
				return retval;
			}
			touch_data->buttons_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_ID:
		case TOUCH_REPORT_GESTURE_SWIPE:
		case TOUCH_REPORT_GESTURE_CIRCLE:
		case TOUCH_REPORT_GESTURE_UNICODE:
		case TOUCH_REPORT_GESTURE_VEE:
		case TOUCH_REPORT_GESTURE_TRIANGLE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get gesture double tap\n");
				return retval;
			}
#ifdef SUPPORT_VERIFICATION_REPORT_INFO
			touch_data->gesture_id = tcm_hcd->report.buffer.buf[4];
#else
			touch_data->gesture_id = tcm_hcd->report.buffer.buf[0];
#endif
			offset += bits;
			break;
		case TOUCH_REPORT_GESTURE_INFO:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get frame rate\n");
				return retval;
			}
			touch_data->gesture_extra_info = data;
			offset += bits;
			break;
		case TOUCH_REPORT_GESTURE_COORDINATE:
			bits = config_data[idx++];
			 retval = secure_memcpy((unsigned char *)&touch_data->gesture_six_points[0], sizeof(touch_data->gesture_six_points), \
				 &tcm_hcd->report.buffer.buf[offset/8], bits / 8, bits / 8);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to copy gesture data\n");
				return retval;
				}
			offset += bits;
			break;
		case TOUCH_FRAME_RATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get frame rate\n");
				return retval;
			}
			touch_data->frame_rate = data;
			offset += bits;
			break;
		case TOUCH_POWER_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get power IM\n");
				return retval;
			}
			touch_data->power_im = data;
			offset += bits;
			break;
		case TOUCH_CID_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get CID IM\n");
				return retval;
			}
			touch_data->cid_im = data;
			offset += bits;
			break;
		case TOUCH_RAIL_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get rail IM\n");
				return retval;
			}
			touch_data->rail_im = data;
			offset += bits;
			break;
		case TOUCH_CID_VARIANCE_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get CID variance IM\n");
				return retval;
			}
			touch_data->cid_variance_im = data;
			offset += bits;
			break;
		case TOUCH_NSM_FREQUENCY:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get NSM frequency\n");
				return retval;
			}
			touch_data->nsm_frequency = data;
			offset += bits;
			break;
		case TOUCH_NSM_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get NSM state\n");
				return retval;
			}
			touch_data->nsm_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_DATA:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get number of active objects\n");
				return retval;
			}
			active_objects = data;
			num_of_active_objects = true;
			touch_data->num_of_active_objects = data;
			offset += bits;
			if (touch_data->num_of_active_objects == 0) {
				if (0 == end_of_foreach) {
					LOGE(tcm_hcd->pdev->dev.parent,
						"Invalid report, num_active and end_foreach are 0\n");
					return 0;
				}
				idx = end_of_foreach;
			}
			break;
		case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get num CPU cycles used since last frame\n");
				return retval;
			}
			touch_data->num_of_cpu_cycles = data;
			offset += bits;
			break;
		case TOUCH_FACE_DETECT:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to detect face\n");
				return retval;
			}
			touch_data->fd_data = data;
#ifdef SUPPORT_FACE_DETECT
			ovt_check_face_state(touch_data->fd_data);
#endif
			offset += bits;
			break;
		case TOUCH_TUNING_GAUSSIAN_WIDTHS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
			bits = config_data[idx++];
			offset += bits;
			break;
#ifdef SUPPORT_KNUCKLE_DATA_REPORT
		case TOUCH_KNUCKLE_DATA:
			bits = config_data[idx++];
			bits = bits | (config_data[idx++] << 8);
			retval = secure_memcpy(&touch_data->knuckle_data[0], KNUCKLE_DATA_SIZE, &tcm_hcd->report.buffer.buf[offset/8], bits / 8, bits / 8);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to copy knuckle data\n");
				return retval;
			}
			offset += bits;
			break;
#endif
#if 0
		case TOUCH_FRAME_DATA:
			bits = config_data[idx++];
			bits = bits | (config_data[idx++] << 8);
			if (report_size - (offset / 8) < rows * cols * 2) {
				break;
			}
			if ((offset % 8) || (bits % 8))
				printk("No byte alignment for frame data\n");

			secure_memcpy(touch_data->frame_data, bits/8, &tcm_hcd->report.buffer.buf[offset / 8], bits/8, bits/8);
			printk("ovt-debug-frame  frame data start\n");
			ovt_log_frame_data(touch_data->frame_data, bits/8, 1, cols);
			printk("ovt-debug-frame frame data end\n");
			offset += bits;
			break;
		case TOUCH_5_LINE_INFO:
			bits = config_data[idx++];
			bits = bits | (config_data[idx++] << 8);
			if (report_size - (offset / 8) < cols * 5 * 2) {
				break;
			}
			if ((offset % 8) || (bits % 8))
				printk("No byte alignment for frame data\n");

			secure_memcpy(touch_data->ifp_5_line_data, bits/8, &tcm_hcd->report.buffer.buf[offset / 8], bits/8, bits/8);
			offset += bits;
			printk("ovt-debug-frame 5 line data start\n");
			ovt_log_frame_data(touch_data->ifp_5_line_data, bits/8, 0, cols);
			printk("ovt-debug-frame 5 line data end\n");
			break;
#endif
#ifdef SUPPORT_VERIFICATION_REPORT_INFO
		case TOUCH_REPORT_VERIFICATION_INFO:
			{
				unsigned int tmp_data;
				unsigned char byte1 = 0;
				unsigned char byte3 = 0;
				bits = config_data[idx++];
				offset += bits;
				if (report_size > 4)
				{
					unsigned int expect_result = 0;
					unsigned int cal_result = 0;
					unsigned char *touch_report;

					touch_report = tcm_hcd->report.buffer.buf;
					expect_result = touch_report[0] + ((unsigned int)(touch_report[1]) << 8) + \
													((unsigned int)(touch_report[2]) << 16) + ((unsigned int)(touch_report[3]) << 24);
					cal_result = verificationCrc32(&touch_report[4], report_size - 4);
					byte1 = tcm_hcd->image_packrat_number & 0xff;
					byte3 = (tcm_hcd->image_packrat_number >> 16) & 0xff;
					tmp_data = (tcm_hcd->image_packrat_number & 0xff00ff00) | byte3 | ((unsigned int)byte1 << 16);
					cal_result ^= tmp_data;
					if (expect_result != cal_result) {
						return -1;
					}
				}
			}
			break;
#endif
		default:
			bits = config_data[idx++];
			offset += bits;
			break;
		}
	}

exit:
	return 0;
}

static void touch_process_gesture_event(struct touch_hcd *touch_hcd)
{
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	struct touch_data *touch_data;

	touch_data = &touch_hcd->touch_data;

	if (touch_data->gesture_id) {
		switch (touch_data->gesture_id) {
			case GESTURE_DOUBLE_TAP:
				LOGN(tcm_hcd->pdev->dev.parent, "double tap detect\n");
				input_report_key(touch_hcd->input_dev, KEY_F4, 1);
				input_sync(touch_hcd->input_dev);
				input_report_key(touch_hcd->input_dev, KEY_F4, 0);
				input_sync(touch_hcd->input_dev);
				break;
			case GESTURE_CIRCLE:
				LOGN(tcm_hcd->pdev->dev.parent, "circle detect\n");
				input_report_key(touch_hcd->input_dev, KEY_F5, 1);
				input_sync(touch_hcd->input_dev);
				input_report_key(touch_hcd->input_dev, KEY_F5, 0);
				input_sync(touch_hcd->input_dev);
				break;
			case GESTURE_SWIPE:
				if (touch_data->gesture_extra_info == 0x41) {
					LOGN(tcm_hcd->pdev->dev.parent, "positive x swipe\n");
				} else if (touch_data->gesture_extra_info == 0x42) {
					LOGN(tcm_hcd->pdev->dev.parent, "negative x swipe\n");
				} else if (touch_data->gesture_extra_info == 0x44) {
					LOGN(tcm_hcd->pdev->dev.parent, "positive y swipe\n");
				} else if (touch_data->gesture_extra_info == 0x48) {
					LOGN(tcm_hcd->pdev->dev.parent, "negative y swipe\n");
				} else if (touch_data->gesture_extra_info == 0x80) {
					LOGN(tcm_hcd->pdev->dev.parent, "two finger swipe down\n");
				} else {
					LOGN(tcm_hcd->pdev->dev.parent, "unknown direction swipe: %x\n", touch_data->gesture_extra_info);
				}
				break;
			case GESTURE_UNICODE:
				if (touch_data->gesture_extra_info == 0x63) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode c\n");
				} else if (touch_data->gesture_extra_info == 0x65) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode e\n");
				} else if (touch_data->gesture_extra_info == 0x6d) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode m\n");
				} else if (touch_data->gesture_extra_info == 0x73) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode s\n");
				} else if (touch_data->gesture_extra_info == 0x77) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode w\n");
				} else if (touch_data->gesture_extra_info == 0x7a) {
					LOGN(tcm_hcd->pdev->dev.parent, "unicode z\n");
				} else {
					LOGN(tcm_hcd->pdev->dev.parent, "unknown unicode value :%x\n", touch_data->gesture_extra_info);
				}
				break;
			case GESTURE_VEE:
				if (touch_data->gesture_extra_info == 0x01) {
					LOGN(tcm_hcd->pdev->dev.parent, "up vee\n");
				} else if (touch_data->gesture_extra_info == 0x02) {
					LOGN(tcm_hcd->pdev->dev.parent, "down vee\n");
					input_report_key(touch_hcd->input_dev, KEY_F6, 1);
					input_sync(touch_hcd->input_dev);
					input_report_key(touch_hcd->input_dev, KEY_F6, 0);
					input_sync(touch_hcd->input_dev);
				} else if (touch_data->gesture_extra_info == 0x04) {
					LOGN(tcm_hcd->pdev->dev.parent, "left vee\n");
				} else if (touch_data->gesture_extra_info == 0x08) {
					LOGN(tcm_hcd->pdev->dev.parent, "right vee\n");
				} else {
					LOGN(tcm_hcd->pdev->dev.parent, "unknown vee value :%x\n", touch_data->gesture_extra_info);
				}
				break;
			case GESTURE_TRIANGLE:
				LOGN(tcm_hcd->pdev->dev.parent, "triangle detect: %x\n", touch_data->gesture_extra_info);
				break;
			default:
				LOGN(tcm_hcd->pdev->dev.parent, "unknown gesture: %x\n", touch_data->gesture_id);
				break;
		}
	}

	//aod process
	if (touch_data->aod_status) {
		LOGN(tcm_hcd->pdev->dev.parent, "single tap detect\n");
		input_report_key(touch_hcd->input_dev, KEY_F7, 1);
		input_sync(touch_hcd->input_dev);
		input_report_key(touch_hcd->input_dev, KEY_F7, 0);
		input_sync(touch_hcd->input_dev);
		LOGN(tcm_hcd->pdev->dev.parent, "aod status:%x\n", touch_data->aod_status);
	}
}

/**
 * touch_report() - Report touch events
 *
 * Retrieve data from the touch report generated by the device and report touch
 * events to the input subsystem.
 */
static void touch_report(void)
{
	int retval;
	unsigned int idx;
	unsigned int x;
	unsigned int y;
#ifdef REPORT_Z_MAJOR_VALUE
	unsigned int z;
	unsigned int major;
#endif
	unsigned int temp;
	unsigned int status;
	unsigned int touch_count;
	struct touch_data *touch_data;
	struct object_data *object_data;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	if (!touch_hcd->init_touch_ok)
		return;

	if (touch_hcd->input_dev == NULL)
		return;

	if (touch_hcd->suspend_touch)
		return;

	mutex_lock(&touch_hcd->report_mutex);

	retval = touch_parse_report();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to parse touch report\n");
		goto exit;
	}

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;

#if WAKEUP_GESTURE
	if (tcm_hcd->in_suspend && tcm_hcd->wakeup_gesture_enabled) {
		// input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 1);
		// input_sync(touch_hcd->input_dev);
		// input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 0);
		// input_sync(touch_hcd->input_dev);
		touch_process_gesture_event(touch_hcd);
	}
	if (tcm_hcd->aod_enabled) {
		touch_process_gesture_event(touch_hcd);
	}
#endif

	if ((tcm_hcd->in_suspend) || (touch_hcd->suspend_touch_finger))
		goto exit;

	touch_count = 0;

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		if (touch_hcd->prev_status[idx] == LIFT &&
				object_data[idx].status == LIFT)
			status = NOP;
		else
			status = object_data[idx].status;

		switch (status) {
		case LIFT:
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_mt_report_slot_state(touch_hcd->input_dev,
					MT_TOOL_FINGER, 0);
#endif
			break;
		case FINGER:
		case GLOVED_FINGER:
			x = object_data[idx].x_pos;
			y = object_data[idx].y_pos;
#ifdef REPORT_Z_MAJOR_VALUE
			z = object_data[idx].z;
			major = (object_data[idx].x_width + object_data[idx].y_width) / 2;
#endif
			if (bdata->swap_axes) {
				temp = x;
				x = y;
				y = temp;
			}
			if (bdata->x_flip)
				x = touch_hcd->input_params.max_x - x;
			if (bdata->y_flip)
				y = touch_hcd->input_params.max_y - y;
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_mt_report_slot_state(touch_hcd->input_dev,
					MT_TOOL_FINGER, 1);
#endif
			input_report_key(touch_hcd->input_dev,
					BTN_TOUCH, 1);
			input_report_key(touch_hcd->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_POSITION_Y, y);
#ifdef REPORT_Z_MAJOR_VALUE
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_PRESSURE, z);
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_TOUCH_MAJOR, major);
#endif

#ifndef TYPE_B_PROTOCOL
			input_mt_sync(touch_hcd->input_dev);
#endif

#ifdef REPORT_Z_MAJOR_VALUE
			LOGD(tcm_hcd->pdev->dev.parent,
					"Finger %d: x = %d, y = %d z = %d, x_width:%d, y_width:%d\n",
					idx, x, y, z, object_data[idx].x_width, object_data[idx].y_width);
#else
			LOGD(tcm_hcd->pdev->dev.parent,
					"Finger %d: x = %d, y = %d\n",
					idx, x, y);
#endif
			touch_count++;
			if(tcm_hcd->tp_glove_flag) {
				LOGD(tcm_hcd->pdev->dev.parent,
					"glove_mode:%d\n", touch_data->glove_status);
			}
			break;
		default:
			break;
		}

		touch_hcd->prev_status[idx] = object_data[idx].status;
	}

	if (touch_count == 0) {
		input_report_key(touch_hcd->input_dev,
				BTN_TOUCH, 0);
		input_report_key(touch_hcd->input_dev,
				BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(touch_hcd->input_dev);
#endif
	}

	input_sync(touch_hcd->input_dev);

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/**
 * touch_set_input_params() - Set input parameters
 *
 * Set the input parameters of the input device based on the information
 * retrieved from the application information packet. In addition, set up an
 * array for tracking the status of touch objects.
 */
#ifdef MTK_PLATFORM
extern struct tpd_device *tpd;
extern int tpd_register_flag;
#endif

static int touch_set_input_params(void)
{
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

#ifdef MTK_PLATFORM
	touch_hcd->input_dev = tpd->dev;
#endif

	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_POSITION_X, 0, touch_hcd->max_x, 0, 0);
	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_POSITION_Y, 0, touch_hcd->max_y, 0, 0);

#ifdef REPORT_Z_MAJOR_VALUE
	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_PRESSURE, 0, MAX_Z_VALUE, 0, 0);
	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_TOUCH_MAJOR, 0, MAX_MAJOR_VALUE, 0, 0);
	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_TOUCH_MINOR, 0, MAX_MINOR_VALUE, 0, 0);
#endif

	input_mt_init_slots(touch_hcd->input_dev, touch_hcd->max_objects,
			INPUT_MT_DIRECT);

	touch_hcd->input_params.max_x = touch_hcd->max_x;
	touch_hcd->input_params.max_y = touch_hcd->max_y;
	touch_hcd->input_params.max_objects = touch_hcd->max_objects;

	if (touch_hcd->max_objects == 0)
		return 0;

	kfree(touch_hcd->prev_status);
	touch_hcd->prev_status = kzalloc(touch_hcd->max_objects, GFP_KERNEL);
	if (!touch_hcd->prev_status) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->prev_status\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * touch_get_input_params() - Get input parameters
 *
 * Retrieve the input parameters to register with the input subsystem for
 * the input device from the application information packet. In addition,
 * the touch report configuration is retrieved and stored.
 */
static int touch_get_input_params(void)
{
	int retval;
	unsigned int temp;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct ovt_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	app_info = &tcm_hcd->app_info;
	touch_hcd->max_x = le2_to_uint(app_info->max_x);
	touch_hcd->max_y = le2_to_uint(app_info->max_y);
	touch_hcd->max_objects = le2_to_uint(app_info->max_objects);

	if (bdata->swap_axes) {
		temp = touch_hcd->max_x;
		touch_hcd->max_x = touch_hcd->max_y;
		touch_hcd->max_y = temp;
	}

	LOCK_BUFFER(tcm_hcd->config);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_GET_TOUCH_REPORT_CONFIG,
			NULL,
			0,
			&tcm_hcd->config.buf,
			&tcm_hcd->config.buf_size,
			&tcm_hcd->config.data_length,
			NULL,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n",
				STR(CMD_GET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(tcm_hcd->config);
		return retval;
	}

	UNLOCK_BUFFER(tcm_hcd->config);

	return 0;
}

/**
 * touch_set_input_dev() - Set up input device
 *
 * Allocate an input device, configure the input device based on the particular
 * input events to be reported, and register the input device with the input
 * subsystem.
 */
static int touch_set_input_dev(void)
{
	int retval;

#ifndef MTK_PLATFORM
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	touch_hcd->input_dev = input_allocate_device();
	if (touch_hcd->input_dev == NULL) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate input device\n");
		return -ENODEV;
	}

	touch_hcd->input_dev->name = TOUCH_INPUT_NAME;
	touch_hcd->input_dev->phys = TOUCH_INPUT_PHYS_PATH;
	touch_hcd->input_dev->id.product = OMNIVISION_TCM_ID_PRODUCT;
	touch_hcd->input_dev->id.version = OMNIVISION_TCM_ID_VERSION;
	touch_hcd->input_dev->dev.parent = tcm_hcd->pdev->dev.parent;
	input_set_drvdata(touch_hcd->input_dev, tcm_hcd);
#else
	touch_hcd->input_dev = tpd->dev;
#endif
	set_bit(EV_SYN, touch_hcd->input_dev->evbit);
	set_bit(EV_KEY, touch_hcd->input_dev->evbit);
	set_bit(EV_ABS, touch_hcd->input_dev->evbit);
	set_bit(BTN_TOUCH, touch_hcd->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, touch_hcd->input_dev->keybit);
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, touch_hcd->input_dev->propbit);
#endif

#if WAKEUP_GESTURE
	set_bit(KEY_F4, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_F4);
	set_bit(KEY_F5, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_F5);
	set_bit(KEY_F6, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_F6);
	set_bit(KEY_F7, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_F7);
#endif

	retval = touch_set_input_params();

#ifndef MTK_PLATFORM
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set input parameters\n");
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}

	retval = input_register_device(touch_hcd->input_dev);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to register input device\n");
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}
#endif

	return 0;
}
/*

*/

/**
 * touch_set_report_config() - Set touch report configuration
 *
 * Send the SET_TOUCH_REPORT_CONFIG command to configure the format and content
 * of the touch report.
 */
static int touch_set_report_config(void)
{
	int retval;
	unsigned int idx;
	unsigned int length;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	int rows, cols;

	app_info = &tcm_hcd->app_info;
	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

#ifdef USE_DEFAULT_TOUCH_REPORT_CONFIG
	return 0;
#endif

	length = le2_to_uint(app_info->max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&touch_hcd->out,
			length);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	idx = 0;
#ifdef SUPPORT_VERIFICATION_REPORT_INFO
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_VERIFICATION_INFO;
	touch_hcd->out.buf[idx++] = 32;
#endif
	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_ID;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_CIRCLE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_SWIPE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_UNICODE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_VEE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_TRIANGLE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_PAD_TO_NEXT_BYTE;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_PALM_DETECTED;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_AOD_STATE;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_WATERPROOF_INFO;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_GLOVE_STATE;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_FACE_DETECT;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_WIDTH;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_WIDTH;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Z;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_SET_TOUCH_REPORT_CONFIG,
			touch_hcd->out.buf,
			length,
			&touch_hcd->resp.buf,
			&touch_hcd->resp.buf_size,
			&touch_hcd->resp.data_length,
			NULL,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n",
				STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	LOGN(tcm_hcd->pdev->dev.parent,
			"Set touch config done\n");

	return 0;
}

static int touch_set_lpwg_report_config(void)
{
	int retval;
	unsigned int idx;
	unsigned int length;
	struct ovt_tcm_app_info *app_info;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	int rows, cols;

	app_info = &tcm_hcd->app_info;
	rows = le2_to_uint(app_info->num_of_image_rows);
	cols = le2_to_uint(app_info->num_of_image_cols);

#ifdef USE_DEFAULT_TOUCH_REPORT_CONFIG
	return 0;
#endif

	length = le2_to_uint(app_info->max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = ovt_tcm_alloc_mem(tcm_hcd,
			&touch_hcd->out,
			length);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	idx = 0;
#ifdef SUPPORT_VERIFICATION_REPORT_INFO
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_VERIFICATION_INFO;
	touch_hcd->out.buf[idx++] = 32;
#endif
	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_ID;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_CIRCLE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_SWIPE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_UNICODE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_VEE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_TRIANGLE;
	touch_hcd->out.buf[idx++] = 1;
	touch_hcd->out.buf[idx++] = TOUCH_PAD_TO_NEXT_BYTE;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_COORDINATE;
	touch_hcd->out.buf[idx++] = 192;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_PALM_DETECTED;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_AOD_STATE;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_WATERPROOF_INFO;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_GLOVE_STATE;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_FACE_DETECT;
	touch_hcd->out.buf[idx++] = 8;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_WIDTH;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_WIDTH;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Z;
	touch_hcd->out.buf[idx++] = 16;	
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_SET_TOUCH_REPORT_CONFIG,
			touch_hcd->out.buf,
			length,
			&touch_hcd->resp.buf,
			&touch_hcd->resp.buf_size,
			&touch_hcd->resp.data_length,
			NULL,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n",
				STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	LOGN(tcm_hcd->pdev->dev.parent,
			"Set touch config done\n");

	return 0;
}

/**
 * touch_check_input_params() - Check input parameters
 *
 * Check if any of the input parameters registered with the input subsystem for
 * the input device has changed.
 */
static int touch_check_input_params(void)
{
	unsigned int size;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (touch_hcd->max_x == 0 && touch_hcd->max_y == 0)
		return 0;

	if (touch_hcd->input_params.max_objects != touch_hcd->max_objects) {
		kfree(touch_hcd->touch_data.object_data);
		size = sizeof(*touch_hcd->touch_data.object_data);
		size *= touch_hcd->max_objects;
		touch_hcd->touch_data.object_data = kzalloc(size, GFP_KERNEL);
		if (!touch_hcd->touch_data.object_data) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to allocate memory for touch_hcd->touch_data.object_data\n");
			return -ENOMEM;
		}
		return 1;
	}

	if (touch_hcd->input_params.max_x != touch_hcd->max_x)
		return 1;

	if (touch_hcd->input_params.max_y != touch_hcd->max_y)
		return 1;

	return 0;
}

/**
 * touch_set_input_reporting() - Configure touch report and set up new input
 * device if necessary
 *
 * After a device reset event, configure the touch report and set up a new input
 * device if any of the input parameters has changed after the device reset.
 */
static int touch_set_input_reporting(void)
{
	int retval;
	struct ovt_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (IS_NOT_FW_MODE(tcm_hcd->id_info.mode) ||
			tcm_hcd->app_status != APP_STATUS_OK) {
		LOGN(tcm_hcd->pdev->dev.parent,
				"Identifying mode = 0x%02x\n",
				tcm_hcd->id_info.mode);

		return 0;
	}

	touch_hcd->init_touch_ok = false;

	touch_free_objects();

	mutex_lock(&touch_hcd->report_mutex);

	retval = touch_set_report_config();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set report config\n");
		goto exit;
	}

	retval = touch_get_input_params();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to get input parameters\n");
		goto exit;
	}

	retval = touch_check_input_params();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to check input parameters\n");
		goto exit;
	} else if (retval == 0) {
		LOGN(tcm_hcd->pdev->dev.parent,
				"Input parameters unchanged\n");
		goto exit;
	}
#ifndef MTK_PLATFORM
	if (touch_hcd->input_dev != NULL) {
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
	}
#endif
	retval = touch_set_input_dev();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set up input device\n");
		goto exit;
	}

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->init_touch_ok = retval < 0 ? false : true;

	return retval;
}


int touch_init(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;

	touch_hcd = kzalloc(sizeof(*touch_hcd), GFP_KERNEL);
	if (!touch_hcd) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd\n");
		return -ENOMEM;
	}

	touch_hcd->tcm_hcd = tcm_hcd;

	mutex_init(&touch_hcd->report_mutex);

	INIT_BUFFER(touch_hcd->out, false);
	INIT_BUFFER(touch_hcd->resp, false);

	retval = touch_set_input_reporting();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set up input reporting\n");
		goto err_set_input_reporting;
	}

	tcm_hcd->report_touch = touch_report;

	return 0;

err_set_input_reporting:
	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

	return retval;
}

int touch_remove(struct ovt_tcm_hcd *tcm_hcd)
{
	if (!touch_hcd)
		goto exit;

	tcm_hcd->report_touch = NULL;

	if (touch_hcd->input_dev)
		input_unregister_device(touch_hcd->input_dev);

	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

exit:

	return 0;
}

int touch_reinit(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval = 0;

	if (!touch_hcd) {
		retval = touch_init(tcm_hcd);
		return retval;
	}

	touch_free_objects();

	if (IS_NOT_FW_MODE(tcm_hcd->id_info.mode)) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Application mode is not running (firmware mode = %d)\n",
				tcm_hcd->id_info.mode);
		return 0;
	}

	if (!tcm_hcd->in_hdl_mode) {
		retval = tcm_hcd->identify(tcm_hcd, false);
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to do identification\n");
			return retval;
		}
	}

	retval = touch_set_input_reporting();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set up input reporting\n");
	}

	return retval;
}
// gesture  mode use
int touch_suspend(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	touch_hcd->suspend_touch_finger = true;
	touch_hcd->suspend_touch = true;

	touch_free_objects();

	if (tcm_hcd->wakeup_gesture_enabled || tcm_hcd->aod_enabled) {
		if (!touch_hcd->irq_wake) {
			enable_irq_wake(tcm_hcd->irq);
			touch_hcd->irq_wake = true;
		}

		touch_hcd->suspend_touch = false;
#ifdef CONFIG_SUPPORT_MULTI_FIRMWARE
		tcm_hcd->download_firmware_image_id(tcm_hcd, LPWG_FIRMWARE);
#endif
		retval = tcm_hcd->set_dynamic_config(tcm_hcd,
				DC_IN_WAKEUP_GESTURE_MODE,
				1);
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to enable wakeup gesture mode\n");
			return retval;
		}
		tcm_hcd->aod_enabled = false;
		printk("touch_suspend aod is suspend end\n");
		if (tcm_hcd->aod_enabled) {
			unsigned short gesture_type_v = 0xff;
			//unsigned short start_x = 0, start_y = 0, width = 1080, height = 1920;
			//0x80: only enable single tap
			//0xff: enable all gesture type
			//display off aod enable, just set gesture type
			retval = tcm_hcd->set_dynamic_config(tcm_hcd,
					DC_GESTURE_TYPE_CONTROL,
					gesture_type_v);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to set aod gesture type:%x\n", gesture_type_v);
				return retval;
			}
			// tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_startX, start_x);
			// tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_startY, start_y);
			// tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_width, width);
			// tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_height, height);
		}

		retval = touch_set_lpwg_report_config();
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to set lpwg report config\n");
			return retval;
		}

		retval = touch_get_input_params();
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to get lpwg input parameters\n");
			return retval;
		}
	}

	return 0;
}

int touch_resume(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	touch_hcd->suspend_touch = false;
	touch_hcd->suspend_touch_finger = false;

	if (tcm_hcd->wakeup_gesture_enabled || tcm_hcd->aod_enabled || tcm_hcd->aod_gesture_flag) {
		tcm_hcd->aod_gesture_flag = false;
		if (touch_hcd->irq_wake) {
			disable_irq_wake(tcm_hcd->irq);
			touch_hcd->irq_wake = false;
		}

		retval = tcm_hcd->set_dynamic_config(tcm_hcd,
				DC_IN_WAKEUP_GESTURE_MODE,
				0);
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to disable wakeup gesture mode\n");
			return retval;
		}
		if (tcm_hcd->aod_enabled) {
			unsigned short start_x = 0, start_y = 0, width = 1080, height = 1920;
			//display on aod enable
#ifdef CONFIG_SUPPORT_MULTI_FIRMWARE
			tcm_hcd->download_firmware_image_id(tcm_hcd, LPWG_FIRMWARE);
#endif
			retval = tcm_hcd->set_dynamic_config(tcm_hcd,
					DCFG_ENTRY_alwaysOnDisplay_enable,
					1);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to disable wakeup gesture mode\n");
				return retval;
			}
			// tcm_hcd->aod_enabled = false;
			// printk("touch_resume tcm_hcd->aod_enabled is resume end\n");
			tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_startX, start_x);
			tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_startY, start_y);
			tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_width, width);
			tcm_hcd->set_dynamic_config(tcm_hcd, DCFG_ENTRY_alwaysOnDisplay_height, height);
		} else {
#ifdef CONFIG_SUPPORT_MULTI_FIRMWARE
			tcm_hcd->download_firmware_image_id(tcm_hcd, NORMAL_APP_FIRMWARE);
#endif
		}
	}

	return 0;
}




