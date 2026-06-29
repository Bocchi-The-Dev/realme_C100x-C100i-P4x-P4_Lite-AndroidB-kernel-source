/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "cam_buf_manager.h"
#include "isp_ltm.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ISP_LTM: %d %d %s : "fmt, current->pid, __LINE__, __func__

#define ISP_LTM_TIMEOUT           msecs_to_jiffies(100)
#define ISP_LTM_CLIP_TURN_OFF_VAL 65535
/*
 * row : 2, 4, 8
 * col : 2, 4, 8
 */
#define MAX_TILE                  64

/*
 * LTM logical and algorithm
 */

/*
 * Input:
 * frame size: x, y
 *
 * Output:
 * frame size after binning: x, y
 * binning factor
 *
 * Notes:
 * Sharkl5 ONLY suppout 1/2 binning
 *
 */
static int ispltm_binning_factor_calc(ltm_param_t *histo)
{
	int ret = 0;
	int min_tile_num = 0;
	int set_tile_num = 0;
	int frame_size = 0;
	int binning_factor = 0;
	int pow_factor = 0;

	frame_size = histo->frame_width * histo->frame_height;
	/*
	 * min_tile_num = (uint8)ceil(
	 * (float)(frame_width*frame_height)/TILE_MAX_SIZE);
	 */
	min_tile_num = (frame_size + TILE_MAX_SIZE - 1) / TILE_MAX_SIZE;
	set_tile_num = histo->tile_num_x * histo->tile_num_y;

	/*
	 * binning_factor = (uint8)ceil(MAX(log(min_tile_num/64.0)/log(4.0),0));
	 */
	if (min_tile_num <= set_tile_num) {
		binning_factor = 0;
		pow_factor = 1; /* pow(2.0, binning_factor) */
	} else if (min_tile_num <= set_tile_num * 4) {
		binning_factor = 1;
		pow_factor = 2; /* pow(2.0, binning_factor) */
	} else {
		BUG_ON(0);
	}

	/*
	 * frame_width  = frame_width /(2*(uint16)pow(2.0, binning_factor)) *2;
	 * frame_height = frame_height/(2*(uint16)pow(2.0, binning_factor)) *2;
	 */
	pr_debug("B binning_factor[%d], pow_factor[%d], frame_width[%d], frame_height[%d]\n",
		binning_factor, pow_factor, histo->frame_width, histo->frame_height);
	if (pow_factor != 0) {
		histo->frame_width = histo->frame_width / (2 * pow_factor) * 2;
		histo->frame_height = histo->frame_height / (2 * pow_factor) * 2;
	}
	histo->binning_en = binning_factor;
	pr_debug("A binning_factor[%d], pow_factor[%d], frame_width[%d], frame_height[%d]\n",
		binning_factor, pow_factor, histo->frame_width, histo->frame_height);

	return ret;
}

static void ispltm_rgb_map_dump_data_rtl(ltm_param_t *param_map,
				uint32_t *img_info,
				ltm_map_rtl_t *param_map_rtl)
{
	int temp;
	int tile_index_xs, tile_index_xe;
	int tile_index_ys, tile_index_ye;
	int tile_1st_xs, tile_1st_ys;
	uint8_t tile_x_num_out, tile_y_num_out;
	uint8_t tile_left_flag = 0, tile_right_flag = 0;
	uint16_t tile0_start_x, tile0_start_y, tileX_start_x;
	uint16_t tile1_start_x, tile1_start_y;
	int img_tile1_xs_offset, img_tile1_ys_offset;

	/* slice infomation */
	int img_start_x = img_info[0];
	int img_start_y = img_info[1];
	int img_end_x = img_info[2];
	int img_end_y = img_info[3];

	/* frame infomation */
	uint8_t cropUp = param_map->cropUp;
	/* uint8_t cropDown = param_map->cropDown; */
	uint8_t cropLeft = param_map->cropLeft;
	/* uint8_t cropRight = param_map->cropRight; */
	uint8_t tile_num_x = param_map->tile_num_x;
	uint8_t tile_num_y = param_map->tile_num_y;
	uint16_t tile_width = param_map->tile_width;
	uint16_t tile_height = param_map->tile_height;

	tile_index_xs = (img_start_x + tile_width / 2 - cropLeft) / tile_width - 1;
	if (tile_index_xs < 0)
		tile_index_xs = 0;
	tile_index_xe = (img_end_x + tile_width / 2 - cropLeft) / tile_width;
	if (tile_index_xe > tile_num_x - 1)
		tile_index_xe = tile_num_x - 1;
	tile_x_num_out = tile_index_xe - tile_index_xs + 1;

	tile_index_ys = (img_start_y + tile_height / 2 - cropUp) / tile_height - 1;
	if (tile_index_ys < 0)
		tile_index_ys = 0;
	tile_index_ye = (img_end_y + tile_height / 2 - cropUp) / tile_height;
	if (tile_index_ye > tile_num_y - 1)
		tile_index_ye = tile_num_y - 1;
	tile_y_num_out = tile_index_ye - tile_index_ys + 1;

	tile_1st_xs = (img_start_x - cropLeft) / tile_width;
	if (tile_1st_xs < 0)
		tile_1st_xs = 0;
	if (tile_1st_xs > tile_num_x - 1)
		tile_1st_xs = tile_num_x - 1;
	pr_debug("img_start_x[%d], cropLeft[%d], tile_width[%d], tile_1st_xs[%d]\n",
		img_start_x, cropLeft, tile_width, tile_1st_xs);

	tile_1st_ys = (img_start_y - cropUp) / tile_height;
	if (tile_1st_ys < 0)
		tile_1st_ys = 0;
	if (tile_1st_ys > tile_num_y - 1)
		tile_1st_ys = tile_num_y - 1;
	pr_debug("img_start_y[%d], cropUp[%d], tile_height[%d], tile_1st_ys[%d]\n",
		img_start_y, cropUp, tile_height, tile_1st_ys);

	tile1_start_x = tile_1st_xs * tile_width + cropLeft;
	tile1_start_y = tile_1st_ys * tile_height + cropUp;
	img_tile1_xs_offset = img_start_x - tile1_start_x;
	img_tile1_ys_offset = img_start_y - tile1_start_y;
	pr_debug("tile_1st_xs[%d], cropLeft[%d], tile_width[%d], img_start_x[%d], tile1_start_x[%d], img_tile1_xs_offset[%d]\n",
		tile_1st_xs, cropLeft, tile_width, img_start_x, tile1_start_x, img_tile1_xs_offset);

	tile0_start_x = tile_index_xs * tile_width + cropLeft;
	tile0_start_y = tile_index_ys * tile_height + cropUp;

	tileX_start_x = tile_index_xe * tile_width + cropLeft;
	temp = img_start_x - (int)tile0_start_x;
	if ((temp >= tile_width) && (temp < tile_width * 3 / 2))
		tile_left_flag = 1;
	temp = (int)tileX_start_x - img_end_x;
	if ((temp > 0) && (temp <= tile_width / 2))
		tile_right_flag = 1;

	/* output parameters for rtl */
	param_map_rtl->tile_index_xs = tile_index_xs;
	param_map_rtl->tile_index_ys = tile_index_ys;
	param_map_rtl->tile_index_xe = tile_index_xe;
	param_map_rtl->tile_index_ye = tile_index_ye;
	param_map_rtl->tile_x_num_rtl = tile_x_num_out - 1;
	param_map_rtl->tile_y_num_rtl = tile_y_num_out - 1;
	param_map_rtl->tile_width_rtl = tile_width;
	param_map_rtl->tile_height_rtl = tile_height;
	param_map_rtl->tile_size_pro_rtl = tile_width * tile_height;
	param_map_rtl->tile_start_x_rtl = img_tile1_xs_offset;
	param_map_rtl->tile_start_y_rtl = img_tile1_ys_offset;
	param_map_rtl->tile_left_flag_rtl = tile_left_flag;
	param_map_rtl->tile_right_flag_rtl = tile_right_flag;
}

static int ispltm_histo_param_calc(
	struct isp_ltm_ctx_desc *ctx, ltm_param_t *param_histo, uint32_t alignment)
{
	uint32_t max_tile_col, min_tile_row;
	uint32_t tile_num_x, tile_num_y;
	uint32_t cropRows, cropCols, cropUp, cropLeft, cropRight, cropDown;
	uint32_t min_tile_width, max_tile_height, tile_width, tile_height;
	uint32_t clipLimit_min, clipLimit;
	uint32_t strength = param_histo->strength;
	uint32_t frame_width = param_histo->frame_width;
	uint32_t frame_height = param_histo->frame_height;
	uint32_t calculate_times = 0;

	ispltm_binning_factor_calc(param_histo);

	frame_width = param_histo->frame_width;
	frame_height = param_histo->frame_height;

	if (param_histo->tile_num_auto) {
		if (!ctx->ltmhist_update) {
			int v_ceil = 0;
			int tmp = 0;

			max_tile_col = MAX(MIN(frame_width / (LTM_MIN_TILE_WIDTH * 2) * 2,
					TILE_NUM_MAX), TILE_NUM_MIN);
			min_tile_width = frame_width / (max_tile_col * alignment) * alignment;
			max_tile_height = TILE_MAX_SIZE / (min_tile_width * 2) * 2;
			/*
			 * min_tile_row = (uint8)MAX(MIN(ceil((float)frame_height /
			 *  max_tile_height), TILE_NUM_MAX), TILE_NUM_MIN);
			 */
			v_ceil = (frame_height + max_tile_height - 1) / max_tile_height;
			min_tile_row = MAX(MIN(v_ceil, TILE_NUM_MAX), TILE_NUM_MIN);

			tile_num_y = (min_tile_row / 2) * 2;
			tile_num_x = MIN(MAX(((tile_num_y * frame_width / frame_height) / 2) * 2,
					TILE_NUM_MIN), max_tile_col);

			tile_width = frame_width / (alignment * tile_num_x) * alignment;
			tile_height = frame_height / (2 * tile_num_y) * 2;

			while (tile_width * tile_height >= TILE_MAX_SIZE) {
				tile_num_y = MIN(MAX(tile_num_y + 2, TILE_NUM_MIN), TILE_NUM_MAX);
				tmp = ((tile_num_y * frame_width / frame_height) / 2) * 2;
				tile_num_x = MIN(MAX(tmp, TILE_NUM_MIN), max_tile_col);

				tile_width = frame_width / (alignment * tile_num_x) * alignment;
				tile_height = frame_height / (2 * tile_num_y) * 2;
				calculate_times++;
				if (calculate_times > 2) {
					tile_num_y = 8;
					tile_num_x = 8;
					tile_width = frame_width / (4 * tile_num_x) * 4;
					tile_height = frame_height / (2 * tile_num_y) * 2;
					break;
				}
			}
			if (tile_num_x && tile_num_y) {
				ctx->hists.tile_num_x = tile_num_x;
				ctx->hists.tile_num_y = tile_num_y;
				ctx->ltmhist_update = CAM_ENABLE;
			} else
				pr_err("fail to get tile num x_y:%d, %d.\n",
					tile_num_x, tile_num_y);
		} else {
			tile_num_x = ctx->hists.tile_num_x;
			tile_num_y = ctx->hists.tile_num_y;
			tile_width = frame_width / (alignment * tile_num_x) * alignment;
			tile_height = frame_height / (2 * tile_num_y) * 2;
		}
	} else {
		tile_num_x = param_histo->tile_num_x;
		tile_num_y = param_histo->tile_num_y;
		tile_width = frame_width / (alignment * tile_num_x) * alignment;
		tile_height = frame_height / (2 * tile_num_y) * 2;
	}

	cropRows = frame_height - tile_height * tile_num_y;
	cropCols = frame_width - tile_width * tile_num_x;
	cropUp = cropRows >> 1;
	cropDown = cropRows >> 1;
	cropLeft = cropCols >> 1;
	cropRight = cropCols >> 1;

	clipLimit_min = (tile_width * tile_height) >> BIN_NUM_BIT;
	clipLimit = (clipLimit_min * strength) >> BIN_NUM_BIT;

	/* update patameters */
	param_histo->cropUp = cropUp;
	param_histo->cropDown = cropDown;
	param_histo->cropLeft = cropLeft;
	param_histo->cropRight = cropRight;
	param_histo->cropRows = cropRows;
	param_histo->cropCols = cropCols;
	param_histo->tile_width = tile_width;
	param_histo->tile_height = tile_height;
	param_histo->frame_width = frame_width;
	param_histo->frame_height = frame_height;
	param_histo->clipLimit = clipLimit;
	param_histo->clipLimit_min = clipLimit_min;
	param_histo->tile_num_x = tile_num_x;
	param_histo->tile_num_y = tile_num_y;
	param_histo->tile_size = tile_width * tile_height;

	return 0;
}

static int ispltm_histo_config_gen(struct isp_ltm_ctx_desc *ctx, struct isp_ltm_stat_info *tuning)
{
	int ret = 0;
	struct isp_ltm_hist_param hist_param = {0};
	struct isp_ltm_hist_param *param = &hist_param;
	struct isp_ltm_hists *hists = &ctx->hists;
	struct cam_frame *ltm_hist_frame = NULL;

	/* Check bypass condition */
	param->bypass = tuning->bypass || hists->bypass;

	pr_debug("ltm hist, ctx bypass %d, tuning bypass %d\n", param->bypass, tuning->bypass);
	if (param->bypass) {
		hists->bypass = 1;
		return 0;
	}

	param->strength = tuning->strength;
	param->channel_sel = tuning->channel_sel;
	param->region_est_en = tuning->region_est_en;
	param->text_point_thres = tuning->text_point_thres;
	param->text_proportion = tuning->ltm_text.textture_proporion;
	param->tile_num_auto = tuning->tile_num_auto;
	param->tile_num_x = tuning->tile_num.tile_num_x;
	param->tile_num_y = tuning->tile_num.tile_num_y;
	param->frame_height = ctx->frame_height;
	param->frame_width = ctx->frame_width;
	pr_debug("tunning: strength %d, channel_sel %d, region_est_en %d, text_point_thres %d, textture_proporion %d, num_auto %d, num_x %d, num_y %d\n",
			tuning->strength, tuning->channel_sel, tuning->region_est_en, tuning->text_point_thres, tuning->ltm_text.textture_proporion,
			tuning->tile_num_auto, tuning->tile_num.tile_num_x, tuning->tile_num.tile_num_y);
	pr_debug("frame height %d, width %d clip_limit %d clip_limit_min %d\n",  ctx->frame_height, ctx->frame_width, tuning->clip_limit,tuning->clip_limit_min);

	ispltm_histo_param_calc(ctx, param, ISP_LTM_TILE_W_ALIGNMENT);

	hists->bypass = param->bypass;
	hists->channel_sel = param->channel_sel;
	hists->binning_en = param->binning_en;
	hists->region_est_en = param->region_est_en;
	hists->buf_sel = 0;
	hists->buf_full_mode = 0;
	hists->roi_start_x = param->cropLeft;
	hists->roi_start_y = param->cropUp;
	hists->tile_width = param->tile_width;
	hists->tile_num_x_minus = param->tile_num_x - 1;
	hists->tile_height = param->tile_height;
	hists->tile_num_y_minus = param->tile_num_y - 1;

	if ((hists->tile_width * hists->tile_height >
		LTM_MAX_TILE_RANGE) ||
		hists->tile_width < LTM_MIN_TILE_WIDTH ||
		hists->tile_height < LTM_MIN_TILE_HEIGHT ||
		hists->roi_start_x > LTM_MAX_ROI_X ||
		hists->roi_start_y > LTM_MAX_ROI_Y)
		hists->bypass = 1;

	if (hists->bypass) {
		pr_debug("tile_width %d, tile_height %d, roi_start_x %d, roi_start_y %d\n",
			hists->tile_width, hists->tile_height, hists->roi_start_x, hists->roi_start_y);
		return 0;
	}

	ltm_hist_frame = cam_buf_manager_buf_dequeue(ctx->hist_outpool, NULL, ctx->buf_manager_handle);
	if (!ltm_hist_frame) {
		if (ctx->resbuf_get_cb)
			ctx->resbuf_get_cb((void *)&ltm_hist_frame, ctx->resbuf_cb_data);
		if (!ltm_hist_frame) {
			pr_err("fail to ctx id %d, frame %p\n", ctx->ctx_id, ltm_hist_frame);
			return -1;
		}
	}
	ltm_hist_frame->common.ltm_info.tile_width = param->tile_width;
	ltm_hist_frame->common.ltm_info.tile_height = param->tile_height;
	ltm_hist_frame->common.ltm_info.tile_num_x = param->tile_num_x;
	ltm_hist_frame->common.ltm_info.tile_num_y = param->tile_num_y;
	ltm_hist_frame->common.ltm_info.frame_width = param->frame_width;
	ltm_hist_frame->common.ltm_info.frame_height = param->frame_height;
	ltm_hist_frame->common.ltm_info.statis_no = ctx->slice_no;
	ltm_hist_frame->common.ltm_info.statis_num = ctx->slice_num;
	ltm_hist_frame->common.width = param->frame_width;
	ltm_hist_frame->common.height = param->frame_height;
	ltm_hist_frame->common.fid = ctx->fid;

	if (tuning->clip_limit_min != ISP_LTM_CLIP_TURN_OFF_VAL) {
		hists->clip_limit = param->clipLimit;
		hists->clip_limit_min = param->clipLimit_min;
	} else {
		hists->clip_limit = tuning->clip_limit;
		hists->clip_limit_min = tuning->clip_limit_min;
	}
	hists->texture_proportion = param->text_proportion;
	hists->text_point_thres = param->text_point_thres;
	hists->addr = ltm_hist_frame->common.buf.iova[0];
	hists->pitch = param->tile_num_x - 1;
	hists->wr_num = param->tile_num_x * 16;
	ret = cam_buf_manager_buf_enqueue(ctx->hist_resultpool, ltm_hist_frame, NULL, ctx->buf_manager_handle);
	if (ret) {
		pr_err("fail to set ltmhist out buffer to result pool\n");
		if (ltm_hist_frame->common.is_reserved)
			cam_queue_empty_frame_put(ltm_hist_frame);
		else
			ret = cam_buf_manager_buf_enqueue(ctx->hist_outpool, ltm_hist_frame, NULL, ctx->buf_manager_handle);
		if (ret)
			cam_queue_empty_frame_put(ltm_hist_frame);
		return -1;
	}

	memcpy(hists->ltm_hist_table, tuning->ltm_hist_table, sizeof(tuning->ltm_hist_table));

	ctx->frame_width_stat = param->frame_width;
	ctx->frame_height_stat = param->frame_height;

	pr_debug("ltm hist idx[%d], hist addr[0x%lx] bypass %d\n", ctx->fid, hists->addr, hists->bypass);
	pr_debug("binning_en[%d], tile_num_x_minus[%d], tile_num_y_minus[%d], tile_num_auto[%d]\n",
		hists->binning_en, hists->tile_num_x_minus, hists->tile_num_y_minus, tuning->tile_num_auto);
	pr_debug("tile_height[%d], tile_width[%d], clip_limit_min[%d], clip_limit[%d]\n",
		hists->tile_height, hists->tile_width, hists->clip_limit_min, hists->clip_limit);
	pr_debug("idx[%d], roi_start_x[%d], roi_start_y[%d], addr[0x%lx]\n",
		ctx->fid, hists->roi_start_x, hists->roi_start_y, hists->addr);
	pr_debug("region_est_en[%d], texture_proportion[%d]\n",
			hists->region_est_en, hists->texture_proportion);

	return ret;
}

static int ispltm_map_config_gen(struct isp_ltm_ctx_desc *ctx,
			struct isp_ltm_map_info *tuning)
{
	uint32_t ratio_delta = 0;
	uint32_t slice_info[4] = {0};
	uint32_t ratio_w = 0, ratio_h = 0;
	uint32_t crop_left_curr = 0, crop_right_curr = 0;
	uint32_t crop_cols_curr = 0, crop_rows_curr = 0;
	uint32_t crop_up_curr = 0, crop_down_curr = 0;
	uint32_t frame_width_stat = 0, frame_height_stat = 0;
	uint32_t frame_width_map = 0, frame_height_map = 0;
	struct isp_ltm_tile_size ts = {0};
	struct isp_ltm_tile_size tm = {0};
	struct isp_ltm_rtl_param  rtl_param = {0};
	struct isp_ltm_tile_num_minus1 mnum = {0};
	struct isp_ltm_hist_param map_param = {0};
	struct isp_ltm_map *map = &ctx->map;
	struct isp_ltm_rtl_param *prtl = &rtl_param;
	struct isp_ltm_hist_param *param = &map_param;

	map->bypass = map->bypass || tuning->bypass;
	pr_debug("map bypass %d, tuning bypass %d\n", map->bypass, tuning->bypass);

	if (map->bypass)
		return 0;

	mnum.tile_num_x = tuning->tile_x_num;
	mnum.tile_num_y = tuning->tile_y_num;
	ts.tile_width = tuning->tile_width;
	ts.tile_height = tuning->tile_height;
	frame_width_stat = tuning->frame_width;
	frame_height_stat = tuning->frame_height;
	frame_width_map = ctx->frame_width;
	frame_height_map = ctx->frame_height;

	if ((frame_width_stat == 0) || (frame_height_stat == 0) || frame_width_map == 0 || frame_height_map == 0) {
		pr_err("fail to w_stat %d, h_stat %d, width map %d, height map %d\n",
			frame_width_stat, frame_height_stat,
			frame_width_map, frame_height_map);
		map->bypass = 1;
		return 0;
	}

	ratio_delta = abs((frame_width_stat * ctx->frame_height) *
		100 / (frame_height_stat * ctx->frame_width) - 100);
	if (ratio_delta <= 5)
		pr_debug("fid %d delta %d prv size [%d %d] cap size [%d %d]\n",
				ctx->fid, ratio_delta, frame_width_stat,
				frame_height_stat, ctx->frame_width,
				ctx->frame_height);
	else {
		pr_debug("fid %d pre size [%d %d] match cap size [%d %d] with cap size\n",
			ctx->fid, frame_width_stat, frame_height_stat,
			ctx->frame_width, ctx->frame_height);
		map->bypass = 1;
		return 0;
	}

	pr_debug("tile_num_x[%d], tile_num_y[%d], tile_width[%d], tile_height[%d], \
		frame_width_stat[%d], frame_height_stat[%d], \
		frame_width_map[%d], frame_height_map[%d] ALIGNMENT[%d]\n",
		mnum.tile_num_x, mnum.tile_num_y,
		ts.tile_width, ts.tile_height,
		frame_width_stat, frame_height_stat,
		frame_width_map, frame_height_map,
		ISP_LTM_TILE_W_ALIGNMENT);

	/*
	 * frame_width_map/frame_width_stat should be
	 * equal to frame_height_map/frame_height_stat
	 */
	if (ISP_LTM_TILE_W_ALIGNMENT == 4) {
		if (frame_width_stat != 0 && frame_height_stat != 0) {
			ratio_w = ((frame_width_map << 8) + (frame_width_stat / 2)) / frame_width_stat;
			ratio_h = ((frame_height_map << 8) + (frame_height_stat / 2)) / frame_height_stat;
		}
		tm.tile_width = ((ratio_w * ts.tile_width) >> 10) << 2;
		tm.tile_height = ((ratio_h * ts.tile_height) >> 9) << 1;
	} else {
		if (frame_width_stat != 0 && frame_height_stat != 0) {
			ratio_w = ((frame_width_map << 8) + (frame_width_stat / 2)) / frame_width_stat;
			ratio_h = ((frame_height_map << 8) + (frame_height_stat / 2)) / frame_height_stat;
		}
		tm.tile_width = ((ratio_w * ts.tile_width) >> 9) << 1;
		tm.tile_height = ((ratio_h * ts.tile_height) >> 9) << 1;
	}

       /*down search for proper tile size to ensure crop >= 0*/
	while(frame_width_map < (tm.tile_width * mnum.tile_num_x)) {
		if (ISP_LTM_TILE_W_ALIGNMENT == 4)
			tm.tile_width -= 4;
		else
			tm.tile_width -= 2;
	}

	while(frame_height_map < (tm.tile_height * mnum.tile_num_y)) {
		tm.tile_height -= 2;
	}

	crop_cols_curr = frame_width_map - tm.tile_width * mnum.tile_num_x;
	crop_rows_curr = frame_height_map - tm.tile_height * mnum.tile_num_y;
	crop_up_curr = crop_rows_curr >> 1;
	crop_down_curr = crop_rows_curr >> 1;
	crop_left_curr = crop_cols_curr >> 1;
	crop_right_curr = crop_cols_curr >> 1;

	/* update parameters */
	param->cropUp = crop_up_curr;
	param->cropDown = crop_down_curr;
	param->cropLeft = crop_left_curr;
	param->cropRight = crop_right_curr;
	param->cropCols = crop_cols_curr;
	param->cropRows = crop_rows_curr;

	param->tile_num_x = mnum.tile_num_x;
	param->tile_num_y = mnum.tile_num_y;
	param->tile_width = tm.tile_width;
	param->tile_height = tm.tile_height;
	param->frame_width = frame_width_map;
	param->frame_height = frame_height_map;
	param->tile_size = tm.tile_width * tm.tile_height;

	slice_info[0] = 0;
	slice_info[1] = 0;
	slice_info[2] = frame_width_map - 1;
	slice_info[3] = frame_height_map - 1;

	ispltm_rgb_map_dump_data_rtl(param, slice_info, prtl);
	/*
	 * burst8_en : 0 ~ burst8; 1 ~ burst16
	 */
	map->burst8_en = 0;
	/*
	 * map auto bypass if hist error happen
	 */
	map->hist_error_en = 0;
	/*
	 * wait_en & wait_line
	 * fetch data raw, rgb: set 0
	 * fetch data yuv     : set 1
	 */
	map->fetch_wait_en = 0;
	map->fetch_wait_line = 0;

	map->tile_width = tm.tile_width;
	map->tile_height = tm.tile_height;
	map->tile_x_num = prtl->tile_x_num_rtl;
	map->tile_y_num = prtl->tile_y_num_rtl;
	map->tile_size_pro = tm.tile_width * tm.tile_height;
	map->tile_start_x = prtl->tile_start_x_rtl;
	map->tile_left_flag = prtl->tile_left_flag_rtl;
	map->tile_start_y = prtl->tile_start_y_rtl;
	map->tile_right_flag = prtl->tile_right_flag_rtl;
	map->hist_pitch = mnum.tile_num_x - 1;
	map->mem_init_addr = ctx->ltm_frame[ctx->cur_mapbuf_id].buf.iova[CAM_BUF_IOMMUDEV_ISP];

	pr_debug("tile_width[%d], tile_height[%d], tile_x_num[%d], tile_y_num[%d]\n",
		map->tile_width, map->tile_height, map->tile_x_num, map->tile_y_num);
	pr_debug("tile_size_pro[%d], tile_start_x[%d], tile_left_flag[%d]\n",
		map->tile_size_pro, map->tile_start_x, map->tile_left_flag);
	pr_debug("tile_start_y[%d], tile_right_flag[%d], hist_pitch[%d]\n",
		map->tile_start_y, map->tile_right_flag, map->hist_pitch);
	pr_debug("ltm map idx[%d], map addr[0x%lx]\n",
		ctx->fid, map->mem_init_addr);

	return 0;
}

static int ispltm_pipe_proc(void *handle, void *param)
{
	int ret = 0;
	struct isp_ltm_ctx_desc *ctx = NULL;
	struct isp_ltm_info *ltm_info = NULL;
	struct isp_hw_k_blk_func ltm_cfg_func;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr %p\n", handle);
		return -EFAULT;
	}

	ctx = (struct isp_ltm_ctx_desc *)handle;
	ltm_info = (struct isp_ltm_info *)param;

	if (ctx->enable == 0)
		return 0;

	ltm_cfg_func.index = ISP_K_BLK_LTM;
	ctx->hw->isp_ioctl(ctx->hw, ISP_HW_CFG_K_BLK_FUNC_GET, &ltm_cfg_func);
	if (ltm_cfg_func.k_blk_func == NULL)
		return 0;

	pr_debug("type[%d], fid[%d], frame_width[%d], frame_height[%d] bypass %d, static %d, map %d\n",
		ctx->mode, ctx->fid, ctx->frame_width, ctx->frame_height, ctx->bypass, ltm_info->ltm_stat.bypass, ltm_info->ltm_map.bypass);

	switch (ctx->mode) {
	case MODE_LTM_PRE:
		ret = ispltm_histo_config_gen(ctx, &ltm_info->ltm_stat);
		if (ret < 0) {
			pr_err("fail to preview hist config, ctx id %d, fid %d\n", ctx->ctx_id, ctx->fid);
			break;
		}
		ispltm_map_config_gen(ctx, &ltm_info->ltm_map);
		ltm_cfg_func.k_blk_func(ctx);
		break;
	case MODE_LTM_CAP:
		ctx->hists.bypass = 1;
		ret = ispltm_histo_config_gen(ctx, &ltm_info->ltm_stat);
		if (ret < 0) {
			pr_err("fail to capture hist config fail, maybe none-zsl, ctx id %d, fid %d\n", ctx->ctx_id, ctx->fid);
			break;
		}
		ispltm_map_config_gen(ctx, &ltm_info->ltm_map);
		ltm_cfg_func.k_blk_func(ctx);
		break;
	case MODE_LTM_OFF:
	default:
		ctx->bypass = 1;
		ltm_cfg_func.k_blk_func(ctx);
		break;
	}

	return ret;
}

static int ispltm_cfg_param(void *handle,
		enum isp_ltm_cfg_cmd cmd, void *param)
{
	int ret = 0;
	struct img_trim *crop = NULL;
	struct cam_frame *pframe = NULL;
	struct isp_ltm_ctx_desc *ltm_ctx = NULL;
	struct dcam_isp_k_block *isp_blk_param = NULL;

	if (!handle || !param) {
		pr_err("fail to get valid input ptr\n");
		return -EFAULT;
	}

	ltm_ctx = (struct isp_ltm_ctx_desc *)handle;
	switch (cmd) {
	case ISP_LTM_CFG_EB:
		ltm_ctx->enable = *(uint32_t *)param;
		pr_debug("ctx_id %d, LTM enable %d\n", ltm_ctx->ctx_id, ltm_ctx->enable);
		break;
	case ISP_LTM_CFG_MODE:
		ltm_ctx->mode = *(uint32_t *)param;
		pr_debug("ctx_id %d, LTM mode %d\n", ltm_ctx->ctx_id,ltm_ctx->mode);
		break;
	case ISP_LTM_CFG_FRAME_ID:
		pframe = (struct cam_frame *)param;
		ltm_ctx->fid = pframe->common.fid;
		ltm_ctx->slice_num = pframe->common.slice_info.slice_num;
		ltm_ctx->slice_no = pframe->common.slice_info.slice_no;
		if (!pframe->common.slice_info.slice_num)
			ltm_ctx->slice_num = 1;
		pr_debug("LTM frame id %d, slice num %d\n", ltm_ctx->fid, ltm_ctx->slice_num);
		break;
	case ISP_LTM_CFG_SIZE_INFO:
		crop = (struct img_trim *)param;
		ltm_ctx->frame_width = crop->size_x;
		ltm_ctx->frame_height = crop->size_y;
		pr_debug("LTM frame id %d, crop %d, %d, map %d\n",
			ltm_ctx->fid, ltm_ctx->frame_width,
			ltm_ctx->frame_height, ltm_ctx->map.bypass);
		break;
	case ISP_LTM_CFG_HIST_BYPASS:
		ltm_ctx->hists.bypass = !(*(uint32_t *)param);
		pr_debug("LTM frame id %d, hist bypass %d\n", ltm_ctx->fid, ltm_ctx->hists.bypass);
		break;
	case ISP_LTM_CFG_MAP_BYPASS:
		ltm_ctx->map.bypass = !(*(uint32_t *)param);
		pr_debug("LTM frame id %d, map bypass %d\n", ltm_ctx->fid, ltm_ctx->map.bypass);
		break;
	case ISP_LTM_CFG_MAP_BUF:
		isp_blk_param = (struct dcam_isp_k_block *)param;
		ltm_ctx->cur_mapbuf_id = !ltm_ctx->cur_mapbuf_id;
		if (ltm_ctx->ltm_frame[ltm_ctx->cur_mapbuf_id].buf.addr_k)
			memcpy((void *)ltm_ctx->ltm_frame[ltm_ctx->cur_mapbuf_id].buf.addr_k,
				&isp_blk_param->ltm_map_info[ltm_ctx->slice_no * 8192],
				sizeof(uint16_t) * 8192);
		break;
	default:
		pr_err("fail to get known cmd: %d\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static struct isp_ltm_ctx_desc *ispltm_ctx_init(uint32_t idx, uint32_t cam_id, void *hw)
{
	struct isp_ltm_ctx_desc *ltm_ctx = NULL;

	ltm_ctx = cam_buf_kernel_sys_vzalloc(sizeof(struct isp_ltm_ctx_desc));
	if (!ltm_ctx) {
		pr_err("fail to alloc isp %d ltm ctx\n", idx);
		return NULL;
	}

	ltm_ctx->ctx_id = idx;
	ltm_ctx->cam_id = cam_id;
	ltm_ctx->hw = hw;
	ltm_ctx->ltm_ops.cfg_param = ispltm_cfg_param;
	ltm_ctx->ltm_ops.pipe_proc = ispltm_pipe_proc;

	return ltm_ctx;
}

static void ispltm_ctx_deinit(void *handle)
{
	uint32_t i = 0;
	struct isp_ltm_ctx_desc *ltm_ctx = NULL;

	if (!handle) {
		pr_err("fail to get valid ltm handle\n");
		return;
	}

	ltm_ctx = (struct isp_ltm_ctx_desc *)handle;

	if (ltm_ctx->mode != MODE_LTM_OFF) {
		for (i = 0; i < ISP_LTM_BUF_NUM; i++) {
			if (ltm_ctx->ltm_frame[i].buf.mapping_state & CAM_BUF_MAPPING_ISP) {
				cam_buf_manager_buf_status_cfg(&ltm_ctx->ltm_frame[i].buf, CAM_BUF_STATUS_MOVE_TO_ALLOC, CAM_BUF_IOMMUDEV_ISP);
				cam_buf_free(&ltm_ctx->ltm_frame[i].buf);
			}
		}
	}

	if (ltm_ctx)
		cam_buf_kernel_sys_vfree(ltm_ctx);

	ltm_ctx = NULL;
}

/* * external function interface * */
int isp_ltm_map_slice_config_gen(struct isp_ltm_ctx_desc *ctx,
	struct isp_ltm_rtl_param *prtl, uint32_t *slice_info, void *ltminfo)
{
	struct isp_ltm_hist_param map_param = {0};
	struct isp_ltm_hist_param *param = &map_param;
	struct isp_ltm_tile_num_minus1 mnum = {0};
	struct isp_ltm_tile_size ts = {0};
	struct isp_ltm_tile_size tm = {0};
	struct isp_ltm_info *ltm_info =(struct isp_ltm_info *)ltminfo;

	uint32_t frame_width_stat = 0, frame_height_stat = 0;
	uint32_t frame_width_map = 0, frame_height_map = 0;

	uint32_t ratio_w = 0;
	uint32_t ratio_h = 0;
	uint32_t crop_cols_curr = 0, crop_rows_curr = 0;
	uint32_t crop_up_curr = 0, crop_down_curr = 0;
	uint32_t crop_left_curr = 0, crop_right_curr = 0;

	mnum.tile_num_x = ltm_info->ltm_map.tile_x_num;
	mnum.tile_num_y = ltm_info->ltm_map.tile_y_num;
	ts.tile_width = ltm_info->ltm_map.tile_width;
	ts.tile_height =ltm_info->ltm_map.tile_height;
	frame_width_stat = ltm_info->ltm_map.frame_width;
	frame_height_stat = ltm_info->ltm_map.frame_height;
	frame_width_map = ctx->frame_width;
	frame_height_map = ctx->frame_height;

	if (ctx->mode == MODE_LTM_CAP) {
		pr_debug("tile_num_x[%d], tile_num_y[%d], tile_width[%d], tile_height[%d], \
			frame_width_stat[%d], frame_height_stat[%d], \
			frame_width_map[%d], frame_height_map[%d]\n",
			mnum.tile_num_x, mnum.tile_num_y,
			ts.tile_width, ts.tile_height,
			frame_width_stat, frame_height_stat,
			frame_width_map, frame_height_map);
	}

	/*
	 * frame_width_map/frame_width_stat should be
	 * equal to frame_height_map/frame_height_stat
	 */
	if (ISP_LTM_TILE_W_ALIGNMENT == 4) {
		if (frame_width_stat != 0 && frame_height_stat != 0) {
			ratio_w = ((frame_width_map << 8) + (frame_width_stat / 2)) / frame_width_stat;
			ratio_h = ((frame_height_map << 8) + (frame_height_stat / 2)) / frame_height_stat;
		}

		tm.tile_width = (ratio_w * ts.tile_width) >> 10 << 2;
		tm.tile_height = (ratio_h * ts.tile_height) >> 9 << 1;
	} else {
		if (frame_width_stat != 0 && frame_height_stat != 0) {
			ratio_w = ((frame_width_map << 8) + (frame_width_stat / 2)) / frame_width_stat;
			ratio_h = ((frame_height_map << 8) + (frame_height_stat / 2)) / frame_height_stat;
		}
		tm.tile_width = ((ratio_w * ts.tile_width) >> 9) << 1;
		tm.tile_height = ((ratio_h * ts.tile_height) >> 9) << 1;
	}

	/*down search for proper tile size to ensure crop >= 0*/
	while(frame_width_map < (tm.tile_width * mnum.tile_num_x)) {
		if (ISP_LTM_TILE_W_ALIGNMENT == 4)
			tm.tile_width -= 4;
		else
			tm.tile_width -= 2;
	}

	while(frame_height_map < (tm.tile_height * mnum.tile_num_y)) {
		tm.tile_height -= 2;
	}

	crop_cols_curr = frame_width_map - tm.tile_width * mnum.tile_num_x;
	crop_rows_curr = frame_height_map - tm.tile_height * mnum.tile_num_y;
	crop_up_curr = crop_rows_curr >> 1;
	crop_down_curr = crop_rows_curr >> 1;
	crop_left_curr = crop_cols_curr >> 1;
	crop_right_curr = crop_cols_curr >> 1;

	/* update parameters */
	param->cropUp = crop_up_curr;
	param->cropDown = crop_down_curr;
	param->cropLeft = crop_left_curr;
	param->cropRight = crop_right_curr;
	param->cropCols = crop_cols_curr;
	param->cropRows = crop_rows_curr;

	param->tile_num_x = mnum.tile_num_x;
	param->tile_num_y = mnum.tile_num_y;
	param->tile_width = tm.tile_width;
	param->tile_height = tm.tile_height;
	param->frame_width = frame_width_map;
	param->frame_height = frame_height_map;
	param->tile_size = tm.tile_width * tm.tile_height;

	ispltm_rgb_map_dump_data_rtl(param, slice_info, prtl);

	return 0;
}

void *isp_ltm_rgb_ctx_get(uint32_t idx, enum camera_id cam_id, void *hw)
{
	struct isp_ltm_ctx_desc *ltm_ctx = NULL;

	ltm_ctx = ispltm_ctx_init(idx, cam_id, hw);
	if (!ltm_ctx) {
		pr_err("fail to get invalid ltm_ctx\n");
		return NULL;
	}

	return ltm_ctx;
}

void isp_ltm_rgb_ctx_put(void *ltm_handle)
{
	ispltm_ctx_deinit(ltm_handle);
}
