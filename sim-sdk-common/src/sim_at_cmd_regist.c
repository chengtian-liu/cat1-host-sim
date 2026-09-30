/*
 * sim_at_cmd_regist.c -- simulator-trimmed basic command table
 *
 * The real device's application/at_cmd/src/at_cmd_regist.c expands into a
 * 700+ line table via numerous feature macros for cloud vendors/production
 * lines/networks etc.; the simulator enables only the subset relevant to
 * business development whose handler dependencies can all be satisfied on
 * host:
 *
 *   Standard AT protocol commands (E / &F / &V / &W / Z / V / Q / O / S0-5):
 *                    ensure serial tools/gateways complete the handshake
 *
 * From Phase 1, data-plane business commands added (handlers compiled on
 * the xynet side, macros aligned with quec/define.cmake:
 * AT_PING_QUEC/AT_SOCK_QUEC/AT_BASE_QUEC/AT_NTP_QUEC):
 *   at_walltime.c        : CCLK / QLTS / QNTP (compiled from SDK xy_walltime.c)
 *   at_tcpip_cmd.c       : XYACT / QNETDEVCTL / QIDNSCFG / QIDNSGIP / QIGETERROR
 *   at_ping.c            : QPING
 *   at_socket_default.c  : QICFG / QISDE / QIOPEN / QICLOSE / QISTATE /
 *                          QISEND / QIRD / QISENDEX / QISWTMD
 *
 * Table structure and fields are identical to the real device (@see at_cmd_t
 * in at_com.h), terminated by a {0,0} sentinel; walked by proc_from_farps
 * in at_ctl.c. As each group of stubs/real dependencies is completed, the
 * corresponding commands are added back to this table.
 *
 * ===== Prefix format notes (important!) =====
 * The new SDK's atPrefixAndParamParse keeps the head tag (+, &, #, *) in
 * the prefix:
 *   AT+QPING=...  -> prefix "+QPING"
 *   AT&F           -> prefix "&F"
 *   ATE            -> prefix "E" (basic command without head tag)
 * Therefore prefixes in the registry must match the SDK's native
 * at_cmd_regist.c exactly.
 */

#include "at_ctl.h"
#include "at_com.h"
#include "at_utils.h"
#include "xy_at_api.h"

/* The following handlers are defined on the SDK side; at_XYACT_req is a local empty stub */
extern int at_QIDNSCFG_req(char *at_buf, char **prsp_cmd); /* at_socket_default.c */
extern int at_QIDNSGIP_req(char *at_buf, char **prsp_cmd); /* at_socket_default.c */
extern int at_QIGETERROR_req(char *at_buf, char **prsp_cmd);/* at_socket_default.c */
extern int at_QNTP_req(char *at_buf, char **prsp_cmd);     /* at_socket_default.c */

/*
 * Shared stub: all basic commands that "reply OK on receipt" share one
 * handler function.
 * The response MUST be sent via atReply() -- setting *prsp_cmd and returning
 * AT_END is not enough, because proc_at_proxy_req goes straight to
 * goto END_PROC on AT_END: it never calls at_send_to_tty and never resets
 * the channel state. Skipping atReply leaves the AT channel stuck in BUSY
 * forever, with all subsequent commands piling into the pending list with
 * nobody consuming them.
 */
static int at_stub_ok(char *at_buf, char **prsp_cmd)
{
    (void)at_buf;
    (void)prsp_cmd;
    int atHandle = get_current_ttyFd();
    atReply(atHandle, AT_RC_OK, AT_SUCCESS_CODE, NULL);
    return AT_END;
}

/*
 * at_ATE_req -- echo control (ATE0 / ATE1)
 *
 * Reuses the implementation path of the SDK's at_basic_cmd.c directly:
 * atConfigSet(AT_ECHO_MODE_CFG); at_farps_channel.c reads this config on
 * every received AT to decide whether to echo, same as the real device.
 */
static int at_ATE_req(char *at_buf, char **prsp_cmd)
{
    (void)prsp_cmd;
    int atHandle = get_current_ttyFd();
    uint8_t echo_mode = 0;

    if (g_req_type == AT_CMD_ACTIVE)
    {
        if (at_strncasecmp(at_buf, "0V1"))
        {
            /* Windows PPP ATE0V1 combined command, pass through */
        }
        else if (at_parse_param("%1d[0-1]", at_buf, &echo_mode) != XY_OK)
        {
            atReply(atHandle, AT_RC_ERROR, AT_SUCCESS_CODE, NULL);
            return AT_END;
        }
        atConfigSet(atHandle, AT_ECHO_MODE_CFG, echo_mode);
        atReply(atHandle, AT_RC_OK, AT_SUCCESS_CODE, NULL);
    }
    else
    {
        atReply(atHandle, AT_RC_ERROR, AT_SUCCESS_CODE, NULL);
    }
    return AT_END;
}

/* at_XYACT_req: even lighter than at_stub_ok -- does not even reply OK,
 * only marks the command handled. It must still go through the atReply
 * pipeline to ensure the channel state resets correctly. */
static int at_XYACT_req(char *at_buf, char **prsp_cmd)
{
    (void)at_buf;
    (void)prsp_cmd;
    int atHandle = get_current_ttyFd();
    atReply(atHandle, AT_RC_OK, AT_SUCCESS_CODE, NULL);
    return AT_END;
}

#include "at_cmd_regist_decl.h"

struct at_serv_proc_e at_basic_req[] = {
    /* ---- SDK basic commands (at_basic_cmd.h) ----
     * Standard AT protocol commands; serial tools/gateways almost always
     * send them, so they must return OK to pass the handshake even without
     * stubbing. All share the at_stub_ok shared stub function.
     * +NRB/+RESET etc. are Xinyi private commands; +QIREGAPP/QCAMCFG/
     * REQLIC/LIC/TYAUTH etc. would be pure stubs -- if they miss the table
     * they fall through to the virtual PS returning ERROR, which is
     * actually more honest. */
    {"E",      at_ATE_req},
    {"&F",     at_stub_ok},
    {"&V",     at_stub_ok},
    {"&W",     at_stub_ok},
    {"Z",      at_stub_ok},
    {"V",      at_stub_ok},
    {"Q",      at_stub_ok},
    {"O",      at_stub_ok},
    {"S0",     at_stub_ok},
    {"S3",     at_stub_ok},
    {"S4",     at_stub_ok},
    {"S5",     at_stub_ok},

    /* ---- Phase 1 data-plane business (order matches real-device at_cmd_regist.c) ---- */
    /* at_walltime.c: CCLK compiled unconditionally; QLTS belongs to AT_BASE_QUEC; QNTP belongs to AT_NTP_QUEC */
    {"+CCLK",  at_stub_ok},
    {"+QLTS",  at_stub_ok},
    {"+QNTP",  at_QNTP_req},

    /* at_tcpip_cmd.c: XYACT compiled unconditionally, the rest belong to AT_BASE_QUEC.
     * The QSCLK/QPOWD/QCFG/QURCCFG/CIPGSMLOC handlers in the same section on
     * the real device belong to components not compiled in (lowpower/lbs),
     * not registered for now */
    {"+XYACT",      at_XYACT_req},
    {"+QNETDEVCTL", at_stub_ok},
    {"+QIDNSCFG",   at_QIDNSCFG_req},
    {"+QIDNSGIP",   at_QIDNSGIP_req},
    {"+QIGETERROR", at_QIGETERROR_req},

    /* at_ping.c (AT_PING_QUEC) */
    {"+QPING", at_QPING_req},

    /* at_socket_default.c (AT_SOCK_QUEC). Order same as real device: QISEND
     * comes before QISENDEX; the AT framework matches by "name prefix +
     * terminator boundary", so no misjudgment */
    {"+QICFG",    at_QICFG_req},
    {"+QISDE",    at_QISDE_req},
    {"+QIOPEN",   at_QIOPEN_req},
    {"+QICLOSE",  at_QICLOSE_req},
    {"+QISTATE",  at_QISTATE_req},
    {"+QISEND",   at_QISEND_req},
    {"+QIRD",     at_QIRD_req},
    {"+QISENDEX", at_QISENDEX_req},
    {"+QISWTMD",  at_QISWTMD_req},

    /* ---- Phase 2 FS (at_fs.c, AT_FS_QUEC; order same as real device) ----
     * QFMKDIR/QFRMDIR are commented out in the real-device table too, likewise not registered */
#if AT_FS_QUEC
    {"+QFLDS",      at_QFLDS_req},
    {"+QFLST",      at_QFLST_req},
    {"+QFDEL",      at_QFDEL_req},
    {"+QFUPL",      at_QFUPL_req},
    {"+QFDWL",      at_QFDWL_req},
    {"+QFOPEN",     at_QFOPEN_req},
    {"+QFREAD",     at_QFREAD_req},
    {"+QFWRITE",    at_QFWRITE_req},
    {"+QFSEEK",     at_QFSEEK_req},
    {"+QFPOSITION", at_QFPOSITION_req},
    {"+QFCLOSE",    at_QFCLOSE_req},
#endif /* AT_FS_QUEC */

    /* ---- Phase 2 SSL (at_socket_ssl.c, AT_SSL_QUEC; order same as real device) ---- */
#if AT_SSL_QUEC
    {"+QSSLCFG",   at_QSSLCFG_req},
    {"+QSSLOPEN",  at_QSSLOPEN_req},
    {"+QSSLSEND",  at_QSSLSEND_req},
    {"+QSSLRECV",  at_QSSLRECV_req},
    {"+QSSLCLOSE", at_QSSLCLOSE_req},
    {"+QSSLSTATE", at_QSSLSTATE_req},
#endif /* AT_SSL_QUEC */

    /* ---- Phase 2 HTTP (at_http.c, AT_HTTP_QUEC; order same as real device) ---- */
#if AT_HTTP_QUEC
    {"+QHTTPCFG",      at_QHTTPCFG_req},
    {"+QHTTPURL",      at_QHTTPURL_req},
    {"+QHTTPGET",      at_QHTTPGET_req},
    {"+QHTTPGETEX",    at_QHTTPGETEX_req},
    {"+QHTTPPOST",     at_QHTTPPOST_req},
    {"+QHTTPPOSTFILE", at_QHTTPPOSTFILE_req},
    {"+QHTTPREAD",     at_QHTTPREAD_req},
    {"+QHTTPREADFILE", at_QHTTPREADFILE_req},
    {"+QHTTPSTOP",     at_QHTTPSTOP_req},
#endif /* AT_HTTP_QUEC */

    /* ---- Phase 2 FTP (at_ftp_default.c, AT_FTP_QUEC; order same as real device) ---- */
#if AT_FTP_QUEC
    {"+QFTPCFG",    at_QFTPCFG_req},
    {"+QFTPOPEN",   at_QFTPOPEN_req},
    {"+QFTPCWD",    at_QFTPCWD_req},
    {"+QFTPPWD",    at_QFTPPWD_req},
    {"+QFTPPUT",    at_QFTPPUT_req},
    {"+QFTPGET",    at_QFTPGET_req},
    {"+QFTPSIZE",   at_QFTPSIZE_req},
    {"+QFTPDEL",    at_QFTPDEL_req},
    {"+QFTPMKDIR",  at_QFTPMKDIR_req},
    {"+QFTPRMDIR",  at_QFTPRMDIR_req},
    {"+QFTPLIST",   at_QFTPLIST_req},
    {"+QFTPNLST",   at_QFTPNLST_req},
    {"+QFTPMLSD",   at_QFTPMLSD_req},
    {"+QFTPMDTM",   at_QFTPMDTM_req},
    {"+QFTPRENAME", at_QFTPRENAME_req},
    {"+QFTPLEN",    at_QFTPLEN_req},
    {"+QFTPSTAT",   at_QFTPSTAT_req},
    {"+QFTPCLOSE",  at_QFTPCLOSE_req},
#endif /* AT_FTP_QUEC */

    /* ---- Phase 2 MQTT (at_mqtt_default.c, AT_MQTT_QUEC; order same as real device) ---- */
#if AT_MQTT_QUEC
    {"+QMTCFG",   at_QMTCFG_req},
    {"+QMTOPEN",  at_QMTOPEN_req},
    {"+QMTCLOSE", at_QMTCLOSE_req},
    {"+QMTDISC",  at_QMTDISC_req},
    {"+QMTCONN",  at_QMTCONN_req},
    {"+QMTSUB",   at_QMTSUB_req},
    {"+QMTUNS",   at_QMTUNS_req},
    {"+QMTPUB",   at_QMTPUB_req},
    {"+QMTPUBEX", at_QMTPUBEX_req},
    {"+QMTRECV",  at_QMTRECV_req},
#endif /* AT_MQTT_QUEC */

    /* ---- CMUX (at_cmux.c, XY_CMUX) ---- */
    {"+CMUX", at_CMUX_req},

    /* ---- PPP (at_ppp.c, XY_PPP, order matches real-device at_cmd_regist.c) ---- */
#if XY_PPP
    {"D",         at_ATD_req},
    {"H",         at_ATH_req},
    {"+QPPPDROP", at_QPPPDROP_req},
#endif /* XY_PPP */

    {0, 0} /* can not delete!!! */
};

at_cmd_t *g_at_basic_req = at_basic_req;
