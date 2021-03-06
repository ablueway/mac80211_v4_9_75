/*
 * Copyright (c) 2015 iComm Semiconductor Ltd.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SSV6006_PRIV_H_
#define _SSV6006_PRIV_H_

#include <smac/ssv_rc_minstrel.h>

//--
void ssv_attach_ssv6006_common(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6051_phy(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6051_cabrioA_BBRF(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_phy(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006c_phy(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006c_mac(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_cabrioA_BBRF(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_geminiA_BBRF(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_turismoA_BBRF(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_turismoB_BBRF(struct ssv_hal_ops *hal_ops);
void ssv_attach_ssv6006_turismoC_BBRF(struct ssv_hal_ops *hal_ops);
bool ssv6006_rc_get_previous_ampdu_rpt(struct ssv_minstrel_ht_sta *mhs, int pkt_no, int *rpt_idx);
void ssv6006_rc_add_ampdu_rpt_to_list(struct ssv_softc *sc, struct ssv_minstrel_ht_sta *mhs, void *rate_rpt, 
			int pkt_no, int ampdu_len, int ampdu_ack_len);
int ssv6006_get_pa_band(int ch);

#endif // _SSV6006_PRIV_H_