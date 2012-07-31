/*
 * Copyright (C) 2012 The Regents of The University of California 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author: Duy Nguyen <duy@soe.ucsc.edu>
 *
 * based on minstrel_ht by Felix Fietkau
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "rate.h"
#include "rc80211_minstrel.h"
#include "rc80211_trams_ht.h"

#define AVG_PKT_SIZE	1200
#define EWMA_LEVEL	75
#define EWMA_LEVEL2     20		

#define RATE_ENOUGH 30
#define UPDATE_INTERVAL 100
#define UPDATE_INTERVAL_RESET 1000

/* Number of bits for an average sized packet */
#define MCS_NBITS (AVG_PKT_SIZE << 3)

/* Number of symbols for a packet with (bps) bits per symbol */
#define MCS_NSYMS(bps) ((MCS_NBITS + (bps) - 1) / (bps))

/* Transmission time for a packet containing (syms) symbols */
#define MCS_SYMBOL_TIME(sgi, syms)					\
	(sgi ?								\
	  ((syms) * 18 + 4) / 5 :	/* syms * 3.6 us */		\
	  (syms) << 2			/* syms * 4 us */		\
	)

/* Transmit duration for the raw data part of an average sized packet */
#define MCS_DURATION(streams, sgi, bps) MCS_SYMBOL_TIME(sgi, MCS_NSYMS((streams) * (bps)))

/* MCS rate information for an MCS group */

#define MCS_GROUP(_streams, _sgi, _ht40) {				\
	.streams = _streams,						\
	.flags =							\
		(_sgi ? IEEE80211_TX_RC_SHORT_GI : 0) |			\
		(_ht40 ? IEEE80211_TX_RC_40_MHZ_WIDTH : 0),		\
	.sgi = _sgi ? IEEE80211_TX_RC_SHORT_GI : 0,			\
	.ht40= _ht40 ? IEEE80211_TX_RC_40_MHZ_WIDTH : 0,		\
	.duration = {							\
		MCS_DURATION(_streams, _sgi, _ht40 ? 54 : 26),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 108 : 52),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 162 : 78),		\
		MCS_DURATION(_streams, _sgi, _ht40 ? 216 : 104),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 324 : 156),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 432 : 208),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 486 : 234),	\
		MCS_DURATION(_streams, _sgi, _ht40 ? 540 : 260)		\
	}								\
}

const struct mcs_group trams_mcs_groups[] = {
        MCS_GROUP(1, 0, 0),
        MCS_GROUP(1, 1, 0), 
        MCS_GROUP(1, 0, 1),
        MCS_GROUP(1, 1, 1),
        MCS_GROUP(2, 0, 0),
        MCS_GROUP(2, 1, 0), 
        MCS_GROUP(2, 0, 1),
        MCS_GROUP(2, 1, 1),
#if TRAMS_MAX_STREAMS >= 3
        MCS_GROUP(3, 0, 0),
        MCS_GROUP(3, 1, 0),
        MCS_GROUP(3, 0, 1),
        MCS_GROUP(3, 1, 1),
#endif
};

//ddn
static int 
trams_upgrade_stream(struct trams_ht_sta *mi)
{
        int group, orig_group;

        orig_group = group = mi->trams_curgroup; 

        while (group + 1 < ARRAY_SIZE(trams_mcs_groups)) {
                group++;

                if (!mi->groups[group].supported) 
                        continue;

                if (trams_mcs_groups[group].streams <
                    trams_mcs_groups[orig_group].streams)
                        continue;

		//ddn just skip 20 channel width 
		if (!trams_mcs_groups[group].ht40 &&
                    trams_mcs_groups[orig_group].ht40)
                        continue;



        //	printk(KERN_DEBUG "upgrade stream to %2d \n", group);
                mi->trams_curgroup = group;
		mi->trams_cur_streams = trams_mcs_groups[group].streams;

                break;
        }
	return group;
}
static int 
trams_upgrade_group(struct trams_ht_sta *mi)
{
        int group, orig_group;

        orig_group = group = mi->trams_curgroup; 

        while (group + 1 < ARRAY_SIZE(trams_mcs_groups)) {
                group++;

                if (!mi->groups[group].supported) 
                        continue;

                if (trams_mcs_groups[group].streams !=  
                    trams_mcs_groups[orig_group].streams)
                        continue;

        //	printk(KERN_DEBUG "upgrade stream to %2d \n", group);
                mi->trams_curgroup = group;
		mi->trams_cur_streams = trams_mcs_groups[group].streams;

                break;
        }
	return group;
}

//ddn
static int 
trams_downgrade_stream(struct trams_ht_sta *mi)
{
        int group, orig_group;

        orig_group = group = mi->trams_curgroup; 

        while (group > 0) {
                group--;

                if (!mi->groups[group].supported)
                        continue;

                if (trams_mcs_groups[group].streams >
                    trams_mcs_groups[orig_group].streams)
                        continue;


		if (mi->trams_skip20 && !trams_mcs_groups[group].ht40 &&
                    trams_mcs_groups[orig_group].ht40) {

			printk(KERN_DEBUG "keep 20\n");
                        continue;
		}

		//printk(KERN_DEBUG "downgrade stream to %2d \n", group);
                mi->trams_curgroup = group;
		mi->trams_cur_streams = trams_mcs_groups[group].streams;

                break;
        }
	return group;
}
static int 
trams_downgrade_group(struct trams_ht_sta *mi)
{
        int group, orig_group;

        orig_group = group = mi->trams_curgroup; 

        while (group > 0) {
                group--;

                if (!mi->groups[group].supported)
                        continue;

                if (trams_mcs_groups[group].streams != 
                    trams_mcs_groups[orig_group].streams)
                        continue;

		//printk(KERN_DEBUG "downgrade stream to %2d \n", group);
                mi->trams_curgroup = group;
		mi->trams_cur_streams = trams_mcs_groups[group].streams;

                break;
        }
	return group;
}

/*
 * Perform EWMA (Exponentially Weighted Moving Average) calculation
 */
static int
trams_ewma(int old, int new, int weight)
{
	return (new * (100 - weight) + old * weight) / 100;
}

//ddn
static void
trams_check_modulation_index(struct trams_ht_sta *mi)
{
	int rate, streams, edx, sgi, ht40;
	int lrate, lstreams, ledx, lsgi, lht40;
	int cgroup, lgroup;
	struct trams_rate_stats *cr;
	struct trams_rate_stats *lr;
	int enough;
	unsigned int last_tp;

	enough = (mi->trams_tx_ok + mi->trams_tx_err >= RATE_ENOUGH);


	//current rates
	rate = mi->trams_cur_ridx;
	streams = mi->trams_cur_streams;
	edx = mi->trams_cur_edx;
	sgi = mi->trams_cur_sgi;
	ht40 = mi->trams_cur_ht40;

	//best rates
	lrate = mi->trams_last_ridx;
	lstreams = mi->trams_last_streams;
	ledx = mi->trams_last_edx;
	lsgi = mi->trams_last_sgi;
	lht40 = mi->trams_last_ht40;

	cgroup = mi->trams_curgroup;
	lgroup = mi->trams_curgroup;

	cr = &mi->groups[cgroup].rates[rate];
	
	lr = &mi->groups[lgroup].rates[lrate];

        if (mi->trams_consecutive > 1) 
        	mi->isMultiplicative = true;
        else 
		mi->isMultiplicative = false;
	

	//check the probe rates
	if (mi->isProbingMod ) {

		mi->isProbingMod = false;

		//this is not working well
        	if( cr->cur_tp < mi->trams_avg_tp && lrate != rate) {

			mi->trams_consecutive = 0;


			rate =  mi->trams_last_ridx;
			streams = mi->trams_last_streams;
			edx = mi->trams_last_edx;
			sgi = mi->trams_last_sgi;
			ht40 = mi->trams_last_ht40;


        		if ( rate != mi->trams_cur_ridx ) {
                		printk(KERN_DEBUG "probe is bad trams_cur_ridx=%2d to rate=%2d group=%d\n", mi->trams_cur_ridx, rate, mi->trams_curgroup);
                		mi->trams_cur_ridx= rate;
        			mi->trams_cur_streams = streams;
		        	mi->trams_cur_edx = edx;
        			mi->trams_cur_sgi = sgi;
        			mi->trams_cur_ht40 = ht40;
			}
			mi->trams_ossilate++;

			
			if (!mi->isMultiplicative) { 
				printk (KERN_DEBUG "reset 900ms \n");
				mi->trams_time_interval = 900; 
			}
			if (mi->isMultiplicative) 
				mi->trams_time_interval = 100; 
			
        	} else if( cr->cur_tp > mi->trams_last_tp && lrate != rate) {

			mi->trams_consecutive++;
			mi->trams_time_interval = 100;
                	printk(KERN_DEBUG "reset ossilation: got it right cr_tp %d last_avg_tp %d\n", cr->cur_tp, mi->trams_avg_tp);

		}
		else 
			mi->trams_consecutive = 0;

		mi->stats_update_adaptive = jiffies;

		return;
	}

	if (mi->trams_tx_ok + mi->trams_tx_err > 0) {
		mi->trams_avg_tp = trams_ewma (mi->trams_avg_tp, cr->cur_tp, EWMA_LEVEL2);
	}

	if (mi->trams_avg_tp == 0)
		return;


	if (cr->cur_tp >= mi->trams_avg_tp) {
		mi->trams_successive = 0;
               	if (rate + 1 <  MCS_GROUP_RATES && !mi->isMultiplicative && !mi->trams_ossilate ) {
                       	rate++;
			printk(KERN_DEBUG "increase additively rate to%2d \n", rate);
			mi->isProbingMod = true;
               	} else if (rate + 1 <  MCS_GROUP_RATES && mi->isMultiplicative && !mi->trams_ossilate ) {
			if (rate + rate < MCS_GROUP_RATES)
				rate = rate + rate;
			else
				rate = MCS_GROUP_RATES - 1;
			printk(KERN_DEBUG "increase multiplicatively rate to%2d \n", rate);
			mi->isProbingMod = true;
		}
	} else if (cr->cur_tp <=  (mi->trams_avg_tp * 90)/100  &&
		cr->cur_tp >= (mi->trams_avg_tp * 75)/100) {
		if (rate > 0) {
  	       		rate--; 
			printk(KERN_DEBUG "decrease additively rate to%2d \n", rate);
		}
		mi->trams_consecutive = 0;
		mi->trams_ossilate = 0;
	} else if (cr->cur_tp < (mi->trams_avg_tp * 75)/100) {
		mi->trams_successive++;
		if (rate >= 1 && mi->trams_successive > 1) {	 
			rate = rate * 3/4;
			printk(KERN_DEBUG "decrease multiplicatively rate to%2d \n", rate);
		}
		else if (rate > 0 && mi->trams_successive == 1)
			rate--;
		mi->trams_consecutive = 0;
		mi->trams_ossilate = 0;
	}


	last_tp = mi->trams_last_tp;
	mi->trams_last_tp = cr->cur_tp;
	mi->trams_last_ridx = mi->trams_cur_ridx; 
	mi->trams_last_streams = mi->trams_cur_streams;


        if ( rate != mi->trams_cur_ridx ) {
                printk(KERN_DEBUG ">>>set trams_cur_ridx=%2d to rate=%2d group=%d  cur_tp %2d last_tp %2d  avg_tp %2d\n", mi->trams_cur_ridx, rate, mi->trams_curgroup, last_tp, cr->cur_tp, mi->trams_avg_tp);
                mi->trams_cur_ridx= rate;
        	mi->trams_cur_streams = streams;
        	mi->trams_cur_edx = edx;
        	mi->trams_cur_sgi = sgi;
        	mi->trams_cur_ht40 = ht40;
		mi->trams_tx_ok = mi->trams_tx_err = mi->trams_tx_retr = 0;
	}
}



/*
 * Look up an MCS group index based on mac80211 rate information
 */
static int
trams_ht_get_group_idx(struct ieee80211_tx_rate *rate)
{
	int streams = (rate->idx / MCS_GROUP_RATES) + 1;
	u32 flags = IEEE80211_TX_RC_SHORT_GI | IEEE80211_TX_RC_40_MHZ_WIDTH;
	int i;

	for (i = 0; i < ARRAY_SIZE(trams_mcs_groups); i++) {
		if (trams_mcs_groups[i].streams != streams)
			continue;
		if (trams_mcs_groups[i].flags != (rate->flags & flags))
			continue;

		return i;
	}

	WARN_ON(1);
	return 0;
}


//ddn
static inline struct trams_rate_stats *
trams_get_ratestats(struct trams_ht_sta *mi, int index)
{
	return &mi->groups[index / MCS_GROUP_RATES].rates[index % MCS_GROUP_RATES];
}


/*
 * Recalculate success probabilities and counters for a rate using EWMA
 */
static void
trams_calc_rate_ewma(struct minstrel_priv *mp, struct trams_rate_stats *mr)
{
	if (unlikely(mr->attempts > 0)) {
		mr->sample_skipped = 0;
		mr->cur_prob = TRAMS_FRAC(mr->success, (mr->attempts > 0 ? mr->attempts:1));
		if (!mr->att_hist)
			mr->probability = mr->cur_prob;
		else
			mr->probability = trams_ewma(mr->probability,
				mr->cur_prob, EWMA_LEVEL);
		mr->att_hist += mr->attempts;
		mr->succ_hist += mr->success;
	} else {
		mr->sample_skipped++;
	}
	mr->last_success = mr->success;
	mr->last_attempts = mr->attempts;
	mr->success = 0;
	mr->attempts = 0;
}

/*
 * Calculate throughput based on the average A-MPDU length, taking into account
 * the expected number of retransmissions and their expected length
 */
static void
trams_ht_calc_tp(struct minstrel_priv *mp, struct trams_ht_sta *mi,
                    int group, int rate)
{
	struct trams_rate_stats *mr;
	unsigned int usecs;


	mr = &mi->groups[group].rates[rate];

	if (mr->probability < TRAMS_FRAC(1, 10)) {
		mr->cur_tp = 0;
		return;
	}

	usecs = mi->overhead / TRAMS_TRUNC(mi->avg_ampdu_len);
	usecs += trams_mcs_groups[group].duration[rate];
	mr->cur_tp = TRAMS_TRUNC((1000000 / usecs) * mr->probability);
}

/*
 * Update rate statistics and select new primary rates
 *
 * Rules for rate selection:
 *  - max_prob_rate must use only one stream, as a tradeoff between delivery
 *    probability and throughput during strong fluctuations
 *  - as long as the max prob rate has a probability of more than 3/4, pick
 *    higher throughput rates, even if the probablity is a bit lower
 */
static void
trams_ht_update_stats(struct minstrel_priv *mp, struct trams_ht_sta *mi)
{
	struct trams_mcs_group_data *mg;
	struct trams_rate_stats *mr;
	int cur_prob, cur_prob_tp, cur_tp, cur_tp2;
	int group, i, index;

	if (mi->ampdu_packets > 0) {
		mi->avg_ampdu_len = trams_ewma(mi->avg_ampdu_len,
			TRAMS_FRAC(mi->ampdu_len, (mi->ampdu_packets > 0 ? mi->ampdu_packets:1)), EWMA_LEVEL);
		mi->ampdu_len = 0;
		mi->ampdu_packets = 0;
	}

	mi->sample_slow = 0;
	mi->sample_count = 0;
	mi->max_tp_rate = 0;
	mi->max_tp_rate2 = 0;
	mi->max_prob_rate = 0;

	for (group = 0; group < ARRAY_SIZE(trams_mcs_groups); group++) {
		cur_prob = 0;
		cur_prob_tp = 0;
		cur_tp = 0;
		cur_tp2 = 0;

		mg = &mi->groups[group];
		if (!mg->supported)
			continue;

		mg->max_tp_rate = 0;
		mg->max_tp_rate2 = 0;
		mg->max_prob_rate = 0;
		mi->sample_count++;

		for (i = 0; i < MCS_GROUP_RATES; i++) {
			if (!(mg->supported & BIT(i)))
				continue;

			mr = &mg->rates[i];
			mr->retry_updated = false;
			index = MCS_GROUP_RATES * group + i;
			trams_calc_rate_ewma(mp, mr);
			trams_ht_calc_tp(mp, mi, group, i);

			if (!mr->cur_tp)
				continue;

			/* ignore the lowest rate of each single-stream group */
			if (!i && trams_mcs_groups[group].streams == 1)
				continue;

			if ((mr->cur_tp > cur_prob_tp && mr->probability >
			     TRAMS_FRAC(3, 4)) || mr->probability > cur_prob) {
				mg->max_prob_rate = index;
				cur_prob = mr->probability;
				cur_prob_tp = mr->cur_tp;
			}

			if (mr->cur_tp > cur_tp) {
				swap(index, mg->max_tp_rate);
				cur_tp = mr->cur_tp;
				mr = trams_get_ratestats(mi, index);
			}

			if (index >= mg->max_tp_rate)
				continue;

			if (mr->cur_tp > cur_tp2) {
				mg->max_tp_rate2 = index;
				cur_tp2 = mr->cur_tp;
			}
		}
	}

	/* try to sample up to half of the available rates during each interval */
	mi->sample_count *= 4;

	cur_prob = 0;
	cur_prob_tp = 0;
	cur_tp = 0;
	cur_tp2 = 0;
	for (group = 0; group < ARRAY_SIZE(trams_mcs_groups); group++) {
		mg = &mi->groups[group];
		if (!mg->supported)
			continue;

		mr = trams_get_ratestats(mi, mg->max_prob_rate);
		if (cur_prob_tp < mr->cur_tp &&
		    trams_mcs_groups[group].streams == 1) {
			mi->max_prob_rate = mg->max_prob_rate;
			cur_prob = mr->cur_prob;
			cur_prob_tp = mr->cur_tp;
		}

		mr = trams_get_ratestats(mi, mg->max_tp_rate);
		if (cur_tp < mr->cur_tp) {
			mi->max_tp_rate = mg->max_tp_rate;
			cur_tp = mr->cur_tp;
		}

		mr = trams_get_ratestats(mi, mg->max_tp_rate2);
		if (cur_tp2 < mr->cur_tp) {
			mi->max_tp_rate2 = mg->max_tp_rate2;
			cur_tp2 = mr->cur_tp;
		}
	}
	

	trams_check_modulation_index(mi);
	mi->stats_update = jiffies;
	mi->stats_update_adaptive = jiffies;
}

static bool
trams_ht_txstat_valid(struct ieee80211_tx_rate *rate)
{
	if (!rate->count)
		return false;

	if (rate->idx < 0)
		return false;

	return !!(rate->flags & IEEE80211_TX_RC_MCS);
}


static void
trams_aggr_check(struct minstrel_priv *mp, struct ieee80211_sta *pubsta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	u16 tid;

	if (unlikely(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	if (likely(sta->ampdu_mlme.tid_tx[tid]))
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	ieee80211_start_tx_ba_session(pubsta, tid, 5000);
}

//ddn
static void
trams_check_enhancement_index(struct trams_ht_sta *mi, struct ieee80211_tx_info *info, struct ieee80211_tx_rate *ar)
{

        //ddn
        bool stream_error = false;
	bool downgrade = false;
	bool upgrade = false;
        int error_offset=0;
        unsigned int trams_prob=0;
        unsigned int usecs;
        struct trams_rate_stats *rate;



        if (mi->avg_ampdu_len == 0)
                mi->avg_ampdu_len = 1;



        rate = trams_get_ratestats (mi,  mi->trams_cur_ridx +  mi->trams_curgroup * MCS_GROUP_RATES);

        //trams_prob = TRAMS_FRAC(info->status.ampdu_ack_len, (info->status.ampdu_len > 0 ? info->status.ampdu_len:1));
        trams_prob = TRAMS_FRAC(rate->success, (rate->attempts > 0 ? rate->attempts :1));
        usecs = mi->overhead / TRAMS_TRUNC(mi->avg_ampdu_len);
        usecs += trams_mcs_groups[mi->trams_curgroup].duration[mi->trams_cur_ridx];
        mi->trams_cur_tp = TRAMS_TRUNC((1000000 / usecs) * trams_prob);


        mi->trams_tx_ok += info->status.ampdu_ack_len;
        mi->trams_tx_err += info->status.ampdu_len - info->status.ampdu_ack_len;

        //need -1 for offseting
        mi->trams_tx_retr += ar[0].count - 1;


        //begin with stream >= 2
        if (trams_mcs_groups[mi->trams_curgroup].streams > 1 &&  info->status.ampdu_len > 1 ) {

                //error offset due to retransmission
                //error_offset = ar[0].count / 2;
                error_offset = 0;

                //check for stream errors
                if ((info->status.ampdu_len - info->status.ampdu_ack_len) > info->status.ampdu_ack_len - error_offset)
                        stream_error = true;
                else if ((info->status.ampdu_len - info->status.ampdu_ack_len) <= info->status.ampdu_ack_len - error_offset)
                        stream_error = false;


                if (stream_error) {
                        mi->trams_stream_failure++;
                        mi->trams_stream_success = 0;
                }
                else if (!stream_error){
                        mi->trams_stream_success++;
                        mi->trams_stream_failure = 0;
                }

		
                if( mi->trams_stream_failure > 3 ) {
                       // trams_downgrade_stream(mi);
                        mi->trams_stream_success = 0;
                        mi->trams_stream_failure = 0;
			downgrade = true;
	                printk(KERN_DEBUG "multi-stream: downgrade stream error CORRECT\n");
                }

		
                //trams_cur_ridx, trams_last_tp, trams_cur_tp checking?
                if( mi->trams_curgroup <=  ARRAY_SIZE(mi->groups) && rate->attempts > 30 &&
                        TRAMS_FRAC(rate->success, (rate->attempts > 0 ? rate->attempts:1)) > TRAMS_FRAC(90, 100) ) {

                        //trams_upgrade_stream(mi);
	                //printk(KERN_DEBUG "multi-stream: upgrade stream \n");
			upgrade = true;
                }

        }
        else if (trams_mcs_groups[mi->trams_curgroup].streams <= 1) {

		
                //if(mi->trams_cur_ridx > 1 && mi->trams_curgroup <=  ARRAY_SIZE(mi->groups) && rate->attempts > 30 &&
                if(mi->trams_curgroup <=  ARRAY_SIZE(mi->groups) && rate->attempts > 30 &&
                TRAMS_FRAC(rate->success, (rate->attempts > 0 ? rate->attempts:1) ) > TRAMS_FRAC(90, 100) ){
                        //trams_upgrade_stream(mi);
			upgrade = true;
	                printk(KERN_DEBUG "single-stream: upgrade stream \n");
                }

		

                //since we only in single stream, we need a way to move up and down indices
                if (mi->trams_curgroup != 0  &&  rate->attempts > 30
                && TRAMS_FRAC(rate->success, rate->attempts) <
                TRAMS_FRAC(15, 100)) {
			mi->trams_skip20 = false;	
                        //trams_downgrade_stream(mi);
			downgrade = true;
	                printk(KERN_DEBUG "single-stream: downgrade stream\n");
                }
		else 
			mi->trams_skip20 = true;	

        }


	if (downgrade && !mi->trams_edown_bad ) {
	//if (downgrade ) {
              	trams_downgrade_stream(mi);
		mi->isProbingEn = true;
	}
	if (upgrade && !mi->trams_eup_bad) {
	//if (upgrade ) {
        	trams_upgrade_stream(mi);
		mi->isProbingEn = true;
	}


	if (!mi->isProbingEn) {
		mi->trams_lastgroup = mi->trams_curgroup; 
       		mi->trams_elast_tp = mi->trams_cur_tp;
	}

}


static void
trams_ht_tx_status(void *priv, struct ieee80211_supported_band *sband,
                      struct ieee80211_sta *sta, void *priv_sta,
                      struct sk_buff *skb)
{
	struct trams_ht_sta_priv *msp = priv_sta;
	struct trams_ht_sta *mi = &msp->ht;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *ar = info->status.rates;
	struct trams_rate_stats *rate;
	struct minstrel_priv *mp = priv;
	bool last = false;
	int group;
	int i = 0;
	


	if (!msp->is_ht)
		return mac80211_minstrel.tx_status(priv, sband, sta, &msp->legacy, skb);

	/* This packet was aggregated but doesn't carry status info */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	if (!(info->flags & IEEE80211_TX_STAT_AMPDU)) {
		info->status.ampdu_ack_len =
			(info->flags & IEEE80211_TX_STAT_ACK ? 1 : 0);
		info->status.ampdu_len = 1;
	}

	mi->ampdu_packets++;
	mi->ampdu_len += info->status.ampdu_len;

	if (!mi->sample_wait && !mi->sample_tries && mi->sample_count > 0) {
		mi->sample_wait = 16 + 2 * TRAMS_TRUNC(mi->avg_ampdu_len);
		mi->sample_tries = 2;
		mi->sample_count--;
	}

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		mi->sample_packets += info->status.ampdu_len;

	for (i = 0; !last; i++) {
		last = (i == IEEE80211_TX_MAX_RATES - 1) ||
		       !trams_ht_txstat_valid(&ar[i + 1]);

		if (!trams_ht_txstat_valid(&ar[i]))
			break;

		group = trams_ht_get_group_idx(&ar[i]);
		rate = &mi->groups[group].rates[ar[i].idx % 8];

		if (last)
			rate->success += info->status.ampdu_ack_len;

		rate->attempts += ar[i].count * info->status.ampdu_len;
		rate->ampdu_len = info->status.ampdu_len;
	}



        mi->trams_tx_ok += info->status.ampdu_ack_len;
        mi->trams_tx_err += info->status.ampdu_len - info->status.ampdu_ack_len;

        //need -1 for offseting
        mi->trams_tx_retr += ar[0].count - 1;
	


	//ddn
	trams_check_enhancement_index(mi, info, ar);
	


	//these function will be called every 100ms
	if (time_after(jiffies, mi->stats_update + (UPDATE_INTERVAL / 2 * HZ) / 1000)) {
		trams_ht_update_stats(mp, mi);
		trams_aggr_check(mp, sta, skb);
	}
	
	if (time_after(jiffies, mi->stats_update_reset + (UPDATE_INTERVAL_RESET / 2 * HZ) / 1000)) {

		mi->trams_eup_bad = 0;
		mi->trams_edown_bad = 0;


		mi->stats_update_reset = jiffies;
	}
	if (time_after(jiffies, mi->stats_update_adaptive + (mi->trams_time_interval/ 2 * HZ) / 1000)) {

		mi->trams_ossilate = 0;

		mi->stats_update_adaptive = jiffies;
	}
}

static void
trams_calc_retransmit(struct minstrel_priv *mp, struct trams_ht_sta *mi,
                         int index)
{
	struct trams_rate_stats *mr;
	const struct mcs_group *group;
	unsigned int tx_time, tx_time_rtscts, tx_time_data;
	unsigned int cw = mp->cw_min;
	unsigned int ctime = 0;
	unsigned int t_slot = 9; /* FIXME */
	unsigned int ampdu_len = TRAMS_TRUNC(mi->avg_ampdu_len);

	mr = trams_get_ratestats(mi, index);
	if (mr->probability < TRAMS_FRAC(1, 10)) {
		mr->retry_count = 1;
		mr->retry_count_rtscts = 1;
		return;
	}

	mr->retry_count = 2;
	mr->retry_count_rtscts = 2;
	mr->retry_updated = true;

	group = &trams_mcs_groups[index / MCS_GROUP_RATES];
	tx_time_data = group->duration[index % MCS_GROUP_RATES] * ampdu_len;

	/* Contention time for first 2 tries */
	ctime = (t_slot * cw) >> 1;
	cw = min((cw << 1) | 1, mp->cw_max);
	ctime += (t_slot * cw) >> 1;
	cw = min((cw << 1) | 1, mp->cw_max);

	/* Total TX time for data and Contention after first 2 tries */
	tx_time = ctime + 2 * (mi->overhead + tx_time_data);
	tx_time_rtscts = ctime + 2 * (mi->overhead_rtscts + tx_time_data);

	/* See how many more tries we can fit inside segment size */
	do {
		/* Contention time for this try */
		ctime = (t_slot * cw) >> 1;
		cw = min((cw << 1) | 1, mp->cw_max);

		/* Total TX time after this try */
		tx_time += ctime + mi->overhead + tx_time_data;
		tx_time_rtscts += ctime + mi->overhead_rtscts + tx_time_data;

		if (tx_time_rtscts < mp->segment_size)
			mr->retry_count_rtscts++;
	} while ((tx_time < mp->segment_size) &&
	         (++mr->retry_count < mp->max_retry));
}


static void
trams_ht_set_rate(struct minstrel_priv *mp, struct trams_ht_sta *mi,
                     struct ieee80211_tx_rate *rate, int index,
                     struct ieee80211_tx_rate_control *txrc,
                     bool sample, bool rtscts)
{
	const struct mcs_group *group = &trams_mcs_groups[index / MCS_GROUP_RATES];
	struct trams_rate_stats *mr;

	mr = trams_get_ratestats(mi, index);
	if (!mr->retry_updated)
		trams_calc_retransmit(mp, mi, index);

	if (sample)
		rate->count = 1;
	else if (mr->probability < TRAMS_FRAC(20, 100))
		rate->count = 2;
	else if (rtscts)
		rate->count = mr->retry_count_rtscts;
	else
		rate->count = mr->retry_count;

	rate->flags = IEEE80211_TX_RC_MCS | group->flags;
	if (rtscts)
		rate->flags |= IEEE80211_TX_RC_USE_RTS_CTS;
	rate->idx = index % MCS_GROUP_RATES + (group->streams - 1) * MCS_GROUP_RATES;
}

static inline int
trams_get_duration(int index)
{
	const struct mcs_group *group = &trams_mcs_groups[index / MCS_GROUP_RATES];
	return group->duration[index % MCS_GROUP_RATES];
}


static void
trams_ht_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
                     struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
	struct ieee80211_tx_rate *ar = info->status.rates;
	struct trams_ht_sta_priv *msp = priv_sta;
	struct trams_ht_sta *mi = &msp->ht;
	struct minstrel_priv *mp = priv;
	int rateidx = 0;

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	if (!msp->is_ht)
		return mac80211_minstrel.get_rate(priv, sta, &msp->legacy, txrc);

	info->flags |= mi->tx_flags;

	rateidx = mi->trams_cur_ridx + mi->trams_curgroup * MCS_GROUP_RATES;
	trams_ht_set_rate (mp, mi, &ar[0], rateidx >= 0 ? rateidx : -1, txrc, false, false);

	// At least 3 tx rates supported
	if (mp->hw->max_rates >= 3) {

		rateidx = (mi->trams_cur_ridx - 1 > 0 ? mi->trams_cur_ridx - 1 : 0) + mi->trams_curgroup * MCS_GROUP_RATES;
		trams_ht_set_rate (mp, mi, &ar[1], rateidx >= 0 ? rateidx : -1, txrc, false, true);

		//duy: this one is borrowed from minstrel since your rate does not include multirate retry
		trams_ht_set_rate(mp, mi, &ar[2], mi->max_prob_rate,
                                     txrc, false, true);

		ar[3].count = 0;
		ar[3].idx = -1;
	// 2 tx rates supported
	} else if (mp->hw->max_rates == 2) {

		rateidx = (mi->trams_cur_ridx - 1 > 0 ? mi->trams_cur_ridx - 1 : 0) + mi->trams_curgroup * MCS_GROUP_RATES;
		trams_ht_set_rate (mp, mi, &ar[1], rateidx >= 0 ? rateidx : -1, txrc, false, true);

		ar[2].count = 0;
		ar[2].idx = -1;
	} else {
		/* Not using MRR, only use the first rate */
		ar[1].count = 0;
		ar[1].idx = -1;
	}

	mi->total_packets++;

	/* wraparound */
	if (mi->total_packets == ~0) {
		mi->total_packets = 0;
		mi->sample_packets = 0;
	}
}

static void
trams_ht_update_caps(void *priv, struct ieee80211_supported_band *sband,
                        struct ieee80211_sta *sta, void *priv_sta,
			enum nl80211_channel_type oper_chan_type)
{
	struct minstrel_priv *mp = priv;
	struct trams_ht_sta_priv *msp = priv_sta;
	struct trams_ht_sta *mi = &msp->ht;
	struct ieee80211_mcs_info *mcs = &sta->ht_cap.mcs;
	struct ieee80211_local *local = hw_to_local(mp->hw);
	u16 sta_cap = sta->ht_cap.cap;
	int n_supported = 0;
	int ack_dur;
	int stbc;
	int i;

	/* fall back to the old minstrel for legacy stations */
	if (!sta->ht_cap.ht_supported)
		goto use_legacy;

	BUILD_BUG_ON(ARRAY_SIZE(trams_mcs_groups) !=
		TRAMS_MAX_STREAMS * TRAMS_STREAM_GROUPS);

	msp->is_ht = true;
	memset(mi, 0, sizeof(*mi));
	mi->stats_update = jiffies;
	mi->stats_update_adaptive = jiffies;

	ack_dur = ieee80211_frame_duration(local, 10, 60, 1, 1);
	mi->overhead = ieee80211_frame_duration(local, 0, 60, 1, 1) + ack_dur;
	mi->overhead_rtscts = mi->overhead + 2 * ack_dur;

	mi->avg_ampdu_len = TRAMS_FRAC(1, 1);

	/* When using MRR, sample more on the first attempt, without delay */
	if (mp->has_mrr) {
		mi->sample_count = 16;
		mi->sample_wait = 0;
	} else {
		mi->sample_count = 8;
		mi->sample_wait = 8;
	}
	mi->sample_tries = 4;

	stbc = (sta_cap & IEEE80211_HT_CAP_RX_STBC) >>
		IEEE80211_HT_CAP_RX_STBC_SHIFT;
	mi->tx_flags |= stbc << IEEE80211_TX_CTL_STBC_SHIFT;

	if (sta_cap & IEEE80211_HT_CAP_LDPC_CODING)
		mi->tx_flags |= IEEE80211_TX_CTL_LDPC;

	if (oper_chan_type != NL80211_CHAN_HT40MINUS &&
	    oper_chan_type != NL80211_CHAN_HT40PLUS)
		sta_cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	for (i = 0; i < ARRAY_SIZE(mi->groups); i++) {
		u16 req = 0;

		mi->groups[i].supported = 0;
		if (trams_mcs_groups[i].flags & IEEE80211_TX_RC_SHORT_GI) {
			if (trams_mcs_groups[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
				req |= IEEE80211_HT_CAP_SGI_40;
			else
				req |= IEEE80211_HT_CAP_SGI_20;
		}

		if (trams_mcs_groups[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			req |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

		if ((sta_cap & req) != req)
			continue;

		mi->groups[i].supported =
			mcs->rx_mask[trams_mcs_groups[i].streams - 1];

		if (mi->groups[i].supported)
			n_supported++;
	}

	if (!n_supported)
		goto use_legacy;

	return;

use_legacy:
	msp->is_ht = false;
	memset(&msp->legacy, 0, sizeof(msp->legacy));
	msp->legacy.r = msp->ratelist;
	return mac80211_minstrel.rate_init(priv, sband, sta, &msp->legacy);
}

static int 
trams_highest_group (struct trams_ht_sta *mi)
{
	int group;
	group = ARRAY_SIZE(trams_mcs_groups);
	while (group > 0 ) {
		group--;
		if (!mi->groups[group].supported)
			continue;

		//perhpas check if these rates are supported
    		mi->trams_cur_ridx = MCS_GROUP_RATES -1;
        	mi->trams_cur_streams = trams_mcs_groups[group].streams;
        	mi->trams_cur_edx = 1;
        	mi->trams_cur_sgi = trams_mcs_groups[group].sgi;
        	mi->trams_cur_ht40 = trams_mcs_groups[group].ht40;

		mi->trams_last_ridx = MCS_GROUP_RATES -1;
		mi->trams_last_streams = trams_mcs_groups[group].streams;
		mi->trams_last_edx = 1;
		mi->trams_last_sgi = trams_mcs_groups[group].sgi;
		mi->trams_last_ht40 = trams_mcs_groups[group].ht40;


		return group;	
	}
	return 0;
}

static void
trams_ht_rate_init(void *priv, struct ieee80211_supported_band *sband,
                      struct ieee80211_sta *sta, void *priv_sta)
{
	struct minstrel_priv *mp = priv;

	//ddn	
	struct trams_ht_sta_priv *msp = priv_sta;
	struct trams_ht_sta *mi = &msp->ht;

	mi->trams_lastgroup = mi->trams_curgroup = trams_highest_group(mi); 
	mi->trams_skip20 = true;
	mi->isProbingEn = false;
	mi->isProbingMod = false;
        mi->trams_last_tp = 0; 
        mi->trams_elast_tp = 0; 
        mi->trams_avg_tp = 0; 
	mi->trams_eup_bad = 0;
	mi->trams_edown_bad = 0;
	mi->trams_ticks = 0;
	mi->trams_time_interval = 1000;
	mi->trams_consecutive = 0;
	mi->isMultiplicative = false;
	mi->trams_ossilate = 0;
	mi->trams_successive = 0;

	//TODO: check to see if it's supported?
	mi->trams_cur_ridx = MCS_GROUP_RATES - 1;
	mi->trams_tx_ok = mi->trams_tx_err = mi->trams_tx_retr = mi->trams_tx_credit = 0;
	mi->trams_stream_failure = mi->trams_stream_success = 0;


	trams_ht_update_caps(priv, sband, sta, priv_sta, mp->hw->conf.channel_type);
}

static void
trams_ht_rate_update(void *priv, struct ieee80211_supported_band *sband,
                        struct ieee80211_sta *sta, void *priv_sta,
                        u32 changed, enum nl80211_channel_type oper_chan_type)
{


	trams_ht_update_caps(priv, sband, sta, priv_sta, oper_chan_type);
}

static void *
trams_ht_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct ieee80211_supported_band *sband;
	struct trams_ht_sta_priv *msp;
	struct minstrel_priv *mp = priv;
	struct ieee80211_hw *hw = mp->hw;
	int max_rates = 0;
	int i;

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		sband = hw->wiphy->bands[i];
		if (sband && sband->n_bitrates > max_rates)
			max_rates = sband->n_bitrates;
	}

	msp = kzalloc(sizeof(struct trams_ht_sta), gfp);
	if (!msp)
		return NULL;

	msp->ratelist = kzalloc(sizeof(struct minstrel_rate) * max_rates, gfp);
	if (!msp->ratelist)
		goto error;

	return msp;

error:
	kfree(msp);
	return NULL;
}

static void
trams_ht_free_sta(void *priv, struct ieee80211_sta *sta, void *priv_sta)
{
	struct trams_ht_sta_priv *msp = priv_sta;

	kfree(msp->ratelist);
	kfree(msp);
}

static void *
trams_ht_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return mac80211_minstrel.alloc(hw, debugfsdir);
}

static void
trams_ht_free(void *priv)
{
	mac80211_minstrel.free(priv);
}

static struct rate_control_ops mac80211_trams_ht = {
	.name = "trams_ht",
	.tx_status = trams_ht_tx_status,
	.get_rate = trams_ht_get_rate,
	.rate_init = trams_ht_rate_init,
	.rate_update = trams_ht_rate_update,
	.alloc_sta = trams_ht_alloc_sta,
	.free_sta = trams_ht_free_sta,
	.alloc = trams_ht_alloc,
	.free = trams_ht_free,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = trams_ht_add_sta_debugfs,
	.remove_sta_debugfs = trams_ht_remove_sta_debugfs,
#endif
};

int __init
rc80211_trams_ht_init(void)
{
	return ieee80211_rate_control_register(&mac80211_trams_ht);
}

void
rc80211_trams_ht_exit(void)
{
	ieee80211_rate_control_unregister(&mac80211_trams_ht);
}
