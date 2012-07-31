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


#ifndef __RC_TRAMS_HT_H
#define __RC_TRAMS_HT_H

#define TRAMS_STREAM_GROUPS	4

#define TRAMS_SCALE	16
#define TRAMS_FRAC(val, div) (((val) << TRAMS_SCALE) / div)
#define TRAMS_TRUNC(val) ((val) >> TRAMS_SCALE)

#define MCS_GROUP_RATES	8
#define TRAMS_MAX_STREAMS 3

struct mcs_group {
	u32 flags;
	unsigned int sgi;
	unsigned int ht40;
	unsigned int streams;
	unsigned int duration[MCS_GROUP_RATES];
};

extern const struct mcs_group trams_mcs_groups[];

struct trams_rate_stats {
	/* current / last sampling period attempts/success counters */
	unsigned int attempts, last_attempts;
	unsigned int success, last_success;

	unsigned int ampdu_len;

	/* total attempts/success counters */
	u64 att_hist, succ_hist;

	/* current throughput */
	unsigned int cur_tp;

	/* packet delivery probabilities */
	unsigned int cur_prob, probability;

	/* maximum retry counts */
	unsigned int retry_count;
	unsigned int retry_count_rtscts;

	


	bool retry_updated;
	u8 sample_skipped;
};

struct trams_mcs_group_data {
	u8 index;
	u8 column;

	/* bitfield of supported MCS rates of this group */
	u8 supported;

	/* selected primary rates */
	unsigned int max_tp_rate;
	unsigned int max_tp_rate2;
	unsigned int max_prob_rate;

	/* MCS rate statistics */
	struct trams_rate_stats rates[MCS_GROUP_RATES];
};

struct trams_ht_sta {
	/* ampdu length (average, per sampling interval) */
	unsigned int ampdu_len;
	unsigned int ampdu_packets;

         //ddn
	unsigned int trams_cur_streams;
	unsigned int trams_last_streams;

	unsigned int trams_cur_ridx;
	unsigned int trams_last_ridx;

	unsigned int trams_cur_edx;
	unsigned int trams_last_edx;

	unsigned int trams_cur_sgi;
	unsigned int trams_last_sgi;

	unsigned int trams_cur_ht40;
	unsigned int trams_last_ht40;

        unsigned int trams_cur_tp;
        unsigned int trams_ecur_tp;
        unsigned int trams_last_tp;
        unsigned int trams_elast_tp;
        unsigned int trams_hist_tp;
	unsigned int trams_avg_tp;
	unsigned int trams_ossilate;
	unsigned int trams_successive;
	
        unsigned int trams_tx_ok;
        unsigned int trams_tx_err;
        unsigned int trams_tx_retr;
        unsigned int trams_tx_credit;
        unsigned int trams_stream_upper;
        unsigned int trams_consec_err;
        unsigned int trams_consec_ok;
        unsigned int trams_consec_streamerr;
        unsigned int trams_stream_failure;
        unsigned int trams_stream_success;
        unsigned int trams_total_frames;
	unsigned int trams_mup_count;
	unsigned int trams_mup_bad;
	unsigned int trams_mdown_bad;
	unsigned int trams_eup_bad;
	unsigned int trams_edown_bad;
	unsigned int trams_ticks;
	unsigned int trams_time_interval;

	unsigned int trams_consecutive;

        unsigned int trams_curgroup;
        unsigned int trams_lastgroup;
	bool trams_skip20;
	bool isProbingEn;
	bool isProbingMod;
 	bool isMultiplicative;





	/* ampdu length (EWMA) */
	unsigned int avg_ampdu_len;

	/* best throughput rate */
	unsigned int max_tp_rate;

	/* second best throughput rate */
	unsigned int max_tp_rate2;

	/* best probability rate */
	unsigned int max_prob_rate;

	/* time of last status update */
	unsigned long stats_update;

	unsigned long stats_update_reset;

	unsigned long stats_update_adaptive;

	/* overhead time in usec for each frame */
	unsigned int overhead;
	unsigned int overhead_rtscts;

	unsigned int total_packets;
	unsigned int sample_packets;

	/* tx flags to add for frames for this sta */
	u32 tx_flags;

	u8 sample_wait;
	u8 sample_tries;
	u8 sample_count;
	u8 sample_slow;

	/* current MCS group to be sampled */
	u8 sample_group;

	/* MCS rate group info and statistics */
	struct trams_mcs_group_data groups[TRAMS_MAX_STREAMS * TRAMS_STREAM_GROUPS];
};

struct trams_ht_sta_priv {
	union {
		struct trams_ht_sta ht;
		struct minstrel_sta_info legacy;
	};
#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *dbg_stats;
#endif
	void *ratelist;
	bool is_ht;
};

void trams_ht_add_sta_debugfs(void *priv, void *priv_sta, struct dentry *dir);
void trams_ht_remove_sta_debugfs(void *priv, void *priv_sta);

#endif
