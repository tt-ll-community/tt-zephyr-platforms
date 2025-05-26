/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cm2dm_msg.h"
#include "fan_ctrl.h"
#include "functional_efuse.h"
#include "fw_table.h"
#include "harvesting.h"
#include "pll.h"
#include "pvt.h"
#include "read_only_table.h"
#include "reg.h"
#include "regulator.h"
#include "status_reg.h"
#include "telemetry.h"
#include "telemetry_internal.h"
#include "gddr.h"

#include <float.h> /* for FLT_MAX */
#include <math.h>  /* for floor */
#include <stdint.h>
#include <string.h>

#include <tenstorrent/post_code.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(telemetry, CONFIG_TT_APP_LOG_LEVEL);

#define RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR 0x80030D20

struct telemetry_entry {
	uint16_t tag;
	uint16_t offset;
};

struct telemetry_table {
	uint32_t version;
	uint32_t entry_count;
	struct telemetry_entry tag_table[TAG_COUNT];
	uint32_t telemetry[TAG_COUNT];
};

/* Global variables */
static struct telemetry_table telemetry_table = {
	.tag_table = {
		[0] = {TAG_BOARD_ID_HIGH, TELEM_OFFSET(TAG_BOARD_ID_HIGH)},
		[1] = {TAG_BOARD_ID_LOW, TELEM_OFFSET(TAG_BOARD_ID_LOW)},
		[3] = {TAG_HARVESTING_STATE, TELEM_OFFSET(TAG_HARVESTING_STATE)},
		[4] = {TAG_UPDATE_TELEM_SPEED, TELEM_OFFSET(TAG_UPDATE_TELEM_SPEED)},
		[5] = {TAG_VCORE, TELEM_OFFSET(TAG_VCORE)},
		[6] = {TAG_TDP, TELEM_OFFSET(TAG_TDP)},
		[7] = {TAG_TDC, TELEM_OFFSET(TAG_TDC)},
		[8] = {TAG_VDD_LIMITS, TELEM_OFFSET(TAG_VDD_LIMITS)},
		[9] = {TAG_THM_LIMITS, TELEM_OFFSET(TAG_THM_LIMITS)},
		[10] = {TAG_ASIC_TEMPERATURE, TELEM_OFFSET(TAG_ASIC_TEMPERATURE)},
		[11] = {TAG_VREG_TEMPERATURE, TELEM_OFFSET(TAG_VREG_TEMPERATURE)},
		[12] = {TAG_BOARD_TEMPERATURE, TELEM_OFFSET(TAG_BOARD_TEMPERATURE)},
		[13] = {TAG_AICLK, TELEM_OFFSET(TAG_AICLK)},
		[14] = {TAG_AXICLK, TELEM_OFFSET(TAG_AXICLK)},
		[15] = {TAG_ARCCLK, TELEM_OFFSET(TAG_ARCCLK)},
		[16] = {TAG_L2CPUCLK0, TELEM_OFFSET(TAG_L2CPUCLK0)},
		[17] = {TAG_L2CPUCLK1, TELEM_OFFSET(TAG_L2CPUCLK1)},
		[18] = {TAG_L2CPUCLK2, TELEM_OFFSET(TAG_L2CPUCLK2)},
		[19] = {TAG_L2CPUCLK3, TELEM_OFFSET(TAG_L2CPUCLK3)},
		[20] = {TAG_ETH_LIVE_STATUS, TELEM_OFFSET(TAG_ETH_LIVE_STATUS)},
		[21] = {TAG_GDDR_STATUS, TELEM_OFFSET(TAG_GDDR_STATUS)},
		[22] = {TAG_GDDR_SPEED, TELEM_OFFSET(TAG_GDDR_SPEED)},
		[23] = {TAG_ETH_FW_VERSION, TELEM_OFFSET(TAG_ETH_FW_VERSION)},
		[24] = {TAG_GDDR_FW_VERSION, TELEM_OFFSET(TAG_GDDR_FW_VERSION)},
		[25] = {TAG_DM_APP_FW_VERSION, TELEM_OFFSET(TAG_DM_APP_FW_VERSION)},
		[26] = {TAG_DM_BL_FW_VERSION, TELEM_OFFSET(TAG_DM_BL_FW_VERSION)},
		[27] = {TAG_FLASH_BUNDLE_VERSION, TELEM_OFFSET(TAG_FLASH_BUNDLE_VERSION)},
		[28] = {TAG_CM_FW_VERSION, TELEM_OFFSET(TAG_CM_FW_VERSION)},
		[29] = {TAG_L2CPU_FW_VERSION, TELEM_OFFSET(TAG_L2CPU_FW_VERSION)},
		[30] = {TAG_FAN_SPEED, TELEM_OFFSET(TAG_FAN_SPEED)},
		[31] = {TAG_TIMER_HEARTBEAT, TELEM_OFFSET(TAG_TIMER_HEARTBEAT)},
		[32] = {TAG_ENABLED_TENSIX_COL, TELEM_OFFSET(TAG_ENABLED_TENSIX_COL)},
		[33] = {TAG_ENABLED_ETH, TELEM_OFFSET(TAG_ENABLED_ETH)},
		[34] = {TAG_ENABLED_GDDR, TELEM_OFFSET(TAG_ENABLED_GDDR)},
		[35] = {TAG_ENABLED_L2CPU, TELEM_OFFSET(TAG_ENABLED_L2CPU)},
		[36] = {TAG_PCIE_USAGE, TELEM_OFFSET(TAG_PCIE_USAGE)},
		[37] = {TAG_NOC_TRANSLATION, TELEM_OFFSET(TAG_NOC_TRANSLATION)},
		[38] = {TAG_FAN_RPM, TELEM_OFFSET(TAG_FAN_RPM)},
		[39] = {TAG_GDDR_0_1_TEMP, TELEM_OFFSET(TAG_GDDR_0_1_TEMP)},
		[40] = {TAG_GDDR_2_3_TEMP, TELEM_OFFSET(TAG_GDDR_2_3_TEMP)},
		[41] = {TAG_GDDR_4_5_TEMP, TELEM_OFFSET(TAG_GDDR_4_5_TEMP)},
		[42] = {TAG_GDDR_6_7_TEMP, TELEM_OFFSET(TAG_GDDR_6_7_TEMP)},
		[43] = {TAG_GDDR_0_1_CORR_ERRS, TELEM_OFFSET(TAG_GDDR_0_1_CORR_ERRS)},
		[44] = {TAG_GDDR_2_3_CORR_ERRS, TELEM_OFFSET(TAG_GDDR_2_3_CORR_ERRS)},
		[45] = {TAG_GDDR_4_5_CORR_ERRS, TELEM_OFFSET(TAG_GDDR_4_5_CORR_ERRS)},
		[46] = {TAG_GDDR_6_7_CORR_ERRS, TELEM_OFFSET(TAG_GDDR_6_7_CORR_ERRS)},
		[47] = {TAG_GDDR_UNCORR_ERRS, TELEM_OFFSET(TAG_GDDR_UNCORR_ERRS)},
		[48] = {TAG_MAX_GDDR_TEMP, TELEM_OFFSET(TAG_MAX_GDDR_TEMP)},
		[49] = {TAG_ASIC_LOCATION, TELEM_OFFSET(TAG_ASIC_LOCATION)},
		[50] = {TAG_BOARD_POWER_LIMIT, TELEM_OFFSET(TAG_BOARD_POWER_LIMIT)},
		[51] = {TAG_INPUT_POWER, TELEM_OFFSET(TAG_INPUT_POWER)},
		[52] = {TAG_ASIC_ID_HIGH, TELEM_OFFSET(TAG_ASIC_ID_HIGH)},
		[53] = {TAG_ASIC_ID_LOW, TELEM_OFFSET(TAG_ASIC_ID_LOW)},
        [54] = {TAG_THERM_TRIP_COUNT, TELEM_OFFSET(TAG_THERM_TRIP_COUNT)},
		[55] = {TAG_TELEM_ENUM_COUNT, TELEM_OFFSET(TAG_TELEM_ENUM_COUNT)},
	},
};
static uint32_t *telemetry = &telemetry_table.telemetry[0];

static struct k_timer telem_update_timer;
static struct k_work telem_update_worker;
static int telem_update_interval = 100;



uint32_t ConvertFloatToTelemetry(float value)
{
	/* Convert float to signed int 16.16 format */

	/* Handle error condition */
	if (value == FLT_MAX || value == -FLT_MAX) {
		return 0x80000000;
	}

	float abs_value = fabsf(value);
	uint16_t int_part = floor(abs_value);
	uint16_t frac_part = (abs_value - int_part) * 65536;
	uint32_t ret_value = (int_part << 16) | frac_part;
	/* Return the 2's complement if the original value was negative */
	if (value < 0) {
		ret_value = -ret_value;
	}
	return ret_value;
}

float ConvertTelemetryToFloat(int32_t value)
{
	/* Convert signed int 16.16 format to float */
	if (value == INT32_MIN) {
		return FLT_MAX;
	} else {
		return value / 65536.0;
	}
}

static void UpdateGddrTelemetry(void)
{
	/* We pack multiple metrics into one field, so need to clear first. */
	for (int i = 0; i < NUM_GDDR / 2; i++) {
		telemetry[TAG_GDDR_0_1_TEMP + i] = 0;
		telemetry[TAG_GDDR_0_1_CORR_ERRS + i] = 0;
	}

	telemetry[TAG_GDDR_UNCORR_ERRS] = 0;
	telemetry[TAG_GDDR_STATUS] = 0;

	for (int i = 0; i < NUM_GDDR; i++) {
		gddr_telemetry_table_t gddr_telemetry;
		/* Harvested instances should read 0b00 for status. */
		if (IS_BIT_SET(tile_enable.gddr_enabled, i)) {
			if (read_gddr_telemetry_table(i, &gddr_telemetry) < 0) {
				LOG_WRN_ONCE("Failed to read GDDR telemetry table while "
					     "updating telemetry");
				continue;
			}
			/* DDR Status:
			 * [0] - Training complete GDDR 0
			 * [1] - Error GDDR 0
			 * [2] - Training complete GDDR 1
			 * [3] - Error GDDR 1
			 * ...
			 * [14] - Training Complete GDDR 7
			 * [15] - Error GDDR 7
			 */
			telemetry[TAG_GDDR_STATUS] |=
						  (gddr_telemetry.training_complete << (i * 2)) |
						  (gddr_telemetry.gddr_error << (i * 2 + 1));

			/* DDR_x_y_TEMP:
			 * [31:24] GDDR y top
			 * [23:16] GDDR y bottom
			 * [15:8]  GDDR x top
			 * [7:0]   GDDR x bottom
			 */
			int shift_val = (i % 2) * 16;

			telemetry[TAG_GDDR_0_1_TEMP + i / 2] |=
				((gddr_telemetry.dram_temperature_top & 0xff) << (8 + shift_val)) |
				((gddr_telemetry.dram_temperature_bottom & 0xff) << shift_val);

			/* GDDR_x_y_CORR_ERRS:
			 * [31:24] GDDR y Corrected Write EDC errors
			 * [23:16] GDDR y Corrected Read EDC Errors
			 * [15:8]  GDDR x Corrected Write EDC errors
			 * [7:0]   GDDR y Corrected Read EDC Errors
			 */
			telemetry[TAG_GDDR_0_1_CORR_ERRS + i / 2] |=
				((gddr_telemetry.corr_edc_wr_errors & 0xff) << (8 + shift_val)) |
				((gddr_telemetry.corr_edc_rd_errors & 0xff) << shift_val);

			/* GDDR_UNCORR_ERRS:
			 * [0]  GDDR 0 Uncorrected Read EDC error
			 * [1]  GDDR 0 Uncorrected Write EDC error
			 * [2]  GDDR 1 Uncorrected Read EDC error
			 * ...
			 * [15] GDDR 7 Uncorrected Write EDC error
			 */
			telemetry[TAG_GDDR_UNCORR_ERRS] |=
				(gddr_telemetry.uncorr_edc_rd_error << (i * 2)) |
				(gddr_telemetry.uncorr_edc_wr_error << (i * 2 + 1));
			/* GDDR speed - in Mbps */
			telemetry[TAG_GDDR_SPEED] = gddr_telemetry.dram_speed;
		}
	}
}

int GetMaxGDDRTemp(void)
{
	int max_gddr_temp = 0;

	for (int i = 0; i < NUM_GDDR; i++) {
		int shift_val = (i % 2) * 16;
		int gddr_temp = telemetry[TAG_GDDR_0_1_TEMP + i / 2];

		max_gddr_temp = MAX(max_gddr_temp, (gddr_temp >> shift_val) & 0xFF);
		max_gddr_temp = MAX(max_gddr_temp, (gddr_temp >> (shift_val + 8)) & 0xFF);
	}

	return max_gddr_temp;
}

static void write_static_telemetry(uint32_t app_version)
{
	telemetry_table.version = TELEMETRY_VERSION;    /* v0.1.0 - Only update when redefining the
							 * meaning of an existing tag
							 */
	telemetry_table.entry_count = TAG_COUNT; /* Runtime count of telemetry entries */

	/* Get the static values */
	telemetry[TAG_BOARD_ID_HIGH] = get_read_only_table()->board_id >> 32;
	telemetry[TAG_BOARD_ID_LOW] = get_read_only_table()->board_id & 0xFFFFFFFF;
	telemetry[TAG_ASIC_ID_HIGH] = READ_FUNCTIONAL_EFUSE(ASIC_ID_HIGH);
	telemetry[TAG_ASIC_ID_LOW] = READ_FUNCTIONAL_EFUSE(ASIC_ID_LOW);
	telemetry[TAG_HARVESTING_STATE] = 0x00000000;
	telemetry[TAG_UPDATE_TELEM_SPEED] = telem_update_interval; /* Expected speed of
								    * update in ms
								    */

	/* TODO: Gather FW versions from FW themselves */
	telemetry[TAG_ETH_FW_VERSION] = 0x00000000;
	if (tile_enable.gddr_enabled != 0) {
		gddr_telemetry_table_t gddr_telemetry;
		/* Use first available instance. */
		uint32_t gddr_inst = find_lsb_set(tile_enable.gddr_enabled) - 1;

		if (read_gddr_telemetry_table(gddr_inst, &gddr_telemetry) < 0) {
			LOG_WRN_ONCE("Failed to read GDDR telemetry table while "
				     "writing static telemetry");
		} else {
			telemetry[TAG_GDDR_FW_VERSION] =
					(gddr_telemetry.mrisc_fw_version_major << 16) |
					 gddr_telemetry.mrisc_fw_version_minor;
		}
	}
	/* DM_APP_FW_VERSION and DM_BL_FW_VERSION assumes zero-init, it might be
	 * initialized by bh_chip_set_static_info in dmfw already, must not clear.
	 */
	telemetry[TAG_FLASH_BUNDLE_VERSION] = get_fw_table()->fw_bundle_version;
	telemetry[TAG_CM_FW_VERSION] = app_version;
	telemetry[TAG_L2CPU_FW_VERSION] = 0x00000000;

	/* Tile enablement / harvesting information */
	telemetry[TAG_ENABLED_TENSIX_COL] = tile_enable.tensix_col_enabled;
	telemetry[TAG_ENABLED_ETH] = tile_enable.eth_enabled;
	telemetry[TAG_ENABLED_GDDR] = tile_enable.gddr_enabled;
	telemetry[TAG_ENABLED_L2CPU] = tile_enable.l2cpu_enabled;
	telemetry[TAG_PCIE_USAGE] =
		((tile_enable.pcie_usage[1] & 0x3) << 2) |
		(tile_enable.pcie_usage[0] & 0x3);
	/* telemetry[TAG_NOC_TRANSLATION] assumes zero-init, see also
	 * UpdateTelemetryNocTranslation.
	 */

	if (get_pcb_type() == PcbTypeP300) {
		/* For the p300 a value of 1 is the left asic and 0 is the right */
		telemetry[TAG_ASIC_LOCATION] =
			FIELD_GET(BIT(6), ReadReg(RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR));
	} else {
		/* For all other supported boards this value is 0 */
		telemetry[TAG_ASIC_LOCATION] = 0;
	}
}

static void update_telemetry(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_TELEMETRY_START);
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(telem_update_interval, &telemetry_internal_data);

	/* Get all dynamically updated values */
	telemetry[TAG_VCORE] =
		telemetry_internal_data
			.vcore_voltage; /* reported in mV, will be truncated to uint32_t */
	telemetry[TAG_TDP] = telemetry_internal_data
				 .vcore_power; /* reported in W, will be truncated to uint32_t */
	telemetry[TAG_TDC] = telemetry_internal_data
				 .vcore_current; /* reported in A, will be truncated to uint32_t */
	telemetry[TAG_VDD_LIMITS] = 0x00000000;      /* VDD limits - Not Available yet */
	telemetry[TAG_THM_LIMITS] = 0x00000000;      /* THM limits - Not Available yet */
	telemetry[TAG_ASIC_TEMPERATURE] = ConvertFloatToTelemetry(
		telemetry_internal_data.asic_temperature); /* ASIC temperature - reported in
							    * signed int 16.16 format
							    */
	telemetry[TAG_VREG_TEMPERATURE] = 0x000000;  /* VREG temperature - need I2C line */
	telemetry[TAG_BOARD_TEMPERATURE] = 0x000000; /* Board temperature - need I2C line */
	telemetry[TAG_AICLK] = GetAICLK(); /* first 16 bits - MAX ASIC FREQ (Not Available yet),
					    * lower 16 bits - current AICLK
					    */
	telemetry[TAG_AXICLK] = GetAXICLK();
	telemetry[TAG_ARCCLK] = GetARCCLK();
	telemetry[TAG_L2CPUCLK0] = GetL2CPUCLK(0);
	telemetry[TAG_L2CPUCLK1] = GetL2CPUCLK(1);
	telemetry[TAG_L2CPUCLK2] = GetL2CPUCLK(2);
	telemetry[TAG_L2CPUCLK3] = GetL2CPUCLK(3);
	telemetry[TAG_ETH_LIVE_STATUS] =
		0x00000000; /* ETH live status lower 16 bits: heartbeat status, upper 16 bits:
			     * retrain_status - Not Available yet
			     */
	telemetry[TAG_FAN_SPEED] = GetFanSpeed(); /* Target fan speed - reported in percentage */
	telemetry[TAG_FAN_RPM] = GetFanRPM();     /* Actual fan RPM */
	UpdateGddrTelemetry();
	telemetry[TAG_MAX_GDDR_TEMP] = GetMaxGDDRTemp();
	telemetry[TAG_INPUT_POWER] = GetInputPower(); /* Input power - reported in W */
	telemetry[TAG_TIMER_HEARTBEAT]++; /* Incremented every time the timer is called */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_TELEMETRY_END);
}

/* Handler functions for zephyr timer and worker objects */
static void telemetry_work_handler(struct k_work *work)
{
	/* Repeat fetching of dynamic telemetry values */
	update_telemetry();
}
static void telemetry_timer_handler(struct k_timer *timer)
{
	k_work_submit(&telem_update_worker);
}

/* Zephyr timer object submits a work item to the system work queue whose thread performs the task
 * on a periodic basis.
 */
/* See:
 * https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html#using-a-timer-expiry-function
 */
static K_WORK_DEFINE(telem_update_worker, telemetry_work_handler);
static K_TIMER_DEFINE(telem_update_timer, telemetry_timer_handler, NULL);

void init_telemetry(uint32_t app_version)
{
	write_static_telemetry(app_version);
	/* fill the dynamic values once before starting timed updates */
	update_telemetry();

	/* Publish the telemetry data pointer for readers in Scratch RAM */
	WriteReg(TELEMETRY_DATA_REG_ADDR, (uint32_t)&telemetry[0]);
	WriteReg(TELEMETRY_TABLE_REG_ADDR, (uint32_t)&telemetry_table);
}

void StartTelemetryTimer(void)
{
	/* Start the timer to update the dynamic telemetry values
	 * Duration (time interval before the timer expires for the first time) and
	 * Period (time interval between all timer expirations after the first one)
	 * are both set to telem_update_interval
	 */
	k_timer_start(&telem_update_timer, K_MSEC(telem_update_interval),
		      K_MSEC(telem_update_interval));
}

void UpdateDmFwVersion(uint32_t bl_version, uint32_t app_version)
{
	telemetry[TAG_DM_BL_FW_VERSION] = bl_version;
	telemetry[TAG_DM_APP_FW_VERSION] = app_version;
}

void UpdateTelemetryNocTranslation(bool translation_enabled)
{
	/* Note that this may be called before init_telemetry. */
	telemetry[TAG_NOC_TRANSLATION] = translation_enabled;
}

void UpdateTelemetryBoardPowerLimit(uint32_t power_limit)
{
	telemetry[TAG_BOARD_POWER_LIMIT] = power_limit;
}

void UpdateTelemetryThermTripCount(uint16_t therm_trip_count)
{
	telemetry[TAG_THERM_TRIP_COUNT] = therm_trip_count;
}

uint32_t GetTelemetryTag(uint16_t tag)
{
	if (tag >= TAG_COUNT) {
		return -1;
	}
	return telemetry[tag];
}
