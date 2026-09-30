/*
 * at_cmd_regist_decl.h -- extern handler declarations needed by sim_at_cmd_regist.c
 *
 * The real device's own headers (at_ping.h / at_socket_default.h / at_fs.h /
 * at_socket_ssl.h / at_http.h / at_ftp_default.h / at_mqtt_default.h)
 * cascade in heavy headers such as socket/lwip/fs, so pulling the full chain
 * into the xyat trim table is not practical.
 * Here the externs are declared centrally per the SDK prototypes, reducing
 * trivial declaration lines in the registration table file.
 *
 * Uniform signature: int handler(char *at_buf, char **prsp_cmd)
 */

#pragma once

/* ---- Phase 1: Ping / Socket ---- */
extern int at_QPING_req(char *at_buf, char **prsp_cmd);   /* at_ping.c */
extern int at_QICFG_req(char *at_buf, char **prsp_cmd);   /* at_socket_default.c */
extern int at_QISDE_req(char *at_buf, char **prsp_cmd);
extern int at_QIOPEN_req(char *at_buf, char **prsp_cmd);
extern int at_QICLOSE_req(char *at_buf, char **prsp_cmd);
extern int at_QISTATE_req(char *at_buf, char **prsp_cmd);
extern int at_QISEND_req(char *at_buf, char **prsp_cmd);
extern int at_QIRD_req(char *at_buf, char **prsp_cmd);
extern int at_QISENDEX_req(char *at_buf, char **prsp_cmd);
extern int at_QISWTMD_req(char *at_buf, char **prsp_cmd);

/* ---- Phase 2: FS (at_fs.c, AT_FS_QUEC) ---- */
#if AT_FS_QUEC
extern int at_QFLDS_req(char *at_buf, char **prsp_cmd);
extern int at_QFLST_req(char *at_buf, char **prsp_cmd);
extern int at_QFDEL_req(char *at_buf, char **prsp_cmd);
extern int at_QFUPL_req(char *at_buf, char **prsp_cmd);
extern int at_QFDWL_req(char *at_buf, char **prsp_cmd);
extern int at_QFOPEN_req(char *at_buf, char **prsp_cmd);
extern int at_QFREAD_req(char *at_buf, char **prsp_cmd);
extern int at_QFWRITE_req(char *at_buf, char **prsp_cmd);
extern int at_QFSEEK_req(char *at_buf, char **prsp_cmd);
extern int at_QFPOSITION_req(char *at_buf, char **prsp_cmd);
extern int at_QFCLOSE_req(char *at_buf, char **prsp_cmd);
#endif /* AT_FS_QUEC */

/* ---- Phase 2: SSL (at_socket_ssl.c, AT_SSL_QUEC) ---- */
#if AT_SSL_QUEC
extern int at_QSSLCFG_req(char *at_buf, char **prsp_cmd);
extern int at_QSSLOPEN_req(char *at_buf, char **prsp_cmd);
extern int at_QSSLSEND_req(char *at_buf, char **prsp_cmd);
extern int at_QSSLRECV_req(char *at_buf, char **prsp_cmd);
extern int at_QSSLCLOSE_req(char *at_buf, char **prsp_cmd);
extern int at_QSSLSTATE_req(char *at_buf, char **prsp_cmd);
#endif /* AT_SSL_QUEC */

/* ---- Phase 2: HTTP (at_http.c, AT_HTTP_QUEC) ---- */
#if AT_HTTP_QUEC
extern int at_QHTTPCFG_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPURL_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPGET_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPGETEX_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPPOST_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPPOSTFILE_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPREAD_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPREADFILE_req(char *at_buf, char **prsp_cmd);
extern int at_QHTTPSTOP_req(char *at_buf, char **prsp_cmd);
#endif /* AT_HTTP_QUEC */

/* ---- Phase 2: FTP (at_ftp_default.c, AT_FTP_QUEC) ---- */
#if AT_FTP_QUEC
extern int at_QFTPCFG_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPOPEN_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPCWD_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPPWD_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPPUT_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPGET_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPSIZE_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPDEL_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPMKDIR_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPRMDIR_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPLIST_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPNLST_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPMLSD_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPMDTM_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPRENAME_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPLEN_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPSTAT_req(char *at_buf, char **prsp_cmd);
extern int at_QFTPCLOSE_req(char *at_buf, char **prsp_cmd);
#endif /* AT_FTP_QUEC */

/* ---- Phase 2: MQTT (at_mqtt_default.c, AT_MQTT_QUEC) ---- */
#if AT_MQTT_QUEC
extern int at_QMTCFG_req(char *at_buf, char **prsp_cmd);
extern int at_QMTOPEN_req(char *at_buf, char **prsp_cmd);
extern int at_QMTCLOSE_req(char *at_buf, char **prsp_cmd);
extern int at_QMTDISC_req(char *at_buf, char **prsp_cmd);
extern int at_QMTCONN_req(char *at_buf, char **prsp_cmd);
extern int at_QMTSUB_req(char *at_buf, char **prsp_cmd);
extern int at_QMTUNS_req(char *at_buf, char **prsp_cmd);
extern int at_QMTPUB_req(char *at_buf, char **prsp_cmd);
extern int at_QMTPUBEX_req(char *at_buf, char **prsp_cmd);
extern int at_QMTRECV_req(char *at_buf, char **prsp_cmd);
#endif /* AT_MQTT_QUEC */

/* ---- CMUX (at_cmux.c, XY_CMUX) ---- */
extern int at_CMUX_req(char *at_buf, char **prsp_cmd);

/* ---- Phase 2: PPP (at_ppp.c, XY_PPP) ---- */
#if XY_PPP
extern int at_ATD_req(char *at_buf, char **prsp_cmd);
extern int at_ATH_req(char *at_buf, char **prsp_cmd);
extern int at_QPPPDROP_req(char *at_buf, char **prsp_cmd);
#endif