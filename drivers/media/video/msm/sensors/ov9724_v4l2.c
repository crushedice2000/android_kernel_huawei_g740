/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "msm_sensor.h"
#include "msm.h"

#define SENSOR_NAME "ov9724"
#define PLATFORM_DRIVER_NAME "msm_camera_ov9724"
#define ov9724_obj ov9724_##obj
#define OV9724_CAMERA_ID_GPIO  75

DEFINE_MUTEX(ov9724_mut);
static struct msm_sensor_ctrl_t ov9724_s_ctrl;

static struct msm_camera_i2c_reg_conf ov9724_start_settings[] = {
	{0x0100, 0x01},
};

static struct msm_camera_i2c_reg_conf ov9724_stop_settings[] = {
	{0x0100, 0x00},
};

static struct msm_camera_i2c_reg_conf ov9724_groupon_settings[] = {
	{0x0104, 0x01},
};

static struct msm_camera_i2c_reg_conf ov9724_groupoff_settings[] = {
	{0x0104, 0x00},
};

static struct msm_camera_i2c_reg_conf ov9724_init_settings[] = {
	{0x0103, 0x01},  //start sensor init,reset init
	{0x3210, 0x43},
	{0x3606, 0x75}, 
	{0x3705, 0x67},
	{0x3601, 0x33},
	{0x3607, 0x94}, 
	{0x3608, 0x38},
	{0x0344, 0x00},  //resolution_1280x720
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x04},
	{0x0349, 0xff},
	{0x034a, 0x02},
	{0x034b, 0xcf},
	{0x034c, 0x05},
	{0x034d, 0x00},
	{0x034e, 0x02},
	{0x034f, 0xd0},
	{0x4908, 0x20},
	{0x4909, 0x14},
	{0x3811, 0x10},
	{0x3813, 0x0a},
	{0x0340, 0x02},  //30fps
	{0x0341, 0xf8},
	{0x0342, 0x06},
	{0x0343, 0x28},
	{0x0305, 0x02},
	{0x0307, 0x3c},
	{0x0202, 0x02},
	{0x0203, 0xf0},
	{0x4801, 0x0f},
	{0x4801, 0x8f},
	{0x4814, 0x2b},
	{0x4307, 0x3a},
	{0x5000, 0x06},
	{0x5001, 0x73},
	{0x0205, 0x3f},
	{0x0100, 0x01}, //streaming on
	{0x0101, 0x02},  //flip on,mirror off
};
static struct msm_camera_i2c_reg_conf ov9724_init_settings_mirror[] = {
	{0x0103, 0x01},  //start sensor init,reset init
	{0x3210, 0x43},
	{0x3606, 0x75}, 
	{0x3705, 0x67},
	{0x3601, 0x33},
	{0x3607, 0x94}, 
	{0x3608, 0x38},
	{0x0344, 0x00},  //resolution_1280x720
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x04},
	{0x0349, 0xff},
	{0x034a, 0x02},
	{0x034b, 0xcf},
	{0x034c, 0x05},
	{0x034d, 0x00},
	{0x034e, 0x02},
	{0x034f, 0xd0},
	{0x4908, 0x20},
	{0x4909, 0x14},
	{0x3811, 0x10},
	{0x3813, 0x0a},
	{0x0340, 0x02},  //30fps
	{0x0341, 0xf8},
	{0x0342, 0x06},
	{0x0343, 0x28},
	{0x0305, 0x02},
	{0x0307, 0x3c},
	{0x0202, 0x02},
	{0x0203, 0xf0},
	{0x4801, 0x0f},
	{0x4801, 0x8f},
	{0x4814, 0x2b},
	{0x4307, 0x3a},
	{0x5000, 0x06},
	{0x5001, 0x73},
	{0x0205, 0x3f},
	{0x0100, 0x01}, //streaming on
	{0x0101, 0x01},  //flip off,mirror on
};
static struct v4l2_subdev_info ov9724_subdev_info[] = {
	{
	.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
	.colorspace = V4L2_COLORSPACE_JPEG,
	.fmt    = 1,
	.order    = 0,
	},
	/* more can be supported, to be added later */
};
static struct msm_camera_i2c_conf_array ov9724_init_conf[] = {
	{&ov9724_init_settings[0],
	ARRAY_SIZE(ov9724_init_settings), 0, MSM_CAMERA_I2C_BYTE_DATA},
};
static struct msm_camera_i2c_conf_array ov9724_init_conf_mirror[] = {
    {&ov9724_init_settings_mirror[0],
    ARRAY_SIZE(ov9724_init_settings_mirror), 0, MSM_CAMERA_I2C_BYTE_DATA},
};
static struct msm_camera_i2c_conf_array ov9724_confs[] = {
	{NULL,0, 0, MSM_CAMERA_I2C_BYTE_DATA},
	{NULL,0, 0, MSM_CAMERA_I2C_BYTE_DATA},
};

static struct msm_sensor_output_info_t ov9724_dimensions[] = {	
	{
		.x_output = 0x500,/*1280*/
		.y_output = 0x2D0,/*720*/
		.line_length_pclk = 0x628,/*hts:1576*/
		.frame_length_lines = 0x2F8,/*vts:760*/
		.vt_pixel_clk =36000000,/*hts*vts*framerate*/
		.op_pixel_clk = 36000000,/*mipi data rate / bpp*/
		.binning_factor = 0, 
	},
	{
		.x_output = 0x500,/*1280*/
		.y_output = 0x2D0,/*720*/
		.line_length_pclk = 0x628,/*hts:1576*/
		.frame_length_lines = 0x2F8,/*vts:760*/
		.vt_pixel_clk =36000000,/*hts*vts*framerate*/
		.op_pixel_clk = 36000000,/*mipi data rate / bpp*/
		.binning_factor = 0,
	},
};

static struct msm_sensor_output_reg_addr_t ov9724_reg_addr = {
	.x_output = 0x034C,
	.y_output = 0x034E,
	.line_length_pclk = 0x0342,
	.frame_length_lines = 0x0340,
};

static struct msm_sensor_id_info_t ov9724_id_info = {
	.sensor_id_reg_addr = 0x300A,
	.sensor_id = 0x9724,
};

static struct msm_sensor_exp_gain_info_t ov9724_exp_gain_info = {
	.coarse_int_time_addr = 0x0202,
	.global_gain_addr = 0x0205,
	.vert_offset = 5,
};
static int32_t  ov9724_write_exp_gain(struct msm_sensor_ctrl_t *s_ctrl,
						uint16_t gain, uint32_t line)
{
    uint32_t fl_lines;
	uint8_t offset;
	fl_lines = s_ctrl->curr_frame_length_lines;
	fl_lines = (fl_lines * s_ctrl->fps_divider) / Q10;
	offset = s_ctrl->sensor_exp_gain_info->vert_offset;
	if (line > (fl_lines - offset))
		fl_lines = line + offset;
	fl_lines += (fl_lines & 0x01);
	s_ctrl->func_tbl->sensor_group_hold_on(s_ctrl);
	msm_camera_i2c_write(s_ctrl->sensor_i2c_client,
		s_ctrl->sensor_output_reg_addr->frame_length_lines, fl_lines,
		MSM_CAMERA_I2C_WORD_DATA);
	msm_camera_i2c_write(s_ctrl->sensor_i2c_client,
		s_ctrl->sensor_exp_gain_info->coarse_int_time_addr, line,
		MSM_CAMERA_I2C_WORD_DATA);
	msm_camera_i2c_write(s_ctrl->sensor_i2c_client,
		s_ctrl->sensor_exp_gain_info->global_gain_addr, gain,
		MSM_CAMERA_I2C_BYTE_DATA);
	s_ctrl->func_tbl->sensor_group_hold_off(s_ctrl);
	msleep(80);
	return 0;
}

int32_t ov9724_sensor_setting(struct msm_sensor_ctrl_t *s_ctrl,
			int update_type, int res)
{	
	int32_t rc = 0;	
	if (update_type == MSM_SENSOR_REG_INIT) 
	{
		s_ctrl->func_tbl->sensor_stop_stream(s_ctrl);
		/*uesd to write special initialization arrays of some sensors*/	
		if(s_ctrl->func_tbl->sensor_write_init_settings)
		{
			s_ctrl->func_tbl->sensor_write_init_settings(s_ctrl);
		}		
		else	
		{
			msm_sensor_write_init_settings(s_ctrl);
		}
	} else if (update_type == MSM_SENSOR_UPDATE_PERIODIC) {
	msm_sensor_write_res_settings(s_ctrl, res);	
	msleep(20);
	v4l2_subdev_notify(&s_ctrl->sensor_v4l2_subdev,	
		NOTIFY_PCLK_CHANGE, &s_ctrl->msm_sensor_reg->
		output_settings[res].op_pixel_clk);
	s_ctrl->func_tbl->sensor_start_stream(s_ctrl);
	msleep(30);
	}
	return rc;
}

static const struct i2c_device_id ov9724_i2c_id[] = {
	{SENSOR_NAME, (kernel_ulong_t)&ov9724_s_ctrl},
	{ }
};

static struct i2c_driver ov9724_i2c_driver = {
	.id_table = ov9724_i2c_id,
	.probe  = msm_sensor_i2c_probe,
	.driver = {
	.name = SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client ov9724_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static int __init msm_sensor_init_module(void)
{
	return i2c_add_driver(&ov9724_i2c_driver);
}

static struct v4l2_subdev_core_ops ov9724_subdev_core_ops = {
	.ioctl = msm_sensor_subdev_ioctl,
	.s_power = msm_sensor_power,
};

static struct v4l2_subdev_video_ops ov9724_subdev_video_ops = {
	.enum_mbus_fmt = msm_sensor_v4l2_enum_fmt,
};

static struct v4l2_subdev_ops ov9724_subdev_ops = {
	.core = &ov9724_subdev_core_ops,
	.video  = &ov9724_subdev_video_ops,
};
void ov9724_customize_sensor_config(struct msm_sensor_ctrl_t * s_ctrl)
{
    char board_name[MAX_SENSOR_NAME] = {0};

    if(s_ctrl && s_ctrl->msm_sensor_reg)
    {
        get_camera_sensor_name(board_name, MAX_SENSOR_NAME);

        if (!strncmp(board_name, "G718", MAX_SENSOR_NAME) && s_ctrl)
        {
            s_ctrl->msm_sensor_reg->init_settings = ov9724_init_conf_mirror;
        }   
        else
        {
            s_ctrl->msm_sensor_reg->init_settings = ov9724_init_conf;
        }
    }
}
static struct msm_sensor_fn_t ov9724_func_tbl = {
	.sensor_start_stream = msm_sensor_start_stream,
	.sensor_stop_stream = msm_sensor_stop_stream,
	.sensor_group_hold_on = msm_sensor_group_hold_on,
	.sensor_group_hold_off = msm_sensor_group_hold_off,
	.sensor_set_fps = msm_sensor_set_fps,
	.sensor_write_exp_gain = ov9724_write_exp_gain,
	.sensor_write_snapshot_exp_gain = ov9724_write_exp_gain,
	.sensor_setting =ov9724_sensor_setting,
	.sensor_csi_setting = msm_sensor_setting1,
	.sensor_set_sensor_mode = msm_sensor_set_sensor_mode,
	.sensor_mode_init = msm_sensor_mode_init,
	.sensor_get_output_info = msm_sensor_get_output_info,
	.sensor_config = msm_sensor_config,
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_get_csi_params = msm_sensor_get_csi_params,
    .sensor_customize_sensor_config = ov9724_customize_sensor_config,
};

static struct msm_sensor_reg_t ov9724_regs = {
	.default_data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.start_stream_conf =ov9724_start_settings,
	.start_stream_conf_size = ARRAY_SIZE(ov9724_start_settings),
	.stop_stream_conf = ov9724_stop_settings,
	.stop_stream_conf_size = ARRAY_SIZE(ov9724_stop_settings),
	.group_hold_on_conf = ov9724_groupon_settings,
	.group_hold_on_conf_size = ARRAY_SIZE(ov9724_groupon_settings),
	.group_hold_off_conf =ov9724_groupoff_settings,
	.group_hold_off_conf_size =
		ARRAY_SIZE(ov9724_groupoff_settings),
	.init_settings = &ov9724_init_conf[0],
	.init_size = ARRAY_SIZE(ov9724_init_conf),
	.mode_settings = &ov9724_confs[0],
	.output_settings = &ov9724_dimensions[0],
	.num_conf = ARRAY_SIZE(ov9724_confs),
};

static struct msm_sensor_ctrl_t ov9724_s_ctrl = {
	.msm_sensor_reg = &ov9724_regs,
	.sensor_i2c_client = &ov9724_sensor_i2c_client,
	.sensor_i2c_addr = 0x6c,
	.sensor_output_reg_addr = &ov9724_reg_addr,
	.sensor_id_info = &ov9724_id_info,
	.sensor_exp_gain_info = &ov9724_exp_gain_info,
	.cam_mode = MSM_SENSOR_MODE_INVALID,
	.msm_sensor_mutex = &ov9724_mut,
	.sensor_i2c_driver = &ov9724_i2c_driver,
	.sensor_v4l2_subdev_info = ov9724_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(ov9724_subdev_info),
	.sensor_v4l2_subdev_ops = &ov9724_subdev_ops,
	.func_tbl = &ov9724_func_tbl,
	.clk_rate = MSM_SENSOR_MCLK_24HZ,
	.sensor_name = "23060126FF-OV-F",
};

module_init(msm_sensor_init_module);
MODULE_DESCRIPTION("OV 1MP Bayer sensor driver");
MODULE_LICENSE("GPL v2");
