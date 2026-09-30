/*
 * sim_factory_nv.c - common default values for the factory NV family
 *
 * The AT / sleep / logging / audio settings in softap_fac_nv_t are
 * essentially identical across the XY SDK family; common defaults are
 * provided here. Pin assignments (SIM / JTAG / debug serial port / power
 * control etc.) differ per chip and are overridden at startup by
 * target-xxx through the sim_target_hooks_t.factory_nv_init callback.
 *
 * Originally defined in sim_shims.c; split out here to remove the problem
 * of "chip-specific values hardcoded in the common layer".
 */

#include "factory_nv.h"

/* ------------------------------------------------------------------ */
/* Common family defaults: all pin fields set to 255 (not connected); */
/* chip-specific fields are overridden by the target's factory_nv_init */
/* callback. Non-pin settings keep the current product defaults.      */
/* ------------------------------------------------------------------ */

softap_fac_nv_t sim_fac_nv = {
    .deepsleep_enable     = 1,
    .sleep_enable         = 1,
    .lightsleep_enable    = 1,
    .wfi_enable           = 1,
    .sleep_mode           = 1,
    .sleep_delay          = 1,
    .vddc_vsel            = 2,
    .sleep_threshold      = 0,
    .deepsleep_threshold  = 2000,
    .freq_hop             = 7,
    .gpio_ret             = 3,
    .rccomp_cfg           = 802,

    .utc_wdt_sec          = 10,
    .off_debug            = 0,

    .lpuart_wakup         = 1,

    .open_log             = 1,
    .log_port             = 2,
    .log_wireshark        = 1,

    .at_uart_rate         = 48,
    .urc_cfg              = 253,
    .atPortCfg            = 253,
    .ap_softheap_size     = 16,
    .cmee_mode            = 1,
    .ate                  = 0,
    .ats3                 = 13,
    .ats4                 = 10,
    .ats5                 = 8,
    .atq                  = 0,
    .atv                  = 1,
    .atx                  = 4,
    .ats0                 = 0,

    /* ---- Pin fields: common default 255 (not connected); chip-specific values overridden by target ---- */
    .main_rts_pin         = 255,
    .main_cts_pin         = 255,
    .main_dcd_pin         = 255,
    .main_dsr_pin         = 255,
    .main_dtr_pin         = 255,
    .aux_cts_pin          = 255,
    .aux_rts_pin          = 255,
    .uartDCDCtl           = 1,
    .uart0_rate           = 255,
    .uart0_rx_pin         = 255,
    .uart0_tx_pin         = 255,

    .zone                 = 32,
    .udpUlfcLimit         = 32,

    .usb_mode             = 31,
    .usb_attr             = 1,
    .ulfcHeapLimit        = 16,

    .ripin_ctl            = 255,
    .ringRiPulseCnt       = 1,
    .ringRiPulseDur       = 12,
    .ringRiPulsePeriod    = 240,
    .urcRiPulseDur        = 120,
    .urcRiPulsePeriod     = 240,
    .urcRiPulseCnt        = 1,
    .riOutputCarrier      = 1,

    /* ---- The following are chip-specific pins; the target MUST override them ---- */
    .wkuprst_ctl          = 255,
    .pwrkey_ctl           = 255,
    .cpjlink              = {255, 255},
    .csp_log_tx           = 255,
    .csp_log_rx           = 255,
    .debug_log_tx         = 255,
    .gnss_baudrate        = 48,
    .sim0_det             = 255,
    .sim1_det             = 255,
    .sim1_vcc             = 255,
    .sim1_rst             = 255,
    .sim1_clk             = 255,
    .sim1_data            = 255,
    .pad_adc0             = 255,
    .pad_adc1             = 255,
    .pad_pwm0             = 255,
    .pad_pwm1             = 255,
    .pad_io0              = 255,
    .pad_io1              = 255,
    .pad_io2              = 255,
    .pad_io3              = 255,
    .state_pin1           = 255,
    .state_pin2           = 255,
    .state_pin3           = 255,

    /* ---- Audio: common across the family ---- */
    .DAC_digital_gain     = 191,
    .ADC_analog_gain      = 5,
    .ADC_digital_gain     = 191,
    .dtmf_tone_level      = -10,
    .dtmf_tone_duration   = 160,
    .super_tone_level1    = -12,
    .super_tone_level2    = -12,
};

softap_fac_nv_t *g_softap_fac_nv = &sim_fac_nv;