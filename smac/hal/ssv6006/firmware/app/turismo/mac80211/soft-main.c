#include <config.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ssv_lib.h>
#include <hdr80211.h>
#include <errno.h>
#include <pbuf.h>
#include <msgevt.h>
#include <common.h>
#include <cmd_def.h>
#include <usb/drv_usb.h>
#include <hwmac/drv_mac.h>
#include <mbox/drv_mbox.h>
#include <log.h>
#include <ssv_pktdef.h>
#include "cmd_engine.h"
#include "soft-main.h"
#include <ieee80211/ieee802_11_defs.h>
#include <pmu/drv_pmu.h>
#include <timer/drv_timer.h>
#include <dbg_timer/dbg_timer.h>
#include <rtc/drv_rtc.h>
#include <phy/drv_phy.h>
#include <p2p/drv_p2p.h>
#include "smart_link.h"
#include "smarticomm_firmware.h"
#include <rtos.h>

#define MAIN_ERROR(fmt, ...)     LOG_ERROR(fmt, ##__VA_ARGS__)

ps_doze_st  g_ps_data;
u32         g_cnt = 0;
u32         g_nullDataCnt = 0;
u32         tim_us_cnt = 0;
bool        g_ps = false;

#ifdef CONFIG_SSV_SMARTLINK
extern int  smart_icomm_state;
#endif //CONFIG_SSV_SMARTLINK

#define BEACON_TIMER    US_TIMER_2

static int ieee802_11_parse_vendor_specific(const u8 *pos, size_t elen,
					                        struct ieee802_11_elems *elems)
{
	unsigned int oui;

	/* first 3 bytes in vendor specific information element are the IEEE
	 * OUI of the vendor. The following byte is used a vendor specific
	 * sub-type. */
	if (elen < 4) {
		LOG_ERROR("short vendor specific information element ignored (len=%lu)\n",(unsigned long) elen);
		
		return -1;
	}


	oui = ((((u32) pos[0]) << 16) | (((u32) pos[1]) << 8) | ((u32) (pos)[2]));
	switch (oui) {
	case OUI_MICROSOFT:
		/* Microsoft/Wi-Fi information elements are further typed and
		 * subtyped */
		switch (pos[3]) {
		case 1:
			/* Microsoft OUI (00:50:F2) with OUI Type 1:
			 * real WPA information element */
			elems->wpa_ie = pos;
			elems->wpa_ie_len = elen;
			break;
		case WMM_OUI_TYPE:
			/* WMM information element */
			if (elen < 5) {
				LOG_ERROR( "short WMM "
					   "information element ignored "
					   "(len=%lu)\n",
					   (unsigned long) elen);
				return -1;
			}
			switch (pos[4]) {
			case WMM_OUI_SUBTYPE_INFORMATION_ELEMENT:
				elems->wmm_info = pos;
				elems->wmm_info_len = elen;
				break;
			case WMM_OUI_SUBTYPE_PARAMETER_ELEMENT:
				elems->wmm_parameter = pos;
				elems->wmm_parameter_len = elen;
				break;
			case WMM_OUI_SUBTYPE_TSPEC_ELEMENT:
				elems->wmm_tspec = pos;
				elems->wmm_tspec_len = elen;
				break;
			default:
				LOG_WARN(  "unknown WMM "
					   "information element ignored "
					   "(subtype=%d len=%lu)\n",
					   pos[4], (unsigned long) elen);
				return -1;
			}
			break;
		case 4:
			/* Wi-Fi Protected Setup (WPS) IE */
			elems->wps_ie = pos;
			elems->wps_ie_len = elen;
			break;
		default:
#if 0			
			LOG_WARN(  "Unknown Microsoft "
				   "information element ignored "
				   "(type=%d len=%lu)\n",
				   pos[3], (unsigned long) elen);
#endif
			return -1;
		}
		break;

	case OUI_BROADCOM:
		switch (pos[3]) {
		case VENDOR_HT_CAPAB_OUI_TYPE:
			elems->vendor_ht_cap = pos;
			elems->vendor_ht_cap_len = elen;
			break;
		default:
			/*LOG_WARN( "Unknown Broadcom "
				   "information element ignored "
				   "(type=%d len=%lu)\n",
				   pos[3], (unsigned long) elen);*/
			return -1;
		}
		break;

	default:
#if 0
		LOG_WARN( "unknown vendor specific information "
			   "element ignored (vendor OUI %02x:%02x:%02x "
			   "len=%lu)\n",
			   pos[0], pos[1], pos[2], (unsigned long) elen);
#endif
		return -1;
	}

	return 0;
}

/**
 * mlme_parse_elems - Parse information elements in management frames
 * @start: Pointer to the start of IEs
 * @len: Length of IE buffer in octets
 * Returns: Parsing result
 */
ParseRes mlme_parse_elems(const u8 *start, size_t len,
				struct ieee802_11_elems *elems)
{
	size_t left = len;
	const u8 *pos = start;
	int unknown = 0;

	memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

    id = *pos++;
    elen = *pos++;
    left -= 2;

		if (elen > left) {
			
				LOG_PRINTF( "IEEE 802.11 element "
					   "parse failed (id=%d elen=%d "
					   "left=%lu)",
					   id, elen, (unsigned long) left);
				
			return ParseFailed;
		}
		//LOG_PRINTF("id=%d\n",id);
		switch (id) {	
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (ieee802_11_parse_vendor_specific(pos, elen,
				elems))
				unknown++;
			break;
		case WLAN_EID_RSN:
			elems->rsn_ie = pos;
			elems->rsn_ie_len = elen;
			break;
		case WLAN_EID_PWR_CAPABILITY:
			elems->power_cap = pos;
			elems->power_cap_len = elen;
			break;
		case WLAN_EID_SUPPORTED_CHANNELS:
			elems->supp_channels = pos;
			elems->supp_channels_len = elen;
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			elems->mdie = pos;
			elems->mdie_len = elen;
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			elems->ftie = pos;
			elems->ftie_len = elen;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		case WLAN_EID_HT_CAP:
			elems->ht_capabilities = pos;
			elems->ht_capabilities_len = elen;
			break;
		case WLAN_EID_HT_OPERATION:
			elems->ht_operation = pos;
			elems->ht_operation_len = elen;
			break;
		case WLAN_EID_LINK_ID:
			if (elen < 18)
				break;
			elems->link_id = pos;
			break;
		case WLAN_EID_INTERWORKING:
			elems->interworking = pos;
			elems->interworking_len = elen;
			break;
		default:
			unknown++;
			//LOG_PRINTF("IEEE 802.11 element parse "
			//	   "ignored unknown element (id=%d elen=%d)",
			//	   id, elen);
			break;
		}

		left -= elen;
		pos += elen;
	}

	if (left)
		return ParseFailed;

	return unknown ? ParseUnknown : ParseOK;
}

void Fill_TxDescriptor(PKT_TxInfo *TxPkt,s32 fixed_field_length)
{
    /* Fill TxPkt descriptor */
    memset((void *)TxPkt, 0, sizeof(PKT_TxInfo));
    TxPkt->len          = HDR80211_MGMT_LEN+(u32)fixed_field_length;
    TxPkt->c_type       = M1_TXREQ;
//    TxPkt->fCmd         = (M_ENG_HWHCI | (M_ENG_CPU<<4) | (M_ENG_TX_MNG<<8));
    TxPkt->fCmd         = (M_ENG_HWHCI | (M_ENG_TX_MNG<<4));
    TxPkt->f80211       = 1;
#if 0
    // do not need set to zero , since it is always set 0 at beginning
    TxPkt->qos          = 0;
    TxPkt->ht           = 0;
    TxPkt->use_4addr    = 0;
    TxPkt->RSVD_0       = 0;

    TxPkt->crate_idx0   = 0;
    TxPkt->drate_idx0   = 0;
    TxPkt->crate_idx1   = 0;
    TxPkt->drate_idx1   = 0;
    TxPkt->crate_idx2   = 0;
    TxPkt->drate_idx2   = 0;
    TxPkt->crate_idx3   = 0;
    TxPkt->drate_idx3   = 0;
#endif
    TxPkt->try_cnt0     = 1; // send for once
    TxPkt->is_last_rate0 = 1;
}

static void mlme_null_send(bool ps_on, bool qos)
{
    u32 size = sizeof(HDR80211_Data);
    PKT_TxInfo *TxPkt;
    HDR80211_Data *data;
    u16 *fc;
    ETHER_ADDR	bssid;
    u32 retry_count = 100;
    s32 extra_size=0;
    //int i;
    
	ETHER_ADDR 	sta_mac;

    //LOG_PRINTF("\nmac=");
    drv_mac_get_sta_mac(&sta_mac.addr[0]);
    //for(i=0;i<ETHER_ADDR_LEN;i++)
    //    LOG_PRINTF("%x ",sta_mac.addr[i]);
    
    //LOG_PRINTF("\n");
    if(qos){
        size+=2;
        extra_size=2;
    }
    drv_mac_get_bssid(&bssid.addr[0]);

    //LOG_PRINTF("\nbssid=");
    //for(i=0;i<ETHER_ADDR_LEN;i++)
    //    LOG_PRINTF("%x ",bssid.addr[i]);
    
    //LOG_PRINTF("\n");
    do {
    	TxPkt = (PKT_TxInfo *)PBUF_MAlloc(size ,TX_BUF);

    	if (TxPkt != NULL)
    	{
    	        memset((u8 *)TxPkt,0,size+drv_mac_get_pbuf_offset());
    		data = (HDR80211_Data *)((u8 *)TxPkt + drv_mac_get_pbuf_offset());
    		break;
    	}

    	if (retry_count--)
    		continue;

 		LOG_PRINTF("No memory to send null data frame.\n");
   		return;
    } while (1);

    memcpy(&data->addr1, bssid.addr, ETHER_ADDR_LEN);
    memcpy(&data->addr2, sta_mac.addr, ETHER_ADDR_LEN);
    memcpy(&data->addr3, bssid.addr, ETHER_ADDR_LEN);

    fc = (u16 *)&data->fc;
    data->dur = 0;

    *fc = M_FC_TODS | FT_DATA;//FST_NULLFUNC;//FST_QOS_NULLFUNC;
    if(qos)
        *fc = *fc | FST_QOS_NULLFUNC;
    else
        *fc = *fc | FST_NULLFUNC;
    
     if (ps_on)
         data->fc.PwrMgmt = 1;
     data->seq_ctrl = 0;

     Fill_TxDescriptor(TxPkt, extra_size);
     TxPkt->do_rts_cts0 = 1;
     
     TX_FRAME((u32)TxPkt);
     //drv_mailbox_cpu_ff((u32)TxPkt, M_ENG_TX_EDCA3);
}

static bool ieee80211_check_tim(struct ieee80211_tim_ie *tim,u8 tim_len, u32 aid)
{
	u8 mask;
	u8 index, indexn1, indexn2;

    //LOG_PRINTF("\nc=%d,p=%d\n ",tim->dtim_count,tim->dtim_period);

	aid &= 0x3fff;
	index = aid / 8;
	mask  = 1 << (aid & 7);

	indexn1 = tim->bitmap_ctrl & 0xfe;
	indexn2 = tim_len + indexn1 - 4;

	if (index < indexn1 || index > indexn2)
		return false;

	index -= indexn1;

	return !!(tim->virtual_map[index] & mask);
}

void mlme_ps_exit(void)
{
    drv_mac_set_rx_flow_data((const u32 *)&g_ps_data.fcmd_data);
    drv_mac_set_rx_flow_mgmt((const u32 *)&g_ps_data.fcmd_mgmt);
    drv_mac_set_rx_flow_ctrl((const u32 *)&g_ps_data.fcmd_ctrl);
        
    g_ps_data.state = PWR_IDLE;
    hwtmr_stop(BEACON_TIMER);
    //drv_mac_set_mtrx_tsp(0);

    g_cnt = 0;
    drv_phy_off();
    drv_phy_bgn();
    drv_phy_on();
    
    mlme_null_send(false, true); 
    g_ps = false;
    LOG_PRINTF("mlme_ps_exit\n");
}


void mlme_active_pmu_sleep(u32 rtc_ticks, bool FromIrq)
{
    OS_EnterCritical();
    
    drv_pmu_setwake_cnt(rtc_ticks);
    drv_pmu_init();
    drv_pmu_sleep();
    drv_pmu_chk();
    //TODO: we should not exit ps if int_event = rtc
    if(!g_ps && drv_pmu_check_interupt_event()==true){                    
        LOG_PRINTF("pmu_check_interupt wakeup!\n");
        mlme_ps_exit();
    }
    OS_LeaveCritical();
}

void mlme_ps_deep(void)
{
#if 1
    mlme_active_pmu_sleep(MAX_RTC_SLEEP_TICKS,false);
#else
    block_all_traffic();
    OS_MsDelay(2000);
    restore_all_traffic();
#endif
    if(g_ps_data.state == PWR_DEEP){
        msg_evt_post_data1(MBOX_SOFT_MAC, MEVT_PS_DEEP, 0,0);
    }
}

void mlme_enter_PS(u32 data)
{
	const u32 mrx_trashcan = M_ENG_MACRX|(M_ENG_TRASH_CAN<<4);
	const u32 mrx_cpu_trashcan = M_ENG_MACRX|(M_ENG_CPU<<4)|(M_ENG_TRASH_CAN<<8);
    LOG_PRINTF("%s+\n", __func__);
    
    g_ps = true;
    
    g_ps_data.timestamp = 0;
    g_cnt =0;
    tim_us_cnt = 0;
    g_nullDataCnt = 0;
    if(0 == data){ //aid =0, Non-associated    
        g_ps_data.state = PWR_DEEP;        
        msg_evt_post_data1(MBOX_SOFT_MAC, MEVT_PS_DEEP, 0,0);
    }else{
        g_ps_data.state = PWR_DOZE;
        mlme_null_send(true, true);  
    }
    g_ps_data.aid = data;
    drv_mac_get_rx_flow_data(&g_ps_data.fcmd_data);
    drv_mac_get_rx_flow_mgmt(&g_ps_data.fcmd_mgmt);
    drv_mac_get_rx_flow_ctrl(&g_ps_data.fcmd_ctrl);
    LOG_PRINTF("data:0x%08x, mgmt:0x%08x, ctrl:0x%08x\n", g_ps_data.fcmd_data, g_ps_data.fcmd_mgmt, g_ps_data.fcmd_ctrl);
    drv_mac_set_rx_flow_data(&mrx_trashcan);
    drv_mac_set_rx_flow_mgmt(&mrx_cpu_trashcan);
    drv_mac_set_rx_flow_ctrl(&mrx_trashcan);
    drv_mac_set_mtrx_tsp(0);
    drv_mac_set_mtrx_tsp(1);

    drv_phy_off();
    if(0 != data){ //aid =0, Non-associated    
        drv_phy_b_only();
        drv_phy_on();
    }
}

static void beacon_loss_notification(void) 
{
    u32 notification_data;
    HDR_HostEvent *host_evt;
    u32 evt_size = sizeof(HDR_HostEvent);

    // Allocate beacon loss notification packet
    do{
        notification_data = (u32)PBUF_MAlloc_Raw(evt_size, 0, NOTYPE_BUF);
    }while(notification_data == 0);
    
    host_evt = (HDR_HostEvent *)notification_data;
    host_evt->c_type = HOST_EVENT;
    host_evt->h_event = SOC_EVT_BEACON_LOSS;
    host_evt->len = evt_size;

    // Send out to host
    TX_FRAME(notification_data);
}

/* rtc clock freq = 32.768MHz => 1 tick = 30.517578125us
 * 1 tick * 1024 = 31250us
 * sleep_rtc_ticks = sleep_us * 1024 / 31250 
 *                 = (sleep_us * 1024 * 34360)>> 30
 *                 = (sleep_us * 34360) >> 20
 * ex:
 *     31/4 = 31*(1/4) = 31*(0.01b) = 31*((0.1000 0000 0000 0000) >> 17)
 *     = 31 * 8000h >> 17 = 31 * 32768 >> 17 = 7
 *     x/31250 = x*(1/31250) 
 *     = x*(0.0000000000000010000110001101111011110100000101101011110110110001101b)
 *     Take the most significant 17 bits and add 1 and then truncate to 16 bits. 
 *     = x*(8638h)>>30 = x*34360>>30
 */
inline u32 us_to_rtc_ticks(u32 sleep_us)
{
    return ((u64)sleep_us * 34360) >> 20;    
}

u32 enterSystemSleep(u16 beacon_int, u8 dtim_count, u8 dtim_period,u32 latency_us)
{
    u32 sleep_us, time_spent, ticks;

    //calc sleep time
    if(0){ //Fixed period at 3 that refer to BRCM/MTK
        sleep_us = 3*beacon_int;
    }else{
        if(dtim_count){
            sleep_us = dtim_count * beacon_int;
            //return sleep_us;
        }else{
            sleep_us = dtim_period * beacon_int;
        }
    }
    sleep_us *= 1024; //transfer time unit to micro seconds
    time_spent = (drv_mac_get_mtx_tsp() - g_ps_data.timestamp) + PMU_WAKEUP_DELAY_US;
    sleep_us -= time_spent;
#if 1               
    if(sleep_us>latency_us){
        sleep_us-=latency_us;
    }
    ticks = us_to_rtc_ticks(sleep_us);
    //printf("ti: %d, ts: %d, su: %d\n  ", ticks, time_spent, sleep_us);
    mlme_active_pmu_sleep(ticks,true);
#else
    block_all_traffic();
    LOG_PRINTF("\nOS_MsDelay=%d",sleep_ms);
    OS_MsDelay(sleep_ms);
    restore_all_traffic();
#endif
    return ticks;

}
void irq_beacon_timer_handler (void *data)
{
    u32 sleep_10us;
    
    hwtmr_stop(BEACON_TIMER);
    ps_doze_st* psdata = (ps_doze_st*)data;
    if(psdata->state == PWR_DOZE)
    {
        sleep_10us = enterSystemSleep(psdata->beacon_int,psdata->dtim_count,psdata->dtim_period,160);
        LOG_PRINTF("timer sleep_10us=%d!\n",sleep_10us);       
    }
}

#ifdef CONFIG_RX_MGMT_CHECK
Time_T pre_t;
int enable_rx_mgmt_check;
void check_beacon(PKT_RxInfo *PktInfo)
{
	static bool show_next_beacon = false;
    ETHER_ADDR bssid;
	HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(PktInfo); 
	drv_mac_get_bssid(&bssid.addr[0]);
	u32 elapsed_time;
	bool show = false; 
	
	if (!memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {
		volatile u32 hci_out = GET_FFO1_CNT;
		volatile u32 hci_in = GET_FF1_CNT;
		volatile u32 txid_len = GET_TX_ID_ALC_LEN;
		volatile u32 rxid_len = GET_RX_ID_ALC_LEN;
		volatile u32 txid_cnt = GET_TX_ID_COUNT;
		volatile u32 rxid_cnt = GET_RX_ID_COUNT;
		//volatile u32 *pointer = NULL;
		elapsed_time = dbg_get_elapsed(&pre_t);
		dbg_get_time(&pre_t);

#if 0
		pointer = (u32 *)0xCE0023EC;
		printf("\npacket CCA:	   %05d\n",((*pointer)>>16)&0xffff);
		printf("packet counter:    %05d\n",(*pointer)&0xffff);
		printf("RX fcs success frame   :%5d\n", GET_MRX_FCS_SUC);
		printf("RX miss a pcaket from PHY frame :%5d\n", GET_MRX_MISS);
		
		pointer = (u32 *)MIB_REG_BASE;
		*pointer = 0x00;
		*pointer = 0xffffffff;
		//Reset PHY MIB
		pointer = (u32 *)0xCE0023F8;
		*pointer = 0x0;
		*pointer = 0x100000;
#endif
		if (enable_rx_mgmt_check == 2)
			printf("b_seq: %d\n", (Mgmt80211->seq_ctrl)); //check beacon sequence number

		if (elapsed_time > 500000 || hci_out > 10 || hci_in > 10)
			show = true;
		if (show || show_next_beacon) {
			printf("T: %ums, S: %d\n", elapsed_time / 1000, Mgmt80211->seq_ctrl);
			printf("B:HCI_IN HCI_OUT TXPG RXPG TXID_CNT RXID_CNT\n");
			printf(" %5d %6d %6d %5d %5d %9d\n\n",				
				hci_in, hci_out, txid_len, rxid_len, txid_cnt, rxid_cnt);			
			if (show)
				show_next_beacon = true;
			else
				show_next_beacon = false;
		}
	}
}
#endif //CONFIG_RX_MGMT_CHECK

static s32 mlme_rx_beacon( PKT_RxInfo *PktInfo )
{
    HDR80211_Mgmt  *Mgmt80211 = HDR80211_MGMT(PktInfo);
    ParseRes        res;
    Time_T          current_Time, wakeup_time;

    current_Time.t = 0;
    hwtmr_stop(BEACON_TIMER);

    if (g_ps_data.state == PWR_DOZE) {
        ETHER_ADDR bssid;

        drv_mac_get_bssid(&bssid.addr[0]);

        if (g_ps_data.aid && !memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {
            struct ieee802_11_elems elems;
            bool directed_tim = false;
            struct ieee80211_tim_ie *tim=NULL;
            //u32 data[2];
            res = mlme_parse_elems((u8 *) Mgmt80211->u.beacon.variable,
                                        PktInfo->len-BASE_LEN(Mgmt80211->u.beacon.variable,Mgmt80211),
                                        &elems);
            if(res == ParseFailed){
                //int i;
                LOG_PRINTF("Beacon error,PktInfo->len=%d\n",PktInfo->len);
                //for(i=0; i<(PktInfo->len-BASE_LEN(Mgmt80211->u.beacon.variable,Mgmt80211));i++)
                //    LOG_PRINTF("%x ",((u8 *) Mgmt80211->u.beacon.variable)[i]);
                //LOG_PRINTF("\n");
                //return 0;
            }else{
                tim = ((struct ieee80211_tim_ie *)(elems.tim));
                g_ps_data.beacon_int = Mgmt80211->u.beacon.beacon_int;
                g_ps_data.dtim_count = tim->dtim_count;
                g_ps_data.dtim_period = tim->dtim_period;
                g_ps_data.timestamp = drv_mac_get_mrx_tsp(PktInfo);
                //printf("c=%d,p=%d,s=%d\n  ",tim->dtim_count,tim->dtim_period,(Mgmt80211->seq_ctrl));
                directed_tim = ieee80211_check_tim(tim,elems.tim_len,g_ps_data.aid);
            }
            if(!directed_tim){
                u32 rtc_ticks;
                //dbg_get_time(&current_Time);
                drv_phy_off();
                block_all_traffic();
                rtc_ticks = enterSystemSleep(g_ps_data.beacon_int,g_ps_data.dtim_count,g_ps_data.dtim_period,1600);
                restore_all_traffic();
                
                if(g_nullDataCnt==0){                    
                    volatile u32 hci_out;// = GET_FFO1_CNT;
                    drv_phy_bgn();                   
                    drv_phy_on();   
                    mlme_null_send(true, true);
                    
                    hci_out = GET_FFO1_CNT;
                    if (hci_out > 0) {
                        dbg_get_time(&current_Time);
                        drv_set_GPIO(1);
                        do{
                            dbg_get_time(&wakeup_time);
                        }while((wakeup_time.ts.lt-current_Time.ts.lt)<100000);
                        drv_set_GPIO(0);
                        printf("[TAG]  MCU - HCI - RX \n");
                        printf("OUTPUT %3d - %3d - %3d\n",
                                GET_FFO0_CNT, GET_FFO1_CNT, GET_FFO4_CNT);
                        printf("INPUT  %3d - %3d - %3d\n",
                                GET_FF0_CNT, GET_FF1_CNT, GET_FF4_CNT);
                        printf("TX[%d]RX[%d]AVA[%d]TXID[%d]RXID[%d]\n\n",GET_TX_ID_ALC_LEN,GET_RX_ID_ALC_LEN,GET_AVA_TAG,GET_TX_ID_COUNT,GET_RX_ID_COUNT);
                    }
                    LOG_PRINTF("%d,B=%d/%d, rtc_ticks=%d\n",g_cnt,tim->dtim_count,tim->dtim_period, rtc_ticks);
                }
                else {
                    drv_phy_b_only();
                    drv_phy_on(); 
                }
                if((g_nullDataCnt*rtc_ticks) > 1000000){ // reached to 30 second to send null data.
                    g_nullDataCnt=0;
                }else{
                    g_nullDataCnt++;
                }
                //LOG_PRINTF("%d,B=%d/%d,S=%d\n",g_cnt,tim->dtim_count,tim->dtim_period,sleep_10us);
                //LOG_PRINTF("B=%d/%d,S=%d,RxT=%d,W9=%d,CrT=%d,D1=%d,D2=%d\n",tim->dtim_count,tim->dtim_period,sleep_ms,PktInfo->time_l,PktInfo->word9,
                //current_Time.ts.lt,(current_Time.ts.lt-PktInfo->word9),(PktInfo->word9-PktInfo->time_l));
                //hwtmr_start(BEACON_TIMER,(5000),irq_beacon_timer_handler,(void*)(&g_ps_data),HTMR_ONESHOT);
                //hwtmr_start(BEACON_TIMER,10,irq_beacon_timer_handler,(void*)(&g_ps_data),HTMR_ONESHOT);
            }else{                           
                LOG_PRINTF("tim for us,tim_us_cnt=%d!!,g_cnt=%d\n",tim_us_cnt,g_cnt);

                if((tim_us_cnt % 20)==0){
                    dbg_get_time(&current_Time);

                    drv_set_GPIO(1);//wakeup host cpu.
                    do{
                        dbg_get_time(&wakeup_time);
                    }while((wakeup_time.ts.lt-current_Time.ts.lt)<100000);

                    drv_set_GPIO(0);
                }
                tim_us_cnt++;
            }
        }
        g_cnt++;
    }

#ifdef CONFIG_RX_MGMT_CHECK
	if (enable_rx_mgmt_check)
		check_beacon(PktInfo);
#endif //CONFIG_RX_MGMT_CHECK

#ifdef CONFIG_P2P_NOA    
    drv_p2p_isr_check_beacon((u32)PktInfo);
#endif    
    return 0;
}

static s32 mlme_rx_probe_req( PKT_RxInfo *PktInfo )
{
#ifdef CONFIG_RX_MGMT_CHECK
	ETHER_ADDR bssid;
	HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(PktInfo);    
	drv_mac_get_bssid(&bssid.addr[0]);
	
	if (!memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {	
		printf("probe_req: [%02x:%02x:%02x:%02x:%02x:%02x]\n", 
		Mgmt80211->sa.addr[0], Mgmt80211->sa.addr[1], Mgmt80211->sa.addr[2],
		Mgmt80211->sa.addr[3], Mgmt80211->sa.addr[4], Mgmt80211->sa.addr[5]);
		//printf("mlme_rx_probe_req\n");
	}
#endif //CONFIG_RX_MGMT_CHECK	
	return 0;
}

static s32 mlme_rx_probe_res( PKT_RxInfo *PktInfo )
{
#ifdef CONFIG_RX_MGMT_CHECK
	ETHER_ADDR bssid;
	HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(PktInfo);    
	drv_mac_get_bssid(&bssid.addr[0]);
	
	if (!memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {
		printf("probe_res: [%02x:%02x:%02x:%02x:%02x:%02x]\n", 
		Mgmt80211->sa.addr[0], Mgmt80211->sa.addr[1], Mgmt80211->sa.addr[2],
		Mgmt80211->sa.addr[3], Mgmt80211->sa.addr[4], Mgmt80211->sa.addr[5]);
	}
#endif //CONFIG_RX_MGMT_CHECK
	return 0;
}

static s32 mlme_rx_disassoc( PKT_RxInfo *PktInfo )
{
#ifdef CONFIG_RX_MGMT_CHECK
	ETHER_ADDR bssid;
	HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(PktInfo);    
	drv_mac_get_bssid(&bssid.addr[0]);
	
	printf("rx_disassoc: [%02x:%02x:%02x:%02x:%02x:%02x]\n", 
		Mgmt80211->sa.addr[0], Mgmt80211->sa.addr[1], Mgmt80211->sa.addr[2],
		Mgmt80211->sa.addr[3], Mgmt80211->sa.addr[4], Mgmt80211->sa.addr[5]);
	if (!memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {
		printf("rx_disassoc reason: %d\n", Mgmt80211->u.disassoc.reason_code);
	}
#endif //CONFIG_RX_MGMT_CHECK
	return 0;
}

static s32 mlme_rx_deauth( PKT_RxInfo *PktInfo )
{
#ifdef CONFIG_RX_MGMT_CHECK
	ETHER_ADDR bssid;
	HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(PktInfo);    
	drv_mac_get_bssid(&bssid.addr[0]);
	
	printf("rx_deauth: [%02x:%02x:%02x:%02x:%02x:%02x]\n", 
		Mgmt80211->sa.addr[0], Mgmt80211->sa.addr[1], Mgmt80211->sa.addr[2],
		Mgmt80211->sa.addr[3], Mgmt80211->sa.addr[4], Mgmt80211->sa.addr[5]);
	if (!memcmp(bssid.addr, Mgmt80211->sa.addr, ETH_ALEN)) {
		printf("rx_deauth reason: %d\n", Mgmt80211->u.deauth.reason_code);
	}
#endif //CONFIG_RX_MGMT_CHECK
	return 0;
}

/**
 *  IEEE 802.11 Management Frame Handler:
 *
 *  @ MLME_RXFUNC_MGMT_STYPE_00: Association Request Handler (subtype=00)
 *  @ MLME_RXFUNC_MGMT_STYPE_01: Association Response Handler (subtype=01)
 *  @ MLME_RXFUNC_MGMT_STYPE_02: Reassociation Request Handler (subtype=02)
 *  @ MLME_RXFUNC_MGMT_STYPE_03: Reassociation Response Handler (subtype=03)
 *  @ MLME_RXFUNC_MGMT_STYPE_04: Probe Request Handler (subtype=04)
 *  @ MLME_RXFUNC_MGMT_STYPE_05: Probe Response Handler (subtype=05)
 *  @ MLME_RXFUNC_MGMT_STYPE_06: Reserved (subtype=06)
 *  @ MLME_RXFUNC_MGMT_STYPE_07: Reserved (subtype=07)
 *  @ MLME_RXFUNC_MGMT_STYPE_08: Beacon Handler (subtype=08)
 *  @ MLME_RXFUNC_MGMT_STYPE_09: ATIM Handler (subtype=09)
 *  @ MLME_RXFUNC_MGMT_STYPE_10: Disassociation Handler (subtype=10)
 *  @ MLME_RXFUNC_MGMT_STYPE_11: Authentication Handler (subtype=11)
 *  @ MLME_RXFUNC_MGMT_STYPE_12: Deauthentication Handler (subtype=12)
 *  @ MLME_RXFUNC_MGMT_STYPE_13: Action Handler (subtype=13)
 *  @ MLME_RXFUNC_MGMT_STYPE_14: Reserved (subtype=14)
 *  @ MLME_RXFUNC_MGMT_STYPE_15: Reserved (subtype=15)
 */
#define MLME_RXFUNC_MGMT_STYPE_00           (MGMT80211_RxHandler)NULL//mlme_rx_assoc_request
#define MLME_RXFUNC_MGMT_STYPE_01           (MGMT80211_RxHandler)NULL//mlme_rx_assoc_response
#define MLME_RXFUNC_MGMT_STYPE_02           (MGMT80211_RxHandler)NULL//mlme_rx_reassoc_request
#define MLME_RXFUNC_MGMT_STYPE_03           (MGMT80211_RxHandler)NULL//mlme_rx_reassoc_response
#define MLME_RXFUNC_MGMT_STYPE_04           mlme_rx_probe_req//mlme_rx_probe_request
#define MLME_RXFUNC_MGMT_STYPE_05           mlme_rx_probe_res//mlme_rx_probe_response
#define MLME_RXFUNC_MGMT_STYPE_06           (MGMT80211_RxHandler)NULL
#define MLME_RXFUNC_MGMT_STYPE_07           (MGMT80211_RxHandler)NULL
#define MLME_RXFUNC_MGMT_STYPE_08           mlme_rx_beacon
#define MLME_RXFUNC_MGMT_STYPE_09           (MGMT80211_RxHandler)NULL//mlme_rx_atim
#define MLME_RXFUNC_MGMT_STYPE_10           mlme_rx_disassoc
#define MLME_RXFUNC_MGMT_STYPE_11           (MGMT80211_RxHandler)NULL//mlme_rx_auth
#define MLME_RXFUNC_MGMT_STYPE_12           mlme_rx_deauth
#define MLME_RXFUNC_MGMT_STYPE_13           (MGMT80211_RxHandler)NULL//mlme_rx_action
#define MLME_RXFUNC_MGMT_STYPE_14           (MGMT80211_RxHandler)NULL
#define MLME_RXFUNC_MGMT_STYPE_15           (MGMT80211_RxHandler)NULL

typedef s32 (*MGMT80211_RxHandler)(PKT_RxInfo *);

MGMT80211_RxHandler const MGMT_RxHandler[] = 
{   /*lint -save -e611 */
    /* stype=00: */ MLME_RXFUNC_MGMT_STYPE_00,
    /* stype=01: */ MLME_RXFUNC_MGMT_STYPE_01,
    /* stype=02: */ MLME_RXFUNC_MGMT_STYPE_02,
    /* stype=03: */ MLME_RXFUNC_MGMT_STYPE_03,
    /* stype=04: */ MLME_RXFUNC_MGMT_STYPE_04,
    /* stype=05: */ MLME_RXFUNC_MGMT_STYPE_05,
    /* stype=06: */ MLME_RXFUNC_MGMT_STYPE_06,
    /* stype=07: */ MLME_RXFUNC_MGMT_STYPE_07,
    /* stype=08: */ MLME_RXFUNC_MGMT_STYPE_08,
    /* stype=09: */ MLME_RXFUNC_MGMT_STYPE_09,
    /* stype=10: */ MLME_RXFUNC_MGMT_STYPE_10,
    /* stype=11: */ MLME_RXFUNC_MGMT_STYPE_11,
    /* stype=12: */ MLME_RXFUNC_MGMT_STYPE_12,
    /* stype=13: */ MLME_RXFUNC_MGMT_STYPE_13,
    /* stype=14: */ MLME_RXFUNC_MGMT_STYPE_14, 
    /* stype=15: */ MLME_RXFUNC_MGMT_STYPE_15,
};  /*lint -restore */

static bool Soft_RxMgmtHandler( PKT_Info *pPktInfo, s32 reason)
{

    const HDR80211_Mgmt *Mgmt80211=HDR80211_MGMT(pPktInfo);
    //HDR80211_Mgmt *Mgmt80211=(HDR80211_Mgmt *)((u8 *)PktInfo+PktInfo->hdr_offset);
    u16 subtype=Mgmt80211->fc.subtype;

    if (MGMT_RxHandler[subtype] != NULL){       
        MGMT_RxHandler[subtype]((PKT_RxInfo *)pPktInfo);
    }else{
        //LOG_PRINTF("NULL RxMgmtHandler\n");
    }
    
	//if(drv_mac_get_op_mode()==WLAN_STA)
	//{	
		//MsgEvent              *MsgEv;
		//const HDR80211_Mgmt   *Mgmt80211=HDR80211_MGMT(pPktInfo);
		//u32                    subtype = (u32)Mgmt80211->fc.subtype;
    
		//MsgEv=(MsgEvent *)msg_evt_alloc();
		//ASSERT_RET(MsgEv != NULL, EMPTY);
		//MsgEv->MsgType = MEVT_PKT_BUF;
		//MsgEv->MsgData = (u32)pPktInfo;
		//msg_evt_post(MBOX_MLME, MsgEv);
	//}
	//else
	//{
		//ENG_MBOX_NEXT((u32)pPktInfo);
	//}
	return false;
}


static bool Soft_RxNoWSIDFoundHandler( PKT_Info *pPktInfo, s32 reason)
{
    int multicast = 0;
    PHDR80211_Data ptr = (PHDR80211_Data)((u32)pPktInfo + pPktInfo->hdr_offset);   

    multicast = ptr->addr1.addr[0] & 1;

	#ifdef CONFIG_SSV_SMARTLINK
	if (smart_icomm_state == START_SMART_ICOMM || smart_icomm_state == RESET_SMART_ICOMM) {

		rx_process_smartConf(pPktInfo, multicast);
		//printf("m:%d,b:%d, Len=%u, addr1=%02X:%02X:%02X:%02X:%02X:%02X, addr2=%02X:%02X:%02X:%02X:%02X:%02X, addr3=%02X:%02X:%02X:%02X:%02X:%02X\n",
		//		multicast, broadcast, pPktInfo->len,
		//		ptr->addr1.addr[0], ptr->addr1.addr[1], ptr->addr1.addr[2], ptr->addr1.addr[3], ptr->addr1.addr[4], ptr->addr1.addr[5],
		//		ptr->addr2.addr[0], ptr->addr2.addr[1], ptr->addr2.addr[2], ptr->addr2.addr[3], ptr->addr2.addr[4], ptr->addr2.addr[5],
		//		ptr->addr3.addr[0], ptr->addr3.addr[1], ptr->addr3.addr[2], ptr->addr3.addr[3], ptr->addr3.addr[4], ptr->addr3.addr[5]);    
	}
	#endif //CONFIG_SSV_SMARTLINK

    if (pPktInfo->unicast == 1)
        ENG_MBOX_SEND(M_ENG_TRASH_CAN, (u32)pPktInfo);
    else  
        ENG_MBOX_NEXT((u32)pPktInfo);

    return true;       
}

static bool SoftMac_SWTrapHandler(PKT_Info *pPktInfo, s32 reason)
{
    /* 
     * If necessary, it can do something 
     * Notice: 
     *  The packet should be dropped directly.
     *  Otherwise, HCI will pass the packet to MCU again.
     */
    ENG_MBOX_SEND(M_ENG_TRASH_CAN, (u32)pPktInfo);
    return true;
}

static bool Soft_RxCtrlHandler(PKT_Info *pPktInfo, s32 reason)
{
    ENG_MBOX_NEXT((u32)pPktInfo);
    return true;
}

static bool SoftMac_SecurityErrorHandler(PKT_Info *pPktInfo, s32 reason)
{    
    /* 
     * Directly drop decrypted error packets.
     */
    printf("trap reason %d\n", reason);
    if ((reason == ID_TRAP_TKIP_MIC_ERROR) || (reason == ID_TRAP_SECURITY_35)) {
        printf("TKIP MMIC ERR\n");
        pPktInfo->reason = 0;
        pPktInfo->tkip_mmic_err = 1;
        printf("Set reason to 0, tkip_mmic_err 1 \n");
        ENG_MBOX_SEND(M_ENG_HWHCI, (u32)pPktInfo);
    } else {    
        ENG_MBOX_SEND(M_ENG_TRASH_CAN, (u32)pPktInfo);
    }
    return true;    
}

extern void txThroughputHandler(u32 sdio_tx_ptk_len);
static bool Soft_TxTputHandler(PKT_Info *pPktInfo, s32 reason)
{
	struct ssv6006_tx_desc *tx_desc = (struct ssv6006_tx_desc *)pPktInfo;

    //printf("%s, len %d, c_type %d\n", __FUNCTION__, (int)tx_desc->len, (int)tx_desc->c_type);
    //hex_dump(pPktInfo, 128);
	txThroughputHandler(tx_desc->len);
	//ENG_MBOX_NEXT((u32)pPktInfo);
    PBUF_MFree((void *)pPktInfo);
    return true;
}

static bool Soft_RxDataHandler( PKT_Info *pPktInfo, s32 reason)
{
    ENG_MBOX_NEXT((u32)pPktInfo);
    return true;
}

/*---------------------------------------------------------------------------*/
// Return true if data has been handled. Or need to go to next step.
typedef bool (*SoftMac_Handler)(PKT_Info *, s32 reason);


/**
 *  Software MAC Hanlder Table: 
 *
 *  Hardware traps tx/rx frames according to some pre-define reasons.
 *  If software interests in a reason, a callback function shall be
 *  registered in this table.
 */
const SoftMac_Handler sgSoftMac_Func[] = { /*lint -save -e611 */
    /* Reason ID=00: */ (SoftMac_Handler)NULL,//SoftMac_NullTrapHandler, 
	/* Reason ID=01: */ (SoftMac_Handler)NULL,
    /* Reason ID=02: */ (SoftMac_Handler)NULL,
    /* Reason ID=03: */ (SoftMac_Handler)NULL,
    /* Reason ID=04: */ (SoftMac_Handler)NULL,
    /* Reason ID=05: */ (SoftMac_Handler)NULL,
    /* Reason ID=06: */ (SoftMac_Handler)NULL,
    /* Reason ID=07: */ Soft_RxNoWSIDFoundHandler,
    /* Reason ID=08: */ (SoftMac_Handler)NULL,
    /* Reason ID=09: */ (SoftMac_Handler)NULL,//SoftHCI_TxEtherTrapHandler,		//ID_TRAP_TX_ETHER_TRAP
    /* Reason ID=10: */ (SoftMac_Handler)NULL,//SoftHCI_RxEtherTrapHandler,		//ID_TRAP_RX_ETHER_TRAP
    /* Reason ID=11: */ (SoftMac_Handler)NULL,
    /* Reason ID=12: */ (SoftMac_Handler)NULL,
    /* Reason ID=13: */ (SoftMac_Handler)NULL,//SoftFrag_RejectHandler,			//ID_TRAP_MODULE_REJECT
    /* Reason ID=14: */ (SoftMac_Handler)NULL,//SoftMac_NullFrameHandler,
    /* Reason ID=15: */ (SoftMac_Handler)NULL,
    /* Reason ID=16: */ (SoftMac_Handler)NULL,
    /* Reason ID=17: */ (SoftMac_Handler)NULL,
    /* Reason ID=18: */ (SoftMac_Handler)NULL,
    /* Reason ID=19: */ (SoftMac_Handler)NULL,
    /* Reason ID=20: */ (SoftMac_Handler)NULL,//SoftFrag_FragHandler,				//ID_TRAP_FRAG_OUT_OF_ENTRY
    /* Reason ID=21: */ (SoftMac_Handler)NULL,
    /* Reason ID=22: */ (SoftMac_Handler)NULL,
    /* Reason ID=23: */ (SoftMac_Handler)NULL,
    /* Reason ID=24: */ (SoftMac_Handler)NULL,
    /* Reason ID=25: */ (SoftMac_Handler)NULL,
    /* Reason ID=26: */ (SoftMac_Handler)NULL,
    /* Reason ID=27: */ (SoftMac_Handler)NULL,
    /* Reason ID=28: */ (SoftMac_Handler)NULL,
    /* Reason ID=29: */ SoftMac_SWTrapHandler,			
    /* Reason ID=30: */ (SoftMac_Handler)NULL,
    /* Reason ID=31: */ (SoftMac_Handler)NULL,
	//For security module
    /* Reason ID=32: */ SoftMac_SecurityErrorHandler,		//ID_TRAP_SECURITY
    /* Reason ID=33: */ SoftMac_SecurityErrorHandler,		//ID_TRAP_KEY_IDX_MISMATCH
    /* Reason ID=34: */ SoftMac_SecurityErrorHandler,		//ID_TRAP_DECODE_FAIL
    /* Reason ID=35: */ SoftMac_SecurityErrorHandler,
    /* Reason ID=36: */ SoftMac_SecurityErrorHandler,		//ID_TRAP_REPLAY
    /* Reason ID=37: */ SoftMac_SecurityErrorHandler,
    /* Reason ID=38: */ SoftMac_SecurityErrorHandler,
    /* Reason ID=39: */ SoftMac_SecurityErrorHandler,
    /* Reason ID=40: */ SoftMac_SecurityErrorHandler,  		//ID_TRAP_SECURITY_BAD_LEN
    /* Reason ID=41: */ SoftMac_SecurityErrorHandler,  		//ID_TRAP_TKIP_MIC_ERROR

    /* SW pre-defined trapping: */
    /* Reason ID=42: */ (SoftMac_Handler)NULL,  
    /* Reason ID=43: */ (SoftMac_Handler)NULL,  
    /* Reason ID=44: */ (SoftMac_Handler)NULL,
    /* Reason ID=45: */ (SoftMac_Handler)NULL,  
    /* Reason ID=46: */ Soft_RxDataHandler,
    /* Reason ID=47: */ Soft_RxMgmtHandler,
    /* Reason ID=48: */ Soft_RxCtrlHandler,
    /* Reason ID=49: */ (SoftMac_Handler)NULL,
    /* Reason ID=50: */ Soft_TxTputHandler,
}; /*lint -restore */


/**
 * s32 SoftMac_GetTrapReason() - Get the trapping reason.
 *
 * The trapping reason can be dividied into following 3 catalogs:
 *
 *      @ Hardware Module Error:
 *      @ Extra-Info set:
 *      @ Software Pre-defined error:
 */
static s32 SoftMac_GetTrapReason( PKT_Info *PktInfo )
{
    s32 reason=0;
    //u32 flows;
    u16 fc;

	fc = GET_HDR80211_FC(PktInfo);

    /* Trap due to hardware module error */
    if (PktInfo->reason != 0)
        return PktInfo->reason;

    /* Trap due to extra_info is set */
    /*if (PktInfo->RSVD_2 == 1)
        return PktInfo->reason;*/

    /* Trap due to software pre-defined trapping */   
    if (IS_RX_PKT(PktInfo)) {
        if (IS_MGMT_FRAME(fc))
            reason = M_CPU_RXMGMT;
		else if (IS_CTRL_FRAME(fc))
			reason = M_CPU_RXCTRL;
        else 
            reason = M_CPU_RXDATA;
    }
    else {
        //if (IS_MGMT_FRAME(fc))
            reason = M_CPU_TXL34CS;
        //else flows = gTxFlowDataReason;
    }
	
    //reason = (flows>>(((s32)PktInfo->fCmdIdx)<<2))&0x0F;
    if (reason > 0) {
        reason += (ID_TRAP_SW_START-1);
    }
    return reason;

}

void RxDataHandler(void* RxInfo)
{
    s32 TrapReason;
    PKT_Info *pPktInfo;
    pPktInfo = (PKT_Info *)RxInfo;
    bool is_handled = false;

    ASSERT_PKT(PBUF_isPkt(pPktInfo), pPktInfo);
    TrapReason = SoftMac_GetTrapReason(pPktInfo);

    printf("TR%d\n",TrapReason);

    if ((TrapReason >= ID_TRAP_MAX) || sgSoftMac_Func[TrapReason] == NULL)
    {
#if 0 //Avoiding UART effect power saving timing.
        if (TrapReason < ID_TRAP_MAX)
        {
            MAIN_ERROR("Trap %d unhandled!!\n", TrapReason);
            hex_dump((void*)pPktInfo,256);
            hex_parser((u32)pPktInfo);
        }
        else
        {
            MAIN_ERROR("Exceptional trap %d!\n", TrapReason);
        }
#endif
    }else{
        is_handled = sgSoftMac_Func[TrapReason](pPktInfo, TrapReason);
    }

    if (!is_handled)
        FRAME_TO_HCI((u32)pPktInfo);
}

// bootstrap
#define BOOTSTRAP_INTERFCAE_MODE_MSK   0x6
#define BOOTSTRAP_INTERFCAE_MODE_SHIFT 0x1

// interface mode
#define INTERFACE_MODE_SPIFL_E 0
#define INTERFACE_MODE_SPIDATA 1
#define INTERFACE_MODE_SDIO    2
#define INTERFACE_MODE_USB     3
static u8 bootstrap_check(void)
{
    u8 mode, interface_mode = 0;

    mode = GET_R_BOOTSTRAP_SAMPLE;
    interface_mode = (mode & BOOTSTRAP_INTERFCAE_MODE_MSK) >> BOOTSTRAP_INTERFCAE_MODE_SHIFT;    

    return interface_mode;
}

// initialization of current interface 
static void drv_interface_init(void)
{
    if (bootstrap_check() == INTERFACE_MODE_USB)
        drv_usb_init();
}

void soft_mac_task( void *args )
{
    
    MsgEvent *MsgEv;
    s32 res;

    printf("%s (%s) starts running.\n", OS_TaskGetName(), __FUNCTION__);

    drv_rtc_init();
    drv_interface_init();
    drv_mac_init();
	drv_phy_init();

    /*lint -save -e716 */
    
    while(1)
    { /*lint -restore */
        res = msg_evt_fetch(MBOX_SOFT_MAC, &MsgEv);
        ASSERT(res == OS_SUCCESS);
	
        switch(MsgEv->MsgType)
        { /*lint -save -e788 */

        
        /**
               *  Message from CPU HW message queue. Dispatch the message
               *  to corresponding function for processing according to the 
               *  reason field of PKT_Info. The reason info is assiged by HW
               *  MAC.
               */
        case MEVT_PKT_BUF:
			//pPktInfo = (PKT_Info *)MsgEv->MsgData;
            //ASSERT_PKT(PBUF_isPkt(pPktInfo), pPktInfo);
            RxDataHandler((void*)MsgEv->MsgData);
            msg_evt_free(MsgEv);

            break;
            
        case MEVT_PS_DOZE:
            {
                LOG_PRINTF("MEVT_PS_DOZE,aid=%d\n",MsgEv->MsgData);
                drv_rtc_cali();
                mlme_enter_PS(MsgEv->MsgData);
                msg_evt_free(MsgEv);
            }
            break;
        case MEVT_PS_WAKEUP:
            {
                LOG_PRINTF("MEVT_PS_WAKEUP\n");
                mlme_ps_exit();
                msg_evt_free(MsgEv);
            }
            break;
       	case MEVT_PS_DEEP:
            {
                LOG_PRINTF("MEVT_PS_DEEP\n");
                OS_MsDelay(10);
                mlme_ps_deep();
                msg_evt_free(MsgEv);
            }
            break;
		case MEVT_BEACON_LOSS:
            {
                LOG_PRINTF("MEVT_BEACON_LOSS\n");
                beacon_loss_notification();
                msg_evt_free(MsgEv);
            }
            break;
        default:
            LOG_PRINTF("%s(): Unknown MsgEvent type(%02x) !!\n", 
                __FUNCTION__, MsgEv->MsgType);
            PBUF_MFree((void *)MsgEv->MsgData);
            msg_evt_free(MsgEv);	
            break;
        }; /*lint -restore */
    }
}


s32 soft_mac_init(void)
{
    /**
        * Allocate one packet buffer from hardware for the purpose of 
        * storing the following information:
        *
        * @ Security Key Storage
        * @ PHY Info Table
        */
    //g_soc_table = (struct soc_table_st *)PBUF_MAlloc_Raw(sizeof(struct soc_table_st), 0, NOTYPE_BUF);
    //ASSERT_RET(g_soc_table != NULL, 0);

   /**
    *  Initialize IEEE 802.11 MLME (MAC Layer Management Entity)
    *  This also initializes bss, sta and ap.
    */
    //ASSERT_RET( MLME_Init() == OS_SUCCESS, 0 );

    /* MIB Counter Init: */

    memset((void *)&g_ps_data, 0, sizeof(ps_doze_st));

    return 0;
}




//----------------------------------------------------
