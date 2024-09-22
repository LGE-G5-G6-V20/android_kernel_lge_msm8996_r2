/*
 * Authors: Shingo Nakao <shingo.x.nakao@sonymobile.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
/*
 * Copyright (C) 2015 Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#define CURRENT_LIMIT_TO_1500_DURING_DCP_CHARGING

#include <linux/device.h>
#include <linux/input.h>

enum {
	ATTR_FV_STS = 0,
	ATTR_FV_CFG,
	ATTR_FCC_CFG,
	ATTR_ICHG_STS,
	ATTR_CHGR_STS,
	ATTR_CHGR_INT,
	ATTR_BAT_IF_INT,
	ATTR_BAT_IF_CFG,
	ATTR_USB_INT,
	ATTR_DC_INT,
	ATTR_USB_ICL_STS,
	ATTR_USB_APSD_DG,
	ATTR_USB_RID_STS,
	ATTR_USB_HVDCP_STS,
	ATTR_USB_CMD_IL,
	ATTR_OTG_INT,
	ATTR_MISC_INT,
	ATTR_MISC_IDEV_STS,
	ATTR_VFLOAT_ADJUST_TRIM,
	ATTR_USB_MAX_CURRENT,
	ATTR_DC_MAX_CURRENT,
	ATTR_DC_TARGET_CURRENT,
	ATTR_USB_ONLINE,
	ATTR_USB_PRESENT,
	ATTR_BAT_TEMP_STATUS,
	ATTR_USB_5V,
	ATTR_USB_6V,
	ATTR_USB_7V,
	ATTR_USB_8V,
	ATTR_USB_9V,
	ATTR_FASTCHG_CURRENT,
	ATTR_OUTPUT_BATT_LOG,
	ATTR_APSD_RERUN_CHECK_DELAY_MS,
	ATTR_IBAT_VOTER,
	ATTR_USBIN_VOTER,
	ATTR_USB_VOTER,
	ATTR_BATTCHG_VOTER,
	ATTR_PULSE_CNT,
	ATTR_THERMAL_PULSE_CNT,
	ATTR_USB_USBIN_IL_CFG,
	ATTR_APSD_RERUN_STATUS,
};

enum temp_status {
	TEMP_STATUS_COLD,
	TEMP_STATUS_COOL,
	TEMP_STATUS_NORMAL,
	TEMP_STATUS_WARM,
	TEMP_STATUS_HOT,
	TEMP_STATUS_NUM,
};

enum voters_type {
	VOTERS_FCC = 1,
	VOTERS_ICL,
	VOTERS_EN,
	VOTERS_BATTCHG
};

char *tempstat_names[] = {
	"COLD",
	"COOL",
	"NORMAL",
	"WARM",
	"HOT",
	"UNKNOWN"
};

static int somc_debug_mask = PR_INFO;
module_param_named(
	somc_debug_mask, somc_debug_mask, int, S_IRUSR | S_IWUSR
);

#define pr_smb_ext(reason, fmt, ...)				\
	do {							\
		if (somc_debug_mask & (reason))			\
			pr_info(fmt, ##__VA_ARGS__);		\
		else						\
			pr_debug(fmt, ##__VA_ARGS__);		\
	} while (0)

#define pr_smb_ext_rt(reason, fmt, ...)					\
	do {								\
		if (somc_debug_mask & (reason))				\
			pr_info_ratelimited(fmt, ##__VA_ARGS__);	\
		else							\
			pr_debug_ratelimited(fmt, ##__VA_ARGS__);	\
	} while (0)

static void batt_log_work(struct work_struct *work)
{
	struct somc_batt_log *batt_log = container_of(work,
				struct somc_batt_log, work.work);
	struct chg_somc_params *params = container_of(batt_log,
			struct chg_somc_params, batt_log);
	struct smbchg_chip *chip = container_of(params,
			struct smbchg_chip, somc_params);

	power_supply_changed(chip->batt_psy);

	if (params->batt_log.output_period > 0)
		schedule_delayed_work(&params->batt_log.work,
		    msecs_to_jiffies(params->batt_log.output_period * 1000));
}

static int somc_chg_get_typec_current_ma(struct smbchg_chip *chip,
							int current_ma)
{
	struct chg_somc_params *params = &chip->somc_params;
	struct somc_chg_det *chg_det = &params->chg_det;
	int ret;

	if (chg_det->typec_current_max &&
				current_ma > chg_det->typec_current_max)
		ret = chg_det->typec_current_max;
	else
		ret = current_ma;

	return ret;
}

static int somc_chg_get_current_ma(struct smbchg_chip *chip,
						enum power_supply_type type)
{
	int current_limit_ma = 0;

	if (type == POWER_SUPPLY_TYPE_USB) {
		if (chip->typec_psy && chip->typec_current_ma) {
			/* Type-C/PD detected by anx/tusb driver
			 * (other mode reports 0 current).
			 */
			current_limit_ma = chip->typec_current_ma;
			pr_smb(PR_LGE, "Selected: USB-PD\n");
		} else {
			/* Flow chart: C-1 SDP */
			current_limit_ma = DEFAULT_SDP_MA;
			pr_smb(PR_LGE, "Selected: C-1, SDP\n");
		}
	} else if (type == POWER_SUPPLY_TYPE_USB_CDP) {
		/* Flow chart: C-1 CDP */
		current_limit_ma = DEFAULT_CDP_MA;
		pr_smb(PR_LGE, "Selected: C-1, CDP\n");
	} else if (type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		/* Flow chart: C-5 */
		current_limit_ma = smbchg_default_hvdcp_icl_ma;
		pr_smb(PR_LGE, "Selected: C-5, HVDCP\n");
	} else if (type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
		if (chip->typec_current_ma > CURRENT_1500_MA) {
			/* Flow chart: C-7 */
			current_limit_ma = somc_chg_get_typec_current_ma(chip,
							chip->typec_current_ma);
			pr_smb(PR_LGE, "Selected: C-7, HVDCP3\n");
		} else if (!chip->typec_current_ma) {
			/* Flow chart: C-8 */
			current_limit_ma = smbchg_default_hvdcp3_icl_ma;
			pr_smb(PR_LGE, "Selected: C-8, HVDCP3\n");
		} else {
			/* Flow chart: C-6 */
			current_limit_ma = CURRENT_1500_MA;
			pr_smb(PR_LGE, "Selected: C-6, 1.5A\n");
		}
	} else if (is_usb_present(chip) &&
				chip->somc_params.chg_det.settled_not_hvdcp) {
		if (chip->typec_current_ma > CURRENT_1500_MA) {
			/* Flow chart: C-3 */
#ifndef CURRENT_LIMIT_TO_1500_DURING_DCP_CHARGING
			current_limit_ma = somc_chg_get_typec_current_ma(chip,
							chip->typec_current_ma);
			pr_smb(PR_LGE, "Selected: C-3, Type-C Max\n");
#else
			current_limit_ma = smbchg_default_dcp_icl_ma;
			pr_smb(PR_LGE, "Selected: C-3, DCP\n");
#endif
		} else {
			/* Flow chart: C-2, C-4 */
			current_limit_ma = smbchg_default_dcp_icl_ma;
			pr_smb(PR_LGE, "Selected: C-2, C-4, DCP\n");
		}
	} else {
		/* Unknown, and DCP before detection of HVDCP */
		current_limit_ma = smbchg_default_dcp_icl_ma;
		pr_smb(PR_LGE, "Selected: Unknown, DCP\n");
	}

	pr_smb(PR_MISC, "type=%d, cur=%dma\n", type, current_limit_ma);

	return current_limit_ma;
}

static int somc_chg_apsd_wait_rerun(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;
	int rc;

	pr_info("APSD rerun\n");
	params->apsd.rerun_wait_irq = true;

	if (chip->schg_version == QPNP_SCHG_LITE) {
		rc = rerun_apsd(chip);
		if (rc < 0)
			pr_err("APSD rerun failed\n");
	} else {
		pr_smb(PR_MISC, "Faking Removal\n");
		rc = fake_insertion_removal(chip, false);
		if (rc < 0) {
			pr_err("Couldn't fake removal HVDCP Removed rc=%d\n", rc);
			goto abort;
		}
		msleep(500);
		pr_smb(PR_MISC, "Faking Insertion\n");
		rc = fake_insertion_removal(chip, true);
		if (rc < 0)
			pr_err("Couldn't fake insertion rc=%d\n", rc);
	}

abort:
	params->apsd.rerun_wait_irq = false;
	if (rc < 0) {
		union power_supply_propval prop = {0, };

		pr_warn("force usb removal\n");
		update_usb_status(chip, 0, true);
		power_supply_set_property(chip->usb_psy,
					POWER_SUPPLY_PROP_USBIN_DET, &prop);
	}
	return rc;
}

#define DEFAULT_BATT_CHARGE_FULL	0
static int somc_chg_get_prop_batt_charge_full(struct smbchg_chip *chip)
{
	int capacity, rc;

	rc = get_property_from_fg(chip,
			POWER_SUPPLY_PROP_CHARGE_FULL, &capacity);
	if (rc) {
		pr_smb_ext(PR_STATUS, "Couldn't get capacityl rc = %d\n", rc);
		capacity = DEFAULT_BATT_CHARGE_FULL;
	}
	return capacity;
}

#define DEFAULT_BATT_CHARGE_FULL_DESIGN	0
static int somc_chg_get_prop_batt_charge_full_design(struct smbchg_chip *chip)
{
	int capacity, rc;

	rc = get_property_from_fg(chip,
			POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &capacity);
	if (rc) {
		pr_smb_ext(PR_STATUS, "Couldn't get capacity rc = %d\n", rc);
		capacity = DEFAULT_BATT_CHARGE_FULL_DESIGN;
	}
	return capacity;
}

#define DEFAULT_BATT_CYCLE_COUNT	0
static int somc_chg_get_prop_batt_cycle_count(struct smbchg_chip *chip)
{
	int count, rc;

	rc = get_property_from_fg(chip,
			POWER_SUPPLY_PROP_CYCLE_COUNT, &count);
	if (rc) {
		pr_smb_ext(PR_STATUS, "Couldn't get cycle count rc = %d\n", rc);
		count = DEFAULT_BATT_CYCLE_COUNT;
	}
	return count;
}

static int somc_chg_get_fv_cmp_cfg(struct smbchg_chip *chip)
{
	int ret;
	u8 reg;

	ret = smbchg_read(chip, &reg,
		chip->chgr_base + VFLOAT_CMP_CFG_REG, 1);
	if (ret) {
		dev_err(chip->dev, "Can't read VFLOAT_CMP_CFG: %d\n", ret);
		return ret;
	}
	return (int)reg;
}

#define UNPLUG_WAKE_PERIOD		(3 * HZ)
void somc_unplug_wakelock(struct chg_somc_params *params)
{
	wake_lock_timeout(&params->unplug_wakelock, UNPLUG_WAKE_PERIOD);
}

static void somc_chg_set_low_batt_suspend_en(struct smbchg_chip *chip)
{
	int rc;

	rc = vote(chip->usb_suspend_votable, LOW_BATT_EN_VOTER, true, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set usb suspend rc %d\n", rc);

	rc = vote(chip->dc_suspend_votable, LOW_BATT_EN_VOTER, true, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set dc suspend rc %d\n", rc);
}

#define LOW_BATT_CAPACITY	0
static void somc_chg_shutdown_lowbatt(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;
	int capacity, rc;

	rc = get_property_from_fg(chip, POWER_SUPPLY_PROP_CAPACITY, &capacity);
	if (rc) {
		pr_smb_ext(PR_STATUS, "Couldn't get capacity rc = %d\n", rc);
		return;
	}
	if (capacity == LOW_BATT_CAPACITY &&
			params->low_batt.shutdown_enabled) {
		pr_smb_ext(PR_INFO, "capacity: %d, low battery shutdown\n",
					capacity);
		somc_chg_set_low_batt_suspend_en(chip);
		if (chip->usb_online)
			schedule_work(&chip->usb_set_online_work);
		if (chip->dc_present)
			chip->dc_present = 0;
	}
}

static void somc_chg_stepchg_set_fastchg_ma(struct smbchg_chip *chip,
			int current_ma)
{
	int rc = 0;

	pr_smb_ext(PR_STEP_CHG,
		"fastchg-ma changed to %dma for stepchg\n", current_ma);
	if (current_ma)
		rc = vote(chip->fcc_votable, STEP_FCC_VOTER, true, current_ma);
	else
		rc = vote(chip->fcc_votable, STEP_FCC_VOTER, false, 0);
	if (rc < 0)
		pr_err("Couldn't vote for fastchg current rc=%d\n", rc);
}

static void somc_chg_check_soc(struct smbchg_chip *chip,
			int current_soc)
{
	struct chg_somc_params *params = &chip->somc_params;
	bool prev_is_step_chg;
	int current_ma;

	if (!params->step_chg.enabled) {
		pr_smb_ext(PR_STEP_CHG, "step chg not support\n");
		return;
	}

	pr_smb_ext(PR_STEP_CHG, "soc=%d prev_soc=%d prev_step_chg=%d\n",
		current_soc, params->step_chg.prev_soc,
		params->step_chg.is_step_chg);
	prev_is_step_chg = params->step_chg.is_step_chg;
	if (current_soc != params->step_chg.prev_soc) {
		params->step_chg.prev_soc = current_soc;
		params->step_chg.is_step_chg =
			current_soc < params->step_chg.thresh ?
			true : false;
	}

	if (params->step_chg.is_step_chg == prev_is_step_chg) {
		pr_smb_ext(PR_STEP_CHG, "step charge does not change\n");
		return;
	}

	if (params->step_chg.is_step_chg)
		current_ma = 0;
	else
		current_ma = params->step_chg.current_ma;
	pr_smb_ext(PR_STEP_CHG, "is_step_chg=%d current_ma=%d\n",
		params->step_chg.is_step_chg, current_ma);
	somc_chg_stepchg_set_fastchg_ma(chip, current_ma);
}

/* 
 * If there's no need for the custom code that fixes battery temp readings
 * on LGE_8996 platforms, use SoMC's standard temp status code. */
#ifndef CONFIG_LGE_FIX_BATT_TEMP_READING
#define HOT_BIT		BIT(0)
#define WARM_BIT	BIT(1)
#define COLD_BIT	BIT(2)
#define COOL_BIT	BIT(3)
static int somc_chg_temp_get_status(u8 temp)
{
	int status;

	if (temp & HOT_BIT)
		status = TEMP_STATUS_HOT;
	else if (temp & COLD_BIT)
		status = TEMP_STATUS_COLD;
	else if (temp & WARM_BIT)
		status = TEMP_STATUS_WARM;
	else if (temp & COOL_BIT)
		status = TEMP_STATUS_COOL;
	else
		status = TEMP_STATUS_NORMAL;

	pr_smb_ext(PR_THERM, "status=%d (0x%x)\n", status, temp);

	return status;
}
#else /* Else, use custom code to check temp status on LGE devices. */
static int lge_chg_temp_get_status(struct smbchg_chip *chip)
{
	int status;
	union power_supply_propval batt_temp;
	struct chg_somc_params *params = &chip->somc_params;

	/* Gets current batt temp from qpnp-fg's bms pointer */
	power_supply_get_property(chip->bms_psy,
					POWER_SUPPLY_PROP_TEMP, &batt_temp);
	/*
	 * Checks if the battery can be considered hot, warm,
	 * cold or cool based on the thresholds set on lge devices'
	 * charger dtsi files (anx7688, anx7418, tusb422). If none
	 * of the cases trigger, it is assumed to be on normal temps.
	 */
	if (batt_temp.intval > params->temp_thresh.hot_threshold)
		status = TEMP_STATUS_HOT;
	else if (batt_temp.intval > params->temp_thresh.warm_threshold)
		status = TEMP_STATUS_WARM;
	else if (batt_temp.intval < params->temp_thresh.cold_threshold)
		status = TEMP_STATUS_COLD;
	else if (batt_temp.intval < params->temp_thresh.cool_threshold)
		status = TEMP_STATUS_COOL;
	else
		status = TEMP_STATUS_NORMAL;

	pr_smb_ext(PR_THERM, "[LGE-TEMP]batt_temp=%d | Thresholds(hot|warm|cool|cold): %d | %d | %d | %d \n",
		batt_temp.intval, params->temp_thresh.hot_threshold, params->temp_thresh.warm_threshold,
		params->temp_thresh.cool_threshold, params->temp_thresh.cold_threshold);
	pr_smb_ext(PR_THERM, "[LGE-TEMP] Temp status=%s.\n", *(tempstat_names + status));

	return status;
}
#endif

static void somc_chg_temp_set_fastchg_ma(
			struct smbchg_chip *chip, int mitigation_current_ma)
{
	int rc = 0;
	struct chg_somc_params *params = &chip->somc_params;
	if(params->temp.status != TEMP_STATUS_NORMAL && chip->fastchg_current_ma < mitigation_current_ma) {
		pr_smb_ext(PR_THERM,
			"%s temp detected but current is below mitigation value, ignoring...\n",
			*(tempstat_names + params->temp.status));

		rc = vote(chip->fcc_votable, TEMP_FCC_VOTER, false, 0);
	}
	else if(params->temp.status != TEMP_STATUS_NORMAL) {
		pr_smb_ext(PR_THERM,
			"%s temp detected, fastchg-ma changed to %dma\n",
			*(tempstat_names + params->temp.status), mitigation_current_ma);

		rc = vote(chip->fcc_votable, TEMP_FCC_VOTER, true, mitigation_current_ma);
	}
	 else {
		pr_smb_ext(PR_THERM,
			"fastchg-ma restored due to %s temp\n",
			*(tempstat_names + params->temp.status));

		rc = vote(chip->fcc_votable, TEMP_FCC_VOTER, false, 0);
	}
		
	if (rc < 0)
		pr_err("Couldn't vote for fastchg current rc=%d\n", rc);
}

static void somc_chg_temp_work(struct work_struct *work)
{
	struct somc_temp_state *temp = container_of(work,
						struct somc_temp_state,
						work);
	struct chg_somc_params *params = container_of(temp,
					struct chg_somc_params, temp);
	struct smbchg_chip *chip = container_of(params,
					struct smbchg_chip, somc_params);
	int status, current_ma;

#ifdef CONFIG_LGE_FIX_BATT_TEMP_READING
	/*
	 * SoMC's temp status reporting isn't really working out for us,
	 * so let's swap it with our own implementation that relies directly
	 * on the temp readings and dtsi thresholds instead of bit logic that
	 * gets passed around multiple smb and fg files.
	 */
	status = lge_chg_temp_get_status(chip);
#else
	status = somc_chg_temp_get_status(params->temp.temp_val);
#endif
	params->temp.status = status;

	if (status == params->temp.prev_status) {
		pr_smb_ext(PR_THERM, "batt temp status did not change, still reporting as %s\n", 
			*(tempstat_names + status));
	} else {
	pr_smb_ext(PR_INFO, "batt temp status changed from %s to %s\n",
		*(tempstat_names + params->temp.prev_status), *(tempstat_names + status));
	}
	
	switch (status) {
	case TEMP_STATUS_HOT:
	case TEMP_STATUS_WARM:
		current_ma = params->temp.warm_current_ma;
		break;
	case TEMP_STATUS_COLD:
	case TEMP_STATUS_COOL:
		current_ma = params->temp.cool_current_ma;
		break;
	default:
		current_ma = 0;
		break;
	}
	somc_chg_temp_set_fastchg_ma(chip, current_ma);
	params->temp.prev_status = params->temp.status;
}

#ifndef CONFIG_LGE_FIX_BATT_TEMP_READING /* We don't even use this with LGE temp readings. */
static void somc_chg_temp_status_transition(
			struct chg_somc_params *params, u8 reg)
{
	params->temp.temp_val = reg;
	schedule_work(&params->temp.work);
}
#endif

static int somc_hvdcp_detect(struct smbchg_chip *chip)
{
	int rc = 0;

	pr_smb_ext(PR_THERM, "usb_supply_type = %d\n", chip->usb_supply_type);
	if (is_hvdcp_present(chip)) {
		if (!chip->hvdcp3_supported &&
			(chip->wa_flags & SMBCHG_HVDCP_9V_EN_WA)) {
			/* force HVDCP 2.0 */
			rc = force_9v_hvdcp(chip);
			if (rc)
				pr_err("could not force 9V HVDCP continuing rc=%d\n",
						rc);
		}
		pr_smb_ext(PR_THERM, "setting usb type = USB_HVDCP\n");
		smbchg_change_usb_supply_type(chip,
				POWER_SUPPLY_TYPE_USB_HVDCP);
		if (chip->batt_psy)
			power_supply_changed(chip->batt_psy);
		smbchg_aicl_deglitch_wa_check(chip);
	} else {
		if (chip->usb_supply_type == POWER_SUPPLY_TYPE_USB_HVDCP) {
			pr_smb_ext(PR_THERM, "setting usb type = USB_DCP\n");
			smbchg_change_usb_supply_type(chip,
					POWER_SUPPLY_TYPE_USB_DCP);
		}
	}
	return rc;
}

static bool somc_chg_therm_is_hvdcp_limit(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;
	int therm_lvl = chip->therm_lvl_sel;
	bool enable = true;

	/*
	 * Can't control hvdcp enable/disable with QC3.0.
	 * Therefore, degrease voltage to 5V by thermal_hvdcp3_adjust_work.
	 */
	if (params->hvdcp3.hvdcp3_detected)
		return false;

	if (!chip->thermal_levels || therm_lvl < 0 ||
	    therm_lvl >= chip->thermal_levels) {
		pr_err("Invalid thermal level\n");
		return false;
	}

	pr_smb_ext(PR_THERM, "HVDCP limit is %s\n",
			enable ? "enable" : "disable");
	return enable;
}

static void somc_chg_therm_set_hvdcp_en(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;
	int rc;
	u8 reg, enable = 0;

	if (params->hvdcp3.hvdcp3_detected)
		return;

	rc = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + CHGPTH_CFG, 1);
	if (rc < 0) {
		dev_err(chip->dev, "Can't read CHGPTH_CFG: %d\n", rc);
		return;
	}

	if (!somc_chg_therm_is_hvdcp_limit(chip))
		enable = HVDCP_EN_BIT;
	pr_smb_ext(PR_THERM, "HVDCP change to %s\n",
			enable ? "enable" : "disable");

	if ((reg & HVDCP_EN_BIT) != enable) {
		rc = smbchg_sec_masked_write(chip,
				chip->usb_chgpth_base + CHGPTH_CFG,
				HVDCP_EN_BIT, enable);
		if (rc < 0)
			dev_err(chip->dev,
				"Couldn't %s HVDCP rc=%d\n",
				enable ? "enable" : "disable", rc);
	}

	rc = somc_hvdcp_detect(chip);
	if (rc)
		pr_err("Failed to force 9V HVDCP=%d\n", rc);
}

static bool somc_chg_is_hvdcp_present(struct smbchg_chip *chip)
{
	int rc;
	u8 reg, hvdcp_sel;

	rc = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USBIN_HVDCP_STS, 1);
	if (rc < 0) {
		pr_err("Couldn't read hvdcp status rc = %d\n", rc);
		return false;
	}

	pr_smb(PR_STATUS, "HVDCP_STS = 0x%02x\n", reg);
	if (chip->schg_version == QPNP_SCHG_LITE)
		hvdcp_sel = SCHG_LITE_USBIN_HVDCP_SEL_BIT;
	else
		hvdcp_sel = USBIN_HVDCP_SEL_BIT;

	pr_smb(PR_STATUS, "hvdcp_sel = 0x%02x\n", hvdcp_sel);
	if (reg & hvdcp_sel)
		return true;

	return false;
}

#define SRC_DET_STS	BIT(2)
#define UV_STS		BIT(0)
static bool somc_chg_is_usb_uv_hvdcp(struct smbchg_chip *chip)
{
	int rc, hvdcp = 0;
	u8 reg;

	hvdcp = somc_chg_is_hvdcp_present(chip);

	rc = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + RT_STS, 1);
	if (rc < 0) {
		pr_err("Can't read RT_STS: %d\n", rc);
		return false;
	}
	pr_debug("UV_STS=%d, SRC_DET_STS=%d, hvdcp=%d\n",
			!!(reg & UV_STS), !!(reg & SRC_DET_STS), hvdcp);

	return !!((reg & UV_STS) && (reg & SRC_DET_STS) && hvdcp);
}

static void somc_chg_apsd_rerun_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct somc_apsd *apsd = container_of(dwork,
			struct somc_apsd, rerun_work);
	struct chg_somc_params *params = container_of(apsd,
			struct chg_somc_params, apsd);
	struct smbchg_chip *chip = container_of(params,
			struct smbchg_chip, somc_params);
	int rc;

	if (somc_chg_is_usb_uv_hvdcp(chip)) {
		somc_chg_charge_error_event(chip,
					CHGERR_USBIN_UV_CONNECTED_HVDCP);
		rc = somc_chg_apsd_wait_rerun(chip);
		if (rc)
			dev_err(chip->dev, "APSD rerun error rc=%d\n", rc);
	}
}

#define RERUN_DELAY_MS		500
static void somc_chg_apsd_rerun_check(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;

	cancel_delayed_work_sync(&params->apsd.rerun_work);
	pr_info("apsd_rerun check start\n");
	if (params->apsd.wq && somc_chg_is_hvdcp_present(chip))
		queue_delayed_work(params->apsd.wq, &params->apsd.rerun_work,
				params->apsd.delay_ms ?
				msecs_to_jiffies(params->apsd.delay_ms) :
				msecs_to_jiffies(RERUN_DELAY_MS));
}

static void somc_chg_apsd_rerun(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;

	pr_info("apsd_rerun start\n");
	if (params->apsd.wq && !params->apsd.rerun_wait_irq)
		queue_delayed_work(params->apsd.wq, &params->apsd.rerun_w,
					msecs_to_jiffies(RERUN_DELAY_MS));
}

static void somc_chg_apsd_rerun_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct somc_apsd *apsd = container_of(dwork,
			struct somc_apsd, rerun_w);
	struct chg_somc_params *params = container_of(apsd,
			struct chg_somc_params, apsd);
	struct smbchg_chip *chip = container_of(params,
			struct smbchg_chip, somc_params);
	int rc;

	rc = somc_chg_apsd_wait_rerun(chip);
	if (rc)
		dev_err(chip->dev, "APSD rerun error rc=%d\n", rc);
}

static void somc_chg_remove_work(struct work_struct *work)
{
	struct somc_usb_remove *usb_remove = container_of(work,
				struct somc_usb_remove, work.work);
	struct chg_somc_params *params = container_of(usb_remove,
			struct chg_somc_params, usb_remove);

	if (usb_remove->unplug_key && !params->low_batt.shutdown_enabled) {
		/* key event for power off charge */
		pr_smb_ext(PR_INFO, "input_report_key KEY_F24\n");
		input_report_key(usb_remove->unplug_key, KEY_F24, 1);
		input_sync(usb_remove->unplug_key);
		input_report_key(usb_remove->unplug_key, KEY_F24, 0);
		input_sync(usb_remove->unplug_key);
	}
}

#define DEFAULT_BATTERY_TYPE	"Unknown"
static const char *somc_chg_get_prop_battery_type(struct smbchg_chip *chip)
{
	int rc;
	union power_supply_propval ret = {0, };

	if (!chip->bms_psy && chip->bms_psy_name)
		chip->bms_psy =
			power_supply_get_by_name((char *)chip->bms_psy_name);
	if (!chip->bms_psy) {
		pr_smb(PR_STATUS, "no bms psy found\n");
		return DEFAULT_BATTERY_TYPE;
	}

	rc = power_supply_get_property(chip->bms_psy,
					POWER_SUPPLY_PROP_BATTERY_TYPE, &ret);
	if (rc) {
		pr_smb(PR_STATUS,
			"bms psy doesn't support reading prop %d rc = %d\n",
			POWER_SUPPLY_PROP_BATTERY_TYPE, rc);
		return DEFAULT_BATTERY_TYPE;
	}

	return ret.strval;
}

static int somc_set_fastchg_current_qns(struct smbchg_chip *chip,
							int current_ma)
{
	int rc = 0;

	pr_smb(PR_STATUS, "QNS setting FCC to %d\n", current_ma);

	rc = vote(chip->fcc_votable, QNS_FCC_VOTER, true, current_ma);
	if (rc < 0)
		pr_err("Couldn't vote en rc %d\n", rc);
	return rc;
}

static void somc_chg_hvdcp3_preparing_set(struct smbchg_chip *chip,
							bool enabled)
{
	chip->somc_params.hvdcp3.preparing = enabled;
	chip->somc_params.hvdcp3.thermal_timeout_cnt = 0;
}

#define HVDCP3_THERM_ADJUST_POL_MS 500
static void somc_chg_hvdcp3_therm_adjust_start(struct smbchg_chip *chip, int ms)
{
	struct somc_hvdcp3 *hvdcp3_params = &chip->somc_params.hvdcp3;

	pr_smb(PR_SOMC, "schedule thermal_hvdcp3_adjust_work %d\n", ms);
	schedule_delayed_work(&hvdcp3_params->thermal_hvdcp3_adjust_work,
				msecs_to_jiffies(ms));
}

static void somc_chg_hvdcp3_therm_adjust_stop(struct smbchg_chip *chip)
{
	struct somc_hvdcp3 *hvdcp3_params = &chip->somc_params.hvdcp3;

	hvdcp3_params->thermal_pulse_cnt = 0;
	pr_smb(PR_SOMC, "cancel thermal_hvdcp3_adjust_work\n");
	cancel_delayed_work_sync(&hvdcp3_params->thermal_hvdcp3_adjust_work);
}

#define INPUT_CURRENT_STATE_START_DELAY_MS	10000
#define INPUT_CURRENT_STATE_DELAY_MS	60000
static void somc_chg_input_current_worker_start(struct smbchg_chip *chip)
{
	chip->somc_params.input_current.input_current_cnt = 0;
	chip->somc_params.input_current.input_current_sum = 0;
	cancel_delayed_work_sync(
			&chip->somc_params.input_current.input_current_work);
	schedule_delayed_work(
			&chip->somc_params.input_current.input_current_work,
			msecs_to_jiffies(INPUT_CURRENT_STATE_START_DELAY_MS));
}

static void somc_chg_input_current_state(struct work_struct *work)
{
	int aicl_ma = 0;

	struct smbchg_chip *chip = container_of(work,
			struct smbchg_chip,
			somc_params.input_current.input_current_work.work);
	struct chg_somc_params *params = &chip->somc_params;

	const char *icl_voter
		= get_effective_client_locked(chip->usb_icl_votable);
	const char *fcc_voter
		= get_effective_client_locked(chip->fcc_votable);

	if (!chip->usb_present)
		return;

	if (get_prop_charge_type(chip) == POWER_SUPPLY_CHARGE_TYPE_FAST &&
		(strcmp(icl_voter, PSY_ICL_VOTER) == 0) &&
		(strcmp(fcc_voter, BATT_TYPE_FCC_VOTER) == 0)) {
		aicl_ma = smbchg_get_aicl_level_ma(chip);
		if (aicl_ma) {
			params->input_current.input_current_cnt++;
			params->input_current.input_current_sum =
				(u64)aicl_ma +
				params->input_current.input_current_sum;
			if (params->input_current.input_current_cnt) {
				params->input_current.input_current_ave =
				(int)(params->input_current.input_current_sum /
				(u64)params->input_current.input_current_cnt);
			}
		}
	}
	schedule_delayed_work(
			&params->input_current.input_current_work,
			msecs_to_jiffies(INPUT_CURRENT_STATE_DELAY_MS));
}

#define PRINTVOTE(buffer, sz, vot, what) \
	sz += scnprintf(buffer + sz, (PAGE_SIZE - size), \
		"%d,", get_client_vote(vot, what));


static ssize_t somc_chg_output_voter_param(struct smbchg_chip *chip,
			char *buf, int buf_size,
			struct votable *votable,
			enum voters_type vtype)
{
	int size = 0;

	switch (vtype) {
		case VOTERS_FCC:
			PRINTVOTE(buf, size, votable, ESR_PULSE_FCC_VOTER);
			PRINTVOTE(buf, size, votable, BATT_TYPE_FCC_VOTER);
			PRINTVOTE(buf, size, votable,RESTRICTED_CHG_FCC_VOTER);
			PRINTVOTE(buf, size, votable, TEMP_FCC_VOTER);
			PRINTVOTE(buf, size, votable, THERMAL_FCC_VOTER);
			PRINTVOTE(buf, size, votable, STEP_FCC_VOTER);
			PRINTVOTE(buf, size, votable, QNS_FCC_VOTER);
			break;
		case VOTERS_ICL:
			PRINTVOTE(buf, size, votable, PSY_ICL_VOTER);
			PRINTVOTE(buf, size, votable, THERMAL_ICL_VOTER);
			PRINTVOTE(buf, size, votable, HVDCP_ICL_VOTER);
			PRINTVOTE(buf, size, votable, USER_ICL_VOTER);
			PRINTVOTE(buf, size, votable, WEAK_CHARGER_ICL_VOTER);
			PRINTVOTE(buf, size, votable, SW_AICL_ICL_VOTER);
			PRINTVOTE(buf, size, votable,
					CHG_SUSPEND_WORKAROUND_ICL_VOTER);
			break;
		case VOTERS_EN:
			PRINTVOTE(buf, size, votable, USER_EN_VOTER);
			PRINTVOTE(buf, size, votable, POWER_SUPPLY_EN_VOTER);
			PRINTVOTE(buf, size, votable, USB_EN_VOTER);
			PRINTVOTE(buf, size, votable, THERMAL_EN_VOTER);
			PRINTVOTE(buf, size, votable, OTG_EN_VOTER);
			PRINTVOTE(buf, size, votable, WEAK_CHARGER_EN_VOTER);
			PRINTVOTE(buf, size, votable, FAKE_BATTERY_EN_VOTER);
			PRINTVOTE(buf, size, votable, LOW_BATT_EN_VOTER);
			break;
		case VOTERS_BATTCHG:
			PRINTVOTE(buf, size, votable, BATTCHG_USER_EN_VOTER);
			PRINTVOTE(buf, size, votable,
					BATTCHG_UNKNOWN_BATTERY_EN_VOTER);
			break;
		default:
			break;
	}

	return size;
}

static void somc_chg_charge_error_event(struct smbchg_chip *chip,
							u32 chgerr_evt)
{
	struct somc_charge_error *charge_error =
					&chip->somc_params.charge_error;

	if (!(charge_error->status & chgerr_evt)) {
		charge_error->status |= chgerr_evt;
		pr_smb(PR_SOMC, "send charge error status (%08x)\n",
							charge_error->status);
		power_supply_changed(chip->batt_psy);
	}
}

static void somc_chg_set_last_uv_time(struct smbchg_chip *chip)
{
	struct somc_charge_error *charge_error =
					&chip->somc_params.charge_error;

	charge_error->last_uv_time_kt = ktime_get_boottime();
}

#define UV_PERIOD_VERY_SHORT_MS		35
#define UNACCEPTABLE_SHORT_UV_COUNT	2
static void somc_chg_check_short_uv(struct smbchg_chip *chip)
{
	ktime_t now_kt, uv_period_kt;
	s64 uv_period_ms;
	struct somc_charge_error *charge_error =
					&chip->somc_params.charge_error;

	now_kt = ktime_get_boottime();
	uv_period_kt = ktime_sub(now_kt, charge_error->last_uv_time_kt);
	uv_period_ms = ktime_to_ms(uv_period_kt);
	if (uv_period_ms > UV_PERIOD_VERY_SHORT_MS)
		return;

	charge_error->short_uv_count++;
	if (charge_error->short_uv_count == UNACCEPTABLE_SHORT_UV_COUNT)
		somc_chg_charge_error_event(chip, CHGERR_USBIN_SHORT_UV);
}

static void somc_chg_reset_charge_error_status_work(struct work_struct *work)
{
	struct somc_charge_error *charge_error = container_of(work,
						struct somc_charge_error,
						status_reset_work.work);

	charge_error->status = 0;
	charge_error->last_uv_time_kt = ktime_set(0, 0);
	charge_error->short_uv_count = 0;
}

#define CHGERR_STS_RESET_DELAY_MS	4000
static void somc_chg_start_charge_error_status_resetting(
						struct smbchg_chip *chip)
{
	struct somc_charge_error *charge_error =
					&chip->somc_params.charge_error;

	schedule_delayed_work(&charge_error->status_reset_work,
				msecs_to_jiffies(CHGERR_STS_RESET_DELAY_MS));
}

static void somc_chg_cancel_charge_error_status_resetting(
						struct smbchg_chip *chip)
{
	struct somc_charge_error *charge_error =
					&chip->somc_params.charge_error;

	cancel_delayed_work_sync(&charge_error->status_reset_work);
}

static ssize_t somc_chg_param_show(struct device *dev,
				struct device_attribute *attr,
				char *buf);
static ssize_t somc_chg_param_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);

static struct device_attribute somc_chg_attrs[] = {
	__ATTR(fv_sts,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(fv_cfg,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(fcc_cfg,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(ichg_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(chgr_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(chgr_int,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(bat_if_int,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(bat_if_cfg,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_int,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(dc_int,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_icl_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_apsd_dg,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_rid_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_hvdcp_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_cmd_il,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(otg_int,			S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(misc_int,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(misc_idev_sts,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(vfloat_adjust_trim,	S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_max_current,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(dc_max_current,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(dc_target_current,	S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_online,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_present,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(bat_temp_status,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(limit_usb5v_level,	S_IRUGO|S_IWUSR,
					somc_chg_param_show,
					somc_chg_param_store),
	__ATTR(output_batt_log,		S_IRUGO|S_IWUSR,
					somc_chg_param_show,
					somc_chg_param_store),
	__ATTR(apsd_rerun_delay_ms,	S_IRUGO|S_IWUSR,
					somc_chg_param_show,
					somc_chg_param_store),
	__ATTR(ibat_voter,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usbin_voter,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_voter,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(battchg_voter,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(pulse_cnt,		S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(thermal_pulse_cnt,	S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(usb_usbin_il_cfg,	S_IRUGO, somc_chg_param_show, NULL),
	__ATTR(apsd_rerun_status,	S_IRUGO, somc_chg_param_show, NULL),
};

#define FV_STS_ADDR		0x0C
#define FCC_CFG_ADDR		0xF2
#define ICHG_STS_ADDR		0x0D
#define BAT_IF_CFG_ADDR		0xF5
#define USB_APDS_DG_ADDR	0x0A
#define USB_RID_STS_ADDR	0x0B
#define USB_CMD_IL_ADDR		0x40
#define USB_HVDCP_STS_ADDR	0x0C
#define USB_USBIN_IL_CFG_ADDR	0xF2
#define VFLOAT_ADJUST_TRIM	0xFE
static ssize_t somc_chg_param_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct smbchg_chip *chip = dev_get_drvdata(dev);
	struct chg_somc_params *params = &chip->somc_params;
	ssize_t size = 0;
	const ptrdiff_t off = attr - somc_chg_attrs;
	int ret;
	u8 reg, reg2;

	switch (off) {
	case ATTR_FV_STS:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + FV_STS_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read FV_STS: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_FV_CFG:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + VFLOAT_CFG_REG, 1);
		if (ret)
			dev_err(dev, "Can't read FV_CFG: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_FCC_CFG:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + FCC_CFG_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read FCC_CFG: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_ICHG_STS:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + ICHG_STS_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read ICHG_STS: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_CHGR_STS:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + CHGR_STS, 1);
		if (ret)
			dev_err(dev, "Can't read CHGR_STS: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_CHGR_INT:
		ret = smbchg_read(chip, &reg,
			chip->chgr_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read CHGR_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_BAT_IF_INT:
		ret = smbchg_read(chip, &reg,
			chip->bat_if_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read BAT_IF_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_BAT_IF_CFG:
		ret = smbchg_read(chip, &reg,
			chip->bat_if_base + BAT_IF_CFG_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read BAT_IF_CFG: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_INT:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read USB_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_DC_INT:
		ret = smbchg_read(chip, &reg,
			chip->dc_chgpth_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read DC_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_ICL_STS:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + ICL_STS_1_REG, 1);
		if (ret) {
			dev_err(dev, "Can't read USB_ICL_STS1: %d\n", ret);
		} else {
			ret = smbchg_read(chip, &reg2,
				chip->usb_chgpth_base + ICL_STS_2_REG,
					1);
			if (ret)
				dev_err(dev,
					"Can't read USB_ICL_STS2: %d\n", ret);
			else
				size = scnprintf(buf, PAGE_SIZE,
						"0x%02X%02X\n", reg, reg2);
		}
		break;
	case ATTR_USB_APSD_DG:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USB_APDS_DG_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read USB_APSD_DG: %d\n",
					ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_RID_STS:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USB_RID_STS_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read USB_RID_STS: %d\n",
					ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_HVDCP_STS:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USB_HVDCP_STS_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read USB_HVDCP_STS: %d\n",
					ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_CMD_IL:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USB_CMD_IL_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read USB_CMD_IL: %d\n",
					ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_OTG_INT:
		ret = smbchg_read(chip, &reg,
			chip->otg_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read OTG_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_MISC_INT:
		ret = smbchg_read(chip, &reg,
			chip->misc_base + RT_STS, 1);
		if (ret)
			dev_err(dev, "Can't read MISC_INT: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_MISC_IDEV_STS:
		ret = smbchg_read(chip, &reg,
			chip->misc_base + IDEV_STS, 1);
		if (ret)
			dev_err(dev, "Can't read MISC_IDEV_STS: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_VFLOAT_ADJUST_TRIM:
		ret = smbchg_read(chip, &reg,
			chip->misc_base + VFLOAT_ADJUST_TRIM, 1);
		if (ret)
			dev_err(dev,
				"Can't read VFLOAT_ADJUST_TRIM: %d\n", ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_USB_MAX_CURRENT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				chip->usb_max_current_ma);
		break;
	case ATTR_DC_MAX_CURRENT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				chip->dc_max_current_ma);
		break;
	case ATTR_DC_TARGET_CURRENT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				chip->dc_target_current_ma);
		break;
	case ATTR_USB_ONLINE:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				chip->usb_online);
		break;
	case ATTR_USB_PRESENT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)is_usb_present(chip));
		break;
	case ATTR_BAT_TEMP_STATUS:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				params->temp.status);
		break;
	case ATTR_OUTPUT_BATT_LOG:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				params->batt_log.output_period);
		break;
	case ATTR_APSD_RERUN_CHECK_DELAY_MS:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
				params->apsd.delay_ms);
		break;
	case ATTR_IBAT_VOTER:
		size = somc_chg_output_voter_param(chip, buf, PAGE_SIZE,
				chip->fcc_votable, VOTERS_FCC);
		break;
	case ATTR_USBIN_VOTER:
		size = somc_chg_output_voter_param(chip, buf, PAGE_SIZE,
				chip->usb_icl_votable, VOTERS_ICL);
		break;
	case ATTR_USB_VOTER:
		size = somc_chg_output_voter_param(chip, buf, PAGE_SIZE,
				chip->usb_suspend_votable, VOTERS_EN);
		break;
	case ATTR_BATTCHG_VOTER:
		size = somc_chg_output_voter_param(chip, buf, PAGE_SIZE,
				chip->battchg_suspend_votable,
				VOTERS_BATTCHG);
		break;
	case ATTR_PULSE_CNT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n", chip->pulse_cnt);
		break;
	case ATTR_THERMAL_PULSE_CNT:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					params->hvdcp3.thermal_pulse_cnt);
		break;
	case ATTR_USB_USBIN_IL_CFG:
		ret = smbchg_read(chip, &reg,
			chip->usb_chgpth_base + USB_USBIN_IL_CFG_ADDR, 1);
		if (ret)
			dev_err(dev, "Can't read USB_USBIN_IL_CFG: %d\n",
					ret);
		else
			size = scnprintf(buf, PAGE_SIZE, "0x%02X\n", reg);
		break;
	case ATTR_APSD_RERUN_STATUS:
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					(chip->hvdcp_3_det_ignore_uv ||
					params->apsd.rerun_wait_irq) ? 1 : 0);
		break;
	default:
		size = 0;
		break;
	}

	return size;
}

static ssize_t somc_chg_param_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct smbchg_chip *chip = dev_get_drvdata(dev);
	struct chg_somc_params *params = &chip->somc_params;
	const ptrdiff_t off = attr - somc_chg_attrs;
	int ret = -EINVAL;

	switch (off) {
	case ATTR_OUTPUT_BATT_LOG:
		ret = kstrtoint(buf, 10, &params->batt_log.output_period);
		if (ret) {
			pr_err("Can't write output_batt_log: %d\n", ret);
			return ret;
		}
		if (params->batt_log.output_period > 0)
			schedule_delayed_work(&params->batt_log.work,
				msecs_to_jiffies(params->batt_log.output_period
					* 1000));
		break;
	case ATTR_APSD_RERUN_CHECK_DELAY_MS:
		ret = kstrtoint(buf, 10, &params->apsd.delay_ms);
		if (ret) {
			pr_err("Can't write APSD_RERUN_CHECKDELAY_MS: %d\n",
					ret);
			return ret;
		}
		break;
	default:
		break;
	}

	return count;
}

static void somc_chg_init(struct chg_somc_params *params)
{
	INIT_WORK(&params->temp.work, somc_chg_temp_work);
	params->apsd.wq = create_singlethread_workqueue("chg_apsd");
	INIT_DELAYED_WORK(&params->apsd.rerun_work,
			somc_chg_apsd_rerun_check_work);
	INIT_DELAYED_WORK(&params->apsd.rerun_w,
			somc_chg_apsd_rerun_work);
	INIT_DELAYED_WORK(&params->charge_error.status_reset_work,
			somc_chg_reset_charge_error_status_work);
	pr_smb_ext(PR_INFO, "somc chg init success\n");
}

#define SOMC_OF_PROP_READ(dev, node, prop, dt_property, retval, optional) \
do {									\
	if (retval)							\
		break;							\
	if (optional)							\
		prop = 0;						\
									\
	retval = of_property_read_u32(node,				\
					"somc," dt_property	,	\
					&prop);				\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0;						\
	else if (retval)						\
		dev_err(dev, "Error reading " #dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

static int somc_chg_smb_parse_dt(struct smbchg_chip *chip,
			struct device_node *node)
{
	struct chg_somc_params *params = &chip->somc_params;
	int rc = 0;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	SOMC_OF_PROP_READ(chip->dev, node,
		params->chg_det.typec_current_max,
		"typec-current-max", rc, 1);

#ifdef CONFIG_LGE_FIX_BATT_TEMP_READING
	/*
	 * Get qpnp-fg's bms power supply for temp readings,
	 * and also the temp thresholds from the device trees.
	 */
	params->bms_psy = power_supply_get_by_name("bms");

	SOMC_OF_PROP_READ(chip->dev, node,
		params->temp_thresh.hot_threshold,
		"fastchg-hot-threshold", rc, 1);
	SOMC_OF_PROP_READ(chip->dev, node,
		params->temp_thresh.warm_threshold,
		"fastchg-warm-threshold", rc, 1);
	SOMC_OF_PROP_READ(chip->dev, node,
		params->temp_thresh.cool_threshold,
		"fastchg-cool-threshold", rc, 1);
	SOMC_OF_PROP_READ(chip->dev, node,
		params->temp_thresh.cold_threshold,
		"fastchg-cold-threshold", rc, 1);
#endif

#ifdef CONFIG_LGE_ADJUSTABLE_CHARGE_LIMIT
	params->lrc.enabled = of_property_read_bool(node, "somc,adj-charge-limit-enabled");
	if (!params->lrc.enabled) {
		pr_smb_ext(PR_LGE, "Charge limit is not enabled for this device.\n");
	} else {
		pr_smb_ext(PR_LGE, "Charge limit is enabled for this device. Setting up charge limit properties...\n");

		params->lrc.socmax = 100;
		params->lrc.socmin = params->lrc.socmax - 2;
		params->lrc.hysteresis = 0;

		pr_smb_ext(PR_LGE, "Charge limit set up. Initial values: socmax=%d socmin=%d hysteresis=%d\n",
					params->lrc.socmax, params->lrc.socmin, params->lrc.hysteresis);
	}
#endif

	return rc;
}

static int somc_chg_create_sysfs_entries(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(somc_chg_attrs); i++) {
		rc = device_create_file(dev, &somc_chg_attrs[i]);
		if (rc < 0) {
			dev_err(dev, "device_create_file failed rc = %d\n", rc);
			goto revert;
		}
	}
	return 0;

revert:
	for (i = i - 1; i >= 0; i--)
		device_remove_file(dev, &somc_chg_attrs[i]);
	return rc;
}

static void somc_chg_remove_sysfs_entries(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(somc_chg_attrs); i++)
		device_remove_file(dev, &somc_chg_attrs[i]);
}

static int somc_chg_register(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;
	int rc;

	rc = somc_chg_create_sysfs_entries(chip->dev);
	if (rc < 0)
		goto exit;

	/* register input device */
	params->usb_remove.unplug_key = input_allocate_device();
	if (!params->usb_remove.unplug_key) {
		dev_err(chip->dev, "can't allocate unplug virtual button\n");
		rc = -ENOMEM;
		goto exit;
	}
	input_set_capability(params->usb_remove.unplug_key, EV_KEY, KEY_F24);
	params->usb_remove.unplug_key->name = "somc_chg_unplug_key";
	params->usb_remove.unplug_key->dev.parent = chip->dev;

	rc = input_register_device(params->usb_remove.unplug_key);
	if (rc) {
		dev_err(chip->dev, "can't register power key: %d\n", rc);
		rc = -ENOMEM;
		goto free_input_dev;
	}
	wake_lock_init(&params->unplug_wakelock, WAKE_LOCK_SUSPEND,
							"unplug_wakelock");
	INIT_DELAYED_WORK(&params->usb_remove.work, somc_chg_remove_work);
	INIT_DELAYED_WORK(&params->batt_log.work, batt_log_work);
	INIT_DELAYED_WORK(&params->input_current.input_current_work,
					somc_chg_input_current_state);

	pr_smb_ext(PR_INFO, "somc chg register success\n");
	return 0;

free_input_dev:
	input_free_device(params->usb_remove.unplug_key);
exit:
	return rc;
}

static void somc_chg_unregister(struct smbchg_chip *chip)
{
	struct chg_somc_params *params = &chip->somc_params;

	somc_chg_remove_sysfs_entries(chip->dev);
	wake_lock_destroy(&params->unplug_wakelock);
	if (params->apsd.wq)
		destroy_workqueue(params->apsd.wq);
}
