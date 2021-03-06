#include <linux/mfd/pm8xxx/batterydata-lib.h>

static struct single_row_lut fcc_temp = {
	.x		= {-20, 0, 25, 40, 60},
	.y		= {2204, 2210, 2212, 2205, 2195},
	.cols	= 5
};

static struct single_row_lut fcc_sf = {
	.x		= {0},
	.y		= {100},
	.cols	= 1
};

static struct sf_lut rbatt_sf = {
	.rows		= 29,
	.cols		= 5,
	.row_entries		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.sf		= {
				{2306, 473, 100, 67, 57},
				{2302, 473, 100, 67, 57},
				{2095, 487, 102, 69, 58},
				{1989, 498, 108, 72, 60},
				{1890, 523, 114, 77, 62},
				{1873, 540, 120, 81, 65},
				{1881, 537, 126, 83, 67},
				{1908, 553, 137, 89, 70},
				{1938, 551, 144, 93, 73},
				{1983, 560, 118, 83, 63},
				{2036, 584, 108, 71, 60},
				{2097, 620, 112, 74, 62},
				{2174, 674, 117, 79, 65},
				{2253, 739, 124, 84, 68},
				{2358, 806, 130, 82, 67},
				{2501, 877, 134, 77, 63},
				{2698, 959, 136, 77, 62},
				{2981, 1069, 140, 78, 63},
				{3242, 1186, 154, 85, 65},
				{3362, 1237, 161, 87, 65},
				{3499, 1296, 167, 88, 65},
				{3696, 1359, 172, 87, 64},
				{4116, 1425, 169, 85, 65},
				{4894, 1483, 171, 86, 67},
				{5946, 1552, 180, 91, 70},
				{7404, 1652, 194, 104, 86},
				{9650, 1773, 237, 139, 113},
				{14036, 1975, 334, 185, 142},
				{45411, 16610, 638, 571, 4968}
	}
};

static struct pc_temp_ocv_lut pc_temp_ocv = {
	.rows		= 29,
	.cols		= 5,
	.temp		= {-20, 0, 25, 40, 60},
	.percent	= {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
	.ocv		= {
				{4333, 4330, 4325, 4322, 4315},
				{4234, 4251, 4258, 4257, 4254},
				{4162, 4188, 4202, 4202, 4199},
				{4104, 4132, 4148, 4148, 4146},
				{4031, 4081, 4097, 4098, 4095},
				{3974, 4027, 4050, 4050, 4047},
				{3929, 3974, 4000, 4003, 4002},
				{3890, 3930, 3961, 3964, 3962},
				{3859, 3885, 3921, 3924, 3923},
				{3835, 3849, 3871, 3875, 3873},
				{3815, 3824, 3838, 3840, 3841},
				{3798, 3806, 3815, 3817, 3818},
				{3782, 3792, 3796, 3799, 3798},
				{3767, 3781, 3781, 3783, 3781},
				{3753, 3769, 3769, 3766, 3761},
				{3736, 3755, 3756, 3747, 3734},
				{3716, 3731, 3731, 3722, 3709},
				{3689, 3703, 3699, 3692, 3680},
				{3656, 3681, 3680, 3674, 3663},
				{3646, 3677, 3678, 3672, 3661},
				{3633, 3671, 3675, 3669, 3658},
				{3618, 3662, 3668, 3662, 3649},
				{3599, 3646, 3649, 3640, 3620},
				{3573, 3615, 3609, 3599, 3578},
				{3538, 3567, 3555, 3547, 3525},
				{3487, 3501, 3487, 3482, 3462},
				{3413, 3407, 3401, 3401, 3382},
				{3288, 3260, 3286, 3281, 3254},
				{3000, 3000, 3000, 3000, 3000}
	}
};

struct bms_battery_data Huawei_HB505076RBC_FMT_data = {
	.fcc				= 2150,
	.fcc_temp_lut			= &fcc_temp,
	.fcc_sf_lut				= &fcc_sf,
	.pc_temp_ocv_lut		= &pc_temp_ocv,
	.rbatt_sf_lut			= &rbatt_sf,
	.default_rbatt_mohm	= 149
};
