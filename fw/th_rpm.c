/**
 * @file       th_rpm.c
 * @brief      RPM task
 * @author     Vladimir Ermakov Copyright (C) 2014.
 * @see        The GNU Public License (GPL) Version 3
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "alert_led.h"
#include "th_rpm.h"
#include "param.h"
#include <string.h>

#ifndef BOARD_MINIECU_V2
# error "unsupported board"
#endif

/* -*- module settings -*- */
int32_t gp_pulses_per_revolution;
int32_t gp_rpm_limit;
int32_t gp_rpm_min_idle;

/* -*- private data -*- */

#define PERIODS_MAX		24
#define UPDATE_TIMEOUT_US	2000000

static float m_curr_rpm;
static uint32_t m_periods_us[PERIODS_MAX];
static systime_t m_last_update;
static uint32_t m_periods_cnt;
static uint32_t m_periods_idx;
static THD_WORKING_AREA(wa_rpm, RPM_WASZ);

static void period_handler(ICUDriver *icup);
static void empty_handler(ICUDriver *icup ATTR_UNUSED);
static void overflow_handler(ICUDriver *icup ATTR_UNUSED);

static const ICUConfig tim2_cfg = {
	.mode = ICU_INPUT_ACTIVE_LOW,
	.frequency = 1000000,		// 1 MHz
	.width_cb = empty_handler,	// XXX: wait chibios git update with fix
	.period_cb = period_handler,
	.overflow_cb = overflow_handler,
	.channel = ICU_CHANNEL_1,
	.dier = 0
};

/* -*- public functions -*- */

uint32_t rpm_get_filtered(void)
{
	return m_curr_rpm;
}

bool rpm_check_limit(void)
{
	return m_curr_rpm > gp_rpm_limit;
}

bool rpm_is_engine_running(void)
{
	return chVTTimeElapsedSinceX(m_last_update) < US2ST(UPDATE_TIMEOUT_US)
		&& m_curr_rpm > gp_rpm_min_idle;
}

/* -*- local -*- */

static void empty_handler(ICUDriver *icup ATTR_UNUSED)
{
}

static void period_handler(ICUDriver *icup)
{
	// store period in circular buffer
	m_periods_cnt = (m_periods_cnt < PERIODS_MAX)? m_periods_cnt + 1 : PERIODS_MAX;
	m_periods_idx = (m_periods_idx >= PERIODS_MAX)? 0 : m_periods_idx;
	m_periods_us[m_periods_idx++] = icuGetPeriodX(icup);

	m_last_update = osalOsGetSystemTimeX();
}

static void overflow_handler(ICUDriver *icup ATTR_UNUSED)
{
	// clear buffer if overflow
	m_periods_cnt = 0;
	m_periods_idx = 0;
}

static uint32_t get_period_average(void)
{
	uint32_t acc = 0;

	for (size_t i = 0; i < m_periods_cnt; i++)
		acc += m_periods_us[i];

	return acc / m_periods_cnt;
}

static THD_FUNCTION(th_rpm, arg ATTR_UNUSED)
{
	chRegSetThreadName("rpm");

	// setup initial values
	m_periods_idx = 0;
	m_periods_cnt = 0;

	/* Start input capture
	 *
	 * HACK: TIM2 32-bit timer, but driver uses it as 16-bit
	 * by setting ARR to 0xffff.
	 */
	icuStart(&ICUD2, &tim2_cfg);
	ICUD2.tim->ARR = UPDATE_TIMEOUT_US;
	icuStartCapture(&ICUD2);
	icuEnableNotifications(&ICUD2);

	alert_component(ALS_RPM, AL_NORMAL);
	while (true) {
		// Update rate: 10 Hz
		chThdSleepMilliseconds(100);

		uint32_t period = get_period_average();
		systime_t elapsed_time = chVTTimeElapsedSinceX(m_last_update);

		/* Filter out unrealistic period (100 usec ==> RPM 9375.0 with 64 pulses)
		 * Or if timed out.
		 */
		if (period > 100 && elapsed_time < US2ST(UPDATE_TIMEOUT_US))
			m_curr_rpm = 60.0f * 1e6 / (period * gp_pulses_per_revolution);
		else
			m_curr_rpm = 0.0f;
	}

	return MSG_OK;
}

void rpm_init(void)
{
	chThdCreateStatic(wa_rpm, sizeof(wa_rpm), RPM_PRIO, th_rpm, NULL);
}
