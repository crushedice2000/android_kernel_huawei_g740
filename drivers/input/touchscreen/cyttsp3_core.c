/*
 * Core Source for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) touchscreen drivers.
 * For use with Cypress Txx2xx and Txx3xx parts.
 * Supported parts include:
 * CY8CTST242
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009-2012 Cypress Semiconductor, Inc.
 * Copyright (C) 2010-2011 Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contact Cypress Semiconductor at www.cypress.com <kev@cypress.com>
 *
 */

#include "cyttsp3_core.h"

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/version.h>	/* Required for kernel version checking */
#include <linux/firmware.h>	/* This enables firmware class loader code */

#ifdef CONFIG_HUAWEI_KERNEL /*add by huawei*/
#include <linux/touch_platform_config.h>
#include <linux/hw_tp_config.h>
#include <hsad/config_interface.h>
#endif /*add by huawei*/
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
#include <linux/hw_dev_dec.h>
#endif
/* helpers */
#define GET_NUM_TOUCHES(x)          ((x) & 0x0F)
#define IS_LARGE_AREA(x)            (((x) & 0x10) >> 4)
#define IS_BAD_PKT(x)               ((x) & 0x20)
#define IS_VALID_APP(x)             ((x) & 0x01)
#define IS_OPERATIONAL_ERR(x)       ((x) & 0x3F)
#define GET_HSTMODE(reg)            ((reg & 0x70) >> 4)
#define IS_BOOTLOADERMODE(reg)      ((reg & 0x10) >> 4)
#define BL_WATCHDOG_DETECT(reg)     (reg & 0x02)
#ifdef CY_USE_GEN2
#define GET_EVNT_IDX(reg)           (reg & 0x03)
#endif /* --CY_USE_GEN2 */
#ifdef CONFIG_HUAWEI_KERNEL
//#define CY_USE_LEVEL_IRQ /*Add by huawei,define if use level_irq*/
//#define CONFIG_TOUCHSCREEN_CYTTSP_KEY /*Add by huawei,define if use cyttsp_key instead of VirtualKey*/
#define HUAWEI_RW_REG 0
#define HUAWEI_READ_TP_ID 0
/*G526 need to restart touch IC for ESD problems*/
#define HUAWEI_ESD_NEED_RESTART 1
/*G526 need to recalibrsation after updating FW*/
#define HUAWEI_NEED_RECALIBRATION
#define CYTTSP3_CORE_CAPACITANCE_TEST_SUPPORT
#define CYTTSP3_TEST_MODE 0x40
/* FW upgrade with .bin file */ 
#define CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
#define CY_FW_FILE_NAME "cyttsp3_fw.bin"

#define CY_FW_MAX_FILE_LEN 30
#define CY_FW_FILE_EXT ".bin"
static char fw_file_name[CY_FW_MAX_FILE_LEN] = {0};
#endif
/* maximum number of concurrent tracks */
#ifdef CY_USE_GEN2
#define CY_NUM_TCH_ID               2
#endif /* --CY_USE_GEN2 */
#ifdef CY_USE_GEN3
#define CY_NUM_TCH_ID               4
#endif /* --CY_USE_GEN3 */
/* maximum number of track IDs */
#define CY_NUM_TRK_ID               16

#ifdef CY_USE_WATCHDOG
/*
 * maximum number of touch reports with
 * current touches=0 before performing Driver reset
 */
#define CY_MAX_NTCH                 100
#endif /* --CY_USE_WATCHDOG */

#define CY_NTCH                     0 /* lift off */
#define CY_TCH                      1 /* touch down */
#define CY_SMALL_TOOL_WIDTH         10
#define CY_REG_BASE                 0x00
#define CY_REG_OP_START             0x1B
#define CY_REG_OP_END               0x1E
#define CY_REG_OP_FEATURE           0x1F
#define CY_REG_SI_START             0x17
#define CY_REG_SI_END               0x1F
#define CY_DELAY_DFLT               20 /* ms */
#define CY_DELAY_MAX                (500/CY_DELAY_DFLT) /* half second */
#define CY_HALF_SEC_TMO_MS          500
#define CY_TWO_SEC_TMO_MS           2000  //Add by huawei
#define CY_TEN_SEC_TMO_MS           10000
#define CY_HNDSHK_BIT               0x80
#define CY_HST_MODE_CHANGE_BIT      0x08
#define CY_CA_BIT                   0x01 /* Charger Armor(tm) bit */
#define CY_WAKE_DFLT                99 /* causes wake strobe on INT line
					* in sample board configuration
					* platform_data->hw_recov() function
					*/
/* device mode bits */
#define CY_OPERATE_MODE             0x00
#define CY_SYSINFO_MODE             0x10
/* power mode select bits */
#define CY_SOFT_RESET_MODE          0x01 /* return to Bootloader mode */
#define CY_DEEP_SLEEP_MODE          0x02
/* abs settings */
/* abs value offsets */
#define CY_NUM_ABS_VAL              5 /* number of abs values per setting */
#define CY_SIGNAL_OST               0
#define CY_MIN_OST                  1
#define CY_MAX_OST                  2
#define CY_FUZZ_OST                 3
#define CY_FLAT_OST                 4
/* axis signal offsets */
#define CY_NUM_ABS_SET              5 /* number of abs signal sets */
#define CY_ABS_X_OST                0
#define CY_ABS_Y_OST                1
#define CY_ABS_P_OST                2
#define CY_ABS_W_OST                3
#define CY_ABS_ID_OST               4
#define CY_IGNORE_VALUE             0xFFFF /* mark unused signals as ignore */

#define HI_TRACKID(reg)        ((reg & 0xF0) >> 4)
#define LO_TRACKID(reg)        ((reg & 0x0F) >> 0)

#define CY_BL_NUM_KEYS		8
#define CY_BL_PAGE_SIZE		16
#define CY_BL_NUM_PAGES		5
#define CY_BL_BUSY		0x80
#define CY_BL_READY_NO_APP	0x10
#define CY_BL_READY_APP		0x11
#define CY_BL_RUNNING		0x20
#define CY_BL_NOERR		0x00
#define CY_BL_ENTER_CMD_SIZE	11
#define CY_BL_EXIT_CMD_SIZE	11
#define CY_BL_WR_BLK_CMD_SIZE	79
#define CY_BL_VERS_SIZE		9
#define CY_BL_FW_NAME_SIZE	NAME_MAX
#define CY_MAX_PRBUF_SIZE	PIPE_BUF
#define CY_IRQ_DEASSERT		1
#define CY_IRQ_ASSERT		0

struct cyttsp_vers {
	u8 tts_verh;
	u8 tts_verl;
	u8 app_idh;
	u8 app_idl;
	u8 app_verh;
	u8 app_verl;
	u8 cid[3];
};

enum cyttsp_sett_flags {
	CY_NO_FLAGS = 0x00,
	CY_FLAG_FLIP = 0x08,
	CY_FLAG_INV_X = 0x10,
	CY_FLAG_INV_Y = 0x20,
};

enum cyttsp_powerstate {
	CY_IDLE_STATE = 0,	/* IC cannot be reached */
	CY_READY_STATE,		/* pre-operational; ready to go to ACTIVE */
	CY_ACTIVE_STATE,	/* app is running, IC is scanning */
	CY_LOW_PWR_STATE,	/* not currently used  */
	CY_SLEEP_STATE,		/* app is running, IC is idle */
	CY_BL_STATE,		/* bootloader is running */
	CY_LDR_STATE,		/* loader is running */
	CY_SYSINFO_STATE,	/* switching to sysinfo mode */
	CY_INVALID_STATE	/* always last in the list */
};

static char *cyttsp_powerstate_string[] = {
	/* Order must match enum cyttsp_powerstate above */
	"IDLE",
	"READY",
	"ACTIVE",
	"LOW_PWR",
	"SLEEP",
	"BOOTLOADER",
	"LOADER",
	"SYSINFO",
	"INVALID"
};

enum cyttsp_ic_grpnum {
	CY_IC_GRPNUM_RESERVED = 0,
	CY_IC_GRPNUM_OP_TAG,	/* Platform Data Operational tagged registers */
	CY_IC_GRPNUM_SI_TAG,	/* Platform Data Sysinfo tagged registers */
	CY_IC_GRPNUM_BL_KEY,	/* Platform Data Bootloader Keys */
	CY_IC_GRPNUM_OP_REG,	/* general Operational registers read/write */
	CY_IC_GRPNUM_SI_REG,	/* general Sysinfo registers read/write */
	CY_IC_GRPNUM_BL_REG,	/* general Bootloader registers read access */
	CY_IC_GRPNUM_NUM	/* always last */
};

/* Touch structure */
struct cyttsp_touch {
	__be16 x;
	__be16 y;
	u8 z;
} __packed;

/* TrueTouch Standard Product Gen3 interface definition */
struct cyttsp_status_regs {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
} __packed;

#ifdef CY_USE_GEN2
struct cyttsp_xydata_g2 {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	struct cyttsp_touch tch1;
	u8 evnt_idx;
	struct cyttsp_touch tch2;
	u8 tt_undef1;
	u8 gest_cnt;
	u8 gest_id;
	u8 tt_undef[11];
	u8 buttons;
	u8 alive;
	u8 tt_undef2;
	u8 act_dist;
	u8 reserved1;
} __packed;
#endif /* --CY_USE_GEN2 */

struct cyttsp_xydata {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	struct cyttsp_touch tch1;
	u8 touch12_id;
	struct cyttsp_touch tch2;
	u8 gest_cnt;
	u8 gest_id;
	struct cyttsp_touch tch3;
	u8 touch34_id;
	struct cyttsp_touch tch4;

	u8 buttons;
	u8 tt_undef;
	u8 alive;
	u8 act_dist;
	u8 feature;
} __packed;

/* TTSP System Information interface definition */
struct cyttsp_sysinfo_data {
	u8 hst_mode;
	u8 mfg_stat;
	u8 mfg_cmd;
	u8 cid[3];
	u8 tt_undef1;
	u8 uid[8];
	u8 bl_verh;
	u8 bl_verl;
	u8 tts_verh;
	u8 tts_verl;
	u8 app_idh;
	u8 app_idl;
	u8 app_verh;
	u8 app_verl;
	u8 tt_undef[4];
	u8 ca_cntrl;
	u8 scn_typ;
	u8 act_intrvl;
	u8 tch_tmout;
	u8 lp_intrvl;
} __packed;

/* TTSP Bootloader Register Map interface definition */
struct cyttsp_bootloader_data {
	u8 bl_file;
	u8 bl_status;
	u8 bl_error;
	u8 blver_hi;
	u8 blver_lo;
	u8 bld_blver_hi;
	u8 bld_blver_lo;
	u8 ttspver_hi;
	u8 ttspver_lo;
	u8 appid_hi;
	u8 appid_lo;
	u8 appver_hi;
	u8 appver_lo;
	u8 cid_0;
	u8 cid_1;
	u8 cid_2;
} __packed;

struct cyttsp_tch {
	struct cyttsp_touch *tch;
	u8 *id;
};

struct cyttsp_trk {
	bool tch;
	int abs[CY_NUM_ABS_SET];
};

struct cyttsp {
	struct device *dev;
	int irq;
	struct input_dev *input;
	struct mutex data_lock;		/* Used to prevent concurrent access */
	struct workqueue_struct *cyttsp_wq;
	struct work_struct cyttsp_resume_startup_work;
	char phys[32];
	const struct bus_type *bus_type;
	const struct cyttsp_touch_platform_data *platform_data;
	struct cyttsp_bus_ops *bus_ops;
	struct cyttsp_xydata xy_data;
	struct cyttsp_bootloader_data bl_data;
	struct cyttsp_sysinfo_data sysinfo_data;
	struct cyttsp_trk prv_trk[CY_NUM_TRK_ID];
	struct cyttsp_tch tch_map[CY_NUM_TCH_ID];
	struct completion bl_int_running;
	struct completion si_int_running;
	struct completion op_int_running;
	struct completion ld_int_running;
	enum cyttsp_powerstate power_state;
	bool irq_enabled;
	bool powered;
	bool was_suspended;
	u16 flags;
#ifdef CY_ENABLE_CA
	bool ca_available;
	bool ca_enabled;
	#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
#ifdef CY_USE_WATCHDOG
	struct work_struct work;
	struct timer_list timer;
	bool watchdog_enabled;
	int ntch_count;
	int prv_tch;
#endif /* --CY_USE_WATCHDOG */
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	char *fwname;
	bool waiting_for_fw;
	int ic_grpnum;
	int ic_grpoffset;
	bool ic_reflash_done;
#endif
#ifdef CY_USE_REG_ACCESS
	u8 rw_regid;
#endif
/*Add by huawei begin*/
	struct work_struct fw_upgrade;
	bool auto_fw_upgrade_done;
	/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
	bool need_force_recalibration;
#endif
/*Add by huawei end*/

};

#ifdef CONFIG_HUAWEI_KERNEL /*add by huawei*/
static int lcd_x = 0;
static int lcd_y = 0;
//static int lcd_all = 0;

#endif /*CONFIG_HUAWEI_KERNEL*/

static struct cyttsp *g_ts = NULL;
static char touch_info[50] = {0};

static ssize_t update_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf);
static ssize_t update_firmware_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
/*chomod, modify the right*/
static struct kobj_attribute update_firmware_attribute = {
	.attr = {.name = "update_firmware", .mode = 0664},
	.show = update_firmware_show,
	.store = update_firmware_store,
};

/*Add by huawei begin*/
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
static int _cyttsp_firmware_class_auto_loader(struct cyttsp *ts,const u8 *data, int size);

static int cyttsp_check_firmware_version_builtin(struct device *dev, const struct firmware *fw);

static void _cyttsp_firmware_cont_builtin(const struct firmware *fw, void *context);

static void _cyttsp_set_auto_fw_upgrade(struct work_struct *fw_upgrade);
#endif
/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
static int _cyttsp_drv_write_regid(struct cyttsp *ts,unsigned long value);
static int _cyttsp_drv_read_reg_data(struct cyttsp *ts);
static int _cyttsp_drv_write_reg_data(struct cyttsp *ts,unsigned long value);
#endif
/*Add by huawei end*/

static int _cyttsp_startup(struct cyttsp *ts);
static int _cyttsp_wakeup(struct cyttsp *ts);
static void _cyttsp_queue_startup(struct cyttsp *ts, bool was_suspended);

static int ttsp_read_block_data(struct cyttsp *ts, u8 addr,
	u8 length, void *buf)
{
	int retval = 0;
	int tries = 0;

	if (buf == NULL || length == 0) {
		pr_err("%s: No data passed - buf=%p length=%d\n",
			__func__, buf, length);
		retval = -EINVAL;
		goto ttsp_read_block_data_exit;
	}

	for (tries = 0, retval = -1;
		(tries < CY_NUM_RETRY) && (retval < 0);
		tries++) {
		retval = ts->bus_ops->read(ts->bus_ops, addr, length, buf);
		if (retval < 0) {
			/*
			 * SPI can require retries based on data content
			 * I2C should not have errors
			 */
			msleep(CY_DELAY_DFLT);
		} else
			break;
	}

	if (retval < 0) {
		pr_err("%s: bus read block data fail r=%d\n",
			__func__, retval);
	}
ttsp_read_block_data_exit:
	return retval;
}

static int ttsp_write_block_data(struct cyttsp *ts, u8 addr,
	u8 length, const void *buf)
{
	int retval = 0;
	int tries = 0;

	if (buf == NULL || length == 0) {
		pr_err("%s: No data passed - buf=%p length=%d\n",
			__func__, buf, length);
		retval = -EINVAL;
		goto ttsp_write_block_data_exit;
	}

	for (tries = 0, retval = -1;
		(tries < CY_NUM_RETRY) && (retval < 0);
		tries++) {
		retval = ts->bus_ops->write(ts->bus_ops, addr, length, buf);
		if (retval < 0) {
			/*
			 * SPI can require retries based on data content
			 * I2C should not have errors
			 */
			msleep(CY_DELAY_DFLT);
		} else
			break;
	}

	if (retval < 0) {
		pr_err("%s: bus write block data fail r=%d\n",
			__func__, retval);
	}

ttsp_write_block_data_exit:
	return retval;
}

static void _cyttsp_change_state(struct cyttsp *ts,
	enum cyttsp_powerstate new_state)
{
	ts->power_state = new_state;
	pr_info("%s: %s\n", __func__,
		ts->power_state < CY_INVALID_STATE ?
		cyttsp_powerstate_string[ts->power_state] :
		"INVALID");
}

static void _cyttsp_dbg_pr_buf(struct cyttsp *ts,
	const u8 *dptr, int size, const char *data_name)
{
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	int i = 0;
	int max = (CY_MAX_PRBUF_SIZE - 1) - sizeof(" truncated...");
	char *pbuf = NULL;

	if (ts->bus_ops->tsdebug >= CY_DBG_LVL_2) {
		pbuf = kzalloc(CY_MAX_PRBUF_SIZE, GFP_KERNEL);
		if (pbuf == NULL) {
			pr_err("%s: buf alloc error\n", __func__);
		} else {
			for (i = 0; i < size && i < max; i++)
				snprintf(pbuf, CY_MAX_PRBUF_SIZE, "%s %02X", pbuf, dptr[i]);
			cyttsp_dbg(ts, CY_DBG_LVL_2,
				"%s:  %s[0..%d]=%s%s\n", __func__,
				data_name, size-1, pbuf,
				size <= max ?
				"" : " truncated...");
			kfree(pbuf);
		}
	}
#endif /* --CONFIG_TOUCHSCREEN_DEBUG */
	return;
}

static int _cyttsp_hndshk(struct cyttsp *ts, u8 hst_mode)
{
	int retval = 0;
	u8 mode = 0;

	mode = hst_mode & CY_HNDSHK_BIT ?
		hst_mode & ~CY_HNDSHK_BIT :
		hst_mode | CY_HNDSHK_BIT;

	retval = ttsp_write_block_data(ts, CY_REG_BASE,
		sizeof(mode), &mode);

	if (retval < 0) {
		pr_err("%s: bus write fail on handshake r=%d\n",
			__func__, retval);
	}

	return retval;
}

static int _cyttsp_load_status_regs(struct cyttsp *ts,
	struct cyttsp_status_regs *status_regs)
{
	int retval = 0;

	memset(status_regs, 0, sizeof(struct cyttsp_status_regs));

	retval = ttsp_read_block_data(ts, CY_REG_BASE,
		sizeof(struct cyttsp_status_regs), status_regs);

	if (retval < 0) {
		pr_err("%s: bus fail reading status regs r=%d\n",
			__func__, retval);
	}

	return retval;
}

static int _cyttsp_wait_ready(struct cyttsp *ts, struct completion *complete,
	u8 *cmd, size_t cmd_size, unsigned long timeout_ms)
{
	unsigned long timeout = 0;
	unsigned long uretval = 0;
	int retval = 0;

	timeout = msecs_to_jiffies(timeout_ms);
	INIT_COMPLETION(*complete);
	if ((cmd != NULL) && (cmd_size != 0)) {
		retval = ttsp_write_block_data(ts, CY_REG_BASE, cmd_size, cmd);
		if (retval < 0) {
			pr_err("%s: bus write fail switch mode r=%d\n",
				__func__, retval);
			_cyttsp_change_state(ts, CY_IDLE_STATE);
			goto _cyttsp_wait_ready_exit;
		}
	}
	mutex_unlock(&ts->data_lock);
	//uretval = wait_for_completion_interruptible_timeout(complete, timeout);//modify by huawei
	uretval = wait_for_completion_timeout(complete, timeout);////add by huawei
	mutex_lock(&ts->data_lock);
	if (uretval == 0) {
		pr_err("%s: Switch Mode Timeout waiting"
			" for ready interrupt - try reading regs\n", __func__);
		/* continue anyway */
		retval = 0;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_2,
		"%s: Switch Mode: ret=%d uretval=%lu timeout=%lu",
		__func__, retval, uretval, timeout);

_cyttsp_wait_ready_exit:
	return retval;
}

static int _cyttsp_load_bl_regs(struct cyttsp *ts)
{
	int retval = 0;

	memset(&(ts->bl_data), 0, sizeof(struct cyttsp_bootloader_data));

	retval = ttsp_read_block_data(ts, CY_REG_BASE,
		sizeof(ts->bl_data), &(ts->bl_data));

	if (retval < 0) {
		pr_err("%s: bus fail reading Bootloader regs r=%d\n",
			__func__, retval);
		/* Calling process determines state change requirement */
		goto cyttsp_load_bl_regs_exit;
	}

	if (IS_BOOTLOADERMODE(ts->bl_data.bl_status)) {
		cyttsp_dbg(ts, CY_DBG_LVL_1,
			"%s: Bootloader Regs:\n"
			"  file=%02X status=%02X error=%02X\n"
			"  BL Version:          0x%02X%02X\n"
			"  Build BL Version:    0x%02X%02X\n"
			"  TTSP Version:        0x%02X%02X\n"
			"  Application ID:      0x%02X%02X\n"
			"  Application Version: 0x%02X%02X\n"
			"  Custom ID:           0x%02X%02X%02X\n",
			__func__,
			ts->bl_data.bl_file, ts->bl_data.bl_status,
			ts->bl_data.bl_error,
			ts->bl_data.blver_hi, ts->bl_data.blver_lo,
			ts->bl_data.bld_blver_hi, ts->bl_data.bld_blver_lo,
			ts->bl_data.ttspver_hi, ts->bl_data.ttspver_lo,
			ts->bl_data.appid_hi, ts->bl_data.appid_lo,
			ts->bl_data.appver_hi, ts->bl_data.appver_lo,
			ts->bl_data.cid_0, ts->bl_data.cid_1,
			ts->bl_data.cid_2);
	} else {
		cyttsp_dbg(ts, CY_DBG_LVL_1,
			"%s: Not Bootloader mode:\n"
			"  mode=%02X status=%02X error=%02X\n",
			__func__,
			ts->bl_data.bl_file, ts->bl_data.bl_status,
			ts->bl_data.bl_error);
	}

cyttsp_load_bl_regs_exit:
	return retval;
}

static int _cyttsp_exit_bl_mode(struct cyttsp *ts)
{
	struct cyttsp_status_regs status_regs;
	int retval = 0;
	u8 bl_cmd[] = {0x00, 0xFF, 0xA5,/* BL file 0, cmd seq, exit cmd */
		0, 1, 2, 3, 4, 5, 6, 7};/* Default BL keys */

	memset(&status_regs, 0, sizeof(struct cyttsp_status_regs));
	if (ts->platform_data->sett[CY_IC_GRPNUM_BL_KEY] != NULL) {
		if ((ts->platform_data->sett[CY_IC_GRPNUM_BL_KEY]->data !=
			NULL) && (ts->platform_data->sett[CY_IC_GRPNUM_BL_KEY]
			->size <= CY_BL_NUM_KEYS)) {
			memcpy(&bl_cmd[3],
				ts->platform_data->sett
				[CY_IC_GRPNUM_BL_KEY]->data,
				ts->platform_data->sett
				[CY_IC_GRPNUM_BL_KEY]->size);
		} else {
			pr_err("%s: Too many platform bootloader keys--%s.\n",
				__func__, "using defaults instead");
		}
	} else {
		pr_err("%s: Missing platform bootloader keys--%s.\n",
			__func__, "using defaults instead");
	}

	_cyttsp_dbg_pr_buf(ts, bl_cmd, sizeof(bl_cmd), "bl_cmd");

	/* send exit command; wait for host mode interrupt */
	/*G526 need less timeout to restart touch IC for ESD problems*/
	retval = _cyttsp_wait_ready(ts, &ts->bl_int_running,
		bl_cmd, sizeof(bl_cmd), CY_TWO_SEC_TMO_MS);
	if (retval < 0) {
		pr_err("%s: fail wait ready r=%d\n", __func__, retval);
		goto _cyttsp_exit_bl_mode_exit;
	}

	retval = _cyttsp_load_status_regs(ts, &status_regs);
	if (retval < 0) {
		pr_err("%s: Fail read status regs r=%d\n",
			__func__, retval);
		goto _cyttsp_exit_bl_mode_exit;
	}

	retval = _cyttsp_hndshk(ts, status_regs.hst_mode);
	if (retval < 0) {
		pr_err("%s: Fail write handshake r=%d\n",
			__func__, retval);
		/* continue anyway */
		retval = 0;
	}

	if (IS_BOOTLOADERMODE(status_regs.tt_mode)) {
		pr_err("%s: Fail exit bootloader mode r=%d\n",
			__func__, retval);
		retval = -EIO;
	} else if (GET_HSTMODE(status_regs.hst_mode) !=
		GET_HSTMODE(CY_OPERATE_MODE)) {
		pr_err("%s: Fail enter op mode=0x%02X\n",
			__func__, status_regs.hst_mode);
		retval = -EIO;
	} else
		_cyttsp_change_state(ts, CY_READY_STATE);

_cyttsp_exit_bl_mode_exit:
	return retval;
}

static int _cyttsp_set_operational_mode(struct cyttsp *ts)
{
	int retval = 0;
	u8 mode = CY_OPERATE_MODE + CY_HST_MODE_CHANGE_BIT;
	u8 *mode_ptr = &mode;
	size_t mode_size = sizeof(mode);

	/* switch to operational mode */
	_cyttsp_change_state(ts, CY_READY_STATE);
_cyttsp_set_operational_mode_retry:
	retval = _cyttsp_wait_ready(ts, &ts->op_int_running,
		mode_ptr, mode_size, CY_TWO_SEC_TMO_MS);//modify by huawei

	if (retval < 0) {
		pr_err("%s: fail wait ready r=%d\n", __func__, retval);
		goto _cyttsp_set_operational_mode_exit;
	}

	retval = ttsp_read_block_data(ts, CY_REG_BASE,
		sizeof(struct cyttsp_xydata), &(ts->xy_data));
	if (retval < 0) {
		pr_err("%s: Fail read status regs r=%d\n",
			__func__, retval);
		goto _cyttsp_set_operational_mode_exit;
	}

	retval = _cyttsp_hndshk(ts, ts->xy_data.hst_mode);
	if (retval < 0) {
		pr_err("%s: Fail write handshake r=%d\n",
			__func__, retval);
		/* continue anyway */
		retval = 0;
	}

	/*
	 * The CY_HST_MODE_CHANGE_BIT is set by the host when changing mode.
	 * When the mode change is complete, the device will pulse the
	 * interrupt line and clear this bit to signify that the
	 * mode change is complete.
	 */
	if (ts->xy_data.hst_mode & CY_HST_MODE_CHANGE_BIT) {
		/*
		 * due to race between last sysinfo heartbeat and
		 * the mode change interrupt, need to try waiting
		 * again without repeating the write operation
		 */
		if (mode_ptr != NULL) {
			mode_ptr = NULL;
			mode_size = 0;
			goto _cyttsp_set_operational_mode_retry;
		} else {
			pr_err("%s: Fail change mode=0x%02X\n",
				__func__, ts->xy_data.hst_mode);
			goto _cyttsp_set_operational_mode_exit;
		}
	}

	if ((GET_HSTMODE(ts->xy_data.hst_mode) !=
		GET_HSTMODE(CY_OPERATE_MODE)) &&
		!IS_BOOTLOADERMODE(ts->xy_data.tt_mode)) {
		pr_err("%s: Fail enter op mode"
			" hst_mode=0x%02X tt_mode=0x%02X\n",
			__func__, ts->xy_data.hst_mode, ts->xy_data.tt_mode);
		retval = -EIO;
		goto _cyttsp_set_operational_mode_exit;
	} else {
		cyttsp_dbg(ts, CY_DBG_LVL_2,
			"%s: Enter Operational mode"
			" hst_mode=0x%02X tt_mode=0x%02X\n",
			__func__, ts->xy_data.hst_mode, ts->xy_data.tt_mode);
	}

#ifdef CY_ENABLE_CA
	ts->ca_available = ts->xy_data.feature & CY_CA_BIT;
	if (ts->ca_available && ts->ca_enabled) {
		/*
		 * this condition occurs if the CA was enabled and then a
		 * driver restart occurred
		 */
		mode = CY_CA_BIT;
		retval = ttsp_write_block_data(ts, CY_REG_BASE, mode, &mode);
		if (retval < 0) {
			pr_err("%s: bus write fail setting CA=%02X r=%d\n",
				__func__, mode, retval);
			/* continue anyway */
			retval = 0;
		}
	}
	pr_info("%s: CA feature is %s=%02X and is %s\n", __func__,
		ts->ca_available ? "available" :
		"not available", ts->xy_data.feature,
		ts->ca_enabled ? "enabled" : "disabled");
#endif

_cyttsp_set_operational_mode_exit:
	if (retval < 0)
		_cyttsp_queue_startup(ts, false);
	return retval;
}

static int _cyttsp_set_sysinfo_mode(struct cyttsp *ts)
{
	int retval = 0;
	u8 mode = CY_SYSINFO_MODE + CY_HST_MODE_CHANGE_BIT;
	u8 *mode_ptr = &mode;
	size_t mode_size = sizeof(mode);

	/* switch to sysinfo mode */
	_cyttsp_change_state(ts, CY_SYSINFO_STATE);
_cyttsp_set_sysinfo_mode_retry:
	retval = _cyttsp_wait_ready(ts, &ts->si_int_running,
		mode_ptr, mode_size, CY_TWO_SEC_TMO_MS);//modify by huawei

	if (retval < 0) {
		pr_err("%s: fail wait ready r=%d\n", __func__, retval);
		goto _cyttsp_set_sysinfo_mode_exit;
	}

	/* sysinfo_data is filled by ISR */
	if (ts->sysinfo_data.hst_mode & CY_HST_MODE_CHANGE_BIT) {
		/*
		 * due to race between last bootloader heartbeat and
		 * the mode change interrupt, need to try waiting
		 * again without repeating the write operation
		 */
		if (mode_ptr != NULL) {
			mode_ptr = NULL;
			mode_size = 0;
			goto _cyttsp_set_sysinfo_mode_retry;
		} else {
			pr_err("%s: Fail change mode=0x%02X\n",
				__func__, ts->sysinfo_data.hst_mode);
			goto _cyttsp_set_sysinfo_mode_exit;
		}
	}

	if (GET_HSTMODE(ts->sysinfo_data.hst_mode) !=
		GET_HSTMODE(CY_SYSINFO_MODE)) {
		pr_err("%s: Fail enter Sysinfo mode hst_mode=0x%02X\n",
			__func__, ts->sysinfo_data.hst_mode);
		retval = -EIO;
	} else {
		cyttsp_dbg(ts, CY_DBG_LVL_2,
			"%s: Enter Sysinfo mode hst_mode=0x%02X\n",
			__func__, ts->sysinfo_data.hst_mode);
	}

_cyttsp_set_sysinfo_mode_exit:
	return retval;
}

static int _cyttsp_wait_sysinfo_heartbeat(struct cyttsp *ts)
{
	unsigned long timeout = 0;
	unsigned long uretval = 0;
	int retval = 0;

	timeout = msecs_to_jiffies(CY_DELAY_DFLT * CY_DELAY_MAX);
	INIT_COMPLETION(ts->si_int_running);
	//uretval = wait_for_completion_interruptible_timeout(//modify by huawei
	uretval = wait_for_completion_timeout(//modify by huawei
		&ts->si_int_running, timeout);
	if (uretval == 0) {
		pr_err("%s: Timeout waiting for Sysinfo heartbeat"
			" uretval=%lu timeout=%lu\n",
			__func__, uretval, timeout);
		retval = -ETIME;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_2,
		"%s: Wait sysinfo irq:"
		" r=%d uretval=%lu timeout=%lu\n",
		__func__, retval, uretval, timeout);

	return retval;
}

static int _cyttsp_put_store_data(struct cyttsp *ts,
	int grpnum, size_t offset, size_t num_write, const u8 *ic_buf)
{
	int retval = 0;

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: Write group=%d bytes at offset=%d\n",
		__func__, grpnum, offset);
	_cyttsp_dbg_pr_buf(ts, ic_buf, num_write, "Group_data");

	retval = ttsp_write_block_data(ts, offset, num_write, ic_buf);
	if (retval < 0) {
		pr_err("%s: Err write numbytes=%d r=%d\n",
			__func__, num_write, retval);
	}

	return retval;
}

static int _cyttsp_put_si_data(struct cyttsp *ts,
	int grpnum, size_t offset, size_t num_write, const u8 *ic_buf)
{
	int retval = 0;

	mutex_unlock(&ts->data_lock);
	/*Modify parameter order of the function call*/
	retval = _cyttsp_put_store_data(ts, grpnum, offset, num_write, ic_buf);
	if (retval < 0)
		pr_err("%s: fail put store data r=%d\n", __func__, retval);
	else {
		/* wait for data good */
		retval = _cyttsp_wait_sysinfo_heartbeat(ts);
		if (retval < 0) {
			pr_err("%s: Timeout waiting for sysinfo"
				" heartbeat r=%d\n", __func__, retval);
			/* continue; data should be good after timeout */
		}
	}
	mutex_lock(&ts->data_lock);

	return retval;
}

static int _cyttsp_set_sysinfo_regs(struct cyttsp *ts)
{
	u8 size = 0;
	u8 tag = 0;
	u8 reg_offset = 0;
	u8 data_len = 0;
	int retval = 0;

	if ((ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG] == NULL) ||
		(ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG]->data == NULL)) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Sysinfo data table missing--%s",
			__func__, "IC defaults will be used\n");
		goto _cyttsp_set_sysinfo_regs_exit;
	}
	size = ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG]->size;
	tag = ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG]->tag;
	reg_offset = CY_REG_SI_START + tag;
	data_len = size - tag;  /* Overflows if tag > size; checked below */

	/* Do bounds checking on the data */
	if (reg_offset > CY_REG_SI_END) {
		pr_err("%s: Sysinfo reg data starts out of bounds--%s",
			__func__, "IC defaults will be used\n");
		goto _cyttsp_set_sysinfo_regs_exit;
	} else if (tag > size) { /* overflow if (size - tag) is negative */
		pr_err("%s: Invalid sysinfo data length--%s",
			__func__, "IC defaults will be used\n");
		goto _cyttsp_set_sysinfo_regs_exit;
	} else if ((reg_offset + data_len - 1) > CY_REG_SI_END) {
		pr_err("%s: Sysinfo reg data overflow--%s",
			__func__, "IC defaults will be used\n");
		goto _cyttsp_set_sysinfo_regs_exit;
	}

	_cyttsp_dbg_pr_buf(ts,
		&(ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG]->data[tag]),
		data_len, "write sysinfo_regs");

	retval = _cyttsp_put_si_data(ts,
		CY_IC_GRPNUM_SI_TAG, reg_offset, data_len,
		&(ts->platform_data->sett[CY_IC_GRPNUM_SI_TAG]->data[tag]));
	if (retval < 0) {
		pr_err("%s: Error writing Sysinfo data r=%d\n",
			__func__, retval);
	}

_cyttsp_set_sysinfo_regs_exit:
	return retval;
}

static int _cyttsp_hard_reset(struct cyttsp *ts)
{
	bool do_soft_reset = false;
	u8 mode = 0;
	int retval = 0;

#ifdef CY_NO_BOOTLOADER
	_cyttsp_change_state(ts, CY_READY_STATE);
#else
	_cyttsp_change_state(ts, CY_BL_STATE);
#endif

	if (ts->platform_data->hw_reset != NULL) {
		retval = ts->platform_data->hw_reset();
		if (retval < 0) {
			if (retval == -ENOSYS) {
				cyttsp_dbg(ts, CY_DBG_LVL_2,
					"%s: hw_reset not implemented\n",
					__func__);
			} else {
				pr_err("%s: fail on hard reset r=%d\n",
					__func__, retval);
			}
			do_soft_reset = true;
		} else {
			cyttsp_dbg(ts, CY_DBG_LVL_2,
				"%s: hard reset ok\n", __func__);
		}
	} else {
		cyttsp_dbg(ts, CY_DBG_LVL_2,
			"%s: no hardware reset function r=%d\n",
			__func__, retval);
		do_soft_reset = true;
	}

	if (do_soft_reset) {
		mode = CY_SOFT_RESET_MODE;
		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			sizeof(mode), &mode);
		if (retval < 0) {
			pr_err("%s: Fail writing soft reset command r=%d\n",
				__func__, retval);
			goto _cyttsp_hard_reset_exit;
		} else {
			cyttsp_dbg(ts, CY_DBG_LVL_2,
				"%s: soft reset ok\n", __func__);
		}
	}

	/* wait for interrupt to set ready completion */
#ifndef CY_NO_BOOTLOADER
	retval = _cyttsp_wait_ready(ts, &ts->bl_int_running,
		NULL, 0, CY_HALF_SEC_TMO_MS);
#endif
	if (retval < 0)
		pr_err("%s: fail wait ready r=%d\n", __func__, retval);

_cyttsp_hard_reset_exit:
	return retval;
}

static int _cyttsp_set_operational_regs(struct cyttsp *ts)
{
	int retval = 0;
	u8 size = 0;
	u8 tag = 0;
	u8 reg_offset = 0;
	u8 data_len = 0;

	if ((ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG] == NULL) ||
		(ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG]->data == NULL)) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Op data table missing--%s",
			__func__, "IC defaults will be used\n");
		goto _cyttsp_set_op_regs_exit;
	}
	size = ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG]->size;
	tag = ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG]->tag;
	reg_offset = CY_REG_OP_START + tag;
	data_len = size - tag;  /* Overflows if tag > size; checked below */

	/* Do bounds checking on the data */
	if (reg_offset > CY_REG_OP_END) {
		pr_err("%s: Op reg data starts out of bounds--%s\n",
			__func__, "IC defaults will be used");
		goto _cyttsp_set_op_regs_exit;
	} else if (tag > size) { /* overflow if (size - tag) is negative */
		pr_err("%s: Invalid op data length--%s\n",
			__func__, "IC defaults will be used");
		goto _cyttsp_set_op_regs_exit;
	} else if ((reg_offset + data_len - 1) > CY_REG_OP_END) {
		pr_err("%s: Op reg data overflow--%s\n",
			__func__, "IC defaults will be used");
		goto _cyttsp_set_op_regs_exit;
	}

	_cyttsp_dbg_pr_buf(ts,
		&(ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG]->data[tag]),
		data_len, "write op_regs");

	retval = ttsp_write_block_data(ts, reg_offset, data_len,
		&(ts->platform_data->sett[CY_IC_GRPNUM_OP_TAG]->data[tag]));

	if (retval < 0) {
		pr_err("%s: bus write fail on Op Regs r=%d\n",
			__func__, retval);
		_cyttsp_change_state(ts, CY_IDLE_STATE);
	}

_cyttsp_set_op_regs_exit:
	return retval;
}

/* map pointers to touch information to allow loop on get xy_data */
static void _cyttsp_init_tch_map(struct cyttsp *ts)
{
	ts->tch_map[0].tch = &ts->xy_data.tch1;
	ts->tch_map[0].id = &ts->xy_data.touch12_id;
	ts->tch_map[1].tch = &ts->xy_data.tch2;
	ts->tch_map[1].id = &ts->xy_data.touch12_id;
	ts->tch_map[2].tch = &ts->xy_data.tch3;
	ts->tch_map[2].id = &ts->xy_data.touch34_id;
	ts->tch_map[3].tch = &ts->xy_data.tch4;
	ts->tch_map[3].id = &ts->xy_data.touch34_id;
}

static void _cyttsp_flip_inv(struct cyttsp *ts, struct cyttsp_trk *cur_trk)
{
	bool flipped = false;
	int tmp = 0;

	if (ts->flags & CY_FLAG_FLIP) {
		tmp = cur_trk->abs[CY_ABS_X_OST];
		cur_trk->abs[CY_ABS_X_OST] = cur_trk->abs[CY_ABS_Y_OST];
		cur_trk->abs[CY_ABS_Y_OST] = tmp;
		flipped = true;
	}
	if (ts->flags & CY_FLAG_INV_X) {
		if (!flipped) {
			cur_trk->abs[CY_ABS_X_OST] =
				ts->platform_data->frmwrk->abs
				[(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_MAX_OST] -
				cur_trk->abs[CY_ABS_X_OST];
		} else {
			cur_trk->abs[CY_ABS_X_OST] =
				ts->platform_data->frmwrk->abs
				[(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_MAX_OST] -
				cur_trk->abs[CY_ABS_X_OST];
		}
	}
	if (ts->flags & CY_FLAG_INV_Y) {
		if (!flipped) {
			cur_trk->abs[CY_ABS_Y_OST] =
				ts->platform_data->frmwrk->abs
				[(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_MAX_OST] -
				cur_trk->abs[CY_ABS_Y_OST];
		} else {
			cur_trk->abs[CY_ABS_Y_OST] =
				ts->platform_data->frmwrk->abs
				[(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_MAX_OST] -
				cur_trk->abs[CY_ABS_Y_OST];
		}
	}
	return;
}

#ifdef CY_USE_GEN2
static void _cyttsp_get_tracks(struct cyttsp *ts, int cur_tch,
	struct cyttsp_trk *cur_trk)
{
	struct cyttsp_xydata_g2 *g2data =
		(struct cyttsp_xydata_g2 *)&ts->xy_data;

	/* extract xy_data for all currently reported touches */
	switch (cur_tch) {
	case 0:
		break;
	case 1:
		if (!ts->prv_trk[0].tch && !ts->prv_trk[1].tch) {
			cur_trk[0].tch = true;
			cur_trk[0].abs[CY_ABS_X_OST] =
				be16_to_cpu(g2data->tch1.x);
			cur_trk[0].abs[CY_ABS_Y_OST] =
				be16_to_cpu(g2data->tch1.y);
			cur_trk[0].abs[CY_ABS_P_OST] = g2data->tch1.z;
			cur_trk[0].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
			cur_trk[0].abs[CY_ABS_ID_OST] = 0;
		} else if (ts->prv_trk[0].tch && !ts->prv_trk[1].tch) {
			cur_trk[0].tch = true;
			cur_trk[0].abs[CY_ABS_X_OST] =
				be16_to_cpu(g2data->tch1.x);
			cur_trk[0].abs[CY_ABS_Y_OST] =
				be16_to_cpu(g2data->tch1.y);
			cur_trk[0].abs[CY_ABS_P_OST] = g2data->tch1.z;
			cur_trk[0].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
			cur_trk[0].abs[CY_ABS_ID_OST] = 0;
		} else if (!ts->prv_trk[0].tch && ts->prv_trk[1].tch) {
			cur_trk[1].tch = true;
			cur_trk[1].abs[CY_ABS_X_OST] =
				be16_to_cpu(g2data->tch1.x);
			cur_trk[1].abs[CY_ABS_Y_OST] =
				be16_to_cpu(g2data->tch1.y);
			cur_trk[1].abs[CY_ABS_P_OST] = g2data->tch1.z;
			cur_trk[1].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
			cur_trk[1].abs[CY_ABS_ID_OST] = 1;
		} else if (ts->prv_trk[0].tch && ts->prv_trk[1].tch) {
			switch (GET_EVNT_IDX(g2data->evnt_idx)) {
			case 1:	/* 1st tch removed; 2nd tch remains */
				cur_trk[0].tch = false;
				cur_trk[1].tch = true;
				cur_trk[1].abs[CY_ABS_X_OST] =
					be16_to_cpu(g2data->tch1.x);
				cur_trk[1].abs[CY_ABS_Y_OST] =
					be16_to_cpu(g2data->tch1.y);
				cur_trk[1].abs[CY_ABS_P_OST] =
					g2data->tch1.z;
				cur_trk[1].abs[CY_ABS_W_OST] =
					CY_SMALL_TOOL_WIDTH;
				cur_trk[1].abs[CY_ABS_ID_OST] = 1;
				break;
			case 0:	/* tch not removed, 2 tchs not present or
				 * tch removed in 2-finger ghosting mode
				 * -- or implies case 2
				 * -- if previous 2 tchs and not case 1
				 */
			case 2:	/* 2nd tch removed; 1st tch remains */
				cur_trk[1].tch = false;
				cur_trk[0].tch = true;
				cur_trk[0].abs[CY_ABS_X_OST] =
					be16_to_cpu(g2data->tch1.x);
				cur_trk[0].abs[CY_ABS_Y_OST] =
					be16_to_cpu(g2data->tch1.y);
				cur_trk[0].abs[CY_ABS_P_OST] =
					g2data->tch1.z;
				cur_trk[0].abs[CY_ABS_W_OST] =
					CY_SMALL_TOOL_WIDTH;
				cur_trk[0].abs[CY_ABS_ID_OST] = 0;
				break;
			case 3:	/* both tchs removed simultaneously */
			default:
				pr_err("%s: bad cur_tch=%d vs. evnt_idx=%d\n",
					__func__, cur_tch,
					GET_EVNT_IDX(g2data->evnt_idx));
				break;
			}
		}
		break;
	case 2:
		cur_trk[0].tch = true;
		cur_trk[0].abs[CY_ABS_X_OST] = be16_to_cpu(g2data->tch1.x);
		cur_trk[0].abs[CY_ABS_Y_OST] = be16_to_cpu(g2data->tch1.y);
		cur_trk[0].abs[CY_ABS_P_OST] = g2data->tch1.z;
		cur_trk[0].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
		cur_trk[0].abs[CY_ABS_ID_OST] = 0;
		cur_trk[1].tch = true;
		cur_trk[1].abs[CY_ABS_X_OST] = be16_to_cpu(g2data->tch2.x);
		cur_trk[1].abs[CY_ABS_Y_OST] = be16_to_cpu(g2data->tch2.y);
		cur_trk[1].abs[CY_ABS_P_OST] = g2data->tch1.z;	/* 2nd tch uses
								 * 1st tch z */
		cur_trk[1].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
		cur_trk[1].abs[CY_ABS_ID_OST] = 1;
		break;
	default:
		break;
	}
	if (cur_trk[0].tch)
		_cyttsp_flip_inv(ts, &cur_trk[0]);
	if (cur_trk[1].tch)
		_cyttsp_flip_inv(ts, &cur_trk[1]);
}
#endif /* --CY_USE_GEN2 */

#ifdef CY_USE_GEN3
static void _cyttsp_get_tracks(struct cyttsp *ts, int cur_tch,
	struct cyttsp_trk *cur_trk)
{
	u8 id = 0;
	int tch = 0;

	/* extract xy_data for all currently reported touches */
	for (tch = 0; tch < cur_tch; tch++) {
		id = tch & 0x01 ?
			LO_TRACKID(*(ts->tch_map[tch].id)) :
			HI_TRACKID(*(ts->tch_map[tch].id));
		cur_trk[id].tch = CY_TCH;
		cur_trk[id].abs[CY_ABS_X_OST] =
			be16_to_cpu((ts->tch_map[tch].tch)->x);
		cur_trk[id].abs[CY_ABS_Y_OST] =
			be16_to_cpu((ts->tch_map[tch].tch)->y);
		cur_trk[id].abs[CY_ABS_P_OST] =
			(ts->tch_map[tch].tch)->z;
		cur_trk[id].abs[CY_ABS_W_OST] = CY_SMALL_TOOL_WIDTH;
		cur_trk[id].abs[CY_ABS_ID_OST] = id;

		_cyttsp_flip_inv(ts, &cur_trk[id]);
	}
}
#endif /* --CY_USE_GEN3 */

/*Add by huawei , Use cyttsp_key instead of VirtualKey*/
#ifdef CONFIG_TOUCHSCREEN_CYTTSP_KEY

#define TT_KEY_BACK_FLAG	0x01
#define TT_KEY_HOME_FLAG	0x02
#define TT_KEY_MENU_FLAG	0x04
static int cyttsp_key_report(struct cyttsp *ts)
{
	static u8 last;
	u8 data = ts->xy_data.buttons;
	u8 toggled = last ^ data;

	if (toggled & TT_KEY_MENU_FLAG)
		input_report_key(ts->input, KEY_MENU,
			!!(data & TT_KEY_MENU_FLAG));

	if (toggled & TT_KEY_BACK_FLAG)
		input_report_key(ts->input, KEY_BACK,
			!!(data & TT_KEY_BACK_FLAG));

	if (toggled & TT_KEY_HOME_FLAG)
		input_report_key(ts->input, KEY_HOME,
			!!(data & TT_KEY_HOME_FLAG));

	last = data;
	return 0;
}
#endif

/* read xy_data for all current touches */
static int _cyttsp_xy_worker(struct cyttsp *ts)
{
	int retval = 0;
	u8 cur_tch = 0;
	u8 id = 0;
	int t = 0;
	int num_sent = 0;
	int signal = 0;
#ifdef CY_USE_LEVEL_IRQ
//	int tries = 0;
#endif
	struct cyttsp_trk cur_trk[CY_NUM_TRK_ID];

	/*do not process anything in test mode*/
	if((ts->xy_data.hst_mode & CYTTSP3_TEST_MODE) != 0)
	{
		goto _cyttsp_xy_worker_exit;
	}
	/* clear current touch tracking structures */
	memset(cur_trk, 0, sizeof(cur_trk));

	/*
	 * Get event data from CYTTSP device.
	 * The event data includes all data
	 * for all active touches.
	 */
	memset(&ts->xy_data, 0, sizeof(struct cyttsp_xydata));
	retval = ttsp_read_block_data(ts, CY_REG_BASE,
		sizeof(struct cyttsp_xydata), &ts->xy_data);
	if (retval < 0) {
		pr_err("%s: read fail on operational reg r=%d--%s\n",
			__func__, retval, "restart will be scheduled");
		_cyttsp_change_state(ts, CY_IDLE_STATE);
		goto _cyttsp_xy_worker_error_exit;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_1,
		"%s:\n"
		"  hm=%02X tm=%02X ts=%02X\n"
		"  X1=%04X Y1=%04X Z1=%02X ID12=%02X\n"
		"  X2=%04X Y2=%04X Z2=%02X\n"
		"  X3=%04X Y3=%04X Z3=%02X ID34=%02X\n"
		"  X4=%04X Y4=%04X Z4=%02X AD=%02X\n",
		__func__,
		ts->xy_data.hst_mode, ts->xy_data.tt_mode, ts->xy_data.tt_stat,
		be16_to_cpu(ts->xy_data.tch1.x),
		be16_to_cpu(ts->xy_data.tch1.y),
		ts->xy_data.tch1.z, ts->xy_data.touch12_id,
		be16_to_cpu(ts->xy_data.tch2.x),
		be16_to_cpu(ts->xy_data.tch2.y),
		ts->xy_data.tch2.z,
		be16_to_cpu(ts->xy_data.tch3.x),
		be16_to_cpu(ts->xy_data.tch3.y),
		ts->xy_data.tch3.z, ts->xy_data.touch34_id,
		be16_to_cpu(ts->xy_data.tch4.x),
		be16_to_cpu(ts->xy_data.tch4.y),
		ts->xy_data.tch4.z, ts->xy_data.act_dist);

	/* provide flow control handshake */
	retval = _cyttsp_hndshk(ts, ts->xy_data.hst_mode);
	if (retval < 0) {
		pr_err("%s: handshake fail on operational reg r=%d\n",
			__func__, retval);
		/* continue anyway */
		retval = 0;
	}

	/* determine number of currently active touches */
	cur_tch = GET_NUM_TOUCHES(ts->xy_data.tt_stat);

	/* check for any error conditions */
	if (IS_BAD_PKT(ts->xy_data.tt_mode)) {
		/*
		 * Packet data isn't ready for some reason.
		 * This isn't normal, but because we did a handshake,
		 * we'll get another interrupt later with the actual data.
		 * We just want to report to the log for debugging later.
		 */
		pr_err("%s: Packet not ready detected\n", __func__);
		goto _cyttsp_xy_worker_error_exit;
	} else if (IS_BOOTLOADERMODE(ts->xy_data.tt_mode)) {
		pr_err("%s: BL mode detected in active state\n", __func__);
		if (BL_WATCHDOG_DETECT(ts->xy_data.tt_mode))
			pr_err("%s: BL watchdog timeout detected\n", __func__);
		_cyttsp_change_state(ts, CY_BL_STATE);
		retval = -EIO;
		goto _cyttsp_xy_worker_error_exit;
	} else if (GET_HSTMODE(ts->xy_data.hst_mode) ==
		GET_HSTMODE(CY_SYSINFO_MODE)) {
		pr_err("%s: got SysInfo interrupt; expected touch "
			"(hst_mode=%02X)\n", __func__,
			ts->xy_data.hst_mode);
		retval = -EIO;
		goto _cyttsp_xy_worker_error_exit;
	} else if (IS_LARGE_AREA(ts->xy_data.tt_stat) == 1) {
		/* terminate all active tracks */
		cur_tch = CY_NTCH;
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Large area detected\n", __func__);
	} else if (cur_tch == (CY_NUM_TRK_ID-1)) {
		/* terminate all active tracks */
		cur_tch = CY_NTCH;
		pr_err("%s: Num touch error detected\n", __func__);
	} else if (cur_tch > CY_NUM_TCH_ID) {
		/* set cur_tch to maximum */
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Number of Touches %d set to max %d\n",
			__func__, cur_tch, CY_NUM_TCH_ID);
		cur_tch = CY_NUM_TCH_ID;
	}

#ifdef CONFIG_TOUCHSCREEN_CYTTSP_KEY
	/*Report cyttsp_key*/
	if (ts->platform_data->frmwrk->enable_vkeys)
	{
		retval = cyttsp_key_report(ts);
	}
#endif

#ifdef CY_USE_WATCHDOG
	/*
	 * send no events if there were no
	 * previous touches and no new touches
	 */
	if ((ts->prv_tch == CY_NTCH) && (cur_tch == CY_NTCH)) {
		if (++ts->ntch_count > CY_MAX_NTCH) {
			/* TTSP device has a stuck operational mode */
			ts->ntch_count = CY_NTCH;
			pr_err("%s: Error, stuck no-touch ct=%d\n",
				__func__, cur_tch);
			_cyttsp_change_state(ts, CY_INVALID_STATE);
			_cyttsp_queue_startup(ts, false);
			retval = -ETIMEDOUT;
			goto _cyttsp_xy_worker_error_exit;
		}
	} else
		ts->ntch_count = CY_NTCH;

	ts->prv_tch = cur_tch;
#endif /* --CY_USE_WATCHDOG */

	/* collect touch information into tracks */
	_cyttsp_get_tracks(ts, cur_tch, cur_trk);

	/* provide input event signaling for each active touch */
	for (id = 0, num_sent = 0; id < CY_NUM_TRK_ID; id++) {
		if (cur_trk[id].tch) {
			t = cur_trk[id].abs[CY_ABS_ID_OST];
			if ((t < ts->platform_data->frmwrk->abs
				[(CY_ABS_ID_OST * CY_NUM_ABS_SET) +
				CY_MIN_OST]) ||
				(t > ts->platform_data->frmwrk->abs
				[(CY_ABS_ID_OST * CY_NUM_ABS_SET) +
				CY_MAX_OST])) {
				pr_err("%s: Touch=%d has bad"
					" track_id=%d\n",
					__func__, id, t);
				continue;
			}
			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_ID_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE) {
				/* send 0 based track id's */
				t -= ts->platform_data->frmwrk->abs
					[(CY_ABS_ID_OST * CY_NUM_ABS_SET) +
					CY_MIN_OST];
				input_report_abs(ts->input, signal, t);
			}

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_X_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					cur_trk[id].abs[CY_ABS_X_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_Y_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					cur_trk[id].abs[CY_ABS_Y_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_P_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					cur_trk[id].abs[CY_ABS_P_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_W_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					cur_trk[id].abs[CY_ABS_W_OST]);

			num_sent++;
			input_mt_sync(ts->input);
			ts->prv_trk[id] = cur_trk[id];
			ts->prv_trk[id].abs[CY_ABS_ID_OST] = t;
			cyttsp_dbg(ts, CY_DBG_LVL_1,
				"%s: ID:%3d  X:%3d  Y:%3d  "
				"Z:%3d  W=%3d  T=%3d\n",
				__func__, id,
				cur_trk[id].abs[CY_ABS_X_OST],
				cur_trk[id].abs[CY_ABS_Y_OST],
				cur_trk[id].abs[CY_ABS_P_OST],
				cur_trk[id].abs[CY_ABS_W_OST],
				t);
		} else if ((ts->platform_data->frmwrk->abs
				[(CY_ABS_P_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST] == ABS_MT_TOUCH_MAJOR) &&
				ts->prv_trk[id].tch) {
			/*
			 * pre-Gingerbread:
			 * need to report last position with
			 * and report that position with
			 * no touch if the touch lifts off
			 */
			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_ID_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_ID_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_X_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_X_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_Y_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_Y_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_P_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal, CY_NTCH);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_W_OST * CY_NUM_ABS_SET) +
				CY_SIGNAL_OST];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal, CY_NTCH);

			num_sent++;
			input_mt_sync(ts->input);
			ts->prv_trk[id].tch = CY_NTCH;
			cyttsp_dbg(ts, CY_DBG_LVL_1,
				"%s: ID:%3d  X:%3d  Y:%3d  "
				"Z:%3d  W=%3d  T=%3d liftoff\n",
				__func__, ts->prv_trk[id].abs[CY_ABS_ID_OST],
				ts->prv_trk[id].abs[CY_ABS_X_OST],
				ts->prv_trk[id].abs[CY_ABS_Y_OST],
				CY_NTCH,
				CY_NTCH,
				ts->prv_trk[id].abs[CY_ABS_ID_OST]);
		}
	}

	if (num_sent == 0) {
		/* in case of 0-touch; all liftoff; Gingerbread+ */
		input_mt_sync(ts->input);
	}

	input_sync(ts->input);
	goto _cyttsp_xy_worker_exit;

_cyttsp_xy_worker_error_exit:
#ifdef CY_USE_LEVEL_IRQ
	/* this delay is only needed if no input signaling is performed
	 * after the handshake;
	 * input signaling to the O/S provides
	 * sufficient delay
	 */
	udelay(100);	/* irq pulse: switch=50us */
#endif
_cyttsp_xy_worker_exit:
	return retval;
}

#ifdef CY_USE_WATCHDOG
#define CY_TIMEOUT msecs_to_jiffies(1000)
static void _cyttsp_start_watchdog_timer(struct cyttsp *ts)
{
	ts->watchdog_enabled = true;
	mod_timer(&ts->timer, jiffies + CY_TIMEOUT);
}

static void _cyttsp_stop_watchdog_timer(struct cyttsp *ts)
{
	ts->watchdog_enabled = false;
	del_timer(&ts->timer);
	cancel_work_sync(&ts->work);
}

static void cyttsp_timer_watchdog(struct work_struct *work)
{
	struct cyttsp *ts = container_of(work, struct cyttsp, work);
	int retval = 0;
	u8 mode = 0;

	if (ts == NULL) {
		pr_err("%s: NULL context pointer\n", __func__);
		goto cyttsp_timer_watchdog_exit;
	}

	mutex_lock(&ts->data_lock);
	if (ts->watchdog_enabled) {
		if (ts->power_state == CY_ACTIVE_STATE) {
			retval = ttsp_read_block_data(ts, CY_REG_BASE,
				sizeof(mode), &mode);
			if (retval < 0) {
				pr_err("%s: Read Block fail (r=%d)"
					" restart queued\n",
					__func__, retval);
				_cyttsp_queue_startup(ts, false);
				goto cyttsp_timer_watchdog_exit_error;
			}
			if (GET_HSTMODE(mode) ==
				GET_HSTMODE(CY_SYSINFO_MODE)) {
				pr_err("%s: reset to op mode m=%02X\n",
					__func__, mode);
				retval = _cyttsp_set_operational_mode(ts);
				if (retval < 0) {
					pr_err("%s: fail set op mode r=%d\n",
						__func__, retval);
					goto cyttsp_timer_watchdog_exit_error;
				}

				retval = _cyttsp_set_operational_regs(ts);
				if (retval < 0) {
					pr_err("%s: fail set op regs r=%d\n",
						__func__, retval);
					goto cyttsp_timer_watchdog_exit_error;
				}
				_cyttsp_change_state(ts, CY_ACTIVE_STATE);
			}
		}
		_cyttsp_start_watchdog_timer(ts);
	}

cyttsp_timer_watchdog_exit_error:
	mutex_unlock(&ts->data_lock);
cyttsp_timer_watchdog_exit:
	return;
}

static void cyttsp_timer(unsigned long handle)
{
	struct cyttsp *ts = (struct cyttsp *)handle;

	if (!work_pending(&ts->work))
		schedule_work(&ts->work);

	return;
}
#endif /* --CY_USE_WATCHDOG */

static bool _cyttsp_chk_bl_startup(struct cyttsp *ts)
{
	return !(ts->bl_data.bl_status & CY_BL_BUSY) &&
		(ts->bl_data.bl_error == CY_BL_RUNNING);
}

static bool _cyttsp_chk_bl_terminate(struct cyttsp *ts)
{
	return (ts->bl_data.bl_status == CY_BL_READY_APP) &&
		(ts->bl_data.bl_error == CY_BL_NOERR);
}

static int _cyttsp_wait_bl_ready(struct cyttsp *ts,
	bool (*cond)(struct cyttsp *), int timeout_ms)
{
	unsigned long timeout = 0;
	unsigned long uretval = 0;
	int retval = 0;

	/* wait for interrupt to set ready completion */
	timeout = msecs_to_jiffies(timeout_ms);

	mutex_unlock(&ts->data_lock);
	//uretval = wait_for_completion_interruptible_timeout(//modify by huawei
	uretval = wait_for_completion_timeout(//modify by huawei
		&ts->ld_int_running, timeout);
	mutex_lock(&ts->data_lock);

	if (uretval == 0) {
		pr_err("%s: Loader timeout waiting for loader ready\n",
			__func__);
		/* do not return error; continue in case of missed interrupt */
	}

	/* if cond == NULL then no status check is required */
	if (cond != NULL) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Loader Start: ret=%d uretval=%lu timeout=%lu\n",
			__func__, retval, uretval, timeout);

		retval = _cyttsp_load_bl_regs(ts);
		if (retval < 0) {
			pr_err("%s: Load BL regs err r=%d\n",
				__func__, retval);
		} else if (cond(ts) == false) {
			if (uretval == 0) {
				pr_err("%s: BL loader timeout err"
					" ret=%d uretval=%lu timeout=%lu\n",
				__func__, retval, uretval, timeout);
				retval = -ETIME;
			} else {
				pr_err("%s: BL loader status err"
					" r=%d bl_f=%02X bl_s=%02X bl_e=%02X\n",
					__func__, retval, ts->bl_data.bl_file,
					ts->bl_data.bl_status,
					ts->bl_data.bl_error);
				retval = -EIO;
			}
		}
	}

	return retval;
}

static int _cyttsp_wr_blk_chunks(struct cyttsp *ts, u8 addr,
	u8 len, const u8 *values)
{
	int retval = 0;
	int block = 1;
	int length = len;
	u8 dataray[CY_BL_PAGE_SIZE + 1];

	memset(dataray, 0, sizeof(dataray));

	/* first page already includes the bl page offset */
	memcpy(dataray, values, sizeof(dataray));
	INIT_COMPLETION(ts->ld_int_running);
	retval = ttsp_write_block_data(ts, addr, sizeof(dataray), dataray);
	if (retval < 0) {
		pr_err("%s: Write chunk err block=0 r=%d\n",
			__func__, retval);
		goto _cyttsp_wr_blk_chunks_exit;
	}

	values += CY_BL_PAGE_SIZE + 1;
	length -= CY_BL_PAGE_SIZE + 1;

	/* remaining pages require bl page offset stuffing */
	for (block = 1; block < CY_BL_NUM_PAGES; block++) {
		if (length < 0) {
			pr_err("%s: Write chunk length err block=%d r=%d\n",
				__func__, block, retval);
			goto _cyttsp_wr_blk_chunks_exit;
		}
		dataray[0] = CY_BL_PAGE_SIZE * block;

		retval = _cyttsp_wait_bl_ready(ts, NULL, 100);
		if (retval < 0) {
			pr_err("%s: wait ready timeout block=%d length=%d\n",
				__func__, block, length);
			goto _cyttsp_wr_blk_chunks_exit;
		}

		memcpy(&dataray[1], values, length >= CY_BL_PAGE_SIZE ?
			CY_BL_PAGE_SIZE : length);
		INIT_COMPLETION(ts->ld_int_running);
		retval = ttsp_write_block_data(ts, addr,
			length >= CY_BL_PAGE_SIZE ?
			sizeof(dataray) : length + 1, dataray);
		if (retval < 0) {
			pr_err("%s: Write chunk err block=%d r=%d\n",
				__func__, block, retval);
			goto _cyttsp_wr_blk_chunks_exit;
		}

		values += CY_BL_PAGE_SIZE;
		length -= CY_BL_PAGE_SIZE;
	}

	retval = _cyttsp_wait_bl_ready(ts, NULL, 200);
	if (retval < 0) {
		pr_err("%s: last wait ready timeout block=%d length=%d\n",
			__func__, block, length);
		goto _cyttsp_wr_blk_chunks_exit;
	}

	retval = _cyttsp_load_bl_regs(ts);
	if (retval < 0) {
		pr_err("%s: Load BL regs err r=%d\n",
			__func__, retval);
		goto _cyttsp_wr_blk_chunks_exit;
	}
	if (!(((ts->bl_data.bl_status == CY_BL_READY_NO_APP) &&
		(ts->bl_data.bl_error == CY_BL_RUNNING)) ||
		(ts->bl_data.bl_status == CY_BL_READY_APP))) {
		pr_err("%s: BL status fail bl_stat=0x%02X, bl_err=0x%02X\n",
			__func__, ts->bl_data.bl_status, ts->bl_data.bl_error);
		retval = -ETIMEDOUT;
	}

_cyttsp_wr_blk_chunks_exit:
	return retval;
}

static int _cyttsp_load_app(struct cyttsp *ts, const u8 *fw, int fw_size)
{
	int retval = 0;
	int loc = 0;

	/* sync up with the next bootloader heartbeat interrupt in order
	 * to remove haearbeat vs. repsonse interrupt ambiguity
	 */
	retval = _cyttsp_wait_ready(ts, &ts->bl_int_running,
		NULL, 0, CY_HALF_SEC_TMO_MS);
	if (retval < 0) {
		pr_err("%s: fail wait ready r=%d\n", __func__, retval);
		goto _cyttsp_load_app_exit;
	}

	_cyttsp_change_state(ts, CY_LDR_STATE);

	/* send bootload initiation command */
	pr_info("%s: Send BL Loader Enter\n", __func__);
	INIT_COMPLETION(ts->ld_int_running);
	retval = ttsp_write_block_data(ts, CY_REG_BASE,
		CY_BL_ENTER_CMD_SIZE, (void *)(&fw[loc]));
	if (retval < 0) {
		pr_err("%s: BL loader fail startup r=%d\n", __func__, retval);
		goto _cyttsp_load_app_exit;
	}

	retval = _cyttsp_wait_bl_ready(ts, _cyttsp_chk_bl_startup,
		CY_TEN_SEC_TMO_MS);
	if (retval < 0) {
		pr_err("%s: BL loader startup err r=%d\n", __func__, retval);
		goto _cyttsp_load_app_exit;
	}

	loc += CY_BL_ENTER_CMD_SIZE;

	/* send bootload firmware load blocks */
	pr_info("%s: Send BL Loader Blocks\n", __func__);
	while ((fw_size - loc) > CY_BL_WR_BLK_CMD_SIZE) {
		cyttsp_dbg(ts, CY_DBG_LVL_2, "%s: BL loader block=%d"
			" f=%02X s=%02X e=%02X loc=%d\n", __func__,
			loc / CY_BL_WR_BLK_CMD_SIZE,
			ts->bl_data.bl_file, ts->bl_data.bl_status,
			ts->bl_data.bl_error, loc);
		retval = _cyttsp_wr_blk_chunks(ts, CY_REG_BASE,
			CY_BL_WR_BLK_CMD_SIZE, &fw[loc]);
		if (retval < 0) {
			pr_err("%s: BL loader fail"
				" r=%d block=%d loc=%d\n", __func__, retval,
				loc / CY_BL_WR_BLK_CMD_SIZE, loc);
			goto _cyttsp_load_app_exit;
		}

		retval = _cyttsp_load_bl_regs(ts);
		if (retval < 0) {
			pr_err("%s: BL Read error r=%d\n", __func__, retval);
			goto _cyttsp_load_app_exit;
		}

		if ((ts->bl_data.bl_status & CY_BL_BUSY) ||
			(ts->bl_data.bl_error != CY_BL_RUNNING)) {
			/* signal a status err */
			pr_err("%s: BL READY ERR on write block"
				" bl_file=%02X bl_status=%02X bl_error=%02X\n",
				__func__, ts->bl_data.bl_file,
				ts->bl_data.bl_status, ts->bl_data.bl_error);
			retval = -EIO;
			goto _cyttsp_load_app_exit;
		} else {
			/* point to next block */
			loc += CY_BL_WR_BLK_CMD_SIZE;
		}
	}

	/* send bootload terminate command */
	pr_info("%s: Send BL Loader Terminate\n", __func__);
	if (loc == (fw_size - CY_BL_EXIT_CMD_SIZE)) {
		INIT_COMPLETION(ts->ld_int_running);
		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			CY_BL_EXIT_CMD_SIZE, (void *)(&fw[loc]));
		if (retval < 0) {
			pr_err("%s: BL fail Terminate r=%d\n",
				__func__, retval);
			goto _cyttsp_load_app_exit;
		}

		retval = _cyttsp_wait_bl_ready(ts, _cyttsp_chk_bl_terminate,
			CY_TEN_SEC_TMO_MS);
		if (retval < 0) {
			pr_err("%s: BL Loader Terminate err r=%d\n",
				__func__, retval);
			/*
			 * if app valid bit is set
			 * return success and let driver
			 * try a normal restart anyway
			 */
			if (IS_VALID_APP(ts->bl_data.bl_status)) {
				pr_err("%s: BL Loader Valid App indicated"
					" bl_s=%02X\n", __func__,
					ts->bl_data.bl_status);
				retval = 0;
			}
		}
	} else {
		pr_err("%s: FW size mismatch\n", __func__);
		retval = -EINVAL;
		goto _cyttsp_load_app_exit;
	}

_cyttsp_load_app_exit:
	return retval;
}

/*modify by huawei begin*/
/*use for *.h file to update FW*/
#ifndef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE 
static int _cyttsp_boot_loader(struct cyttsp *ts)
{
	int retval = 0;
	bool new_vers = false;
	struct cyttsp_vers *fw = NULL;
/*Del some lines*/

#ifdef CONFIG_TOUCHSCREEN_DEBUG
	/*
	 * Prevent forced old firmware
	 * from being re-written by platform firmware
	 * on warm startup.
	 * Cold startup will restore old firmware
	 * to new platform firmware
	 */
	if (ts->ic_reflash_done) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Prevent writing over old firmware\n",
			__func__);
		goto _cyttsp_boot_loader_exit;
	}
#endif

	if (ts->power_state == CY_SLEEP_STATE) {
		pr_err("%s: cannot upgrade firmware in %s state\n",
			__func__, cyttsp_powerstate_string[ts->power_state]);
		goto _cyttsp_boot_loader_exit;
	}

	if ((ts->platform_data->fw->ver == NULL) ||
		(ts->platform_data->fw->img == NULL)) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: empty version list or no image\n",
			__func__);
		goto _cyttsp_boot_loader_exit;
	}

	if (ts->platform_data->fw->vsize != CY_BL_VERS_SIZE) {
		pr_err("%s: bad fw version list size=%d\n",
			__func__, ts->platform_data->fw->vsize);
		goto _cyttsp_boot_loader_exit;
	}

	/* automatically update firmware if new version detected */
	fw = (struct cyttsp_vers *)ts->platform_data->fw->ver;
#ifdef CY_ANY_DIFF_NEW_VER
	new_vers =
		(((u16)fw->app_verh << 8) +
		(u16)fw->app_verl) !=
		(((u16)ts->bl_data.appver_hi << 8) +
		(u16)ts->bl_data.appver_lo);
#else
	new_vers =
		(((u16)fw->app_verh << 8) +
		(u16)fw->app_verl) >
		(((u16)ts->bl_data.appver_hi << 8) +
		(u16)ts->bl_data.appver_lo);
#endif
/*Del some lines*/

	if (!IS_VALID_APP(ts->bl_data.bl_status) || new_vers) {
		retval = _cyttsp_load_app(ts,
			ts->platform_data->fw->img,
			ts->platform_data->fw->size);
		if (retval < 0) {
			pr_err("%s: bus fail on load fw\n", __func__);
			_cyttsp_change_state(ts, CY_IDLE_STATE);
			goto _cyttsp_boot_loader_exit;
		}

		/* reset TTSP Device back to bootloader mode */
		retval = _cyttsp_hard_reset(ts);
		if (retval < 0) {
			pr_err("%s: Fail on hard reset r=%d\n",
				__func__, retval);
		}
	}

_cyttsp_boot_loader_exit:
	return retval;
}
#endif
/*Add by huawei end*/

/* Driver version */
static ssize_t cyttsp_drv_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Driver: %s\nVersion: %s\nDate: %s\n",
		ts->input->name, CY_DRIVER_VERSION, CY_DRIVER_DATE);
}
static DEVICE_ATTR(drv_ver, S_IRUGO, cyttsp_drv_ver_show, NULL);

/* Driver status */
static ssize_t cyttsp_drv_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Driver state is %s\n",
		cyttsp_powerstate_string[ts->power_state]);
}
static DEVICE_ATTR(drv_stat, S_IRUGO, cyttsp_drv_stat_show, NULL);

#ifdef CONFIG_TOUCHSCREEN_DEBUG
/* Group Number */
static ssize_t cyttsp_ic_grpnum_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Current Group: %d\n", ts->ic_grpnum);
}
static ssize_t cyttsp_ic_grpnum_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	int retval = 0;

	mutex_lock(&ts->data_lock);
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		pr_err("%s: Failed to convert value\n", __func__);
		goto cyttsp_ic_grpnum_store_error_exit;
	}

	if (value > 0xFF) {
		value = 0xFF;
		pr_err("%s: value is greater than max;"
			" set to %d\n", __func__, (int)value);
	}
	ts->ic_grpnum = value;

cyttsp_ic_grpnum_store_error_exit:
	retval = size;
	mutex_unlock(&ts->data_lock);
	return retval;
}
static DEVICE_ATTR(ic_grpnum, S_IRUSR | S_IWUSR,
	cyttsp_ic_grpnum_show, cyttsp_ic_grpnum_store);

/* Group Offset */
static ssize_t cyttsp_ic_grpoffset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Current Offset: %u\n", ts->ic_grpoffset);
}
static ssize_t cyttsp_ic_grpoffset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	int retval = 0;

	mutex_lock(&ts->data_lock);
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		pr_err("%s: Failed to convert value\n", __func__);
		goto cyttsp_ic_grpoffset_store_error_exit;
	}

	if (value > 0xFF) {
		value = 0xFF;
		pr_err("%s: value is greater than max;"
			" set to %d\n", __func__, (int)value);
	}
	ts->ic_grpoffset = value;

cyttsp_ic_grpoffset_store_error_exit:
	retval = size;
	mutex_unlock(&ts->data_lock);
	return retval;
}
static DEVICE_ATTR(ic_grpoffset, S_IRUSR | S_IWUSR,
	cyttsp_ic_grpoffset_show, cyttsp_ic_grpoffset_store);

/* Group Data */
static ssize_t _cyttsp_get_show_data(struct cyttsp *ts,
	size_t offset, size_t num_read, u8 *ic_buf, char *buf)
{
	ssize_t uretval = 0;
	int retval = 0;

	if (num_read > 0) {
		retval = ttsp_read_block_data(ts, offset, num_read, ic_buf);
		if (retval < 0) {
			pr_err("%s: Cannot read Group %d Data.\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Cannot read Group %d Data.\n",
				ts->ic_grpnum);
		}
	}

	return uretval;
}
static ssize_t cyttsp_ic_grpdata_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	int i = 0;
	ssize_t uretval = 0;
	int retval = 0;
	int num_read = 0;
	int grpsize = 0;
	u8 ic_buf[sizeof(struct cyttsp_xydata)];

	mutex_lock(&ts->data_lock);

	memset(ic_buf, 0, sizeof(ic_buf));

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: grpnum=%d grpoffset=%u\n",
		__func__, ts->ic_grpnum, ts->ic_grpoffset);

	switch (ts->ic_grpnum) {
	case CY_IC_GRPNUM_OP_TAG:
		if ((ts->ic_grpoffset + CY_REG_OP_START) >
			sizeof(struct cyttsp_xydata)) {
			grpsize = sizeof(struct cyttsp_xydata) -
				CY_REG_OP_START;
			goto cyttsp_ic_grpdata_show_error_exit;
		}
		num_read = sizeof(struct cyttsp_xydata) - (ts->ic_grpoffset +
			CY_REG_OP_START);
		uretval = _cyttsp_get_show_data(ts, ts->ic_grpoffset +
			CY_REG_OP_START, num_read, ic_buf, buf);
		if (uretval > 0)
			goto cyttsp_ic_grpdata_show_exit;
		break;
	case CY_IC_GRPNUM_SI_TAG:
		if ((ts->ic_grpoffset + CY_REG_SI_START) >
			sizeof(struct cyttsp_sysinfo_data)) {
			grpsize = sizeof(struct cyttsp_sysinfo_data) -
				CY_REG_SI_START;
			goto cyttsp_ic_grpdata_show_error_exit;
		}
#ifdef CY_USE_WATCHDOG
		_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		retval = _cyttsp_set_sysinfo_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot access SI Group %d Data.\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Cannot access SI Group %d Data.\n",
					ts->ic_grpnum);
			goto cyttsp_ic_grpdata_show_exit;
		}

		num_read = sizeof(struct cyttsp_sysinfo_data) -
			(ts->ic_grpoffset + CY_REG_SI_START);
		uretval = _cyttsp_get_show_data(ts, ts->ic_grpoffset +
			CY_REG_SI_START, num_read, ic_buf, buf);

		retval = _cyttsp_set_operational_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot restore op mode\n",
				__func__);
		} else {
			_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
			_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		}
		if (uretval > 0)
			goto cyttsp_ic_grpdata_show_exit;
		break;
	case CY_IC_GRPNUM_BL_KEY:
		if (ts->platform_data->sett[ts->ic_grpnum] == NULL) {
			pr_err("%s: Missing table for Group %d\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Missing table for Group %d\n",
				ts->ic_grpnum);
			goto cyttsp_ic_grpdata_show_exit;
		}
		if (ts->platform_data->sett[ts->ic_grpnum]->data == NULL) {
			pr_err("%s: Missing table data for Group %d\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Missing table for Group %d\n",
				ts->ic_grpnum);
			goto cyttsp_ic_grpdata_show_exit;
		}
		if (ts->ic_grpoffset >=
			ts->platform_data->sett[ts->ic_grpnum]->size) {
			grpsize = ts->platform_data->sett[ts->ic_grpnum]->size;
			goto cyttsp_ic_grpdata_show_error_exit;
		}
		num_read = ts->platform_data->sett[ts->ic_grpnum]->size -
			ts->ic_grpoffset;

		memcpy(ic_buf, &ts->platform_data->sett
			[ts->ic_grpnum]->data[ts->ic_grpoffset],
			num_read);
		break;
	case CY_IC_GRPNUM_OP_REG:
		grpsize = sizeof(struct cyttsp_xydata);
		if (ts->ic_grpoffset >= grpsize)
			goto cyttsp_ic_grpdata_show_error_exit;
		num_read = grpsize - ts->ic_grpoffset;
		uretval = _cyttsp_get_show_data(ts, ts->ic_grpoffset,
			num_read, ic_buf, buf);
		if (uretval > 0)
			goto cyttsp_ic_grpdata_show_exit;
		break;
	case CY_IC_GRPNUM_SI_REG:
		grpsize = sizeof(struct cyttsp_sysinfo_data);
		if (ts->ic_grpoffset >= grpsize)
			goto cyttsp_ic_grpdata_show_error_exit;
		num_read = grpsize - ts->ic_grpoffset;
#ifdef CY_USE_WATCHDOG
		_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		retval = _cyttsp_set_sysinfo_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot access SI Group %d Data.\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Cannot access SI Group %d Data.\n",
					ts->ic_grpnum);
			goto cyttsp_ic_grpdata_show_exit;
		}
		uretval = _cyttsp_get_show_data(ts, ts->ic_grpoffset,
			num_read, ic_buf, buf);

		retval = _cyttsp_set_operational_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot restore op mode\n",
				__func__);
		} else {
			_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
			_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		}

		if (uretval > 0)
			goto cyttsp_ic_grpdata_show_exit;
		break;
	case CY_IC_GRPNUM_BL_REG:
		grpsize = sizeof(struct cyttsp_bootloader_data);
		if (ts->ic_grpoffset >= grpsize)
			goto cyttsp_ic_grpdata_show_error_exit;
		num_read = grpsize - ts->ic_grpoffset;
		retval = _cyttsp_hard_reset(ts);
		if (retval < 0) {
			pr_err("%s: Cannot access BL Group %d Data.\n",
				__func__, ts->ic_grpnum);
			uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Cannot access BL Group %d Data.\n",
					ts->ic_grpnum);
			goto cyttsp_ic_grpdata_show_exit;
		}
		uretval = _cyttsp_get_show_data(ts, ts->ic_grpoffset,
			num_read, ic_buf, buf);

		/* startup will restore to CY_ACTIVE_MODE if successful */
		retval = _cyttsp_startup(ts);
		if (retval < 0) {
			pr_err("%s: Cannot restore op mode\n",
				__func__);
		}

		if (uretval > 0)
			goto cyttsp_ic_grpdata_show_exit;
		break;
	default:
		pr_err("%s: Group %d does not exist.\n",
			__func__, ts->ic_grpnum);
		uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Group %d does not exist.\n",
			ts->ic_grpnum);
		goto cyttsp_ic_grpdata_show_exit;
	}

	snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Group %d, Offset %u:\n", ts->ic_grpnum, ts->ic_grpoffset);
	for (i = 0; i < num_read; i++)
		snprintf(buf, CY_MAX_PRBUF_SIZE, "%s0x%02X\n", buf, ic_buf[i]);
	uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
		"%s(%d bytes)\n", buf, num_read);

	goto cyttsp_ic_grpdata_show_exit;

cyttsp_ic_grpdata_show_error_exit:
	pr_err("%s: Offset %u exceeds group %d size of %d\n",
		__func__, ts->ic_grpoffset, ts->ic_grpnum, grpsize);
	uretval = snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Offset %u exceeds group %d size of %d\n",
		ts->ic_grpoffset, ts->ic_grpnum, grpsize);

cyttsp_ic_grpdata_show_exit:
	mutex_unlock(&ts->data_lock);
	return uretval;
}

static ssize_t cyttsp_ic_grpdata_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	int retval = 0;
	const char *pbuf = buf;
	int scan_read = 0;
	int num_write = 0;
	int num_commas = 0;
	char scan_buf[5];	/* sizeof "0xHH" plus null byte */
	ssize_t grpsize = 0;
	u8 *ic_buf = NULL;

	mutex_lock(&ts->data_lock);

	memset(scan_buf, 0, sizeof(scan_buf));
	ic_buf = kzalloc(sizeof(struct cyttsp_xydata), GFP_KERNEL);
	if (ic_buf == NULL) {
		pr_err("%s: Error, kzalloc scan buffer memory\n", __func__);
		goto cyttsp_ic_grpdata_store_exit;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: grpnum=%d grpoffset=%u\n",
		__func__, ts->ic_grpnum, ts->ic_grpoffset);

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: pbuf=%p buf=%p size=%d sizeof(scan_buf)=%d\n",
		__func__, pbuf, buf, size, sizeof(scan_buf));

	while (pbuf <= ((buf + size) - (sizeof(scan_buf) - 1))) {
		num_commas = 0;
		while (((*pbuf == ' ') || (*pbuf == ',')) &&
			(pbuf < ((buf + size) - 4))) {
			if (*pbuf == ',')
				num_commas++;
			if (num_commas > 1) {
				pr_err("%s: Invalid data format. "
					"\",,\" not allowed.\n",
					__func__);
				retval = size;
				goto cyttsp_ic_grpdata_store_exit;
			}
			pbuf++;
		}

		if (num_write > sizeof(struct cyttsp_xydata)) {
			pr_err("%s: Num store data > max=%d\n", __func__,
				sizeof(sizeof(struct cyttsp_xydata)));
			goto cyttsp_ic_grpdata_store_exit;
		}

		if (pbuf <= ((buf + size) - (sizeof(scan_buf) - 1))) {
			memset(scan_buf, 0, sizeof(scan_buf));
			for (scan_read = 0; scan_read < (sizeof(scan_buf) - 1);
				scan_read++)
				scan_buf[scan_read] = *pbuf++;
			retval = strict_strtoul(scan_buf, 16, &value);
			if (retval < 0) {
				pr_err("%s: Invalid data format. "
					"Use \"0xHH,...,0xHH\" instead.\n",
					__func__);
				goto cyttsp_ic_grpdata_store_exit;
			}
			if (value > 0xFF) {
				pr_err("%s: invalid value=0x%08X; must be in"
					" the range 0x00 - 0xFF\n",
					__func__, (int)value);
				goto cyttsp_ic_grpdata_store_exit;
			}
			ic_buf[num_write] = value;
			cyttsp_dbg(ts, CY_DBG_LVL_3,
				"%s: ic_buf[%d] = 0x%02X\n",
				__func__, num_write, ic_buf[num_write]);
			num_write++;
		} else {
			pr_err("%s: Incomplete final value\n", __func__);
			goto cyttsp_ic_grpdata_store_exit;
		}
	}

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: After scan: grpnum=%d grpoffset=%d num_write=%d"
		" ic_buf[0]=%02X ic_buf[1]=%02X\n",
		__func__, ts->ic_grpnum, ts->ic_grpoffset,
		num_write, ic_buf[0], ic_buf[1]);
	if (num_write == 0) {
		pr_err("%s: No data scanned; num_write=%d grpnum=%d\n",
			__func__, num_write, ts->ic_grpnum);
		goto cyttsp_ic_grpdata_store_exit;
	}

	switch (ts->ic_grpnum) {
	case CY_IC_GRPNUM_OP_TAG:
		if ((ts->ic_grpoffset + CY_REG_OP_START) >
			sizeof(struct cyttsp_xydata)) {
			grpsize = sizeof(struct cyttsp_xydata) -
				CY_REG_OP_START;
			goto cyttsp_ic_grpdata_store_size_error_exit;
		}
		if (num_write > (sizeof(struct cyttsp_xydata) -
			(CY_REG_OP_START + ts->ic_grpoffset))) {
			pr_err("%s Too much data for op registers\n",
				__func__);
			goto cyttsp_ic_grpdata_store_exit;
		}
		retval = _cyttsp_put_store_data(ts, ts->ic_grpnum,
			ts->ic_grpoffset + CY_REG_OP_START, num_write, ic_buf);
		if (retval < 0) {
			pr_err("%s: fail put store data grpnum=%d r=%d\n",
				__func__, ts->ic_grpnum, retval);
		}
		break;
	case CY_IC_GRPNUM_SI_TAG:
		if ((ts->ic_grpoffset + CY_REG_SI_START) >
			sizeof(struct cyttsp_sysinfo_data)) {
			grpsize = sizeof(struct cyttsp_sysinfo_data) -
				CY_REG_SI_START;
			goto cyttsp_ic_grpdata_store_size_error_exit;
		}
		if (num_write > (sizeof(struct cyttsp_sysinfo_data) -
			(CY_REG_SI_START + ts->ic_grpoffset))) {
			pr_err("%s Too much data for sysinfo registers\n",
				__func__);
			goto cyttsp_ic_grpdata_store_exit;
		}
#ifdef CY_USE_WATCHDOG
		_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		retval = _cyttsp_set_sysinfo_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot write SI Group Data.\n",
			__func__);
			goto cyttsp_ic_grpdata_store_exit;
		}

		retval = _cyttsp_put_si_data(ts, ts->ic_grpnum,
			ts->ic_grpoffset + CY_REG_SI_START, num_write, ic_buf);
		if (retval < 0) {
			pr_err("%s: Error writing Sysinfo data r=%d\n",
				__func__, retval);
			/* continue anyway */
			retval = 0;
		}

		retval = _cyttsp_set_operational_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot restore operating mode.\n",
				__func__);
		} else {
			_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
			_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		}
		break;
	case CY_IC_GRPNUM_BL_KEY:
		pr_err("%s: Group=%d is read only\n",
			__func__, ts->ic_grpnum);
		break;
	case CY_IC_GRPNUM_OP_REG:
		grpsize = sizeof(struct cyttsp_xydata);
		if ((ts->ic_grpoffset + num_write) > grpsize)
			goto cyttsp_ic_grpdata_store_size_error_exit;

		retval = _cyttsp_put_store_data(ts, ts->ic_grpnum,
			ts->ic_grpoffset, num_write, ic_buf);
		if (retval < 0) {
			pr_err("%s: fail put store data grpnum=%d r=%d\n",
				__func__, ts->ic_grpnum, retval);
		} else {
#ifdef CY_ENABLE_CA
			if (ts->ic_grpoffset == CY_REG_OP_FEATURE) {
				if (ic_buf[0] & CY_CA_BIT)
					ts->ca_enabled = true;
				else
					ts->ca_enabled = false;
			}
#endif
		}
		break;
	case CY_IC_GRPNUM_SI_REG:
		grpsize = sizeof(struct cyttsp_sysinfo_data);
		if ((ts->ic_grpoffset + num_write) > grpsize)
			goto cyttsp_ic_grpdata_store_size_error_exit;

#ifdef CY_USE_WATCHDOG
		_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		retval = _cyttsp_set_sysinfo_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot write SI Group Data.\n", __func__);
			goto cyttsp_ic_grpdata_store_exit;
		}

		retval = _cyttsp_put_si_data(ts, ts->ic_grpnum,
			ts->ic_grpoffset + CY_REG_SI_START, num_write, ic_buf);
		if (retval < 0) {
			pr_err("%s: Error writing Sysinfo data r=%d\n",
				__func__, retval);
			/* continue anyway */
			retval = 0;
		}

		retval = _cyttsp_set_operational_mode(ts);
		if (retval < 0) {
			pr_err("%s: Cannot restore operating mode.\n",
				__func__);
		} else {
			_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
			_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		}
		break;
	case CY_IC_GRPNUM_BL_REG:
		pr_err("%s: Group=%d is read only\n",
			__func__, ts->ic_grpnum);
		break;
	default:
		pr_err("%s: Group %d does not exist.\n",
			__func__, ts->ic_grpnum);
		break;
	}

	goto cyttsp_ic_grpdata_store_exit;

cyttsp_ic_grpdata_store_size_error_exit:
	pr_err("%s: Offset %u exceeds group %d size of %d\n",
		__func__, ts->ic_grpoffset, ts->ic_grpnum, grpsize);
cyttsp_ic_grpdata_store_exit:
	if (ic_buf != NULL)
		kfree(ic_buf);
	mutex_unlock(&ts->data_lock);
	retval = size;
	return retval;
}
static DEVICE_ATTR(ic_grpdata, S_IRUSR | S_IWUSR,
	cyttsp_ic_grpdata_show, cyttsp_ic_grpdata_store);

static ssize_t cyttsp_drv_flags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Current Driver Flags: 0x%04X\n", ts->flags);
}
static ssize_t cyttsp_drv_flags_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	ssize_t retval = 0;

	mutex_lock(&(ts->data_lock));
	retval = strict_strtoul(buf, 16, &value);
	if (retval < 0) {
		pr_err("%s: Failed to convert value\n", __func__);
		goto cyttsp_drv_flags_store_error_exit;
	}

	if (value > 0xFFFF) {
		pr_err("%s: value=%lu is greater than max;"
			" drv_flags=0x%04X\n", __func__, value, ts->flags);
	} else {
		ts->flags = value;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: drv_flags=0x%04X\n", __func__, ts->flags);

cyttsp_drv_flags_store_error_exit:
	retval = size;
	mutex_unlock(&(ts->data_lock));
	return retval;
}
static DEVICE_ATTR(drv_flags, S_IRUSR | S_IWUSR,
	cyttsp_drv_flags_show, cyttsp_drv_flags_store);

static ssize_t cyttsp_hw_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	ssize_t retval = 0;

	mutex_lock(&(ts->data_lock));
	retval = _cyttsp_startup(ts);
	mutex_unlock(&(ts->data_lock));
	if (retval < 0) {
		pr_err("%s: fail hw_reset device restart r=%d\n",
			__func__, retval);
	}

	retval = size;
	return retval;
}
static DEVICE_ATTR(hw_reset, S_IWUSR, NULL, cyttsp_hw_reset_store);

static ssize_t cyttsp_hw_recov_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	ssize_t retval = 0;

	mutex_lock(&(ts->data_lock));
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		pr_err("%s: Failed to convert value\n", __func__);
		goto cyttsp_hw_recov_store_error_exit;
	}

	if (ts->platform_data->hw_recov == NULL) {
		pr_err("%s: no hw_recov function\n", __func__);
		goto cyttsp_hw_recov_store_error_exit;
	}

	retval = ts->platform_data->hw_recov((int)value);
	if (retval < 0) {
		pr_err("%s: fail hw_recov(value=%d) function r=%d\n",
			__func__, (int)value, retval);
	}

cyttsp_hw_recov_store_error_exit:
	retval = size;
	mutex_unlock(&(ts->data_lock));
	return retval;
}
static DEVICE_ATTR(hw_recov, S_IWUSR, NULL, cyttsp_hw_recov_store);
#endif

static ssize_t cyttsp_ic_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Application Version: 0x%02X%02X\n"
		"TTSP Version:        0x%02X%02X\n"
		"Applicaton ID:       0x%02X%02X\n"
		"Custom ID:           0x%02X%02X%02X\n",
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);
}
static DEVICE_ATTR(ic_ver, S_IRUGO, cyttsp_ic_ver_show, NULL);

#ifdef CONFIG_TOUCHSCREEN_DEBUG
static ssize_t cyttsp_hw_irqstat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval = 0;
	struct cyttsp *ts = dev_get_drvdata(dev);

	if (ts->platform_data->irq_stat != NULL) {
		retval = ts->platform_data->irq_stat();
		switch (retval) {
		case 0:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Interrupt line is LOW.\n");
		case 1:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Interrupt line is HIGH.\n");
		default:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Function irq_stat() returned %d.\n", retval);
		}
	} else {
		return snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Function irq_stat() undefined.\n");
	}
}
static DEVICE_ATTR(hw_irqstat, S_IRUSR | S_IWUSR,
	cyttsp_hw_irqstat_show, NULL);
#endif

/* Disable Driver IRQ */
static ssize_t cyttsp_drv_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Driver IRQ is %s\n", ts->irq_enabled ? "ENABLED" : "DISABLED");
}
static ssize_t cyttsp_drv_irq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int retval = 0;
	struct cyttsp *ts = dev_get_drvdata(dev);
	unsigned long value = 0;

	mutex_lock(&ts->data_lock);

	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0)
		pr_err("%s: Failed to convert value\n", __func__);
	else {
		switch (value) {
		case 0:
			if (ts->irq_enabled == false) {
				pr_info("%s: Driver IRQ already DISABLED\n",
					__func__);
			} else {
				/* Disable IRQ */
				disable_irq_nosync(ts->irq);
#ifdef CY_USE_WATCHDOG
				_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				pr_info("%s: Driver IRQ now DISABLED\n",
					__func__);
				ts->irq_enabled = false;
			}
			break;
		case 1:
			if (ts->irq_enabled == true) {
				pr_info("%s: Driver IRQ already ENABLED\n",
					__func__);
			} else {
				/* Enable IRQ */
				enable_irq(ts->irq);
#ifdef CY_USE_WATCHDOG
				_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				pr_info("%s: Driver IRQ now ENABLED\n",
					__func__);
				ts->irq_enabled = true;
			}
			break;
		default:
			pr_err("%s: value=%lu is out of range; enter"
				" either 0 (disable) or 1 (enable)\n",
				__func__, value);
		}
	}

	mutex_unlock(&ts->data_lock);
	retval = size;
	return retval;
}
static DEVICE_ATTR(drv_irq, S_IRUSR | S_IWUSR,
	cyttsp_drv_irq_show, cyttsp_drv_irq_store);

#ifdef CONFIG_TOUCHSCREEN_DEBUG
/* Force firmware upgrade */
static int _cyttsp_firmware_class_loader(struct cyttsp *ts,
	const u8 *data, int size)
{
	int retval = 0;
	const u8 *img = NULL;
	int img_size = 0;

	if ((data == NULL) || (size == 0)) {
		pr_err("%s: empty firmware image data=%p size=%d\n",
			__func__, data, size);
		_cyttsp_change_state(ts, CY_IDLE_STATE);
		goto _cyttsp_firmware_class_loader_exit;
	} else {
		img = data + data[0] + 1;
		img_size = size - (data[0] + 1);

		retval = _cyttsp_hard_reset(ts);
		if (retval < 0) {
			pr_err("%s: Fail reset device on loader\n", __func__);
			goto _cyttsp_firmware_class_loader_exit;
		}

		retval = _cyttsp_load_bl_regs(ts);
		if (retval < 0) {
			pr_err("%s: Fail read bootloader\n", __func__);
			goto _cyttsp_firmware_class_loader_exit;
		}

		cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: call load_app\n", __func__);
		retval = _cyttsp_load_app(ts, img, img_size);
		if (retval < 0)
			pr_err("%s: fail on load fw r=%d\n", __func__, retval);
	}

_cyttsp_firmware_class_loader_exit:
	return retval;
}
static void cyttsp_firmware_cont(const struct firmware *fw, void *context)
{
	int retval = 0;
	struct device *dev = context;
	struct cyttsp *ts = dev_get_drvdata(dev);

	if (fw == NULL) {
		pr_err("%s: firmware not found\n", __func__);
		goto cyttsp_firmware_cont_exit;
	}

#ifdef CY_USE_WATCHDOG
	_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */

	mutex_lock(&ts->data_lock);

	retval = _cyttsp_firmware_class_loader(ts, fw->data, fw->size);
	if (retval < 0) {
		/* mark an error and then try restarting anyway */
		pr_err("%s: Fail firmware class loader r=%d\n",
			__func__, retval);
	} else {
		/* prevent warm startup from overwriting the force load */
		ts->ic_reflash_done = true;
	}

	retval = _cyttsp_startup(ts);
	if (retval < 0) {
		pr_info("%s: Fail _cyttsp_startup after fw load r=%d\n",
			__func__, retval);
	}

	mutex_unlock(&ts->data_lock);
	release_firmware(fw);

cyttsp_firmware_cont_exit:
	ts->waiting_for_fw = false;
	return;
}
static ssize_t cyttsp_ic_reflash_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"%s\n", ts->waiting_for_fw ?
		"Driver is waiting for firmware load" :
		"No firmware loading in progress");
}
static ssize_t cyttsp_ic_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int i = 0;
	int retval = 0;
	struct cyttsp *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->data_lock);
	if (ts->waiting_for_fw) {
		pr_err("%s: Driver is already waiting for firmware\n",
			__func__);
		retval = -EALREADY;
		goto cyttsp_ic_reflash_store_exit;
	}

	/*
	 * must configure FW_LOADER in .config file
	 * CONFIG_HOTPLUG=y
	 * CONFIG_FW_LOADER=y
	 * CONFIG_FIRMWARE_IN_KERNEL=y
	 * CONFIG_EXTRA_FIRMWARE=""
	 * CONFIG_EXTRA_FIRMWARE_DIR=""
	 */

	if (size > CY_BL_FW_NAME_SIZE) {
		pr_err("%s: Filename too long\n", __func__);
		retval = -ENAMETOOLONG;
		goto cyttsp_ic_reflash_store_exit;
	} else {
		/*
		 * name string must be in alloc() memory
		 * or is lost on context switch
		 * strip off any line feed character(s)
		 * at the end of the buf string
		 */
		for (i = 0; buf[i]; i++) {
			if (buf[i] < ' ')
				ts->fwname[i] = 0;
			else
				ts->fwname[i] = buf[i];
		}
	}

	pr_info("%s: Enabling firmware class loader\n", __func__);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	retval = request_firmware_nowait(THIS_MODULE,
		FW_ACTION_NOHOTPLUG, (const char *)ts->fwname, ts->dev,
		GFP_KERNEL, ts->dev, cyttsp_firmware_cont);
#else /* Kernel 2.6.32 */
	retval = request_firmware_nowait(THIS_MODULE,
		FW_ACTION_NOHOTPLUG, (const char *)ts->fwname, ts->dev,
		ts->dev, cyttsp_firmware_cont);
#endif
	if (retval < 0) {
		pr_err("%s: Fail request firmware class file load\n",
			__func__);
		ts->waiting_for_fw = false;
		goto cyttsp_ic_reflash_store_exit;
	} else {
		ts->waiting_for_fw = true;
		retval = size;
	}

cyttsp_ic_reflash_store_exit:
	mutex_unlock(&ts->data_lock);
	return retval;
}
static DEVICE_ATTR(ic_reflash, S_IRUSR | S_IWUSR,
	cyttsp_ic_reflash_show, cyttsp_ic_reflash_store);
#endif

#ifdef CONFIG_TOUCHSCREEN_DEBUG
/* Driver debugging */
static ssize_t cyttsp_drv_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Debug Setting: %u\n", ts->bus_ops->tsdebug);
}
static ssize_t cyttsp_drv_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	int retval = 0;
	unsigned long value = 0;

	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		pr_err("%s: Failed to convert value\n", __func__);
		goto cyttsp_drv_debug_store_exit;
	}

	switch (value) {
	case CY_DBG_LVL_0:
	case CY_DBG_LVL_1:
	case CY_DBG_LVL_2:
	case CY_DBG_LVL_3:
		pr_info("%s: Debug setting=%d\n", __func__, (int)value);
		ts->bus_ops->tsdebug = value;
		break;
#ifdef CY_USE_DEBUG_TOOLS
	case CY_DBG_SUSPEND:
		retval = cyttsp_suspend(ts);
		pr_info("%s: SUSPEND (ts=%p) r=%d\n", __func__, ts, retval);
		break;
	case CY_DBG_RESUME:
		retval = cyttsp_resume(ts);
		pr_info("%s: RESUME (ts=%p) r=%d\n", __func__, ts, retval);
		break;
	case CY_DBG_RESTART:
		pr_info("%s: RESTART (ts=%p)\n",
			__func__, ts);
		_cyttsp_queue_startup(ts, false);
		break;
#endif
	default:
		ts->bus_ops->tsdebug = CY_DBG_LVL_MAX;
		pr_err("%s: Invalid debug setting; set to max=%d\n",
			__func__, ts->bus_ops->tsdebug);
		break;
	}

	retval = size;

cyttsp_drv_debug_store_exit:
	return retval;
}
static DEVICE_ATTR(drv_debug, S_IRUSR | S_IWUSR,
	cyttsp_drv_debug_show, cyttsp_drv_debug_store);
#endif

#ifdef CY_USE_REG_ACCESS
#define CY_RW_REGID_MAX             0xFF
#define CY_RW_REG_DATA_MAX          0xFF
static ssize_t cyttsp_drv_rw_regid_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Current Read/Write Regid=%02X(%d)\n",
		ts->rw_regid, ts->rw_regid);
}
static ssize_t cyttsp_drv_rw_regid_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	int retval = 0;
	unsigned long value = 0;

	mutex_lock(&ts->data_lock);
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		retval = strict_strtoul(buf, 16, &value);
		if (retval < 0) {
			pr_err("%s: Failed to convert value\n",
				__func__);
			goto cyttsp_drv_rw_regid_store_exit;
		}
	}

	if (value > CY_RW_REGID_MAX) {
		ts->rw_regid = CY_RW_REGID_MAX;
		pr_err("%s: Invalid Read/Write Regid; set to max=%d\n",
			__func__, ts->rw_regid);
	} else
		ts->rw_regid = value;

	retval = size;

cyttsp_drv_rw_regid_store_exit:
	mutex_unlock(&ts->data_lock);
	return retval;
}
static DEVICE_ATTR(drv_rw_regid, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
	cyttsp_drv_rw_regid_show, cyttsp_drv_rw_regid_store);

static ssize_t cyttsp_drv_rw_reg_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	int retval = 0;
	u8 reg_data = 0;

	retval = ttsp_read_block_data(ts,
		CY_REG_BASE + ts->rw_regid, sizeof(reg_data), &reg_data);

	if (retval < 0)
		return snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Read/Write Regid(%02X(%d) Failed\n",
			ts->rw_regid, ts->rw_regid);
	else
		return snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Read/Write Regid=%02X(%d) Data=%02X(%d)\n",
			ts->rw_regid, ts->rw_regid, reg_data, reg_data);
}
static ssize_t cyttsp_drv_rw_reg_data_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	int retval = 0;
	unsigned long value = 0;
	u8 regdata = 0;

	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		retval = strict_strtoul(buf, 16, &value);
		if (retval < 0) {
			pr_err("%s: Failed to convert value\n",
				__func__);
			goto cyttsp_drv_rw_reg_data_store_exit;
		}
	}

	if (value > CY_RW_REG_DATA_MAX) {
		pr_err("%s: Invalid Register Data Range; no write\n",
			__func__);
	} else {
		regdata = (u8)value;
		if (ts->rw_regid == CY_REG_BASE &&
			!(regdata & (CY_SOFT_RESET_MODE |
			CY_DEEP_SLEEP_MODE))) {
			mutex_lock(&ts->data_lock);
			if (GET_HSTMODE(regdata) ==
				GET_HSTMODE(CY_OPERATE_MODE)) {
				retval = _cyttsp_set_operational_mode(ts);
				if (retval < 0) {
					pr_err("%s: Fail set operating mode.\n",
						__func__);
				} else {
					_cyttsp_change_state(ts,
						CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
					_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				}
			} else if (GET_HSTMODE(regdata) ==
				GET_HSTMODE(CY_SYSINFO_MODE))  {
#ifdef CY_USE_WATCHDOG
				_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				retval = _cyttsp_set_sysinfo_mode(ts);
				if (retval < 0) {
					pr_err("%s: Fail set sysinfo mode.\n",
						__func__);
				}
			}
			else if (GET_HSTMODE(regdata) ==
				GET_HSTMODE(CYTTSP3_TEST_MODE)){/*test mode read capacitor value*/
				retval = ttsp_write_block_data(ts, ts->rw_regid,
					sizeof(regdata), &regdata);
				if (retval < 0) {
					pr_err("%s:Failed write to Regid=%02X(%d)"
						" Data=%02X(%d)\n", __func__,
						ts->rw_regid, ts->rw_regid,
						regdata, regdata);
				}
			}
			mutex_unlock(&ts->data_lock);
		} else {
			retval = ttsp_write_block_data(ts, ts->rw_regid,
				sizeof(regdata), &regdata);
			if (retval < 0) {
				pr_err("%s: Failed write to Regid=%02X(%d)"
					" Data=%02X(%d)\n", __func__,
					ts->rw_regid, ts->rw_regid,
					regdata, regdata);
			}
#ifdef CY_ENABLE_CA
			if ((ts->power_state == CY_ACTIVE_STATE) &&
				(ts->rw_regid == CY_REG_OP_FEATURE)) {
				if (regdata & CY_CA_BIT)
					ts->ca_enabled = true;
				else
					ts->ca_enabled = false;
			}
#endif
		}
	}

	retval = size;

cyttsp_drv_rw_reg_data_store_exit:
	return retval;
}
static DEVICE_ATTR(drv_rw_reg_data, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
	cyttsp_drv_rw_reg_data_show, cyttsp_drv_rw_reg_data_store);
#endif

#ifdef CYTTSP3_CORE_CAPACITANCE_TEST_SUPPORT
#define CY_I2C_DATA_SIZE  (128) /*tp i2c transfer limit*/
typedef enum{
	SystemInformationMode,
	GetMutualDataMode,
	GetSelfDataMode,
	BulkReadMode,
	OperationalMode,
}cyttsp_reg_state; /*states of the TP to switch*/

static struct device * access_dev = NULL; /*save dev node for easy access*/

static int _cyttsp_validate_cap_value(unsigned char *buf, 
											unsigned char min, 
											unsigned char max,
											int count)
{
	int i = 0;

	for (;i<count;++i)
	{
		if (buf[i] < min || buf[i] > max)
		{
			pr_err("%s: failed on: buf[%d]=%d\n",__func__,i, buf[i]);
			return -1;
		}
	}

	return 0;
}

static int _cyttsp_set_regstate(cyttsp_reg_state state)
{
	int ret=0;
	char *regid;
	char *regdata;
	struct device *dev = access_dev;

	/*set register and command value based on the state*/
	switch(state)
	{
		case SystemInformationMode:
			regid = "0";
			regdata = "0x10";
			break;
		case GetMutualDataMode:
			regid = "0x1C";
			regdata = "0x01";
			break;
		case GetSelfDataMode:
			regid = "0x1C";
			regdata = "0x02";
			break;
		case BulkReadMode:
			regid = "0";
			regdata = "0x40";
			break;
		case OperationalMode:
		default:
			regid = "0";
			regdata = "0x00";
			break;
	}

	/*set the register id*/
	ret = cyttsp_drv_rw_regid_store(dev, NULL, regid, strlen(regid));
	if (ret <= 0) {
		pr_err("%s: Error in cyttsp_drv_rw_regid_store ret=%d\n", __func__, ret);
		return 1;
	}

	/*send command to register set*/
	ret = cyttsp_drv_rw_reg_data_store(dev, NULL, regdata, strlen(regdata));
	if (ret <= 0) {
		pr_err("%s: Error in cyttsp_drv_rw_reg_data_store ret=%d\n", __func__, ret);
		return 1;
	}

	return 0;
}

static int _cyttsp_get_cap_data(cyttsp_reg_state state, int data_required, char* buf, int start_reg)
{
	int rc = 0;
	struct cyttsp *ts = dev_get_drvdata(access_dev);
	int current_read_len= 0;
	int total_read_len= 0;

	/* set the TP as test mode */
	if (_cyttsp_set_regstate(SystemInformationMode))
		goto exit;

	/*give 100 ms to stablize the state for IC*/
	msleep(100);

	/*set state as requested by caller */
	if (_cyttsp_set_regstate(state))
		goto exit;

	/*set state to dump the data*/
	if (_cyttsp_set_regstate(BulkReadMode))
		goto exit;

	/*give 100 ms to stablize the state for IC*/
	msleep(100);

	/*loop till we need the data*/
	while (data_required > 0)
	{
		/*get the data which can be read in one time*/
		current_read_len = (data_required > CY_I2C_DATA_SIZE)?CY_I2C_DATA_SIZE:data_required;

		cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: datapendingsize=%d\n",__func__,data_required);
		/*read the block data*/
		rc = ttsp_read_block_data(ts, 
			CY_REG_BASE + start_reg + total_read_len + ts->rw_regid, 
			current_read_len, 
			buf);

		/*if the read data fail, do not try to read more data*/
		if (0 > rc)
		{
			pr_err("%s: the data is incorrect rc=%d\n",__func__,rc);
			break;
		}

		/*update the seek variables*/
		total_read_len += current_read_len;
		buf = buf + total_read_len;
		data_required-=current_read_len;
	}
exit:
	/* set the TP as operational mode */
	rc = _cyttsp_set_regstate(OperationalMode);
	if(rc > 0)
		pr_err("%s,set the register id to %d or send command to register set Failed\n",__func__, OperationalMode);
	msleep(100);

	pr_debug("%s: total_read_len=%d\n",__func__,total_read_len);
	return total_read_len;
}

static ssize_t cyttsp_touch_mmi_test_show(struct kobject *kobj, 
	struct kobj_attribute *attr,char *buf)
{
	int rc = 0, test_state=0;
	int i = 0, j = 0;
	static unsigned char *touch_capacitance = NULL;
	tp_test_data *test_data = NULL;
	int max_cap_lines = 0;
	struct cyttsp *ts = dev_get_drvdata(access_dev);

	/*check if the kobj is null return*/
	if (kobj == NULL)
	{
		pr_err("%s: touch_mmi_test dev is null\n", __func__);
		return -EINVAL;
	}

	/*as its only for testing always get the value and not make it static*/
	get_tp_test_config(&test_data);

	/*debug the values of TP*/
	cyttsp_dbg(ts, CY_DBG_LVL_3, "tx_lines=%d\n", test_data->tx_lines);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "rx_lines=%d\n", test_data->rx_lines);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "min_mut_cap=%d\n", test_data->min_mut_cap);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "max_mut_cap=%d\n", test_data->max_mut_cap);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "min_self_cap=%d\n", test_data->min_self_cap);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "max_self_cap=%d\n", test_data->max_self_cap);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "first_reg_num=%d\n", test_data->first_reg_num);
	cyttsp_dbg(ts, CY_DBG_LVL_3, "ignore_reg_count=%d\n", test_data->ignore_reg_count);

	max_cap_lines = (test_data->tx_lines * test_data->rx_lines);

	/*if already executing function exit, as functions can execute multiple time
	but the register can be set just once*/
	if (NULL == touch_capacitance)
	{
		touch_capacitance = kzalloc(sizeof(char) * 
			(max_cap_lines + test_data->rx_lines + 1), GFP_KERNEL);
		if (NULL == touch_capacitance)
		{
			pr_err("%s: allocate memory failed\n", __func__);
			return -EINVAL;
		}
	}
	else
	{
		pr_err("%s: already processing TP capacitance\n", __func__);
		return -EINVAL;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: get the capacitance of TP start\n",__func__);

	/*reset the touch_capacitance */
	memset(touch_capacitance, 0, max_cap_lines + test_data->rx_lines);

	mutex_lock(&ts->data_lock);

	switch(ts->power_state)
	{
		case CY_READY_STATE:
		case CY_ACTIVE_STATE:
		case CY_LOW_PWR_STATE:
			break;
		case CY_IDLE_STATE:
		case CY_SLEEP_STATE:
		case CY_BL_STATE:
		case CY_LDR_STATE:
		case CY_SYSINFO_STATE:
		case CY_INVALID_STATE:
		default:
			pr_err("%s: Can't test TP in state:%d \n", __func__, ts->power_state);
			rc = sprintf(buf, "Fail\n");
			mutex_unlock(&ts->data_lock);
			kfree(touch_capacitance);
			touch_capacitance = NULL;
			return strlen(buf);
	}

	mutex_unlock(&ts->data_lock);

	/*get the mutual capacitance*/
	rc = _cyttsp_get_cap_data(GetMutualDataMode, 
								max_cap_lines, 
								touch_capacitance,
								test_data->first_reg_num);
	if(rc <= 0)
	{
		test_state = 1;
		pr_err("%s: failed to get mutual capacitance\n", __func__);
	}
	else
	{
		/*validate the mutual capacitance value*/
		if (0 != _cyttsp_validate_cap_value(
				touch_capacitance, 
				test_data->min_mut_cap - test_data->mut_tolerance, 
				test_data->max_mut_cap + - test_data->mut_tolerance, /*skip last value as some TP does not have it*/
				max_cap_lines - test_data->ignore_reg_count))
		{
			test_state = 1;
			pr_err("%s: validation fail 1\n", __func__);
		}
	}

	/*get the self capacitance value*/
	rc = _cyttsp_get_cap_data(GetSelfDataMode, 
								test_data->rx_lines, 
								touch_capacitance + max_cap_lines,
								test_data->first_reg_num);
	if (rc <= 0)
	{
		test_state = 1;
		pr_err("%s: failed to get self capacitance\n", __func__);
	}
	else
	{
		/*validate the mutual capacitance value*/
		if (0 != _cyttsp_validate_cap_value(
			touch_capacitance + max_cap_lines,
			test_data->min_self_cap - test_data->self_tolerance,
			test_data->max_self_cap + test_data->self_tolerance,
			test_data->rx_lines))
		{
			test_state = 1;
			pr_err("%s: validation fail 2\n", __func__);
		}
	}

	if (test_state)
		rc = sprintf(buf, "FAIL\n");
	else
		rc = sprintf(buf, "PASS\n");

	/*in case of error, return the available value, others will be 0*/
	for (i = 0; i < test_data->rx_lines; ++i)
	{
		for (j = 0; j < test_data->tx_lines; ++j)
		{
			sprintf(buf, "%s%d\t", buf, (touch_capacitance+(i*test_data->tx_lines))[j]);
		}
		sprintf(buf, "%s\n", buf);
	}

	sprintf(buf, "%s\n", buf);

	/*put the self capacitance*/
	for (i = 0; i < test_data->rx_lines; ++i)
	{
		sprintf(buf, "%s%d\t", buf, (touch_capacitance + max_cap_lines)[i]);
		if (test_data->tx_lines == (i+1))
			sprintf(buf, "%s\n", buf);
	}

	for (i = 0; i < max_cap_lines; ++i)
		cyttsp_dbg(ts, CY_DBG_LVL_3, "reg:%d:::%d\n", 7+i, touch_capacitance[i]);

	for (i = 0; i < test_data->rx_lines; ++i)
		cyttsp_dbg(ts, CY_DBG_LVL_3, "reg:%d:::%d\n", 7+i, touch_capacitance[i+(max_cap_lines)]);

	rc = strlen(buf);
	pr_info("The TP capacitance data is: %s \nlen=%d\n", buf , rc);
	kfree(touch_capacitance);
	touch_capacitance = NULL;

	return rc;
}

static struct kobj_attribute touch_mmi_test_attribute = {
	.attr = {.name = "touch_mmi_test", .mode = 0664},
	.show = cyttsp_touch_mmi_test_show,
	.store = NULL,
};

#endif

static void cyttsp_ldr_init(struct cyttsp *ts)
{
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	if (device_create_file(ts->dev, &dev_attr_drv_debug))
		pr_err("%s: Cannot create drv_debug\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_drv_flags))
		pr_err("%s: Cannot create drv_flags\n", __func__);
#endif

#ifdef CYTTSP3_CORE_CAPACITANCE_TEST_SUPPORT
	access_dev = ts->dev;
#endif

	if (device_create_file(ts->dev, &dev_attr_drv_irq))
		pr_err("%s: Cannot create drv_irq\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_drv_stat))
		pr_err("%s: Cannot create drv_stat\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_drv_ver))
		pr_err("%s: Cannot create drv_ver\n", __func__);

#ifdef CONFIG_TOUCHSCREEN_DEBUG
	if (device_create_file(ts->dev, &dev_attr_hw_irqstat))
		pr_err("%s: Cannot create hw_irqstat\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_hw_reset))
		pr_err("%s: Cannot create hw_reset\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_hw_recov))
		pr_err("%s: Cannot create hw_recov\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_ic_grpdata))
		pr_err("%s: Cannot create ic_grpdata\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_ic_grpnum))
		pr_err("%s: Cannot create ic_grpnum\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_ic_grpoffset))
		pr_err("%s: Cannot create ic_grpoffset\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_ic_reflash))
		pr_err("%s: Cannot create ic_reflash\n", __func__);
#endif

	if (device_create_file(ts->dev, &dev_attr_ic_ver))
		pr_err("%s: Cannot create ic_ver\n", __func__);

#ifdef CY_USE_REG_ACCESS
	if (device_create_file(ts->dev, &dev_attr_drv_rw_regid))
		pr_err("%s: Cannot create drv_rw_regid\n", __func__);

	if (device_create_file(ts->dev, &dev_attr_drv_rw_reg_data))
		pr_err("%s: Cannot create drv_rw_reg_data\n", __func__);
#endif

	return;
}

static void cyttsp_ldr_free(struct cyttsp *ts)
{
	device_remove_file(ts->dev, &dev_attr_drv_irq);
	device_remove_file(ts->dev, &dev_attr_drv_ver);
	device_remove_file(ts->dev, &dev_attr_drv_stat);
	device_remove_file(ts->dev, &dev_attr_ic_ver);
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	device_remove_file(ts->dev, &dev_attr_drv_debug);
	device_remove_file(ts->dev, &dev_attr_drv_flags);
	device_remove_file(ts->dev, &dev_attr_hw_irqstat);
	device_remove_file(ts->dev, &dev_attr_hw_reset);
	device_remove_file(ts->dev, &dev_attr_hw_recov);
	device_remove_file(ts->dev, &dev_attr_ic_grpdata);
	device_remove_file(ts->dev, &dev_attr_ic_grpnum);
	device_remove_file(ts->dev, &dev_attr_ic_grpoffset);
	device_remove_file(ts->dev, &dev_attr_ic_reflash);
#endif
#ifdef CY_USE_REG_ACCESS
	device_remove_file(ts->dev, &dev_attr_drv_rw_regid);
	device_remove_file(ts->dev, &dev_attr_drv_rw_reg_data);
#endif
}

static int _cyttsp_resume_sleep(struct cyttsp *ts)
{
	int retval = 0;
#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
	u8 sleep = CY_DEEP_SLEEP_MODE;

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: Put the part back to sleep\n", __func__);

	retval = ttsp_write_block_data(ts, CY_REG_BASE, sizeof(sleep), &sleep);
	if (retval < 0) {
		pr_err("%s: Failed to write sleep bit r=%d\n",
			__func__, retval);
	} else {
#ifdef CY_USE_WATCHDOG
		_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
	}
#endif
	return retval;
}

static void _cyttsp_firmware_get_tp_fw_file_name(char *name, int len)
{
	/*erase the old name*/
	name[0] = '\0';

	/*read the firmware file name from product config*/
	get_tp_fw_file_name(name, len);

	/*If the product config does not have the file name use default name*/
	if ('\0' != name[0]
		&& (strlen(name) < (CY_FW_MAX_FILE_LEN - strlen(CY_FW_FILE_EXT))))
	{
		/*hsad can't have . in value so append the ext in driver*/
		/*we already know strlen(name) plus strlen(CY_FW_FILE_EXT) will less than 
		  the whole size of name,so I use length of CY_FW_FILE_EXT directly*/
		strncat(name, CY_FW_FILE_EXT, strlen(CY_FW_FILE_EXT));
	}
	else
	{
		strncpy(name, CY_FW_FILE_NAME, CY_FW_MAX_FILE_LEN);
	}

	pr_info("%s TP FW file is: %s\n",__func__, name);
	return;
}

/*Add by huawei begin*/
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
/*
 *When upgrading firmware by user, please make sure that 
 *the bin file put into the /system/etc/firmware directory 
 *is named exactly as "cyttsp3_fw.bin".
 */
//static int _cyttsp_set_auto_fw_upgrade(struct cyttsp *ts) //modify by huawei
static void _cyttsp_set_auto_fw_upgrade(struct work_struct *fw_upgrade) //modify by huawei
{
    int retval = 0;

	struct cyttsp *ts = container_of(fw_upgrade,struct cyttsp, fw_upgrade);
    struct device *dev = ts->dev;

	mutex_lock(&ts->data_lock);
    /*
     * Prevent forced old firmware
     * from being re-written by .bin firmware
     * on warm startup.
     * Cold startup will restore old firmware
     * to new .bin firmware
     */
    if (ts->auto_fw_upgrade_done) {
            cyttsp_dbg(ts, CY_DBG_LVL_3,
                    "%s: Prevent writing over old firmware\n",
                    __func__);
            goto _cyttsp_set_auto_fw_upgrade_exit;
    }

	if (ts->waiting_for_fw) {
		pr_err("%s: Driver is already waiting for firmware\n",
			__func__);
		retval = -EALREADY;
		goto _cyttsp_set_auto_fw_upgrade_exit;
	}
	
    if (ts->power_state == CY_SLEEP_STATE) {
            pr_err("%s: cannot upgrade firmware in %s state\n",
                    __func__, cyttsp_powerstate_string[ts->power_state]);
            goto _cyttsp_set_auto_fw_upgrade_exit;
    }

    _cyttsp_firmware_get_tp_fw_file_name(fw_file_name, sizeof(fw_file_name));

    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
    retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    fw_file_name, dev, GFP_KERNEL, ts->dev,
                    _cyttsp_firmware_cont_builtin);
    #else /* Kernel 2.6.32 */
    retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    fw_file_name, dev, ts->dev,
                    _cyttsp_firmware_cont_builtin);
    #endif

    if (retval < 0) {
        pr_err("%s: Fail request firmware class file load\n",
                __func__);
		ts->waiting_for_fw = false;
		goto _cyttsp_set_auto_fw_upgrade_exit;
	} else {
		ts->waiting_for_fw = true;
	}

_cyttsp_set_auto_fw_upgrade_exit:
	mutex_unlock(&ts->data_lock);
    return;
}

/* Force firmware upgrade */
static int _cyttsp_firmware_class_auto_loader(struct cyttsp *ts,
	const u8 *data, int size)
{
	int retval = 0;
	const u8 *img = NULL;
	int img_size = 0;

	if ((data == NULL) || (size == 0)) {
		pr_err("%s: empty firmware image data=%p size=%d\n",
			__func__, data, size);
		_cyttsp_change_state(ts, CY_IDLE_STATE);
		goto _cyttsp_firmware_class_auto_loader_exit;
	} else {
		img = data + data[0] + 1;
		img_size = size - (data[0] + 1);

		retval = _cyttsp_hard_reset(ts);
		if (retval < 0) {
			pr_err("%s: Fail reset device on loader\n", __func__);
			goto _cyttsp_firmware_class_auto_loader_exit;
		}

		retval = _cyttsp_load_bl_regs(ts);
		if (retval < 0) {
			pr_err("%s: Fail read bootloader\n", __func__);
			goto _cyttsp_firmware_class_auto_loader_exit;
		}

		cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: call load_app\n", __func__);
		retval = _cyttsp_load_app(ts, img, img_size);
		if (retval < 0)
			pr_err("%s: fail on load fw r=%d\n", __func__, retval);

		//modify by huawei
		/* reset TTSP Device back to bootloader mode */
		retval = _cyttsp_hard_reset(ts);
		if (retval < 0) {
			pr_err("%s: Fail on hard reset r=%d\n",
				__func__, retval);
		}
		ts->auto_fw_upgrade_done = true; 
		/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
		ts->need_force_recalibration = true;
#endif

		retval = _cyttsp_exit_bl_mode(ts);
		if (retval < 0) {
			pr_err("%s: Fail on exit bootloader r=%d\n", __func__, retval);
			goto _cyttsp_firmware_class_auto_loader_exit;
		}
		//modify by huawei
	}

_cyttsp_firmware_class_auto_loader_exit:
	return retval;
}

static void _cyttsp_firmware_cont_builtin(const struct firmware *fw, void *context) 
{
	int retval = 0;
	//struct cyttsp3_device *ttsp = context;
    struct device *dev = context;
    struct cyttsp *ts = dev_get_drvdata(dev);
    int upgrade;

    if (fw == NULL) {
            pr_info("%s: No firmware bin file\n", __func__);
            goto _cyttsp_firmware_cont_builtin_exit;
    }

    if (fw->data == NULL || fw->size == 0) {
            pr_err("%s: Invalid firmware\n", __func__);
            goto _cyttsp_firmware_cont_builtin_exit;
    }

    pr_info("%s: Found firmware\n", __func__);

	/*Firmware upgrade when startup fial or new version */
	mutex_lock(&ts->data_lock);
	retval = _cyttsp_startup(ts);
	if (retval < 0) 
	{
		pr_err("%s: Fail _cyttsp_startup after fw load r=%d\n",
			__func__, retval);
		if (ts->power_state != CY_IDLE_STATE)
			_cyttsp_change_state(ts, CY_IDLE_STATE);
		ts->powered = false;
	} 
	else
	{
		ts->powered = true;
	}
    // Update firmware if new version is detected
    upgrade = cyttsp_check_firmware_version_builtin(dev, fw);
    if ((false == ts->powered) || upgrade) {
        #ifdef CY_USE_WATCHDOG
                _cyttsp_stop_watchdog_timer(ts);
        #endif /* --CY_USE_WATCHDOG */

        retval = _cyttsp_firmware_class_auto_loader(ts, fw->data, fw->size);
        if (retval < 0) {
                /* mark an error and then try restarting anyway */
                pr_err("%s: Fail firmware class loader r=%d\n",
                        __func__, retval);
        }

        retval = _cyttsp_startup(ts);
/*G526 cypress TP update firmware disturb by cyttsp_open*/
		if (retval < 0) 
		{
			pr_err("%s: Fail _cyttsp_startup after fw load r=%d\n",
				__func__, retval);
			if (ts->power_state != CY_IDLE_STATE)
				_cyttsp_change_state(ts, CY_IDLE_STATE);
			ts->powered = false;
		} 
		else
		{
			ts->powered = true;
		}

		pr_info("%s: Powered ON(%d) r=%d\n",
			__func__, (int)ts->powered, retval);

    }
	mutex_unlock(&ts->data_lock);

_cyttsp_firmware_cont_builtin_exit:
    release_firmware(fw);
    ts->waiting_for_fw = false;
    return;
}

static int cyttsp_check_firmware_version_builtin(struct device *dev, const struct firmware *fw) 
{
    bool new_vers = false;
    struct cyttsp *ts = dev_get_drvdata(dev);
    u8 fw_app_verh;
    u8 fw_app_verl;

    fw_app_verh = *(fw->data + 5);
    fw_app_verl = *(fw->data + 6);
#ifdef CY_ANY_DIFF_NEW_VER
	new_vers =
			(((u16)fw_app_verh << 8) +
			(u16)fw_app_verl) !=
			(((u16)ts->bl_data.appver_hi << 8) +
			(u16)ts->bl_data.appver_lo); 
#else
	new_vers =
			(((u16)fw_app_verh << 8) +
			(u16)fw_app_verl) >
			(((u16)ts->bl_data.appver_hi << 8) +
			(u16)ts->bl_data.appver_lo); 
#endif

    if (!IS_VALID_APP(ts->bl_data.bl_status) || new_vers)
            return 1;
    else
            return 0;
}
#endif

static ssize_t update_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf)
{
	return 1;
}

static ssize_t update_firmware_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	struct cyttsp *ts = g_ts;
	
	mutex_lock(&ts->data_lock);
	
	printk("#################update_firmware_store######################\n");

	if ( (buf[0] == '2')&&(buf[1] == '\0') )
	{
		/*
		 * Prevent forced old firmware
		 * from being re-written by .bin firmware
		 * on warm startup.
		 * Cold startup will restore old firmware
		 * to new .bin firmware
		 */
		/*Modify for developer debug*/
		/*if (ts->auto_fw_upgrade_done) {
				cyttsp_dbg(ts, CY_DBG_LVL_3,
						"%s: Prevent writing over old firmware\n",
						__func__);
				goto cyttsp_ic_reflash_store_exit;
		}*/
		
		if (ts->waiting_for_fw) {
			pr_err("%s: Driver is already waiting for firmware\n",
				__func__);
			retval = -EALREADY;
			goto cyttsp_ic_reflash_store_exit;
		}
		
		if (ts->power_state == CY_SLEEP_STATE) {
			if (ts->irq_enabled)
				enable_irq(ts->irq);
				printk("%s: upgrade firmware in %s state\n",
						__func__, cyttsp_powerstate_string[ts->power_state]);
				//pr_err("%s: cannot upgrade firmware in %s state\n",
				//		__func__, cyttsp_powerstate_string[ts->power_state]);
				//goto cyttsp_ic_reflash_store_exit;
		}
		
		/*
		 * must configure FW_LOADER in .config file
		 * CONFIG_HOTPLUG=y
		 * CONFIG_FW_LOADER=y
		 * CONFIG_FIRMWARE_IN_KERNEL=y
		 * CONFIG_EXTRA_FIRMWARE=""
		 * CONFIG_EXTRA_FIRMWARE_DIR=""
		 */
	
		pr_info("%s: Enabling firmware class loader\n", __func__);

		_cyttsp_firmware_get_tp_fw_file_name(fw_file_name, sizeof(fw_file_name));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
			retval = request_firmware_nowait(THIS_MODULE,
				FW_ACTION_HOTPLUG, fw_file_name, ts->dev,
				GFP_KERNEL, ts->dev, cyttsp_firmware_cont);
#else /* Kernel 2.6.32 */
			retval = request_firmware_nowait(THIS_MODULE,
				FW_ACTION_HOTPLUG, fw_file_name, ts->dev,
				ts->dev, cyttsp_firmware_cont);
#endif
			if (retval < 0) {
				pr_err("%s: Fail request firmware class file load\n",
					__func__);
				ts->waiting_for_fw = false;
				goto cyttsp_ic_reflash_store_exit;
			} else {
				ts->waiting_for_fw = true;
			}
		
	}
	
cyttsp_ic_reflash_store_exit:
	mutex_unlock(&ts->data_lock);
	return retval;
 }

static int _cyttsp_firmware_file(void)
{
	int retval;
	struct kobject *kobject_ts;
	kobject_ts = kobject_create_and_add("touch_screen", NULL);
	if (!kobject_ts)
	{
		printk("create kobjetct error!\n");
		return -1;
	}
	
	retval = sysfs_create_file(kobject_ts, &update_firmware_attribute.attr);
	if (retval)
	{
		kobject_put(kobject_ts);
		printk("create file error\n");
		return -1;
	}

#ifdef CYTTSP3_CORE_CAPACITANCE_TEST_SUPPORT
	/*add the node touch_mmi_test for mmi test apk*/
	retval = sysfs_create_file(kobject_ts, &touch_mmi_test_attribute.attr);
	if (retval)
	{
		kobject_put(kobject_ts);
		printk("touch_mmi_test create file error\n");
		return -1;
	}
#endif

	return 0;	
}

/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
static int _cyttsp_drv_write_regid(struct cyttsp *ts,unsigned long value)
{
	int retval = 0;

	#if HUAWEI_RW_REG
	mutex_lock(&ts->data_lock);
	#endif
	/*
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		retval = strict_strtoul(buf, 16, &value);
		if (retval < 0) {
			pr_err("%s: Failed to convert value\n",
				__func__);
			goto cyttsp_drv_rw_regid_store_exit;
		}
	}
	*/

	if (value > CY_RW_REGID_MAX) {
		ts->rw_regid = CY_RW_REGID_MAX;
		cyttsp_dbg(ts, CY_DBG_LVL_0,"%s: Invalid Read/Write Regid; set to max=%d\n",
			__func__, ts->rw_regid);
		retval = -1;
	} else
		ts->rw_regid = value;

	#if HUAWEI_RW_REG
	mutex_unlock(&ts->data_lock);
	#endif
	return retval;
}

static int _cyttsp_drv_read_reg_data(struct cyttsp *ts)
{
	int retval = 0;
	u8 reg_data = 0;

	retval = ttsp_read_block_data(ts,
		CY_REG_BASE + ts->rw_regid, sizeof(reg_data), &reg_data);

	if (retval < 0){
		cyttsp_dbg(ts, CY_DBG_LVL_0,
			"%s:Read/Write Regid(%02X(%d) Failed\n",
			__func__, ts->rw_regid, ts->rw_regid);
		return retval;
	}else
		cyttsp_dbg(ts, CY_DBG_LVL_0,
			"%s:Read/Write Regid=%02X(%d) Data=%02X(%d)\n",
			__func__, ts->rw_regid, ts->rw_regid, reg_data, reg_data);
	return ((int)reg_data);

}
static int _cyttsp_drv_write_reg_data(struct cyttsp *ts,unsigned long value)
{
	int retval = 0;
	//unsigned long value = 0;
	u8 regdata = 0;

	/*
	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		retval = strict_strtoul(buf, 16, &value);
		if (retval < 0) {
			pr_err("%s: Failed to convert value\n",
				__func__);
			goto cyttsp_drv_rw_reg_data_store_exit;
		}
	}
	*/

	if (value > CY_RW_REG_DATA_MAX) {
		pr_err("%s: Invalid Register Data Range; no write\n",
			__func__);
		return -1;
	} else {
		regdata = (u8)value;
		if (ts->rw_regid == CY_REG_BASE &&
			!(regdata & (CY_SOFT_RESET_MODE |
			CY_DEEP_SLEEP_MODE))) {
#if HUAWEI_RW_REG
			mutex_lock(&ts->data_lock);
#endif
			if (GET_HSTMODE(regdata) ==
				GET_HSTMODE(CY_OPERATE_MODE)) {
				retval = _cyttsp_set_operational_mode(ts);
				if (retval < 0) {
					pr_err("%s: Fail set operating mode.\n",
						__func__);
				} else {
					_cyttsp_change_state(ts,
						CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
					_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				}
			} else if (GET_HSTMODE(regdata) ==
				GET_HSTMODE(CY_SYSINFO_MODE))  {
#ifdef CY_USE_WATCHDOG
				_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
				retval = _cyttsp_set_sysinfo_mode(ts);
				if (retval < 0) {
					pr_err("%s: Fail set sysinfo mode.\n",
						__func__);
#if HUAWEI_RW_REG
					mutex_unlock(&ts->data_lock);
#endif
					return retval;
				}
			}
#if HUAWEI_RW_REG
			mutex_unlock(&ts->data_lock);
#endif
		} else {
			retval = ttsp_write_block_data(ts, ts->rw_regid,
				sizeof(regdata), &regdata);
			if (retval < 0) {
				cyttsp_dbg(ts, CY_DBG_LVL_0,"%s: Failed write to Regid=%02X(%d)"
					" Data=%02X(%d)\n", __func__,
					ts->rw_regid, ts->rw_regid,
					regdata, regdata);
				return retval;
			}
#ifdef CY_ENABLE_CA
			if ((ts->power_state == CY_ACTIVE_STATE) &&
				(ts->rw_regid == CY_REG_OP_FEATURE)) {
				if (regdata & CY_CA_BIT)
					ts->ca_enabled = true;
				else
					ts->ca_enabled = false;
			}
#endif
		}
	}

	return 0;
}
#endif /*HUAWEI_NEED_RECALIBRATION*/
/*Add by huawei end*/

static char * cyttsp_get_touch_module_name(void)
{
	int moudle_value = 0;
	char * module_name = NULL;

	moudle_value = get_tp_id();
	printk("gpio_value is %d \n",moudle_value);
	if(moudle_value < 0)
	{
		pr_err("%s: Get cyttsp gpio value [%d] fail!\n", __func__,moudle_value);
		return NULL;
	}
	else if(TP_COB_IDA == moudle_value)
	{
		pr_err("%s: Cyttsp tp not connected!\n", __func__);
		return NULL;
	}
	else if(TP_COB_ID8 == moudle_value)
	{
		module_name = "TRULY";
		printk("%s: %s!\n", __func__,module_name);
	}
	else if(TP_COB_ID0 == moudle_value)
	{
		module_name = "OFILM";
		printk("%s: %s!\n", __func__,module_name);
	}
	else if(TP_COB_ID2 == moudle_value)
	{
		module_name = "EELY";
		printk("%s: %s!\n", __func__,module_name);
	}

	return module_name;
}

/* named Rule
 * TMA340: Cypress-IC-Module.ver
 * for example: Cypress-TMA340-TRULY.2
 */
char * cyttsp_get_touch_info(void)
{
	u32 config_id = 0;
	char * module_name = NULL;
	struct cyttsp *ts = NULL;

	module_name = cyttsp_get_touch_module_name();
	if (NULL == module_name)
	{
		printk("cyttsp:module_name == NULL \n");
		return NULL;
	}

	ts = g_ts;
	if (NULL == &(ts->sysinfo_data))
	{
		printk("cyttsp:app version == NULL \n");
		return NULL;
	}
	config_id = (((u32)ts->sysinfo_data.app_verh << 8) +
				(u32)ts->sysinfo_data.app_verl); 
	
	/*The cyttsp3 driver only used for TMA340*/
	snprintf(touch_info, sizeof(touch_info), "Cypress-TMA340-%s.%d", module_name, config_id);

	return touch_info;
}
static int _cyttsp_startup(struct cyttsp *ts)
{
	int retval = 0;
	/*G526 need to restart touch IC for ESD problems*/
#if HUAWEI_ESD_NEED_RESTART
	int rst_gpio = 0;
#endif

#ifdef CY_USE_WATCHDOG
	ts->ntch_count = CY_NTCH;
	ts->prv_tch = CY_NTCH;
#endif /* --CY_USE_WATCHDOG */

	retval = _cyttsp_hard_reset(ts);
	if (retval < 0) {
		pr_err("%s: Fail on HW reset r=%d\n", __func__, retval);
		_cyttsp_change_state(ts, CY_INVALID_STATE);
		_cyttsp_queue_startup(ts, false);
		goto _cyttsp_startup_exit;
	}

#ifndef CY_NO_BOOTLOADER
	retval = _cyttsp_load_bl_regs(ts);
	if (retval < 0) {
		/*G526 need to restart touch IC for ESD problems*/
#if HUAWEI_ESD_NEED_RESTART
		if(ts->platform_data->get_touch_reset_gpio)
		{
			rst_gpio = ts->platform_data->get_touch_reset_gpio();
		}
		else
		{
			pr_err("%s: Function get_touch_reset_gpio is NULL! \n", __func__);
			goto _cyttsp_startup_exit;
		}		
		retval = gpio_request(rst_gpio, NULL);
		if (retval < 0) 
		{
			pr_err("%s: Fail request RST pin r=%d\n", __func__, retval);
			pr_err("%s: Try free RST gpio=%d\n", __func__,rst_gpio);
			gpio_free(rst_gpio);
			retval = gpio_request(rst_gpio, NULL);
			if (retval < 0)
			{
				pr_err("%s: Fail 2nd request RST pin r=%d\n", __func__,retval);
				goto _cyttsp_startup_exit;
			}
		}
		retval = gpio_tlmm_config(GPIO_CFG(rst_gpio,0, GPIO_CFG_OUTPUT, 
							GPIO_CFG_PULL_UP,GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		if (0 != retval)
		{
			pr_err("%s:touch rst gpio config failed\n", __func__);
			gpio_free(rst_gpio);
			goto _cyttsp_startup_exit;
		}
		
		gpio_direction_output(rst_gpio, 0);//can not power down if rst is high
		if (0 != retval)
		{
			pr_err("%s: failed to pull down rst pin! \n", __func__);
			gpio_free(rst_gpio);
			goto _cyttsp_startup_exit;
		}
		
		retval = gpio_direction_output(MSM_8930_TOUCH_POWER_EN_PIN,0);
		if (0 != retval)
		{
			pr_err("%s: failed to disable cypress vdd\n", __func__);
			gpio_free(rst_gpio);
			goto _cyttsp_startup_exit;
		}
		mdelay(2000);
		
		retval = gpio_direction_output(MSM_8930_TOUCH_POWER_EN_PIN,1);
		if (0 != retval)
		{
			pr_err("%s: failed to enable cypress vdd\n", __func__);
			gpio_free(rst_gpio);
			goto _cyttsp_startup_exit;
		}
		mdelay(100);
		
		gpio_direction_output(rst_gpio, 1);
		if (0 != retval)
		{
			pr_err("%s: failed to pull up rst pin! \n", __func__);
			gpio_free(rst_gpio);
			goto _cyttsp_startup_exit;
		}
		gpio_free(rst_gpio);

		retval = _cyttsp_hard_reset(ts);
		if (retval < 0)
		{
			pr_err("%s: Error on h/w reset2 r=%d\n", __func__, retval);
			goto _cyttsp_startup_exit;
		}
#else
		pr_err("%s: Fail on bl regs read r=%d\n", __func__, retval);
		_cyttsp_change_state(ts, CY_IDLE_STATE);
		goto _cyttsp_startup_exit;
#endif
	}

/*Add by huawei begin*/
#ifndef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
	retval = _cyttsp_boot_loader(ts);
	if (retval < 0) {
		pr_err("%s: Fail on boot loader r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;  /* We are now in IDLE state */
	}
#endif
/*Add by huawei end*/

	retval = _cyttsp_exit_bl_mode(ts);
	if (retval < 0) {
		pr_err("%s: Fail on exit bootloader r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;
	}

#endif

	retval = _cyttsp_set_sysinfo_mode(ts);
	if (retval < 0) {
		pr_err("%s: Fail on set sysinfo mode r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;
	}

	retval = _cyttsp_set_sysinfo_regs(ts);
	if (retval < 0) {
		pr_err("%s: Fail on set sysinfo regs r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;
	}

	retval = _cyttsp_set_operational_mode(ts);
	if (retval < 0) {
		pr_err("%s: Fail on set op mode r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;
	}

	/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
	if(true == ts->need_force_recalibration)
	{
		retval = _cyttsp_drv_write_regid(ts,0x1F);
		if (retval < 0) {
			pr_err("%s: bus write fail on Op Regs r=%d\n",
				__func__, retval);
			goto _cyttsp_startup_exit;
		}

		retval = _cyttsp_drv_write_reg_data(ts,0x80);
		if (retval < 0) {
			pr_err("%s: bus write fail on Op Regs r=%d\n",
				__func__, retval);
			goto _cyttsp_startup_exit;
		}
		msleep(1000);

		retval = _cyttsp_drv_read_reg_data(ts);
		if (retval < 0) {
			pr_err("%s: bus write fail on Op Regs r=%d\n",
				__func__, retval);
			goto _cyttsp_startup_exit;
		}
		ts->need_force_recalibration = false;
	}	
#endif /*HUAWEI_NEED_RECALIBRATION*/

	retval = _cyttsp_set_operational_regs(ts);
	if (retval < 0) {
		pr_err("%s: Fail on set op regs r=%d\n", __func__, retval);
		goto _cyttsp_startup_exit;
	}

	/* successful startup; switch to active state */
	pr_info("%s: hm=%02X tv=%02X%02X ai=0x%02X%02X "
		"av=0x%02X%02X ci=0x%02X%02X%02X\n", __func__,
		ts->sysinfo_data.hst_mode,
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);
	_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
	_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */

_cyttsp_startup_exit:
	if (ts->was_suspended) {
		ts->was_suspended = false;
		retval = _cyttsp_resume_sleep(ts);
		if (retval < 0) {
			pr_err("%s: Fail resume sleep, IC is active r=%d\n",
				__func__, retval);
		}
	}
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
	if(0 == retval){
		/* detect current device successful, set the flag as present */
		set_hw_dev_flag(DEV_I2C_TOUCH_PANEL);
	}
#endif
	return retval;
}

static void _cyttsp_queue_startup(struct cyttsp *ts, bool was_suspended)
{
	int id = 0;
	int num_sent = 0;
	int signal = 0;

	_cyttsp_change_state(ts, CY_BL_STATE);

	/* terminate any active tracks */
	for (id = 0, num_sent = 0; id < CY_NUM_TRK_ID; id++) {
		if (ts->prv_trk[id].tch) {
			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_ID_OST*CY_NUM_ABS_SET)+0];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_ID_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_X_OST*CY_NUM_ABS_SET)+0];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_X_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_Y_OST*CY_NUM_ABS_SET)+0];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal,
					ts->prv_trk[id].abs[CY_ABS_Y_OST]);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_P_OST*CY_NUM_ABS_SET)+0];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal, CY_NTCH);

			signal = ts->platform_data->frmwrk->abs
				[(CY_ABS_W_OST*CY_NUM_ABS_SET)+0];
			if (signal != CY_IGNORE_VALUE)
				input_report_abs(ts->input, signal, CY_NTCH);
			num_sent++;
			input_mt_sync(ts->input);
			ts->prv_trk[id].tch = CY_NTCH;
			cyttsp_dbg(ts, CY_DBG_LVL_3,
				"%s: ID:%3d  X:%3d  Y:%3d  "
				"Z:%3d  W=%3d  T=%3d liftoff\n",
				__func__, ts->prv_trk[id].abs[CY_ABS_ID_OST],
				ts->prv_trk[id].abs[CY_ABS_X_OST],
				ts->prv_trk[id].abs[CY_ABS_Y_OST],
				CY_NTCH,
				CY_NTCH,
				ts->prv_trk[id].abs[CY_ABS_ID_OST]);
		}
	}
	if (num_sent == 0) {
		/* in case of 0-touch; all liftoff; Gingerbread+ */
		input_mt_sync(ts->input);
	}
	input_sync(ts->input);
	memset(ts->prv_trk, CY_NTCH, sizeof(ts->prv_trk));
	ts->was_suspended = was_suspended;
	queue_work(ts->cyttsp_wq, &ts->cyttsp_resume_startup_work);
	pr_info("%s: startup queued\n", __func__);
}

static irqreturn_t cyttsp_irq(int irq, void *handle)
{
	struct cyttsp *ts = handle;
#ifdef CY_USE_LEVEL_IRQ
//	int tries = 0;
#endif
	int retval = 0;

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: GOT IRQ ps=%d\n", __func__, ts->power_state);

	mutex_lock(&ts->data_lock);

	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: DO IRQ ps=%d\n", __func__, ts->power_state);

	switch (ts->power_state) {
	case CY_BL_STATE:
		complete(&ts->bl_int_running);
#ifdef CY_USE_LEVEL_IRQ
		udelay(2000);	/* irq pulse: heartbeat=1ms; exit_bl=500us */
#endif
		break;
	case CY_SYSINFO_STATE:
		retval = ttsp_read_block_data(ts, CY_REG_BASE,
			sizeof(struct cyttsp_sysinfo_data),
			&(ts->sysinfo_data));
		if (retval < 0) {
			pr_err("%s: Fail read status and version regs r=%d\n",
				__func__, retval);
			goto _cyttsp_irq_sysinfo_exit;
		}
		retval = _cyttsp_hndshk(ts, ts->sysinfo_data.hst_mode);
		if (retval < 0) {
			pr_err("%s: Fail write handshake r=%d\n",
				__func__, retval);
			/* continue anyway */
			retval = 0;
		}
#ifdef CY_USE_LEVEL_IRQ
		udelay(100);	/* irq pulse: sysinfo mode switch=50us */
#endif
		complete(&ts->si_int_running);
_cyttsp_irq_sysinfo_exit:
		break;
	case CY_READY_STATE:
		complete(&ts->op_int_running);
#ifdef CY_USE_LEVEL_IRQ
		udelay(100);	/* irq pulse: operational mode switch=50us */
#endif
		break;
	case CY_LDR_STATE:
		complete(&ts->ld_int_running);
#ifdef CY_USE_LEVEL_IRQ
		udelay(2000);	/* irq pulse: bl_ready=1ms */
#endif
		break;
	case CY_SLEEP_STATE:
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: Attempt to process touch after enter sleep or"
			" unexpected wake event\n", __func__);
		retval = _cyttsp_wakeup(ts); /* in case its really asleep */
		if (retval < 0) {
			pr_err("%s: wakeup fail r=%d\n",
				__func__, retval);
			_cyttsp_queue_startup(ts, true);
			break;
		}
		retval = _cyttsp_resume_sleep(ts);
		if (retval < 0) {
			pr_err("%s: resume suspend fail r=%d\n",
				__func__, retval);
			_cyttsp_queue_startup(ts, true);
			break;
		}
		break;
	case CY_IDLE_STATE:
		/* device now available; signal initialization */
		pr_info("%s: Received IRQ in IDLE state\n",
			__func__);
		/* Try to determine the IC's current state */
		retval = _cyttsp_load_bl_regs(ts);
		if (retval < 0) {
			pr_err("%s: Still unable to find IC after IRQ\n",
				__func__);
		} else if (IS_BOOTLOADERMODE(ts->bl_data.bl_status)) {
			pr_info("%s: BL mode found in IDLE state\n",
				__func__);
			/* change state to make sure any other heartbeats
			 * simply signal complete until the restart has
			 * been entered from the* startup queue
			 */
			_cyttsp_change_state(ts, CY_BL_STATE);
			_cyttsp_queue_startup(ts, false);
		} else {
			pr_err("%s: interrupt received in IDLE state -"
				" try processing touch\n",
				__func__);
			_cyttsp_change_state(ts, CY_ACTIVE_STATE);
			retval = _cyttsp_xy_worker(ts);
			if (retval < 0) {
				pr_err("%s: xy_worker IDLE fail r=%d\n",
					__func__, retval);
				_cyttsp_queue_startup(ts, false);
				break;
			}
		}
		break;
	case CY_ACTIVE_STATE:
		retval = _cyttsp_xy_worker(ts);
		if (retval < 0) {
			pr_err("%s: xy_worker ACTIVE fail r=%d\n",
				__func__, retval);
			_cyttsp_queue_startup(ts, false);
			break;
		}
		break;
	case CY_INVALID_STATE:
		/*To resolve ESD problem*/
		//_cyttsp_queue_startup(ts, false);
		break;
	default:
		pr_err("%s: Unexpected interrupt ps=%d\n",
			__func__, ts->power_state);
		break;
	}

	mutex_unlock(&ts->data_lock);
	return IRQ_HANDLED;
}

static void cyttsp_ts_work_func(struct work_struct *work)
{
	struct cyttsp *ts =
		container_of(work, struct cyttsp, cyttsp_resume_startup_work);
	int retval = 0;

	mutex_lock(&ts->data_lock);

	retval = _cyttsp_startup(ts);
	if (retval < 0) {
		pr_err("%s: Startup failed with error code %d\n",
			__func__, retval);
		_cyttsp_change_state(ts, CY_INVALID_STATE);
		_cyttsp_queue_startup(ts, false);
	}

	mutex_unlock(&ts->data_lock);

	return;
}

static int _cyttsp_wakeup(struct cyttsp *ts)
{
	int retval = 0;
#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
	struct cyttsp_status_regs status_regs;
	int wake = CY_WAKE_DFLT;

	memset(&status_regs, 0, sizeof(struct cyttsp_status_regs));

	if (ts->platform_data->hw_recov == NULL) {
		cyttsp_dbg(ts, CY_DBG_LVL_3,
			"%s: no hw_recov function\n", __func__);
	} else {
		/* try wake using strobe on host alert pin */
		retval = ts->platform_data->hw_recov(wake);
		if (retval < 0) {
			if (retval == -ENOSYS) {
				cyttsp_dbg(ts, CY_DBG_LVL_3,
					"%s: no hw_recov wake=%d"
					" function\n", __func__, wake);
			} else {
				pr_err("%s: fail hw_recov(wake=%d)"
					" function r=%d\n",
					__func__, wake, retval);
				retval = -ENOSYS;
			}
		}
	}

	/* either device is already awake or a read will wake it up */
	retval = _cyttsp_load_status_regs(ts, &status_regs);
	if (retval < 0) {
		pr_err("%s: failed to access device on resume r=%d\n",
			__func__, retval);
		goto _cyttsp_wakeup_error_exit;
	}

	if (IS_BOOTLOADERMODE(status_regs.tt_mode)) {
		pr_err("%s: device in bootloader mode on wakeup"
			" tt_mode=0x%02X\n", __func__, status_regs.tt_mode);
		retval = -EIO;
		goto _cyttsp_wakeup_error_exit;
	}

	if (GET_HSTMODE(status_regs.hst_mode) !=
		GET_HSTMODE(CY_OPERATE_MODE)) {
		pr_err("%s: device not in op mode on wakeup hst_mode=0x%02X\n",
			__func__, status_regs.hst_mode);
		retval = -EIO;
		goto _cyttsp_wakeup_error_exit;
	}

	retval = _cyttsp_hndshk(ts, status_regs.hst_mode);
	if (retval < 0) {
		pr_err("%s: fail resume INT handshake(r=%d)\n",
			__func__, retval);
		/* continue anyway */
		retval = 0;
	}

	goto _cyttsp_wakeup_exit;

_cyttsp_wakeup_error_exit:
	/* caller must initiate startup sequence to restore touch */
	_cyttsp_change_state(ts, CY_IDLE_STATE);
	pr_err("%s: device reset required\n", __func__);
_cyttsp_wakeup_exit:
#endif
	return retval;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
int cyttsp_resume(void *handle)
{
	struct cyttsp *ts = handle;
	int retval = 0;

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: Resuming...", __func__);

	mutex_lock(&ts->data_lock);

	switch (ts->power_state) {
	case CY_SLEEP_STATE:
		if (ts->irq_enabled) {
#ifdef CY_USE_LEVEL_IRQ
			/* Workaround level interrupt unmasking issue */
			disable_irq_nosync(ts->irq);
			udelay(5);
#endif
			enable_irq(ts->irq);
		}

		retval = _cyttsp_wakeup(ts);
		if (retval < 0) {
			pr_err("%s: wakeup fail r=%d\n",
				__func__, retval);
			_cyttsp_queue_startup(ts, false);
			break;
		}
		_cyttsp_change_state(ts, CY_ACTIVE_STATE);
#ifdef CY_USE_WATCHDOG
		_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		break;
	case CY_ACTIVE_STATE:
		pr_err("%s: Already in %s state\n", __func__,
			cyttsp_powerstate_string[ts->power_state]);
		break;
	case CY_IDLE_STATE:
	case CY_READY_STATE:
	case CY_BL_STATE:
	case CY_LDR_STATE:
	case CY_SYSINFO_STATE:
	case CY_INVALID_STATE:
	default:
		/*Do not disturb FW loading*/
		if (ts->waiting_for_fw) {
			pr_err("%s: Driver is waiting for firmware\n",
				__func__);
			retval = -EALREADY;
			break;
		}
		pr_err("%s: Attempt to resume in %s state\n", __func__,
			cyttsp_powerstate_string[ts->power_state]);
		_cyttsp_queue_startup(ts, false);
		break;
	}

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: Wake Up %s\n", __func__,
		(retval < 0) ? "FAIL" : "PASS");

	mutex_unlock(&ts->data_lock);
	return retval;
}
EXPORT_SYMBOL_GPL(cyttsp_resume);

int cyttsp_suspend(void *handle)
{
	int retval = 0;
	struct cyttsp *ts = handle;
	u8 sleep = CY_DEEP_SLEEP_MODE;

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: Suspending...\n", __func__);

#ifdef CY_USE_WATCHDOG
	_cyttsp_stop_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
	if (ts->irq_enabled)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->data_lock);

	if (ts->power_state == CY_ACTIVE_STATE) {
#ifdef CONFIG_TOUCHSCREEN_DEBUG
		if (ts->waiting_for_fw) {
			retval = -EBUSY;
			pr_err("%s: Suspend Blocked while waiting for"
				" fw load in %s state\n", __func__,
				cyttsp_powerstate_string[ts->power_state]);
			goto cyttsp_suspend_exit;
		}
#endif
		retval = ttsp_write_block_data(ts, CY_REG_BASE +
			offsetof(struct cyttsp_xydata, hst_mode),
			sizeof(sleep), &sleep);
		if (retval < 0) {
			pr_err("%s: Failed to write sleep bit\n", __func__);
			if (ts->irq_enabled)
				enable_irq(ts->irq);
		} else
			_cyttsp_change_state(ts, CY_SLEEP_STATE);
	} else {
		switch (ts->power_state) {
		case CY_SLEEP_STATE:
			pr_err("%s: already in %s state\n", __func__,
				cyttsp_powerstate_string[ts->power_state]);
			break;
		case CY_LDR_STATE:
		case CY_SYSINFO_STATE:
		case CY_READY_STATE:
			retval = -EBUSY;
			pr_err("%s: Suspend Blocked while in %s state\n",
				__func__,
				cyttsp_powerstate_string[ts->power_state]);
			break;
		case CY_IDLE_STATE:
		case CY_LOW_PWR_STATE:
		case CY_BL_STATE:
		case CY_INVALID_STATE:
		default:
			pr_err("%s: Cannot enter suspend from %s state\n",
				__func__,
				cyttsp_powerstate_string[ts->power_state]);
			break;
		}

#ifdef CY_USE_WATCHDOG
		_cyttsp_start_watchdog_timer(ts);
#endif /* --CY_USE_WATCHDOG */
		if (ts->irq_enabled)
			enable_irq(ts->irq);
	}

#ifdef CONFIG_TOUCHSCREEN_DEBUG
cyttsp_suspend_exit:
#endif
	mutex_unlock(&ts->data_lock);

	return retval;
}
EXPORT_SYMBOL_GPL(cyttsp_suspend);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
void cyttsp_early_suspend(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);
	int retval = 0;

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: EARLY SUSPEND ts=%p\n", __func__, ts);
	retval = cyttsp_suspend(ts);
	if (retval < 0) {
		pr_err("%s: Early suspend failed with error code %d\n",
			__func__, retval);
	}
}

void cyttsp_late_resume(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);
	int retval = 0;

	cyttsp_dbg(ts, CY_DBG_LVL_3, "%s: LATE RESUME ts=%p\n", __func__, ts);
	retval = cyttsp_resume(ts);
	if (retval < 0) {
		pr_err("%s: Late resume failed with error code %d\n",
			__func__, retval);
	}
}
#endif

void cyttsp_core_release(void *handle)
{
	struct cyttsp *ts = handle;

	if (ts == NULL) {
		pr_err("%s: Null context pointer on driver release\n",
			__func__);
		goto cyttsp_core_release_exit;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	cyttsp_ldr_free(ts);
	/* mutex must not be locked when destroy is called */
	if (mutex_is_locked(&ts->data_lock))
		mutex_unlock(&ts->data_lock);
	mutex_destroy(&ts->data_lock);
	free_irq(ts->irq, ts);
	input_unregister_device(ts->input);
	if (ts->cyttsp_wq != NULL) {
		destroy_workqueue(ts->cyttsp_wq);
		ts->cyttsp_wq = NULL;
	}
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	if (ts->fwname != NULL) {
		kfree(ts->fwname);
		ts->fwname = NULL;
	}
#endif
	kfree(ts);
	ts = NULL;

cyttsp_core_release_exit:
	return;
}
EXPORT_SYMBOL_GPL(cyttsp_core_release);

static int cyttsp_open(struct input_dev *dev)
{
	int retval = 0;

	struct cyttsp *ts = input_get_drvdata(dev);

	if (ts == NULL) {
		pr_err("%s: NULL context pointer\n", __func__);
		retval = -ENOMEM;
		goto cyttsp_open_exit;
	}

/*G526 cypress TP update firmware disturb by cyttsp_open*/
	if(ts->waiting_for_fw)
		goto cyttsp_open_exit;
	
	mutex_lock(&ts->data_lock);
	if (!ts->powered) {
		/* Communicate with the IC */
		retval = _cyttsp_startup(ts);
		if (retval < 0) {
			pr_err("%s: startup fail r=%d\n",
				__func__, retval);
			if (ts->power_state != CY_IDLE_STATE)
				_cyttsp_change_state(ts, CY_IDLE_STATE);
			ts->powered = false;
		} else {
			ts->powered = true;
		}

		pr_info("%s: Powered ON(%d) r=%d\n",
			__func__, (int)ts->powered, retval);
	}
	mutex_unlock(&ts->data_lock);
cyttsp_open_exit:
	return retval;
}

static void cyttsp_close(struct input_dev *dev)
{
	/*
	 * close() normally powers down the device
	 * this call simply returns unless power
	 * to the device can be controlled by the driver
	 */
	return;
}

void *cyttsp_core_init(struct cyttsp_bus_ops *bus_ops,
	struct device *dev, int irq, char *name)
{
	unsigned long irq_flags = 0;
	int i = 0;
	int min = 0;
	int max = 0;
	int signal = CY_IGNORE_VALUE;
	int retval = 0;
	struct input_dev *input_device;
	struct cyttsp *ts = NULL;
    char r_data[2] = {0};
    
#ifdef CONFIG_HUAWEI_KERNEL  /*add by huawei begin*/
	struct cyttsp_touch_platform_data *touch_pdata = NULL;
	struct tp_resolution_conversion tp_type_self_check = {0};
#endif  /*add by huawei end*/

	if (dev == NULL) {
		pr_err("%s: Error, dev pointer is NULL\n", __func__);
		goto error_alloc_data;
	}

	if (bus_ops == NULL) {
		pr_err("%s: Error, bus_ops pointer is NULL\n", __func__);
		goto error_alloc_data;
	}
	
#ifdef CONFIG_HUAWEI_KERNEL /*add by huawei begin*/
	touch_pdata = dev->platform_data;

	if(NULL == touch_pdata)
	{
		printk("the touch_pdata is NULL please check the init code !\n");
		retval = -ENOMEM;
		goto err_platform_data_init_failed;
	}

	if(touch_pdata->read_touch_probe_flag)
	{
		retval = touch_pdata->read_touch_probe_flag();
		if(retval)
		{
			printk(KERN_ERR "%s: the touch driver has detected! \n", __func__);
			goto err_platform_data_init_failed;
		}
		else
		{
		    	printk(KERN_ERR "%s: it's the first touch driver! \n", __func__);
		}
	}
	
	/*Add for touch power on*/
    retval = gpio_request(MSM_8930_TOUCH_POWER_EN_PIN, "tp_1v8");
    if(0 != retval)
    {
		dev_err(dev, "%s: Fail to request tp_1v8 gpio PIN.\n", __func__);
		goto err_platform_data_init_failed;
    }
    retval = gpio_tlmm_config(GPIO_CFG(MSM_8930_TOUCH_POWER_EN_PIN, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
    if(0 != retval)
    {
        printk("%s: Failed to config gpio %d. code: %d.\n",
            __func__, MSM_8930_TOUCH_POWER_EN_PIN, retval);
        gpio_free(MSM_8930_TOUCH_POWER_EN_PIN);
		goto err_platform_data_init_failed;
    }
    retval = gpio_direction_output(MSM_8930_TOUCH_POWER_EN_PIN,1);
    if(0 != retval)
    {
        printk("%s: Failed to setup gpio %d. code: %d\n",
            __func__, MSM_8930_TOUCH_POWER_EN_PIN, retval);
        gpio_free(MSM_8930_TOUCH_POWER_EN_PIN);
		goto err_platform_data_init_failed;
    }

	if(touch_pdata->get_touch_resolution)
	{
		retval = touch_pdata->get_touch_resolution(&tp_type_self_check);
		if(retval < 0)
		{
			printk(KERN_ERR "%s: get_touch_resolution failed \n", __func__);
			goto err_platform_data_init_failed;
		}
		else
		{
			lcd_x = tp_type_self_check.lcd_x;
			lcd_y = tp_type_self_check.lcd_y;
			//lcd_all = tp_type_self_check.lcd_all;
		}
	}
#endif  /*add by huawei end*/

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		pr_err("%s: Error, kzalloc context memory\n", __func__);
		goto error_alloc_data;
	}

#ifdef CONFIG_TOUCHSCREEN_DEBUG
	ts->fwname = kzalloc(CY_BL_FW_NAME_SIZE, GFP_KERNEL);
	if (ts->fwname == NULL) {
		pr_err("%s: Error, kzalloc fwname\n", __func__);
		goto error_alloc_failed;
	}
#endif
	ts->cyttsp_wq =
		create_singlethread_workqueue("cyttsp_resume_startup_w");
	if (ts->cyttsp_wq == NULL) {
		pr_err("%s: No memory for cyttsp_resume_startup_wq\n",
			__func__);
		goto error_alloc_failed;
	}

	ts->power_state = CY_INVALID_STATE;
	ts->powered = false;
	ts->was_suspended = false;
	
	ts->auto_fw_upgrade_done = false;  // Add by huawei for fw upgrade using bin file
	/*G526 need to recalibrsation after updating FW*/
#ifdef HUAWEI_NEED_RECALIBRATION
	ts->need_force_recalibration = false;
#endif
	
	ts->dev = dev;
	ts->bus_ops = bus_ops;
	ts->platform_data = dev->platform_data;
	if (ts->platform_data == NULL) {
		pr_err("%s: Error, platform data is NULL\n", __func__);
		goto error_alloc_failed;
	}

	if (ts->platform_data->frmwrk == NULL) {
		pr_err("%s: Error, platform data framework is NULL\n",
		__func__);
		goto error_alloc_failed;
	}

	if (ts->platform_data->frmwrk->abs == NULL) {
		pr_err("%s: Error, platform data in framwork array is NULL\n",
		__func__);
		goto error_alloc_failed;
	}

	mutex_init(&ts->data_lock);
	init_completion(&ts->bl_int_running);
	init_completion(&ts->si_int_running);
	init_completion(&ts->op_int_running);
	init_completion(&ts->ld_int_running);
	ts->flags = ts->platform_data->flags;
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	ts->waiting_for_fw = false;
	ts->ic_grpnum = 0;
	ts->ic_grpoffset = 0;
	ts->ic_reflash_done = false;
	ts->bus_ops->tsdebug = CY_DBG_LVL_0;
#endif
#ifdef CY_ENABLE_CA
	ts->ca_available = false;
	ts->ca_enabled = false;
#endif
#ifdef CY_USE_REG_ACCESS
	ts->rw_regid = 0;
#endif

	ts->irq = irq;
	if (ts->irq <= 0) {
		pr_err("%s: Error, failed to allocate irq\n", __func__);
		goto error_init;
	}

    retval = ttsp_read_block_data(ts,0x00, 2, r_data);
    if( 0 != retval )
    {
        pr_err("%s: cyttsp3 ### TMA340 not exit### \n", __func__);
        goto error_init;
    }

	/* Create the input device and register it. */
	input_device = input_allocate_device();
	if (input_device == NULL) {
		pr_err("%s: Error, failed to allocate input device\n",
			__func__);
		goto error_init;
	}

	ts->input = input_device;
	input_device->name = name;
	snprintf(ts->phys, sizeof(ts->phys), "%s", dev_name(dev));
	input_device->phys = ts->phys;
	input_device->dev.parent = ts->dev;
	ts->bus_type = bus_ops->dev->bus;
#ifdef CY_USE_WATCHDOG
	INIT_WORK(&ts->work, cyttsp_timer_watchdog);
	setup_timer(&ts->timer, cyttsp_timer, (unsigned long)ts);
	ts->ntch_count = CY_NTCH;
	ts->prv_tch = CY_NTCH;
#endif /* --CY_USE_WATCHDOG */
	input_device->open = cyttsp_open;
	input_device->close = cyttsp_close;
	input_set_drvdata(input_device, ts);
	dev_set_drvdata(dev, ts);

	_cyttsp_init_tch_map(ts);
	memset(ts->prv_trk, CY_NTCH, sizeof(ts->prv_trk));

	__set_bit(EV_ABS, input_device->evbit);
	set_bit(INPUT_PROP_DIRECT,input_device->propbit); /*Add by huawei to choose direct mode for JellyBean*/

	/*Add by huawei*/
	ts->platform_data->frmwrk->abs[(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_MAX_OST] = lcd_x;
	ts->platform_data->frmwrk->abs[(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_MAX_OST] = lcd_y;

	for (i = 0; i < (ts->platform_data->frmwrk->size / CY_NUM_ABS_VAL);/*Add by huawei  "every setting use CY_NUM_ABS_VAL params"*/
		i++) {
		signal = ts->platform_data->frmwrk->abs
			[(i * CY_NUM_ABS_SET) + CY_SIGNAL_OST];
		if (signal != CY_IGNORE_VALUE) {
			min = ts->platform_data->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_MIN_OST];
			max = ts->platform_data->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_MAX_OST];
			if (i == CY_ABS_ID_OST) {
				/* shift track ids down to start at 0 */
				max = max - min;
				min = min - min;
			}
			input_set_abs_params(input_device,
				signal,
				min,
				max,
				ts->platform_data->frmwrk->abs
					[(i * CY_NUM_ABS_SET) + CY_FUZZ_OST],
				ts->platform_data->frmwrk->abs
					[(i * CY_NUM_ABS_SET) + CY_FLAT_OST]);
		}
	}

	if (ts->flags & CY_FLAG_FLIP) {
		input_set_abs_params(input_device,
			ABS_MT_POSITION_X,
			ts->platform_data->frmwrk->abs
			[(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_MIN_OST],
			ts->platform_data->frmwrk->abs
			[(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_MAX_OST],
			ts->platform_data->frmwrk->abs[
				(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_FUZZ_OST],
			ts->platform_data->frmwrk->abs[
				(CY_ABS_Y_OST * CY_NUM_ABS_SET) + CY_FLAT_OST]);

		input_set_abs_params(input_device,
			ABS_MT_POSITION_Y,
			ts->platform_data->frmwrk->abs
			[(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_MIN_OST],
			ts->platform_data->frmwrk->abs
			[(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_MAX_OST],
			ts->platform_data->frmwrk->abs[
				(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_FUZZ_OST],
			ts->platform_data->frmwrk->abs[
				(CY_ABS_X_OST * CY_NUM_ABS_SET) + CY_FLAT_OST]);
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	input_set_events_per_packet(input_device, 6 * CY_NUM_TCH_ID);
#endif

#ifdef CONFIG_TOUCHSCREEN_CYTTSP_KEY
	/*Add by huawei to init vkey capability*/
	if (ts->platform_data->frmwrk->enable_vkeys)
	{
		input_set_capability(input_device, EV_KEY, KEY_MENU);
		input_set_capability(input_device, EV_KEY, KEY_HOME);
		input_set_capability(input_device, EV_KEY, KEY_BACK);
	}
#endif

	/* enable interrupts */
#ifdef CY_USE_LEVEL_IRQ
	irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
#else
	irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
#endif
	cyttsp_dbg(ts, CY_DBG_LVL_3,
		"%s: Initialize IRQ: flags=%08X\n",
		__func__, (unsigned int)irq_flags);

#ifdef CONFIG_HUAWEI_KERNEL /*add by huawei*/
	if(ts->platform_data->set_touch_interrupt_gpio)
	{
		retval = ts->platform_data->set_touch_interrupt_gpio();
	}

	INIT_WORK(&ts->cyttsp_resume_startup_work, cyttsp_ts_work_func);

	printk("Requesting IRQ...\n");
#endif /*add by huawei*/

	retval = request_threaded_irq(ts->irq, NULL, cyttsp_irq,
		irq_flags, ts->input->name, ts);
	if (retval < 0) {
		pr_err("%s: IRQ request failed r=%d\n",
			__func__, retval);
		ts->irq_enabled = false;
		goto error_init;
	}
	else
	{
		if (irq_set_irq_wake(ts->irq, 1) < 0)
				printk(KERN_ERR "failed to set IRQ wake\n");
	}
	

	ts->irq_enabled = true;

	retval = input_register_device(input_device);
	if (retval < 0) {
		pr_err("%s: Error, failed to register input device r=%d\n",
			__func__, retval);
		goto error_input_register_device;
	}

	/* Add /sys files */
	cyttsp_ldr_init(ts);
/*Init manual update firmware*/
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
	g_ts = ts;
	for (i = 0 ; i < 3; i++) 
	{
		retval= _cyttsp_firmware_file();   
		if (!retval)
		{
		  break;
		}
	}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = cyttsp_early_suspend;
	ts->early_suspend.resume = cyttsp_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	
/*Move up*/

    /* as of now, i2c communication and registering input device are successful */
	if(touch_pdata->set_touch_probe_flag)
    {
        pr_info("%s: Cypress3 probe ok, set touch flag as 1 \n",__func__);
	    touch_pdata->set_touch_probe_flag(1);
    }   

/*Add by huawei*/
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP3_USE_FW_BIN_FILE
	INIT_WORK(&ts->fw_upgrade, _cyttsp_set_auto_fw_upgrade);
	schedule_work(&ts->fw_upgrade);
#endif

	goto no_error;

error_input_register_device:
	input_free_device(input_device);
error_init:
	mutex_destroy(&ts->data_lock);
error_alloc_failed:
#ifdef CONFIG_TOUCHSCREEN_DEBUG
	if (ts->fwname != NULL) {
		kfree(ts->fwname);
		ts->fwname = NULL;
	}
#endif
	if (ts != NULL) {
		kfree(ts);
		ts = NULL;
	}
error_alloc_data:
err_platform_data_init_failed:
	pr_err("%s: Failed Initialization\n", __func__);
no_error:
	return ts;
}
EXPORT_SYMBOL_GPL(cyttsp_core_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard touchscreen driver core");
MODULE_AUTHOR("Cypress");
