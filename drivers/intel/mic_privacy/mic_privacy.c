/*
 * Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "mic_privacy_registers.h"
#include <zephyr/drivers/mic_privacy.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#define LOG_DOMAIN mic_priv_zephyr

#define DT_DRV_COMPAT intel_adsp_mic_privacy

LOG_MODULE_REGISTER(LOG_DOMAIN);


#define CONFIG_ADSP_DMICPVC_ADDRESS      (DT_INST_REG_ADDR(0))
#define CONFIG_ADSP_DFMICPVCP_ADDRESS    (CONFIG_ADSP_DMICPVC_ADDRESS)
#define CONFIG_ADSP_DFMICPVCS_ADDRESS    (CONFIG_ADSP_DMICPVC_ADDRESS + 0x0004)
#define CONFIG_ADSP_DFFWMICPVCCS_ADDRESS (CONFIG_ADSP_DMICPVC_ADDRESS + 0x0006)

#define CONFIG_ADSP_DMICVSSX_ADDRESS     (0x16000)
#define CONFIG_ADSP_DMICXLVSCTL_ADDRESS (CONFIG_ADSP_DMICVSSX_ADDRESS + 0x0004)
#define CONFIG_ADSP_DMICXPVCCS_ADDRESS (CONFIG_ADSP_DMICVSSX_ADDRESS + 0x0010)


static inline void ace_mic_priv_intc_unmask(void)
{
	ACE_DINT[0].ie[ACE_INTL_MIC_PRIV] = BIT(0);
}

static void mic_privacy_enable_fw_managed_irq(bool enable_irq, const void * fn)
{
	union DFFWMICPVCCS pv_ccs;

	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	if (enable_irq) {
		pv_ccs.part.mdstschgie = 1;
	} else {
		pv_ccs.part.mdstschgie = 0;
	}
	sys_write16(pv_ccs.full, CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);

	if (enable_irq && !irq_is_enabled(DT_INST_IRQN(0))) {

		irq_connect_dynamic(DT_INST_IRQN(0), 0,
			fn,
			DEVICE_DT_INST_GET(0), 0);

		irq_enable(DT_INST_IRQN(0));
		ace_mic_priv_intc_unmask();
	}
}

static void mic_privacy_clear_fw_managed_irq(void)
{
	union DFFWMICPVCCS pv_ccs;
	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	pv_ccs.part.mdstschg = 1;
	sys_write16(pv_ccs.full, CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
}

static enum mic_privacy_policy mic_privacy_get_policy(void)
{
	union DFMICPVCP micpvcp;

	micpvcp.full = sys_read32(CONFIG_ADSP_DFMICPVCP_ADDRESS);

	if (micpvcp.part.ddze == 2 && micpvcp.part.ddzpl == 1)
		return HW_MANAGED;
	else if (micpvcp.part.ddze == 2 && micpvcp.part.ddzpl == 0)
		return FW_MANAGED;
	else if (micpvcp.part.ddze == 3)
		return FORCE_MIC_DISABLED;
	else
		return DISABLED;
}

static uint32_t mic_privacy_get_privacy_policy_register_raw_value(void)
{
	return sys_read32(CONFIG_ADSP_DFMICPVCP_ADDRESS);
}

static uint32_t mic_privacy_get_dma_data_zeroing_wait_time(void)
{
	union DFMICPVCP micpvcp;

	micpvcp.full = sys_read32(CONFIG_ADSP_DFMICPVCP_ADDRESS);
	return micpvcp.part.ddzwt;
}

static uint32_t mic_privacy_get_dma_data_zeroing_link_select(void)
{
	union DFMICPVCP micpvcp;

	micpvcp.full = sys_read32(CONFIG_ADSP_DFMICPVCP_ADDRESS);
	return micpvcp.part.ddzls;
}

static uint32_t mic_privacy_get_fw_managed_mic_disable_status(void)
{
	union DFFWMICPVCCS pv_ccs;

	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	return pv_ccs.part.mdsts;
}

static void mic_privacy_set_fw_managed_mode(bool is_fw_managed_enabled)
{
	union DFFWMICPVCCS pv_ccs;

	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	if (is_fw_managed_enabled) {
		pv_ccs.part.fmmd = 1;
	} else {
		pv_ccs.part.fmmd = 0;
	}
	sys_write16(pv_ccs.full, CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
}

static void mic_privacy_set_fw_mic_disable_status(bool fw_mic_disable_status)
{
	union DFFWMICPVCCS pv_ccs;

	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	if (fw_mic_disable_status) {
		pv_ccs.part.fmdsts = 1;
	} else {
		pv_ccs.part.fmdsts = 0;
	}
	sys_write16(pv_ccs.full, CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
}

static uint32_t mic_privacy_get_fw_mic_disable_status(void)
{
	union DFFWMICPVCCS pv_ccs;

	pv_ccs.full = sys_read16(CONFIG_ADSP_DFFWMICPVCCS_ADDRESS);
	return pv_ccs.part.fmdsts;
}

static int intel_adsp_mic_priv_init(const struct device *dev)
{
	return 0;
};

static const struct mic_privacy_api_funcs mic_privacy_ops = {
	.enable_fw_managed_irq = mic_privacy_enable_fw_managed_irq,
	.clear_fw_managed_irq = mic_privacy_clear_fw_managed_irq,
	.get_policy = mic_privacy_get_policy,
	.get_privacy_policy_register_raw_value = mic_privacy_get_privacy_policy_register_raw_value,
	.get_dma_data_zeroing_wait_time = mic_privacy_get_dma_data_zeroing_wait_time,
	.get_dma_data_zeroing_link_select = mic_privacy_get_dma_data_zeroing_link_select,
	.get_fw_managed_mic_disable_status = mic_privacy_get_fw_managed_mic_disable_status,
	.set_fw_managed_mode = mic_privacy_set_fw_managed_mode,
	.set_fw_mic_disable_status = mic_privacy_set_fw_mic_disable_status,
	.get_fw_mic_disable_status = mic_privacy_get_fw_mic_disable_status
};


#define INTEL_ADSP_MIC_PRIVACY_INIT(inst)                                                     \
	static void intel_adsp_mic_priv##inst##_irq_config(void);		\
	                                                                                         \
	static const struct intel_adsp_mic_priv_cfg intel_adsp_mic_priv##inst##_config = {           \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.regblock_size  = DT_INST_REG_SIZE(inst),					   \
	};                                                                                         \
												   \
	static struct intel_adsp_mic_priv_data intel_adsp_mic_priv##inst##_data = {};                \
																\
	DEVICE_DT_INST_DEFINE(inst, &intel_adsp_mic_priv_init,					   \
			      NULL,					   \
			      &intel_adsp_mic_priv##inst##_data,                                    \
			      &intel_adsp_mic_priv##inst##_config, POST_KERNEL,                     \
			      0,                                            \
			      &mic_privacy_ops);		\
									\

DT_INST_FOREACH_STATUS_OKAY(INTEL_ADSP_MIC_PRIVACY_INIT)


