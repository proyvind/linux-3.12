/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Copyright (C) 2009-2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>

#include <asm/mach-ralink-openwrt/ralink_regs.h>

#include "ralink_soc_eth.h"

#define RT305X_RESET_FE         BIT(21)
#define RT305X_RESET_ESW        BIT(23)
#define SYSC_REG_RESET_CTRL     0x034

static const u32 rt5350_reg_table[FE_REG_COUNT] = {
	[FE_REG_PDMA_GLO_CFG] = RT5350_PDMA_GLO_CFG,
	[FE_REG_PDMA_RST_CFG] = RT5350_PDMA_RST_CFG,
	[FE_REG_DLY_INT_CFG] = RT5350_DLY_INT_CFG,
	[FE_REG_TX_BASE_PTR0] = RT5350_TX_BASE_PTR0,
	[FE_REG_TX_MAX_CNT0] = RT5350_TX_MAX_CNT0,
	[FE_REG_TX_CTX_IDX0] = RT5350_TX_CTX_IDX0,
	[FE_REG_RX_BASE_PTR0] = RT5350_RX_BASE_PTR0,
	[FE_REG_RX_MAX_CNT0] = RT5350_RX_MAX_CNT0,
	[FE_REG_RX_CALC_IDX0] = RT5350_RX_CALC_IDX0,
	[FE_REG_FE_INT_ENABLE] = RT5350_FE_INT_ENABLE,
	[FE_REG_FE_INT_STATUS] = RT5350_FE_INT_STATUS,
	[FE_REG_FE_DMA_VID_BASE] = 0,
};

static void rt305x_fe_reset(void)
{
	rt_sysc_w32(RT305X_RESET_FE, SYSC_REG_RESET_CTRL);
	rt_sysc_w32(0, SYSC_REG_RESET_CTRL);
}

static void rt5350_set_mac(struct fe_priv *priv, unsigned char *mac)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->page_lock, flags);
	fe_w32((mac[0] << 8) | mac[1], RT5350_SDM_MAC_ADRH);
	fe_w32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		RT5350_SDM_MAC_ADRL);
	spin_unlock_irqrestore(&priv->page_lock, flags);
}

static void rt5350_fwd_config(struct fe_priv *priv)
{
	unsigned long sysclk = priv->sysclk;

	if (sysclk) {
		sysclk /= FE_US_CYC_CNT_DIVISOR;
		sysclk <<= FE_US_CYC_CNT_SHIFT;

		fe_w32((fe_r32(FE_FE_GLO_CFG) &
			~(FE_US_CYC_CNT_MASK << FE_US_CYC_CNT_SHIFT)) | sysclk,
			FE_FE_GLO_CFG);
	}

	fe_w32(fe_r32(RT5350_SDM_CFG) & ~0xffff, RT5350_SDM_CFG);
	fe_w32(fe_r32(RT5350_SDM_CFG) | RT5350_SDM_ICS_EN | RT5350_SDM_TCS_EN | RT5350_SDM_UCS_EN,
		RT5350_SDM_CFG);
}

static void rt5350_fe_reset(void)
{
	rt_sysc_w32(RT305X_RESET_FE | RT305X_RESET_ESW, SYSC_REG_RESET_CTRL);
	rt_sysc_w32(0, SYSC_REG_RESET_CTRL);
}

static struct fe_soc_data rt3050_data = {
	.mac = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
	.reset_fe = rt305x_fe_reset,
	.min_pkt_len = 64,
        .pdma_glo_cfg = FE_PDMA_SIZE_4DWORDS,
	.checksum_bit = RX_DMA_L4VALID,
	.rx_dly_int = FE_RX_DLY_INT,
	.tx_dly_int = FE_TX_DLY_INT,
};

static struct fe_soc_data rt5350_data = {
	.mac = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
	.reg_table = rt5350_reg_table,
	.reset_fe = rt5350_fe_reset,
	.set_mac = rt5350_set_mac,
	.fwd_config = rt5350_fwd_config,
	.min_pkt_len = 64,
        .pdma_glo_cfg = FE_PDMA_SIZE_4DWORDS,
	.checksum_bit = RX_DMA_L4VALID,
	.rx_dly_int = RT5350_RX_DLY_INT,
	.tx_dly_int = RT5350_TX_DLY_INT,
};

const struct of_device_id of_fe_match[] = {
	{ .compatible = "ralink,rt3050-eth", .data = &rt3050_data },
	{ .compatible = "ralink,rt5350-eth", .data = &rt5350_data },
	{},
};

MODULE_DEVICE_TABLE(of, of_fe_match);
