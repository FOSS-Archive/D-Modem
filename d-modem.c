/* 
 * Copyright (C) 2021 Aon plc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#include <pjsua-lib/pjsua.h>

#define SIGNATURE PJMEDIA_SIG_CLASS_PORT_AUD('D','M')

struct dmodem {
	pjmedia_port base;
	pj_timestamp timestamp;
	pj_sock_t sock;
};

static struct dmodem port;
static bool destroying = false;
static pj_pool_t *pool;
static int ctrl_sock = -1;
static pjsua_call_id current_call = PJSUA_INVALID_ID;
static pjsua_acc_id acc_id;
static char *sip_domain;

static void error_exit(const char *title, pj_status_t status) {
	pjsua_perror(__FILE__, title, status);
	if (!destroying) {
		destroying = true;
		pjsua_destroy();
		exit(1);
	}
}

static pj_status_t dmodem_put_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
	struct dmodem *sm = (struct dmodem *)this_port;
	int len;

	if (frame->type == PJMEDIA_FRAME_TYPE_AUDIO) {
		if ((len=write(sm->sock, frame->buf, frame->size)) != frame->size) {
			error_exit("error writing frame",0);
		}
	}

	return PJ_SUCCESS;
}

static pj_status_t dmodem_get_frame(pjmedia_port *this_port, pjmedia_frame *frame) {
	struct dmodem *sm = (struct dmodem *)this_port;
	frame->size = PJMEDIA_PIA_AVG_FSZ(&this_port->info); // MAX? what is

	int len;
	if ((len=read(sm->sock, frame->buf, frame->size)) != frame->size) {
		error_exit("error reading frame",0);
	}

	frame->timestamp.u64 = sm->timestamp.u64;
	frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
	sm->timestamp.u64 += PJMEDIA_PIA_SPF(&this_port->info);

	return PJ_SUCCESS;
}

static pj_status_t dmodem_on_destroy(pjmedia_port *this_port) {
        printf("destroy\n");
        exit(-1);
}

/* Callback called by the library when call's state has changed */
static void on_call_state(pjsua_call_id call_id, pjsip_event *e) {
        pjsua_call_info ci;

	PJ_UNUSED_ARG(e);

        pjsua_call_get_info(call_id, &ci);
        PJ_LOG(3,(__FILE__, "Call %d state=%.*s", call_id,
                                (int)ci.state_text.slen,
                                ci.state_text.ptr));

        if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
                if (ctrl_sock != -1)
                        write(ctrl_sock, "CONNECT\n", 8);
        }

        if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
                if (ctrl_sock != -1)
                        write(ctrl_sock, "DISCONNECT\n", 11);
                current_call = PJSUA_INVALID_ID;
                close(port.sock);
                if (!destroying) {
                        destroying = true;
                        pjsua_destroy();
                        exit(0);
                }
        }
}

/* Callback called by the library when call's media state has changed */
static void on_call_media_state(pjsua_call_id call_id) {
	pjsua_call_info ci;
	pjsua_conf_port_id port_id;
	static int done=0;

	pjsua_call_get_info(call_id, &ci);

//	printf("media_status %d media_cnt %d ci.conf_slot %d aud.conf_slot %d\n",ci.media_status,ci.media_cnt,ci.conf_slot,ci.media[0].stream.aud.conf_slot);
	if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
		if (!done) {
			pjsua_conf_add_port(pool, &port.base, &port_id);
			pjsua_conf_connect(ci.conf_slot, port_id);
			pjsua_conf_connect(port_id, ci.conf_slot);
			done = 1;
		}
	} else {
		done = 0;
	}
}

static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id, pjsip_rx_data *rdata) {
        PJ_UNUSED_ARG(acc_id);
        PJ_UNUSED_ARG(rdata);
        current_call = call_id;
        if (ctrl_sock != -1)
                write(ctrl_sock, "RING\n", 5);
}


int main(int argc, char *argv[]) {
        pj_status_t status;

        if (argc < 4)
                return -1;

        signal(SIGPIPE,SIG_IGN);

        bool listen_only = strcmp(argv[1], "-") == 0;
        char *dialstr = listen_only ? NULL : argv[1];
        port.sock = atoi(argv[2]);
        ctrl_sock = atoi(argv[3]);

        char *sip_login = getenv("SIP_LOGIN");
        if (!sip_login)
                return -1;
        char *user = sip_login;
        char *domain = strchr(user,'@');
        if (!domain)
                return -1;
        *domain++ = '\0';
        char *pass = strchr(user,':');
        if (!pass)
                return -1;
        *pass++ = '\0';
        sip_domain = domain;

        status = pjsua_create();
        if (status != PJ_SUCCESS) error_exit("Error in pjsua_create()", status);

        {
                pjsua_config cfg;
                pjsua_logging_config log_cfg;
                pjsua_media_config med_cfg;

                pjsua_config_default(&cfg);
                cfg.cb.on_call_media_state = &on_call_media_state;
                cfg.cb.on_call_state = &on_call_state;
                cfg.cb.on_incoming_call = &on_incoming_call;

                pjsua_logging_config_default(&log_cfg);
                log_cfg.console_level = 4;

                pjsua_media_config_default(&med_cfg);
                med_cfg.no_vad = true;
                med_cfg.ec_tail_len = 0;
                med_cfg.jb_max = 2000;
                med_cfg.audio_frame_ptime = 5;

                status = pjsua_init(&cfg, &log_cfg, &med_cfg);
                if (status != PJ_SUCCESS) error_exit("Error in pjsua_init()", status);
        }

        pjsua_set_ec(0,0);
        pjsua_set_null_snd_dev();

        pjsua_codec_info codecs[32];
        unsigned count = sizeof(codecs)/sizeof(*codecs);
        pjsua_enum_codecs(codecs,&count);
        for (int i=0; i<count; i++) {
                int pri = 0;
                if (pj_strcmp2(&codecs[i].codec_id,"PCMU/8000/1") == 0)
                        pri = 1;
                else if (pj_strcmp2(&codecs[i].codec_id,"PCMA/8000/1") == 0)
                        pri = 2;
                pjsua_codec_set_priority(&codecs[i].codec_id, pri);
        }

        {
                pjsua_transport_config cfg;
                pjsua_transport_config_default(&cfg);
                cfg.port = 5060;
                status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, NULL);
                if (status != PJ_SUCCESS) error_exit("Error creating transport", status);
        }

        pj_caching_pool cp;
        pj_caching_pool_init(&cp, NULL, 1024*1024);
        pool = pj_pool_create(&cp.factory, "pool1", 4000, 4000, NULL);

        pj_str_t name = pj_str("dmodem");
        memset(&port,0,sizeof(port));
        pjmedia_port_info_init(&port.base.info, &name, SIGNATURE, 9600, 1, 16, 192);
        port.base.put_frame = dmodem_put_frame;
        port.base.get_frame = dmodem_get_frame;
        port.base.on_destroy = dmodem_on_destroy;

        char buf[384];
        memset(buf,0,sizeof(buf));
        write(port.sock, buf, sizeof(buf));

        status = pjsua_start();
        if (status != PJ_SUCCESS) error_exit("Error starting pjsua", status);

        {
                pjsua_acc_config cfg;
                pjsua_acc_config_default(&cfg);
                snprintf(buf,sizeof(buf),"sip:%s@%s",user,domain);
                pj_strdup2(pool,&cfg.id,buf);
                snprintf(buf,sizeof(buf),"sip:%s",domain);
                pj_strdup2(pool,&cfg.reg_uri,buf);
                cfg.register_on_acc_add = true;
                cfg.cred_count = 1;
                cfg.cred_info[0].realm = pj_str("*");
                cfg.cred_info[0].scheme = pj_str("digest");
                cfg.cred_info[0].username = pj_str(user);
                cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
                cfg.cred_info[0].data = pj_str(pass);

                status = pjsua_acc_add(&cfg, PJ_TRUE, &acc_id);
                if (status != PJ_SUCCESS) error_exit("Error adding account", status);
        }

        if (!listen_only && dialstr) {
                snprintf(buf,sizeof(buf),"sip:%s@%s",dialstr,domain);
                pj_str_t uri = pj_str(buf);
                status = pjsua_call_make_call(acc_id, &uri, 0, NULL, NULL, &current_call);
                if (status != PJ_SUCCESS) error_exit("Error making call", status);
        }

        while(1) {
                fd_set rset;
                FD_ZERO(&rset);
                FD_SET(ctrl_sock,&rset);
                select(ctrl_sock+1,&rset,NULL,NULL,NULL);
                if (FD_ISSET(ctrl_sock,&rset)) {
                        int n = read(ctrl_sock, buf, sizeof(buf)-1);
                        if (n <= 0)
                                break;
                        buf[n] = '\0';
                        char *nl = strchr(buf,'\n');
                        if (nl) *nl = '\0';
                        if (strncmp(buf,"ANSWER",6) == 0) {
                                if (current_call != PJSUA_INVALID_ID)
                                        pjsua_call_answer(current_call, 200, NULL, NULL);
                        } else if (strncmp(buf,"HANGUP",6) == 0) {
                                if (current_call != PJSUA_INVALID_ID)
                                        pjsua_call_hangup(current_call, 0, NULL, NULL);
                        } else if (strncmp(buf,"DIAL ",5) == 0) {
                                char uri_buf[384];
                                snprintf(uri_buf,sizeof(uri_buf),"sip:%s@%s",buf+5,sip_domain);
                                pj_str_t uri = pj_str(uri_buf);
                                status = pjsua_call_make_call(acc_id, &uri, 0, NULL, NULL, &current_call);
                                if (status != PJ_SUCCESS) error_exit("Error making call", status);
                        }
                }
        }

        return 0;
}
