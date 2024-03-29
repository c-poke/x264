/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) ENST 2000-200X
 *					All rights reserved
 *
 *  This file is part of GPAC / mp4-to-ts (mp42ts) application
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <gpac/media_tools.h>
#include <gpac/constants.h>
#include <gpac/base_coding.h>
#include <gpac/mpegts.h>

#ifndef GPAC_DISABLE_STREAMING
#include <gpac/ietf.h>
#endif

#ifndef GPAC_DISABLE_SENG
#include <gpac/scene_engine.h>
#endif


#ifdef GPAC_DISABLE_ISOM

#error "Cannot compile MP42TS if GPAC is not built with ISO File Format support"

#endif

#ifdef GPAC_DISABLE_MPEG2TS_MUX

#error "Cannot compile MP42TS if GPAC is not built with MPEG2-TS Muxing support"

#endif


#define DEFAULT_PCR_OFFSET	18000

#define UDP_BUFFER_SIZE	0x40000

#define MP42TS_PRINT_FREQ 634 /*refresh printed info every CLOCK_REFRESH ms*/
#define MP42TS_VIDEO_FREQ 1000 /*meant to send AVC IDR only every CLOCK_REFRESH ms*/

static GFINLINE void usage(const char * progname) 
{
	fprintf(stderr, "USAGE: %s -rate=R [[-prog=prog1]..[-prog=progn]] [-audio=url] [-video=url] [-mpeg4-carousel=n] [-mpeg4] [-time=n] [-src=file] DST [[DST]]\n"
					"\n"
#ifdef GPAC_MEMORY_TRACKING
					"\t-mem-track:  enables memory tracker\n"
#endif
					"\t-rate=R                specifies target rate in kbps of the multiplex (mandatory)\n"
					"\t-real-time             specifies the muxer will work in real-time mode\n"
					"\t                        * automatically set for SDP or BT input\n"
					"\t-pcr-init=V            sets initial value V for PCR - if not set, random value is used\n"
					"\t-pcr-offset=V          offsets all timestamps from PCR by V, in 90kHz. Default value: %d\n"
					"\t-psi-rate=V            sets PSI refresh rate V in ms (default 100ms). If 0, PSI data is only send once at the begining\n"
					"\t-time=n                request the program to stop after n ms\n"
					"\t-single-au             forces 1 PES = 1 AU (disabled by default)\n"
					"\t-rap                   forces RAP/IDR to be aligned with PES start for video streams (disabled by default)\n"
					"                          in this mode, PAT, PMT and PCR will be inserted before the first TS packet of the RAP PES\n"
					"\t-prog=filename         specifies an input file used for a TS service\n"
					"\t                        * currently only supports ISO files and SDP files\n"
					"\t                        * can be used several times, once for each program\n"
					"\t-audio=url             may be mp3/udp or aac/http (shoutcast/icecast)\n"
					"\t-video=url             shall be a raw h264 frame\n"
					"\t-src=filename          update file: must be either an .sdp or a .bt file\n\n"
					"\tDST : Destinations, at least one is mandatory\n"
					"\t  -dst-udp             UDP_address:port (multicast or unicast)\n"
					"\t  -dst-rtp             RTP_address:port\n"
					"\t  -dst-file            Supports the following arguments:\n"
					"\t     -segment-dir=dir       server local directory to store segments\n"
					"\t     -segment-duration=dur  segment duration in seconds\n"
					"\t     -segment-manifest=file m3u8 file basename\n"
					"\t     -segment-http-prefix=p client address for accessing server segments\n"
					"\t     -segment-number=n      only n segments are used using a cyclic pattern\n"
					"\t\n"
					"\tMPEG-4 options\n"
					"\t-mpeg4-carousel=n      carousel period in ms\n"
                    "\t-mpeg4 or -4on2        forces usage of MPEG-4 signaling (IOD and SL Config)\n"
                    "\t-4over2                same as -4on2 and uses PMT to carry OD Updates\n"
                    "\t-bifs-pes              carries BIFS over PES instead of sections\n"
                    "\t-bifs-pes-ex           carries BIFS over PES without writing timestamps in SL\n"
					"\t\n"
					"\t-logs                  set log tools and levels, formatted as a ':'-separated list of toolX[:toolZ]@levelX\n"
					"\t-h or -help            print this screen\n"
					"\n", progname, DEFAULT_PCR_OFFSET
		);
}


#define MAX_MUX_SRC_PROG	100
typedef struct
{
	GF_ISOFile *mp4;
	u32 nb_streams, pcr_idx;
	GF_ESInterface streams[40];
	GF_Descriptor *iod;
#ifndef GPAC_DISABLE_SENG
	GF_SceneEngine *seng;
#endif
	GF_Thread *th;
	char *src_name;
	u32 rate;
	Bool repeat;
	u32 mpeg4_signaling;
	Bool audio_configured;
	u64 samples_done, samples_count;
	u32 nb_real_streams;
	Bool real_time;
	GF_List *od_updates;
} M2TSProgram;

typedef struct
{
	GF_ISOFile *mp4;
	u32 track, sample_number, sample_count;
	GF_ISOSample *sample;
	/*refresh rate for images*/
	u32 image_repeat_ms, nb_repeat_last;
	void *dsi;
	u32 dsi_size;
	u32 nalu_size;
	void *dsi_and_rap;
	Bool loop;
	Bool is_repeat;
	u64 ts_offset;
	M2TSProgram *prog;
	char nalu_delim[6];
} GF_ESIMP4;

typedef struct
{
	u32 carousel_period, ts_delta;
	u16 aggregate_on_stream;
	Bool adjust_carousel_time;
	Bool discard;
	Bool rap;
	Bool critical;
	Bool vers_inc;
} GF_ESIStream;

typedef struct
{
	u32 size;
	char *data;
} GF_SimpleDataDescriptor;

//TODO: find a clean way to save this data
#ifndef GPAC_DISABLE_PLAYER
static u32 audio_OD_stream_id = (u32)-1;
#endif

#define AUDIO_OD_ESID	100
#define AUDIO_DATA_ESID	101
#define VIDEO_DATA_ESID	105

/*output types*/
enum
{
	GF_MP42TS_FILE, /*open mpeg2ts file*/
	GF_MP42TS_UDP,  /*open udp socket*/
	GF_MP42TS_RTP,  /*open rtp socket*/
#ifndef GPAC_DISABLE_PLAYER
	GF_MP42TS_HTTP,	/*open http downloader*/
#endif
};

static GF_Err mp4_input_ctrl(GF_ESInterface *ifce, u32 act_type, void *param)
{
	GF_ESIMP4 *priv = (GF_ESIMP4 *)ifce->input_udta;
	if (!priv) return GF_BAD_PARAM;

	switch (act_type) {
	case GF_ESI_INPUT_DATA_FLUSH:
	{
		GF_ESIPacket pck;
		if (!priv->sample) 
			priv->sample = gf_isom_get_sample(priv->mp4, priv->track, priv->sample_number+1, NULL);

		if (!priv->sample) {
			return GF_IO_ERR;
		}

		pck.flags = 0;
		pck.flags = GF_ESI_DATA_AU_START | GF_ESI_DATA_HAS_CTS;
		if (priv->sample->IsRAP) pck.flags |= GF_ESI_DATA_AU_RAP;
		pck.cts = priv->sample->DTS + priv->ts_offset;
		if (priv->is_repeat) pck.flags |= GF_ESI_DATA_REPEAT;

		if (priv->nb_repeat_last) {
			pck.cts += priv->nb_repeat_last*ifce->timescale * priv->image_repeat_ms / 1000;
		}

		if (priv->sample->CTS_Offset) {
			pck.dts = pck.cts;
			pck.cts += priv->sample->CTS_Offset;
			pck.flags |= GF_ESI_DATA_HAS_DTS;
		}

		if (priv->nalu_size) {
			Bool nalu_delim_sent = 0;
			u32 remain = priv->sample->dataLength;
			char *ptr = priv->sample->data;
			u32 v, size;
			char sc[10];


			/*send nalus*/
			sc[0] = sc[1] = sc[2] = 0; sc[3] = 1;
			while (remain) {
				size = 0;
				v = priv->nalu_size;
				while (v) {
					size |= (u8) *ptr;
					ptr++;
					remain--;
					v-=1;
					if (v) size<<=8;
				}
				remain -= size;

				if (!nalu_delim_sent) {
					nalu_delim_sent = 1;
					/*send a NALU delim: copy over NAL ref idc*/
					sc[4] = (ptr[0] & 0x60) | GF_AVC_NALU_ACCESS_UNIT;
					sc[5] = 0xF0 /*7 "all supported NALUs" (=111) + rbsp trailing (10000)*/;

					pck.data = sc;
					pck.data_len = 6;
					ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
					pck.flags &= ~GF_ESI_DATA_AU_START;

					/*and send SPD / PPS if RAP - it is not clear in the specs whether SPS/PPS should be inserted after
					the AU delimiter NALU*/
					if (priv->sample->IsRAP && priv->dsi && priv->dsi_size) {
						pck.data = priv->dsi;
						pck.data_len = priv->dsi_size;
						ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
						pck.flags &= ~GF_ESI_DATA_AU_START;
					}
				}

				pck.data = sc;
				pck.data_len = 4;
				ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);

				if (!remain) pck.flags |= GF_ESI_DATA_AU_END;
				pck.flags &= ~GF_ESI_DATA_AU_START;

				pck.data = ptr;
				pck.data_len = size;
				ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
				ptr += size;
			}

		} else {

			if (priv->sample->IsRAP && priv->dsi && priv->dsi_size) {
				pck.data = priv->dsi;
				pck.data_len = priv->dsi_size;
				ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
				pck.flags &= ~GF_ESI_DATA_AU_START;
			}

			pck.flags |= GF_ESI_DATA_AU_END;
			pck.data = priv->sample->data;
			pck.data_len = priv->sample->dataLength;
			ifce->output_ctrl(ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
		}

		gf_isom_sample_del(&priv->sample);
		priv->sample_number++;

		if (!priv->prog->real_time && !priv->is_repeat) {
			priv->prog->samples_done++;
			gf_set_progress("Converting to MPEG-2 TS", priv->prog->samples_done, priv->prog->samples_count);
		}

		if (priv->sample_number==priv->sample_count) {
			if (priv->loop) {
				Double scale;
				u64 duration;
				/*increment ts offset*/
				scale = gf_isom_get_media_timescale(priv->mp4, priv->track);
				scale /= gf_isom_get_timescale(priv->mp4);
				duration = (u64) (gf_isom_get_duration(priv->mp4) * scale);
				priv->ts_offset += duration;
				priv->sample_number = 0;
				priv->is_repeat = (priv->sample_count==1) ? 1 : 0;
			}
			else if (priv->image_repeat_ms && priv->prog->nb_real_streams) {
				priv->nb_repeat_last++;
				priv->sample_number--;
				priv->is_repeat = 1;
			} else {
				if (!(ifce->caps & GF_ESI_STREAM_IS_OVER)) {
					ifce->caps |= GF_ESI_STREAM_IS_OVER;
					if (priv->sample_count>1) {
						assert(priv->prog->nb_real_streams);
						priv->prog->nb_real_streams--;
					}
				}
			}
		}
	}
		return GF_OK;

	case GF_ESI_INPUT_DESTROY:
		if (priv->dsi) gf_free(priv->dsi);
		if (ifce->decoder_config) {
			gf_free(ifce->decoder_config);
			ifce->decoder_config = NULL;
		}
		gf_free(priv);
		ifce->input_udta = NULL;
		return GF_OK;
	default:
		return GF_BAD_PARAM;
	}
}

static void fill_isom_es_ifce(M2TSProgram *prog, GF_ESInterface *ifce, GF_ISOFile *mp4, u32 track_num, u32 bifs_use_pes)
{
	GF_ESIMP4 *priv;
	char _lan[4];
	GF_DecoderConfig *dcd;
	u64 avg_rate, duration;

	GF_SAFEALLOC(priv, GF_ESIMP4);

	priv->mp4 = mp4;
	priv->track = track_num;
	priv->loop = prog->real_time ? 1 : 0;
	priv->sample_count = gf_isom_get_sample_count(mp4, track_num);
	prog->samples_count += priv->sample_count;
	if (priv->sample_count>1)
		prog->nb_real_streams++;

	priv->prog = prog;
	memset(ifce, 0, sizeof(GF_ESInterface));
	ifce->stream_id = gf_isom_get_track_id(mp4, track_num);
	dcd = gf_isom_get_decoder_config(mp4, track_num, 1);
	ifce->stream_type = dcd->streamType;
	ifce->object_type_indication = dcd->objectTypeIndication;
	if (dcd->decoderSpecificInfo && dcd->decoderSpecificInfo->dataLength) {
		switch (dcd->objectTypeIndication) {
		case GPAC_OTI_AUDIO_AAC_MPEG4:
			ifce->decoder_config = gf_malloc(sizeof(char)*dcd->decoderSpecificInfo->dataLength);
			ifce->decoder_config_size = dcd->decoderSpecificInfo->dataLength;
			memcpy(ifce->decoder_config, dcd->decoderSpecificInfo->data, dcd->decoderSpecificInfo->dataLength);
			break;
		case GPAC_OTI_VIDEO_MPEG4_PART2:
			priv->dsi = gf_malloc(sizeof(char)*dcd->decoderSpecificInfo->dataLength);
			priv->dsi_size = dcd->decoderSpecificInfo->dataLength;
			memcpy(priv->dsi, dcd->decoderSpecificInfo->data, dcd->decoderSpecificInfo->dataLength);
			break;
		case GPAC_OTI_VIDEO_AVC:
		{
#ifndef GPAC_DISABLE_AV_PARSERS
			GF_AVCConfigSlot *slc;
			u32 i;
			GF_BitStream *bs;
			GF_AVCConfig *avccfg = gf_isom_avc_config_get(mp4, track_num, 1);
			priv->nalu_size = avccfg->nal_unit_size;
			bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			for (i=0; i<gf_list_count(avccfg->sequenceParameterSets);i++) {
				slc = gf_list_get(avccfg->sequenceParameterSets, i);
				gf_bs_write_u32(bs, 1);
				gf_bs_write_data(bs, slc->data, slc->size);
			}
			for (i=0; i<gf_list_count(avccfg->pictureParameterSets);i++) {
				slc = gf_list_get(avccfg->pictureParameterSets, i);
				gf_bs_write_u32(bs, 1);
				gf_bs_write_data(bs, slc->data, slc->size);
			}
			gf_bs_get_content(bs, (char **) &priv->dsi, &priv->dsi_size);
			gf_bs_del(bs);
#endif
			priv->nalu_delim[3] = 1;
			priv->nalu_delim[4] = 0; /*this will be nal header*/
			priv->nalu_delim[5] = 0xF0 /*7 "all supported NALUs" (=111) + rbsp trailing (10000)*/;
		}
			break;
		}
	}
	gf_odf_desc_del((GF_Descriptor *)dcd);
	gf_isom_get_media_language(mp4, track_num, _lan);
	ifce->lang = GF_4CC(_lan[0],_lan[1],_lan[2],' ');

	ifce->timescale = gf_isom_get_media_timescale(mp4, track_num);
	ifce->duration = gf_isom_get_media_timescale(mp4, track_num);
	avg_rate = gf_isom_get_media_data_size(mp4, track_num);
	avg_rate *= ifce->timescale * 8;
	if (0!=(duration=gf_isom_get_media_duration(mp4, track_num)))
		avg_rate /= duration;

	if (gf_isom_has_time_offset(mp4, track_num)) ifce->caps |= GF_ESI_SIGNAL_DTS;

	ifce->bit_rate = (u32) avg_rate;
	ifce->duration = (Double) (s64) gf_isom_get_media_duration(mp4, track_num);
	ifce->duration /= ifce->timescale;

	GF_SAFEALLOC(ifce->sl_config, GF_SLConfig);
	ifce->sl_config->tag = GF_ODF_SLC_TAG;
//	ifce->sl_config->predefined = 3;
	ifce->sl_config->useAccessUnitStartFlag = 1;
	ifce->sl_config->useAccessUnitEndFlag = 1;
	ifce->sl_config->useRandomAccessPointFlag = 1;
	ifce->sl_config->useTimestampsFlag = 1;
	ifce->sl_config->timestampLength = 33;
	ifce->sl_config->timestampResolution = ifce->timescale;

	/*test mode in which time stamps are 90khz and not coded but copied over from PES header*/
	if (bifs_use_pes==2) {
		ifce->sl_config->timestampLength = 0;
		ifce->sl_config->timestampResolution = 90000;
	}
	
#ifdef GPAC_DISABLE_ISOM_WRITE
	fprintf(stdout, "Warning: GPAC was compiled without ISOM Write support, can't set SL Config!\n");
#else
	gf_isom_set_extraction_slc(mp4, track_num, 1, ifce->sl_config);
#endif

	ifce->input_ctrl = mp4_input_ctrl;
	if (priv != ifce->input_udta){
	  if (ifce->input_udta)
	    gf_free(ifce->input_udta);
	  ifce->input_udta = priv;
	}
}


#ifndef GPAC_DISABLE_SENG
static GF_Err seng_input_ctrl(GF_ESInterface *ifce, u32 act_type, void *param)
{
	if (act_type==GF_ESI_INPUT_DESTROY) {
		//TODO: free my data
		if (ifce->input_udta)
		  gf_free(ifce->input_udta);
		ifce->input_udta = NULL;
		return GF_OK;
	}

	return GF_OK;
}
#endif


#ifndef GPAC_DISABLE_STREAMING
typedef struct
{
	/*RTP channel*/
	GF_RTPChannel *rtp_ch;

	/*depacketizer*/
	GF_RTPDepacketizer *depacketizer;

	GF_ESIPacket pck;

	GF_ESInterface *ifce;

	Bool cat_dsi;
	void *dsi_and_rap;

	Bool use_carousel;
	u32 au_sn;
} GF_ESIRTP;

static GF_Err rtp_input_ctrl(GF_ESInterface *ifce, u32 act_type, void *param)
{
	u32 size, PayloadStart;
	GF_Err e;
	GF_RTPHeader hdr;
	char buffer[8000];
	GF_ESIRTP *rtp = (GF_ESIRTP*)ifce->input_udta;

	if (!ifce->input_udta) return GF_BAD_PARAM;

	switch (act_type) {
	case GF_ESI_INPUT_DATA_FLUSH:
		/*flush rtp channel*/
		while (1) {
			size = gf_rtp_read_rtp(rtp->rtp_ch, buffer, 8000);
			if (!size) break;
			e = gf_rtp_decode_rtp(rtp->rtp_ch, buffer, size, &hdr, &PayloadStart);
			if (e) return e;
			gf_rtp_depacketizer_process(rtp->depacketizer, &hdr, buffer + PayloadStart, size - PayloadStart);
		}
		/*flush rtcp channel*/
		while (1) {
			size = gf_rtp_read_rtcp(rtp->rtp_ch, buffer, 8000);
			if (!size) break;
			e = gf_rtp_decode_rtcp(rtp->rtp_ch, buffer, size, NULL);
			if (e == GF_EOS) ifce->caps |= GF_ESI_STREAM_IS_OVER;
		}
		return GF_OK;
	case GF_ESI_INPUT_DESTROY:
		gf_rtp_depacketizer_del(rtp->depacketizer);
		if (rtp->dsi_and_rap) gf_free(rtp->dsi_and_rap);
		gf_rtp_del(rtp->rtp_ch);
		gf_free(rtp);
		ifce->input_udta = NULL;
		return GF_OK;
	}
	return GF_OK;
}
#endif

static GF_Err void_input_ctrl(GF_ESInterface *ifce, u32 act_type, void *param)
{
	return GF_OK;
}

/*AAC import features*/
#ifndef GPAC_DISABLE_PLAYER

void *audio_prog = NULL;
static void SampleCallBack(void *calling_object, u16 ESID, char *data, u32 size, u64 ts);
#define DONT_USE_TERMINAL_MODULE_API
#include "../../modules/aac_in/aac_in.c" 
AACReader *aac_reader = NULL;
u64 audio_discontinuity_offset = 0;

/*create an OD codec and encode the descriptor*/
static GF_Err encode_audio_desc(GF_ESD *esd, GF_SimpleDataDescriptor *audio_desc) 
{
	GF_Err e;
	GF_ODCodec *odc = gf_odf_codec_new();
	GF_ODUpdate *od_com = (GF_ODUpdate*)gf_odf_com_new(GF_ODF_OD_UPDATE_TAG);
	GF_ObjectDescriptor *od = (GF_ObjectDescriptor*)gf_odf_desc_new(GF_ODF_OD_TAG);
	assert( esd );
	assert( audio_desc );
	gf_list_add(od->ESDescriptors, esd);
	od->objectDescriptorID = AUDIO_DATA_ESID;
	gf_list_add(od_com->objectDescriptors, od);

	e = gf_odf_codec_add_com(odc, (GF_ODCom*)od_com);
	if (e) {
		fprintf(stderr, "Audio input error add the command to be encoded\n");
		return e;
	}
	e = gf_odf_codec_encode(odc, 0);
	if (e) {
		fprintf(stderr, "Audio input error encoding the descriptor\n");
		return e;
	}
	e = gf_odf_codec_get_au(odc, &audio_desc->data, &audio_desc->size);
	if (e) {
		fprintf(stderr, "Audio input error getting the descriptor\n");
		return e;
	}
	e = gf_odf_com_del((GF_ODCom**)&od_com);
	if (e) {
		fprintf(stderr, "Audio input error deleting the command\n");
		return e;
	}
	gf_odf_codec_del(odc);

	return GF_OK;
}

#endif


static void SampleCallBack(void *calling_object, u16 ESID, char *data, u32 size, u64 ts)
{		
	u32 i;
	//fprintf(stderr, "update: ESID=%d - size=%d - ts="LLD"\n", ESID, size, ts);

	if (calling_object) {
		M2TSProgram *prog = (M2TSProgram *)calling_object;

#ifndef GPAC_DISABLE_PLAYER
		if (ESID == AUDIO_DATA_ESID) {
			if (audio_OD_stream_id != (u32)-1) {
				/*this is the first time we get some audio data. Therefore we are sure we can retrieve the audio descriptor. Then we'll
				  send it by calling this callback recursively so that a player gets the audio descriptor before audio data.
				  Hack: the descriptor is carried thru the input_udta, you shall delete it*/
				GF_SimpleDataDescriptor *audio_desc = prog->streams[audio_OD_stream_id].input_udta;
				if (audio_desc && !audio_desc->data) /*intended for HTTP/AAC: an empty descriptor was set (vs already filled for RTP/UDP MP3)*/
				{
					/*get the audio descriptor and encode it*/
					GF_ESD *esd = AAC_GetESD(aac_reader);
					assert(esd->slConfig->timestampResolution);
					esd->slConfig->useAccessUnitStartFlag = 1;
					esd->slConfig->useAccessUnitEndFlag = 1;
					esd->slConfig->useTimestampsFlag = 1;
					esd->slConfig->timestampLength = 33;
					/*audio stream, all samples are RAPs*/
					esd->slConfig->useRandomAccessPointFlag = 0;
					esd->slConfig->hasRandomAccessUnitsOnlyFlag = 1;
					for (i=0; i<prog->nb_streams; i++) {
						if (prog->streams[i].stream_id == AUDIO_DATA_ESID) {
							GF_Err e;
							prog->streams[i].timescale = esd->slConfig->timestampResolution;
							e = gf_m2ts_program_stream_update_ts_scale(&prog->streams[i], esd->slConfig->timestampResolution);
							assert(!e);
							if (!prog->streams[i].sl_config) prog->streams[i].sl_config = (GF_SLConfig *)gf_odf_desc_new(GF_ODF_SLC_TAG);
							memcpy(prog->streams[i].sl_config, esd->slConfig, sizeof(GF_SLConfig));
							break;
						}
					}
					esd->ESID = AUDIO_DATA_ESID;
					assert(audio_OD_stream_id != (u32)-1);
					encode_audio_desc(esd, audio_desc);

					/*build the ESI*/
					{
						/*audio OD descriptor: rap=1 and vers_inc=0*/
						GF_SAFEALLOC(prog->streams[audio_OD_stream_id].input_udta, GF_ESIStream);
						((GF_ESIStream*)prog->streams[audio_OD_stream_id].input_udta)->rap = 1;

						/*we have the descriptor; now call this callback recursively so that a player gets the audio descriptor before audio data.*/
						prog->repeat = 1;
						SampleCallBack(prog, AUDIO_OD_ESID, audio_desc->data, audio_desc->size, 0/*gf_m2ts_get_sys_clock(muxer)*/);
						prog->repeat = 0;

						/*clean*/
						gf_free(audio_desc->data);
						gf_free(audio_desc);
						gf_free(prog->streams[audio_OD_stream_id].input_udta);
						prog->streams[audio_OD_stream_id].input_udta = NULL;
					}
				}
			}
			/*update the timescale if needed*/
			else if (!prog->audio_configured) {
				GF_ESD *esd = AAC_GetESD(aac_reader);
				assert(esd->slConfig->timestampResolution);
				for (i=0; i<prog->nb_streams; i++) {
					if (prog->streams[i].stream_id == AUDIO_DATA_ESID) {
						GF_Err e;
						prog->streams[i].timescale = esd->slConfig->timestampResolution;
						prog->streams[i].decoder_config = esd->decoderConfig->decoderSpecificInfo->data;
						prog->streams[i].decoder_config_size = esd->decoderConfig->decoderSpecificInfo->dataLength;
						esd->decoderConfig->decoderSpecificInfo->data = NULL;
						esd->decoderConfig->decoderSpecificInfo->dataLength = 0;
						e = gf_m2ts_program_stream_update_ts_scale(&prog->streams[i], esd->slConfig->timestampResolution);
						if (!e)
							prog->audio_configured = 1;
						break;
					}
				}
				gf_odf_desc_del((GF_Descriptor *)esd);
			}

			/*overwrite timing as it is flushed to 0 on discontinuities*/
			ts += audio_discontinuity_offset;
		}
#endif
		i=0;
		while (i<prog->nb_streams){
			if (prog->streams[i].output_ctrl==NULL) {
				fprintf(stderr, "MULTIPLEX NOT YET CREATED\n");
				return;
			}
			if (prog->streams[i].stream_id == ESID) {
				GF_ESIStream *priv = (GF_ESIStream *)prog->streams[i].input_udta;
				GF_ESIPacket pck;
				memset(&pck, 0, sizeof(GF_ESIPacket));
				pck.data = data;
				pck.data_len = size;
				pck.flags |= GF_ESI_DATA_HAS_CTS;
				pck.flags |= GF_ESI_DATA_HAS_DTS;
				pck.flags |= GF_ESI_DATA_AU_START;
				pck.flags |= GF_ESI_DATA_AU_END;
				if (ts) pck.cts = pck.dts = ts;

				if (priv->rap)
					pck.flags |= GF_ESI_DATA_AU_RAP;
				if (prog->repeat || !priv->vers_inc) {
					pck.flags |= GF_ESI_DATA_REPEAT;
					fprintf(stderr, "RAP carousel from scene engine sent: ESID=%d - size=%d - ts="LLD"\n", ESID, size, ts);
				} else {
					if (ESID != AUDIO_DATA_ESID && ESID != VIDEO_DATA_ESID)	/*don't log A/V inputs*/
						fprintf(stderr, "Update from scene engine sent: ESID=%d - size=%d - ts="LLD"\n", ESID, size, ts); 
				}
				prog->streams[i].output_ctrl(&prog->streams[i], GF_ESI_OUTPUT_DATA_DISPATCH, &pck);
				return;
			}
		i++;
		}
	} 
	return;
}

//static gf_seng_callback * SampleCallBack = &mySampleCallBack;


static volatile Bool run = 1;

#ifndef GPAC_DISABLE_SENG
static GF_ESIStream * set_broadcast_params(M2TSProgram *prog, u16 esid, u32 period, u32 ts_delta, u16 aggregate_on_stream, Bool adjust_carousel_time, Bool force_rap, Bool aggregate_au, Bool discard_pending, Bool signal_rap, Bool signal_critical, Bool version_inc)
{
	u32 i=0;
	GF_ESIStream *priv=NULL;
	GF_ESInterface *esi=NULL;

	/*locate our stream*/
	if (esid) {
		while (i<prog->nb_streams) {
			if (prog->streams[i].stream_id == esid){
				priv = (GF_ESIStream *)prog->streams[i].input_udta; 
				esi = &prog->streams[i]; 
				break;
			}
			else{
				i++;
			}
		}
		/*TODO: stream not found*/
	}

	/*TODO - set/reset the ESID for the parsers*/
	if (!priv) return NULL;

	/*TODO - if discard is set, abort current carousel*/
	if (discard_pending) {
	}

	/*remember RAP flag*/
	priv->rap = signal_rap; 
	priv->critical = signal_critical;
	priv->vers_inc = version_inc;

	priv->ts_delta = ts_delta;
	priv->adjust_carousel_time = adjust_carousel_time;

	/*change stream aggregation mode*/
	if ((aggregate_on_stream != (u16)-1) && (priv->aggregate_on_stream != aggregate_on_stream)) {
		gf_seng_enable_aggregation(prog->seng, esid, aggregate_on_stream);
		priv->aggregate_on_stream = aggregate_on_stream;
	}
	/*change stream aggregation mode*/
	if (priv->aggregate_on_stream==esi->stream_id) {
		if (priv->aggregate_on_stream && (period!=(u32)-1) && (esi->repeat_rate != period)) {
			esi->repeat_rate = period;
		}
	} else {
		esi->repeat_rate = 0;
	}
	return priv;
}
#endif

#ifndef GPAC_DISABLE_SENG

static Bool seng_output(void *param)
{
	GF_Err e;
	u64 last_src_modif, mod_time;
	M2TSProgram *prog = (M2TSProgram *)param;
	GF_SceneEngine *seng = prog->seng;
	GF_SimpleDataDescriptor *audio_desc;
	Bool update_context=0;
	Bool force_rap, adjust_carousel_time, discard_pending, signal_rap, signal_critical, version_inc, aggregate_au;
	u32 period, ts_delta;
	u16 es_id, aggregate_on_stream;
	e = GF_OK;
	gf_sleep(2000); /*TODO: events instead? What are we waiting for?*/
	gf_seng_encode_context(seng, SampleCallBack);
	
	last_src_modif = prog->src_name ? gf_file_modification_time(prog->src_name) : 0;

	/*send the audio descriptor*/
	if (prog->mpeg4_signaling==GF_M2TS_MPEG4_SIGNALING_FULL && audio_OD_stream_id!=(u32)-1) {
		audio_desc = prog->streams[audio_OD_stream_id].input_udta;
		if (audio_desc && audio_desc->data) /*RTP/UDP + MP3 case*/
		{
			assert(audio_OD_stream_id != (u32)-1);
			assert(!aac_reader); /*incompatible with AAC*/
			prog->repeat = 1;
			SampleCallBack(prog, AUDIO_OD_ESID, audio_desc->data, audio_desc->size, 0/*gf_m2ts_get_sys_clock(muxer)*/);
			prog->repeat = 0;
			gf_free(audio_desc->data);
			gf_free(audio_desc);
			prog->streams[audio_OD_stream_id].input_udta = NULL;
		}
	}

	while (run) {
		if (!gf_prompt_has_input()) {
			if (prog->src_name) {
				mod_time = gf_file_modification_time(prog->src_name);
				if (mod_time != last_src_modif) {
					FILE *srcf;
					char flag_buf[201], *flag;
					fprintf(stderr, "Update file modified - processing\n");
					last_src_modif = mod_time;

					srcf = gf_f64_open(prog->src_name, "rt");
					if (!srcf) continue;

					/*checks if we have a broadcast config*/
					if (!fgets(flag_buf, 200, srcf))
					  flag_buf[0] = '\0';
					fclose(srcf);

					aggregate_au = force_rap = adjust_carousel_time = discard_pending = signal_rap = signal_critical = 0;
					version_inc = 1;
					period = -1;
					aggregate_on_stream = -1;
					ts_delta = 0;
					es_id = 0;

					/*find our keyword*/
					flag = strstr(flag_buf, "gpac_broadcast_config ");
					if (flag) {
						flag += strlen("gpac_broadcast_config ");
						/*move to next word*/
						while (flag && (flag[0]==' ')) flag++;

						while (1) {
							char *sep = strchr(flag, ' ');
							if (sep) sep[0] = 0;
							if (!strnicmp(flag, "esid=", 5)) {
								/*ESID on which the update is applied*/
								es_id = atoi(flag+5);
							} else if (!strnicmp(flag, "period=", 7)) {
								/*TODO: target period carousel for ESID ??? (ESID/carousel)*/
								period = atoi(flag+7);
							} else if (!strnicmp(flag, "ts=", 3)) {
								/*TODO: */
								ts_delta = atoi(flag+3);
							} else if (!strnicmp(flag, "carousel=", 9)) {
								/*TODO: why? => sends the update on carousel id specified by this argument*/
								aggregate_on_stream = atoi(flag+9);
							} else if (!strnicmp(flag, "restamp=", 8)) {
								/*CTS is updated when carouselled*/
								adjust_carousel_time = atoi(flag+8);
							} else if (!strnicmp(flag, "discard=", 8)) {
								/*when we receive several updates during a single carousel period, this attribute specifies whether the current update discard pending ones*/
								discard_pending = atoi(flag+8);
							} else if (!strnicmp(flag, "aggregate=", 10)) {
								/*Boolean*/
								aggregate_au = atoi(flag+10);
							} else if (!strnicmp(flag, "force_rap=", 10)) {
								/*TODO: */
								force_rap = atoi(flag+10);
							} else if (!strnicmp(flag, "rap=", 4)) {
								/*TODO: */
								signal_rap = atoi(flag+4);
							} else if (!strnicmp(flag, "critical=", 9)) {
								/*TODO: */
								signal_critical = atoi(flag+9);
							} else if (!strnicmp(flag, "vers_inc=", 9)) {
								/*Boolean to increment m2ts section version number*/
								version_inc = atoi(flag+9);
							}
							if (sep) {
								sep[0] = ' ';
								flag = sep+1;
							} else {
								break;
							}
						}

						set_broadcast_params(prog, es_id, period, ts_delta, aggregate_on_stream, adjust_carousel_time, force_rap, aggregate_au, discard_pending, signal_rap, signal_critical, version_inc);
					}

					e = gf_seng_encode_from_file(seng, es_id, aggregate_au ? 0 : 1, prog->src_name, SampleCallBack);
					if (e){
						fprintf(stderr, "Processing command failed: %s\n", gf_error_to_string(e));
					} else
						gf_seng_aggregate_context(seng, 0);

					update_context=1;

					

				}
			}
			if (update_context) {
				prog->repeat = 1;
				e = gf_seng_encode_context(seng, SampleCallBack);
				prog->repeat = 0;
				update_context = 0;
			}

			gf_sleep(10);
		} else { /*gf_prompt_has_input()*/
			char c = gf_prompt_get_char();
			switch (c) {
				case 'u':
				{
					GF_Err e;
					char szCom[8192];
					fprintf(stderr, "Enter command to send:\n");
					fflush(stdin);
					szCom[0] = 0;
					if (1 > scanf("%[^\t\n]", szCom)){
					    fprintf(stderr, "No command has been properly entered, aborting.\n");
					    break;
					}
					e = gf_seng_encode_from_string(seng, 0, 0, szCom, SampleCallBack);
					if (e) { 
						fprintf(stderr, "Processing command failed: %s\n", gf_error_to_string(e));
					}
					update_context=1;
				}
					break;
				case 'p':
				{
					char rad[GF_MAX_PATH];
					fprintf(stderr, "Enter output file name - \"std\" for stdout: ");
					if (1 > scanf("%s", rad)){
					    fprintf(stderr, "No outfile name has been entered, aborting.\n");
					    break;
					}
					e = gf_seng_save_context(seng, !strcmp(rad, "std") ? NULL : rad);
					fprintf(stderr, "Dump done (%s)\n", gf_error_to_string(e)); 
				}
					break;
				case 'q':
				{
					run = 0;
				}
			}
			e = GF_OK;
		}
	}
	
	
	return e ? 1 : 0;
}
#endif


#ifndef GPAC_DISABLE_STREAMING
static void rtp_sl_packet_cbk(void *udta, char *payload, u32 size, GF_SLHeader *hdr, GF_Err e)
{
	GF_ESIRTP *rtp = (GF_ESIRTP*)udta;
	rtp->pck.data = payload;
	rtp->pck.data_len = size;
	rtp->pck.dts = hdr->decodingTimeStamp;
	rtp->pck.cts = hdr->compositionTimeStamp;
	rtp->pck.flags = 0;
	if (hdr->compositionTimeStampFlag) rtp->pck.flags |= GF_ESI_DATA_HAS_CTS;
	if (hdr->decodingTimeStampFlag) rtp->pck.flags |= GF_ESI_DATA_HAS_DTS;
	if (hdr->randomAccessPointFlag) rtp->pck.flags |= GF_ESI_DATA_AU_RAP;
	if (hdr->accessUnitStartFlag) rtp->pck.flags |= GF_ESI_DATA_AU_START;
	if (hdr->accessUnitEndFlag) rtp->pck.flags |= GF_ESI_DATA_AU_END;

	if (rtp->use_carousel) {
		if ((hdr->AU_sequenceNumber==rtp->au_sn) && hdr->randomAccessPointFlag) rtp->pck.flags |= GF_ESI_DATA_REPEAT;
		rtp->au_sn = hdr->AU_sequenceNumber;
	}

	if (rtp->cat_dsi && hdr->randomAccessPointFlag && hdr->accessUnitStartFlag) {
		if (rtp->dsi_and_rap) gf_free(rtp->dsi_and_rap);
		rtp->pck.data_len = size + rtp->depacketizer->sl_map.configSize;
		rtp->dsi_and_rap = gf_malloc(sizeof(char)*(rtp->pck.data_len));
		memcpy(rtp->dsi_and_rap, rtp->depacketizer->sl_map.config, rtp->depacketizer->sl_map.configSize);
		memcpy((char *) rtp->dsi_and_rap + rtp->depacketizer->sl_map.configSize, payload, size);
		rtp->pck.data = rtp->dsi_and_rap;
	}


	rtp->ifce->output_ctrl(rtp->ifce, GF_ESI_OUTPUT_DATA_DISPATCH, &rtp->pck);
}

static void fill_rtp_es_ifce(GF_ESInterface *ifce, GF_SDPMedia *media, GF_SDPInfo *sdp)
{
	u32 i;
	GF_Err e;
	GF_X_Attribute*att;
	GF_ESIRTP *rtp;
	GF_RTPMap*map;
	GF_SDPConnection *conn;
	GF_RTSPTransport trans;

	/*check connection*/
	conn = sdp->c_connection;
	if (!conn) conn = (GF_SDPConnection*)gf_list_get(media->Connections, 0);

	/*check payload type*/
	map = (GF_RTPMap*)gf_list_get(media->RTPMaps, 0);
	GF_SAFEALLOC(rtp, GF_ESIRTP);

	memset(ifce, 0, sizeof(GF_ESInterface));
	rtp->rtp_ch = gf_rtp_new();
	i=0;
	while ((att = (GF_X_Attribute*)gf_list_enum(media->Attributes, &i))) {
		if (!stricmp(att->Name, "mpeg4-esid") && att->Value) ifce->stream_id = atoi(att->Value);
	}

	memset(&trans, 0, sizeof(GF_RTSPTransport));
	trans.Profile = media->Profile;
	trans.source = conn ? conn->host : sdp->o_address;
	trans.IsUnicast = gf_sk_is_multicast_address(trans.source) ? 0 : 1;
	if (!trans.IsUnicast) {
		trans.port_first = media->PortNumber;
		trans.port_last = media->PortNumber + 1;
		trans.TTL = conn ? conn->TTL : 0;
	} else {
		trans.client_port_first = media->PortNumber;
		trans.client_port_last = media->PortNumber + 1;
	}

	if (gf_rtp_setup_transport(rtp->rtp_ch, &trans, NULL) != GF_OK) {
		gf_rtp_del(rtp->rtp_ch);
		fprintf(stderr, "Cannot initialize RTP transport\n");
		return;
	}
	/*setup depacketizer*/
	rtp->depacketizer = gf_rtp_depacketizer_new(media, rtp_sl_packet_cbk, rtp);
	if (!rtp->depacketizer) {
		gf_rtp_del(rtp->rtp_ch);
		fprintf(stderr, "Cannot create RTP depacketizer\n");
		return;
	}
	/*setup channel*/
	gf_rtp_setup_payload(rtp->rtp_ch, map);
	ifce->input_udta = rtp;
	ifce->input_ctrl = rtp_input_ctrl;
	rtp->ifce = ifce;

	ifce->object_type_indication = rtp->depacketizer->sl_map.ObjectTypeIndication;
	ifce->stream_type = rtp->depacketizer->sl_map.StreamType;
	ifce->timescale = gf_rtp_get_clockrate(rtp->rtp_ch);
	if (rtp->depacketizer->sl_map.config) {
		switch (ifce->object_type_indication) {
		case GPAC_OTI_VIDEO_MPEG4_PART2:
			rtp->cat_dsi = 1;
			break;
		}
	}
	if (rtp->depacketizer->sl_map.StreamStateIndication) {
		rtp->use_carousel = 1;
		rtp->au_sn=0;
	}

	/*DTS signaling is only supported for MPEG-4 visual*/
	if (rtp->depacketizer->sl_map.DTSDeltaLength) ifce->caps |= GF_ESI_SIGNAL_DTS;

	gf_rtp_depacketizer_reset(rtp->depacketizer, 1);
	e = gf_rtp_initialize(rtp->rtp_ch, 0x100000ul, 0, 0, 10, 200, NULL);
	if (e!=GF_OK) {
		gf_rtp_del(rtp->rtp_ch);
		fprintf(stderr, "Cannot initialize RTP channel: %s\n", gf_error_to_string(e));
		return;
	}
	fprintf(stderr, "RTP interface initialized\n");
}
#endif /*GPAC_DISABLE_STREAMING*/

#ifndef GPAC_DISABLE_SENG
void fill_seng_es_ifce(GF_ESInterface *ifce, u32 i, GF_SceneEngine *seng, u32 period)
{
	GF_Err e = GF_OK;
	u32 len;
	GF_ESIStream *stream;
	char *config_buffer = NULL;

	memset(ifce, 0, sizeof(GF_ESInterface));
	e = gf_seng_get_stream_config(seng, i, (u16*) &(ifce->stream_id), &config_buffer, &len, (u32*) &(ifce->stream_type), (u32*) &(ifce->object_type_indication), &(ifce->timescale)); 
	if (e) {
		fprintf(stderr, "Cannot set the stream config for stream %d to %d: %s\n", ifce->stream_id, period, gf_error_to_string(e));
	}

	ifce->repeat_rate = period;
	GF_SAFEALLOC(stream, GF_ESIStream);
	memset(stream, 0, sizeof(GF_ESIStream));
	stream->rap = 1;
	if (ifce->input_udta)
	  gf_free(ifce->input_udta);
	ifce->input_udta = stream;
	
	//fprintf(stderr, "Caroussel period: %d\n", period);
//	e = gf_seng_set_carousel_time(seng, ifce->stream_id, period);
	if (e) {
		fprintf(stderr, "Cannot set carousel time on stream %d to %d: %s\n", ifce->stream_id, period, gf_error_to_string(e));
	}
	ifce->input_ctrl = seng_input_ctrl;

}
#endif

static Bool open_program(M2TSProgram *prog, char *src, u32 carousel_rate, u32 mpeg4_signaling, char *update, char *audio_input_ip, u16 audio_input_port, char *video_buffer, Bool force_real_time, u32 bifs_use_pes)
{
#ifndef GPAC_DISABLE_STREAMING
	GF_SDPInfo *sdp;
#endif
	u32 i;
	
	memset(prog, 0, sizeof(M2TSProgram));
	prog->mpeg4_signaling = mpeg4_signaling;

	/*open ISO file*/
	if (gf_isom_probe_file(src)) {
		u32 nb_tracks;
		Bool has_bifs_od = 0;
		u32 first_audio = 0;
		u32 first_other = 0;
		prog->mp4 = gf_isom_open(src, GF_ISOM_OPEN_READ, 0);
		prog->nb_streams = 0;
		prog->real_time = force_real_time;
		/*on MPEG-2 TS, carry 3GPP timed text as MPEG-4 Part17*/
		gf_isom_text_set_streaming_mode(prog->mp4, 1);
		nb_tracks = gf_isom_get_track_count(prog->mp4); 

		for (i=0; i<nb_tracks; i++) {
			Bool check_deps = 0;
			if (gf_isom_get_media_type(prog->mp4, i+1) == GF_ISOM_MEDIA_HINT) 
				continue; 

			fill_isom_es_ifce(prog, &prog->streams[i], prog->mp4, i+1, bifs_use_pes);

			switch(prog->streams[i].stream_type) {
			case GF_STREAM_OD:
				has_bifs_od = 1;
				prog->streams[i].repeat_rate = carousel_rate;
				break;
			case GF_STREAM_SCENE:
				has_bifs_od = 1;
				prog->streams[i].repeat_rate = carousel_rate;
				break;
			case GF_STREAM_VISUAL:
				/*turn on image repeat*/
				switch (prog->streams[i].object_type_indication) {
				case GPAC_OTI_IMAGE_JPEG:
				case GPAC_OTI_IMAGE_PNG:
					((GF_ESIMP4 *)prog->streams[i].input_udta)->image_repeat_ms = carousel_rate;
					break;
				default:
					check_deps = 1;
					if (gf_isom_get_sample_count(prog->mp4, i+1)>1) {
						/*get first visual stream as PCR*/
						if (!prog->pcr_idx) prog->pcr_idx = i+1;
					}
					break;
				}
				break;
			case GF_STREAM_AUDIO:
				if (!first_audio) first_audio = i+1;
				check_deps = 1;
				break;
			default:
				/*log not supported stream type: %s*/
				break;
			}
			prog->nb_streams++;
			if (gf_isom_get_sample_count(prog->mp4, i+1)>1) first_other = i+1;

			if (check_deps) {
				u32 k;
				Bool found_dep = 0;
				for (k=0; k<nb_tracks; k++) {
					if (gf_isom_get_media_type(prog->mp4, k+1) != GF_ISOM_MEDIA_OD) 
						continue; 

					/*this stream is not refered to by any OD, send as regular PES*/
					if (gf_isom_has_track_reference(prog->mp4, k+1, GF_ISOM_REF_OD, gf_isom_get_track_id(prog->mp4, i+1) )==1) {
						found_dep = 1;
						break;
					}
				}
				if (!found_dep) {
					prog->streams[i].caps |= GF_ESI_STREAM_WITHOUT_MPEG4_SYSTEMS;
				}
			}
		}
		if (has_bifs_od && !prog->mpeg4_signaling) prog->mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_FULL;

		/*if no visual PCR found, use first audio*/
		if (!prog->pcr_idx) prog->pcr_idx = first_audio;
		if (!prog->pcr_idx) prog->pcr_idx = first_other;
		if (prog->pcr_idx) {
			GF_ESIMP4 *priv;
			prog->pcr_idx-=1;
			priv = prog->streams[prog->pcr_idx].input_udta;
			gf_isom_set_default_sync_track(prog->mp4, priv->track);
		}

		prog->iod = gf_isom_get_root_od(prog->mp4);
		if (prog->iod) {
			GF_ObjectDescriptor*iod = (GF_ObjectDescriptor*)prog->iod;
			if (gf_list_count( ((GF_ObjectDescriptor*)prog->iod)->ESDescriptors) == 0) {
				gf_odf_desc_del(prog->iod);
				prog->iod = NULL;
			} else {
				fprintf(stderr, "IOD found for program %s\n", src);

				/*if using 4over2, get rid of OD tracks*/
				if (prog->mpeg4_signaling==GF_M2TS_MPEG4_SIGNALING_SCENE) {
					for (i=0; i<gf_list_count(iod->ESDescriptors); i++) {
						u32 track_num, k;
						GF_M2TSDescriptor *oddesc;
						GF_ISOSample *sample;
						GF_ESD *esd = gf_list_get(iod->ESDescriptors, i);
						if (esd->decoderConfig->streamType!=GF_STREAM_OD) continue;
						track_num = gf_isom_get_track_by_id(prog->mp4, esd->ESID);
						if (gf_isom_get_sample_count(prog->mp4, track_num)>1) continue;

						sample = gf_isom_get_sample(prog->mp4, track_num, 1, NULL);
						if (sample->dataLength >= 255-2) {
							gf_isom_sample_del(&sample);
							continue;
						}
						/*rewrite ESD dependencies*/
						for (k=0; k<gf_list_count(iod->ESDescriptors); k++) {
							GF_ESD *dep_esd = gf_list_get(iod->ESDescriptors, k);
							if (dep_esd->dependsOnESID==esd->ESID) dep_esd->dependsOnESID = esd->dependsOnESID;
						}

						for (k=0; k<prog->nb_streams; k++) {
							if (prog->streams[k].stream_id==esd->ESID) {
								prog->streams[k].stream_type = 0;
								break;
							}
						}

						if (!prog->od_updates) prog->od_updates = gf_list_new();
						GF_SAFEALLOC(oddesc, GF_M2TSDescriptor); 
						oddesc->data_len = sample->dataLength;
						oddesc->data = sample->data;
						oddesc->tag = GF_M2TS_MPEG4_ODUPDATE_DESCRIPTOR;
						sample->data = NULL;
						gf_isom_sample_del(&sample);
						gf_list_add(prog->od_updates, oddesc);

						gf_list_rem(iod->ESDescriptors, i);
						i--;
						gf_odf_desc_del((GF_Descriptor *) esd);
						prog->samples_count--;
					}
				}

			}
		}
		return 1;
	}

#ifndef GPAC_DISABLE_STREAMING
	/*open SDP file*/
	if (strstr(src, ".sdp")) {
		GF_X_Attribute *att;
		char *sdp_buf;
		u32 sdp_size;
		GF_Err e;
		FILE *_sdp = fopen(src, "rt");
		if (!_sdp) {
			fprintf(stderr, "Error opening %s - no such file\n", src);
			return 0;
		}
		gf_f64_seek(_sdp, 0, SEEK_END);
		sdp_size = (u32)gf_f64_tell(_sdp);
		gf_f64_seek(_sdp, 0, SEEK_SET);
		sdp_buf = (char*)gf_malloc(sizeof(char)*sdp_size);
		memset(sdp_buf, 0, sizeof(char)*sdp_size);
		sdp_size = fread(sdp_buf, 1, sdp_size, _sdp);
		fclose(_sdp);

		sdp = gf_sdp_info_new();
		e = gf_sdp_info_parse(sdp, sdp_buf, sdp_size);
		gf_free(sdp_buf);
		if (e) {
			fprintf(stderr, "Error opening %s : %s\n", src, gf_error_to_string(e));
			gf_sdp_info_del(sdp);
			return 0;
		}

		i=0;
		while ((att = (GF_X_Attribute*)gf_list_enum(sdp->Attributes, &i))) {
			char buf[2000];
			u32 size;
			char *buf64;
			u32 size64;
			char *iod_str;
			if (strcmp(att->Name, "mpeg4-iod") ) continue;
			iod_str = att->Value + 1;
			if (strnicmp(iod_str, "data:application/mpeg4-iod;base64", strlen("data:application/mpeg4-iod;base64"))) continue;

			buf64 = strstr(iod_str, ",");
			if (!buf64) break;
			buf64 += 1;
			size64 = strlen(buf64) - 1;
			size = gf_base64_decode(buf64, size64, buf, 2000);

			gf_odf_desc_read(buf, size, &prog->iod);
			break;
		}

		prog->nb_streams = gf_list_count(sdp->media_desc);
		for (i=0; i<prog->nb_streams; i++) {
			GF_SDPMedia *media = gf_list_get(sdp->media_desc, i);
			fill_rtp_es_ifce(&prog->streams[i], media, sdp);
			switch(prog->streams[i].stream_type) {
			case GF_STREAM_OD:
			case GF_STREAM_SCENE:
				prog->mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_FULL;
				prog->streams[i].repeat_rate = carousel_rate;
				break;
			}
			if (!prog->pcr_idx && (prog->streams[i].stream_type == GF_STREAM_VISUAL)) {
				prog->pcr_idx = i+1;
			}
		}

		if (prog->pcr_idx) prog->pcr_idx-=1;
		gf_sdp_info_del(sdp);

		return 2;
	} else 
#endif /*GPAC_DISABLE_STREAMING*/

#ifndef GPAC_DISABLE_SENG
	if (strstr(src, ".bt")) //open .bt file
	{
		u32 load_type=0;
		prog->seng = gf_seng_init(prog, src, load_type, NULL, (load_type == GF_SM_LOAD_DIMS) ? 1 : 0);
		if (!prog->seng) {
			fprintf(stderr, "Cannot create scene engine\n");
			exit(1);
		}
		else{
			fprintf(stderr, "Scene engine created.\n");
		}
		assert( prog );
		assert( prog->seng);
		prog->iod = gf_seng_get_iod(prog->seng);
		if (! prog->iod){
		    fprintf(stderr, __FILE__": No IOD\n");
		}

		prog->nb_streams = gf_seng_get_stream_count(prog->seng);
		prog->rate = carousel_rate;
		prog->mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_FULL;

		for (i=0; i<prog->nb_streams; i++) {
			fill_seng_es_ifce(&prog->streams[i], i, prog->seng, prog->rate);
			//fprintf(stderr, "Fill interface\n");
			if (!prog->pcr_idx && (prog->streams[i].stream_type == GF_STREAM_AUDIO)) {
				prog->pcr_idx = i+1;
			}
		}

		/*when an audio input is present, declare it and store OD + ESD_U*/
		if (audio_input_ip) {
			/*add the audio program*/
			prog->pcr_idx = prog->nb_streams;
			prog->streams[prog->nb_streams].stream_type = GF_STREAM_AUDIO;
			/*hack: http urls are not decomposed therefore audio_input_port remains null*/
			if (audio_input_port) {	/*UDP/RTP*/
				prog->streams[prog->nb_streams].object_type_indication = GPAC_OTI_AUDIO_MPEG1;
			} else { /*HTTP*/
				aac_reader->oti = prog->streams[prog->nb_streams].object_type_indication = GPAC_OTI_AUDIO_AAC_MPEG4;
			}
			prog->streams[prog->nb_streams].input_ctrl = void_input_ctrl;
			prog->streams[prog->nb_streams].stream_id = AUDIO_DATA_ESID;
			prog->streams[prog->nb_streams].timescale = 1000;

			GF_SAFEALLOC(prog->streams[prog->nb_streams].input_udta, GF_ESIStream);
			((GF_ESIStream*)prog->streams[prog->nb_streams].input_udta)->vers_inc = 1;	/*increment version number at every audio update*/
			assert( prog );
			//assert( prog->iod);
			if (prog->iod && ((prog->iod->tag!=GF_ODF_IOD_TAG) || (mpeg4_signaling != GF_M2TS_MPEG4_SIGNALING_SCENE))) {
				/*create the descriptor*/
				GF_ESD *esd;
				GF_SimpleDataDescriptor *audio_desc;
				GF_SAFEALLOC(audio_desc, GF_SimpleDataDescriptor);
				if (audio_input_port) {	/*UDP/RTP*/
					esd = gf_odf_desc_esd_new(0);
					esd->decoderConfig->streamType = prog->streams[prog->nb_streams].stream_type;
					esd->decoderConfig->objectTypeIndication = prog->streams[prog->nb_streams].object_type_indication;
				} else {				/*HTTP*/
					esd = AAC_GetESD(aac_reader);		/*in case of AAC, we have to wait the first ADTS chunk*/
				}
				assert( esd );
				esd->ESID = prog->streams[prog->nb_streams].stream_id;
				if (esd->slConfig->timestampResolution) /*in case of AAC, we have to wait the first ADTS chunk*/
					encode_audio_desc(esd, audio_desc);
				else
					gf_odf_desc_del((GF_Descriptor *)esd);

				/*find the audio OD stream and attach its descriptor*/
				for (i=0; i<prog->nb_streams; i++) {
					if (prog->streams[i].stream_id == AUDIO_OD_ESID) {
						if (prog->streams[i].input_udta)
						  gf_free(prog->streams[i].input_udta);
						prog->streams[i].input_udta = (void*)audio_desc;	/*Hack: the real input_udta type (for our SampleCallBack function) is GF_ESIStream*/
						audio_OD_stream_id = i;
						break;
					}
				}
				if (audio_OD_stream_id == (u32)-1) {
					fprintf(stderr, "Error: could not find an audio OD stream with ESID=100 in '%s'\n", src);
					return 0;
				}
			} else {
				prog->mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_SCENE;
			}
			prog->nb_streams++;
		}

		/*when an audio input is present, declare it and store OD + ESD_U*/
		if (video_buffer) {
			/*add the video program*/
			prog->streams[prog->nb_streams].stream_type = GF_STREAM_VISUAL;
			prog->streams[prog->nb_streams].object_type_indication = GPAC_OTI_VIDEO_AVC;
			prog->streams[prog->nb_streams].input_ctrl = void_input_ctrl;
			prog->streams[prog->nb_streams].stream_id = VIDEO_DATA_ESID;
			prog->streams[prog->nb_streams].timescale = 1000;

			GF_SAFEALLOC(prog->streams[prog->nb_streams].input_udta, GF_ESIStream);
			((GF_ESIStream*)prog->streams[prog->nb_streams].input_udta)->vers_inc = 1;	/*increment version number at every video update*/
			assert(prog);

			if (prog->iod && ((prog->iod->tag!=GF_ODF_IOD_TAG) || (mpeg4_signaling != GF_M2TS_MPEG4_SIGNALING_SCENE))) {
				assert(0); /*TODO*/
#if 0
				/*create the descriptor*/
				GF_ESD *esd;
				GF_SimpleDataDescriptor *video_desc;
				GF_SAFEALLOC(video_desc, GF_SimpleDataDescriptor);
				esd = gf_odf_desc_esd_new(0);
				esd->decoderConfig->streamType = prog->streams[prog->nb_streams].stream_type;
				esd->decoderConfig->objectTypeIndication = prog->streams[prog->nb_streams].object_type_indication;
				esd->ESID = prog->streams[prog->nb_streams].stream_id;

				/*find the audio OD stream and attach its descriptor*/
				for (i=0; i<prog->nb_streams; i++) {
					if (prog->streams[i].stream_id == 103/*TODO: VIDEO_OD_ESID*/) {
						if (prog->streams[i].input_udta)
						  gf_free(prog->streams[i].input_udta);
						prog->streams[i].input_udta = (void*)video_desc;
						audio_OD_stream_id = i;
						break;
					}
				}
				if (audio_OD_stream_id == (u32)-1) {
					fprintf(stderr, "Error: could not find an audio OD stream with ESID=100 in '%s'\n", src);
					return 0;
				}
#endif
			} else {
				assert (prog->mpeg4_signaling == GF_M2TS_MPEG4_SIGNALING_SCENE);
			}

			prog->nb_streams++;
		}

		if (!prog->pcr_idx) prog->pcr_idx=1;
		prog->th = gf_th_new("Carousel");
		prog->src_name = update;
		gf_th_run(prog->th, seng_output, prog);
		return 1;
	} else
#endif
	{
		FILE *f = fopen(src, "rt");
		if (f) {
			fclose(f);
			fprintf(stderr, "Error opening %s - not a supported input media, skipping.\n", src);
		} else {
			fprintf(stderr, "Error opening %s - no such file.\n", src);
		}
		return 0;
	}
}

/*parse MP42TS arguments*/
static GFINLINE GF_Err parse_args(int argc, char **argv, u32 *mux_rate, u32 *carrousel_rate, u64 *pcr_init_val, u32 *pcr_offset, u32 *psi_refresh_rate, Bool *single_au_pes, u32 *bifs_use_pes, 
								  M2TSProgram *progs, u32 *nb_progs, char **src_name, 
								  Bool *real_time, u32 *run_time, char **video_buffer, u32 *video_buffer_size,
								  u32 *audio_input_type, char **audio_input_ip, u16 *audio_input_port,
								  u32 *output_type, char **ts_out, char **udp_out, char **rtp_out, u16 *output_port, 
								  char** segment_dir, u32 *segment_duration, char **segment_manifest, u32 *segment_number, char **segment_http_prefix, Bool *split_rap)
{
	Bool rate_found=0, mpeg4_carousel_found=0, time_found=0, src_found=0, dst_found=0, audio_input_found=0, video_input_found=0, 
		 seg_dur_found=0, seg_dir_found=0, seg_manifest_found=0, seg_number_found=0, seg_http_found = 0, real_time_found=0;
	char *prog_name, *arg = NULL, *error_msg = "no argument found";
	u32 mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_NONE; 
	Bool force_real_time = 0;
	s32 i;
	
	/*first pass: find audio - NO GPAC INIT MODIFICATION MUST OCCUR IN THIS PASS*/
	for (i=1; i<argc; i++) {
		arg = argv[i];
		if (!stricmp(arg, "-h") || strstr(arg, "-help")) {
			usage(argv[0]);
			return GF_EOS;
		}
		else if (!strnicmp(arg, "-pcr-init=", 10)) {
			sscanf(arg, "-pcr-init="LLD, pcr_init_val);
		}
		else if (!strnicmp(arg, "-pcr-offset=", 12)) {
			*pcr_offset = atoi(arg+12);
		}
		else if (!strnicmp(arg, "-video=", 7)) {
			FILE *f;
			if (video_input_found) {
				error_msg = "multiple '-video' found";
				arg = NULL;
				goto error;
			}
			video_input_found = 1;
			arg+=7;
			f = fopen(arg, "rb");
			if (!f) {
				error_msg = "video file not found: ";
				goto error;
			}
			gf_f64_seek(f, 0, SEEK_END);
			*video_buffer_size = (u32)gf_f64_tell(f);
			gf_f64_seek(f, 0, SEEK_SET);
			assert(*video_buffer_size);
			*video_buffer = (char*) gf_malloc(*video_buffer_size);
			{
				s32 readen = fread(*video_buffer, sizeof(char), *video_buffer_size, f);
				if (readen != *video_buffer_size)
					fprintf(stderr, "Error while reading video file, has readen %u chars instead of %u.\n", readen, *video_buffer_size);
			}
			fclose(f);
		} else if (!strnicmp(arg, "-audio=", 7)) {
			if (audio_input_found) {
				error_msg = "multiple '-audio' found";
				arg = NULL;
				goto error;
			}
			audio_input_found = 1;
			arg+=7;
			if (!strnicmp(arg, "udp://", 6) || !strnicmp(arg, "rtp://", 6) || !strnicmp(arg, "http://", 7)) {
				char *sep;
				/*set audio input type*/
				if (!strnicmp(arg, "udp://", 6))
					*audio_input_type = GF_MP42TS_UDP;
				else if (!strnicmp(arg, "rtp://", 6))
					*audio_input_type = GF_MP42TS_RTP;
#ifndef GPAC_DISABLE_PLAYER
				else if (!strnicmp(arg, "http://", 7))
					*audio_input_type = GF_MP42TS_HTTP;
#endif
				/*http needs to get the complete URL*/
				switch(*audio_input_type) {
					case GF_MP42TS_UDP:
					case GF_MP42TS_RTP:
						sep = strchr(arg+6, ':');
						*real_time=1;
						if (sep) {
							*audio_input_port = atoi(sep+1);
							sep[0]=0;
							*audio_input_ip = gf_strdup(arg+6);
							sep[0]=':';
						} else {
							*audio_input_ip = gf_strdup(arg+6);
						}
						break;
#ifndef GPAC_DISABLE_PLAYER
					case GF_MP42TS_HTTP:
						/* No need to dup since it may come from argv */
						*audio_input_ip = arg;
						assert(audio_input_port != 0);
						break;
#endif
					default:
						assert(0);
				}
			}
		} else if (!strnicmp(arg, "-psi-rate=", 10) ) {
			*psi_refresh_rate = atoi(arg+10);
		} else if (!stricmp(arg, "-bifs-pes") ) {
			*bifs_use_pes = 1;
		} else if (!stricmp(arg, "-bifs-pes-ex") ) {
			*bifs_use_pes = 2;
		} else if (!stricmp(arg, "-mpeg4") || !stricmp(arg, "-4on2")) {
			mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_FULL;
		} else if (!stricmp(arg, "-4over2")) {
			mpeg4_signaling = GF_M2TS_MPEG4_SIGNALING_SCENE;
		} else if (!strcmp(arg, "-mem-track")) {
#ifdef GPAC_MEMORY_TRACKING
			gf_sys_close();
			gf_sys_init(1);
			gf_log_set_tool_level(GF_LOG_MEMORY, GF_LOG_DEBUG);
#else
			fprintf(stdout, "WARNING - GPAC not compiled with Memory Tracker - ignoring \"-mem-track\"\n"); 
#endif
		} else if (!strnicmp(arg, "-rate=", 6)) {
			if (rate_found) {
				error_msg = "multiple '-rate' found";
				arg = NULL;
				goto error;
			}
			rate_found = 1;
			*mux_rate = 1024*atoi(arg+6);
		} else if (!strnicmp(arg, "-mpeg4-carousel=", 16)) {
			if (mpeg4_carousel_found) {
				error_msg = "multiple '-mpeg4-carousel' found";
				arg = NULL;
				goto error;
			}
			mpeg4_carousel_found = 1;
			*carrousel_rate = atoi(arg+16);
		} else if (!strnicmp(arg, "-real-time", 10)) {
			if (real_time_found) {
				goto error;
			}
			real_time_found = 1;
			*real_time = 1;
		} else if (!strnicmp(arg, "-time=", 6)) {
			if (time_found) {
				error_msg = "multiple '-time' found";
				arg = NULL;
				goto error;
			}
			time_found = 1;
			*run_time = atoi(arg+6);
		} else if (!stricmp(arg, "-single-au")) {
			*single_au_pes = 1;
		} else if (!stricmp(arg, "-rap")) {
			*split_rap = 1;
		}
	}
	if (*real_time) force_real_time = 1;
	rate_found = 1;

	/*second pass: other*/
	for (i=1; i<argc; i++) {		
		arg = argv[i];		
		if (arg[0]=='-') {
			if (!strnicmp(arg, "-logs=", 6)) {
				if (gf_log_set_tools_levels(argv[i+1]+6) != GF_OK)
					return GF_BAD_PARAM;
			} else if (!strnicmp(arg, "-prog=", 6)) {
				u32 res;
				prog_name = arg+6;
				res = open_program(&progs[*nb_progs], prog_name, *carrousel_rate, mpeg4_signaling, *src_name, *audio_input_ip, *audio_input_port, *video_buffer, force_real_time, *bifs_use_pes);
				if (res) {
					(*nb_progs)++;
					if (res==2) *real_time=1;
				}
			} else if (!strnicmp(arg, "-segment-dir=", 13)) {
				if (seg_dir_found) {
					goto error;
				}
				seg_dir_found = 1;
				*segment_dir = arg+13;
				/* TODO: add the path separation char, if missing */
			} else if (!strnicmp(arg, "-segment-duration=", 18)) {
				if (seg_dur_found) {
					goto error;
				}
				seg_dur_found = 1;
				*segment_duration = atoi(arg+18);
			} else if (!strnicmp(arg, "-segment-manifest=", 18)) {
				if (seg_manifest_found) {
					goto error;
				}
				seg_manifest_found = 1;
				*segment_manifest = arg+18;
			} else if (!strnicmp(arg, "-segment-http-prefix=", 21)) {
				if (seg_http_found) {
					goto error;
				}
				seg_http_found = 1;
				*segment_http_prefix = arg+21;
			} else if (!strnicmp(arg, "-segment-number=", 16)) {
				if (seg_number_found) {
					goto error;
				}
				seg_number_found = 1;
				*segment_number = atoi(arg+16);
			} 
			else if (!strnicmp(arg, "-src=", 5)) {
				if (src_found) {
					error_msg = "multiple '-src' found";
					arg = NULL;
					goto error;
				}
				src_found = 1;
				*src_name = arg+5;
			}
			else if (!strnicmp(arg, "-dst-file=", 10)) {
				dst_found = 1;
				*ts_out = gf_strdup(arg+10);
			}
			else if (!strnicmp(arg, "-dst-udp=", 9)) {
				char *sep = strchr(arg+9, ':');
				dst_found = 1;
				*real_time=1;
				if (sep) {
					*output_port = atoi(sep+1);
					sep[0]=0;
					*udp_out = gf_strdup(arg+9);
					sep[0]=':';
				} else {
					*udp_out = gf_strdup(arg+9);
				}
			}
			else if (!strnicmp(arg, "-dst-rtp=", 9)) {
				char *sep = strchr(arg+9, ':');
				dst_found = 1;
				*real_time=1;
				if (sep) {
					*output_port = atoi(sep+1);
					sep[0]=0;
					*rtp_out = gf_strdup(arg+9);
					sep[0]=':';
				} else {
					*rtp_out = gf_strdup(arg+9);
				}
			}
			else if (!strnicmp(arg, "-audio=", 7) || !strnicmp(arg, "-video=", 7) || !strnicmp(arg, "-mpeg4", 6))
				; /*already treated on the first pass*/
			else {
//				error_msg = "unknown option \"%s\"";
//				goto error;
			}
		} 
#if 0
		else { /*"dst" argument (output)*/
			if (dst_found) {
				error_msg = "multiple output arguments (no '-') found";
				arg = NULL;
				goto error;
			}
			dst_found = 1;
			if (!strnicmp(arg, "rtp://", 6) || !strnicmp(arg, "udp://", 6)) {
				char *sep = strchr(arg+6, ':');
				*output_type = (arg[0]=='r') ? 2 : 1;
				*real_time=1;
				if (sep) {
					*output_port = atoi(sep+1);
					sep[0]=0;
					*ts_out = gf_strdup(arg+6);
					sep[0]=':';
				} else {
					*ts_out = gf_strdup(arg+6);
				}
			} else {
				*output_type = 0;
				*ts_out = gf_strdup(arg);
			}
		}
#endif
	}
	/*syntax is correct; now testing the presence of mandatory arguments*/
	if (dst_found && *nb_progs && rate_found) {
		return GF_OK;
	} else {
		if (!dst_found)
			fprintf(stderr, "Error: Destination argument not found\n\n");
		if (! *nb_progs)
			fprintf(stderr, "Error: No Programs are available\n\n");
		if (!rate_found)
			fprintf(stderr, "Error: Rate argument not found\n\n");
		return GF_BAD_PARAM;
	}

error:	
	if (!arg) {
		fprintf(stderr, "Error: %s\n\n", error_msg);
	} else {
		fprintf(stderr, "Error: %s \"%s\"\n\n", error_msg, arg);
	}
	return GF_BAD_PARAM;
}

/* adapted from http://svn.assembla.com/svn/legend/segmenter/segmenter.c */
static GF_Err write_manifest(char *manifest, char *segment_dir, u32 segment_duration, char *segment_prefix, char *http_prefix, 
							u32 first_segment, u32 last_segment, Bool end) {
	FILE *manifest_fp;
	u32 i;
	char manifest_tmp_name[GF_MAX_PATH];
	char manifest_name[GF_MAX_PATH];
	char *tmp_manifest = manifest_tmp_name;

	if (segment_dir) {
		sprintf(manifest_tmp_name, "%stmp.m3u8", segment_dir);
		sprintf(manifest_name, "%s%s", segment_dir, manifest);
	} else {
		sprintf(manifest_tmp_name, "tmp.m3u8");
		sprintf(manifest_name, "%s", manifest);
	}

	manifest_fp = fopen(tmp_manifest, "w");
	if (!manifest_fp) {
		fprintf(stderr, "Could not create m3u8 manifest file (%s)\n", tmp_manifest);
		return GF_BAD_PARAM;
	}

	fprintf(manifest_fp, "#EXTM3U\n#EXT-X-TARGETDURATION:%u\n#EXT-X-MEDIA-SEQUENCE:%u\n", segment_duration, first_segment);

	for (i = first_segment; i <= last_segment; i++) {
		fprintf(manifest_fp, "#EXTINF:%u,\n%s%s_%u.ts\n", segment_duration, http_prefix, segment_prefix, i);
	}

	if (end) {
		fprintf(manifest_fp, "#EXT-X-ENDLIST\n");
	}
	fclose(manifest_fp);

	if (!rename(tmp_manifest, manifest_name)) {
		return GF_OK;
	} else {
		if (remove(manifest_name)) {
			fprintf(stdout, "Error removing file %s\n", manifest_name);
			return GF_IO_ERR;
		} else if (rename(tmp_manifest, manifest_name)) {
			fprintf(stderr, "Could not rename temporary m3u8 manifest file (%s) into %s\n", tmp_manifest, manifest_name);
			return GF_IO_ERR;
		} else {
			return GF_OK;
		}
	}
}

int main(int argc, char **argv)
{
	/********************/
	/*   declarations   */
	/********************/
	const char *ts_pck;
	GF_Err e;
	u32 run_time;
	Bool real_time, single_au_pes, split_rap;
	u64 pcr_init_val=0;
	u32 i, j, mux_rate, nb_progs, cur_pid, carrousel_rate, last_print_time, last_video_time, bifs_use_pes, psi_refresh_rate;
	char *ts_out = NULL, *udp_out = NULL, *rtp_out = NULL, *audio_input_ip = NULL;
	FILE *ts_output_file = NULL;
	GF_Socket *ts_output_udp_sk = NULL, *audio_input_udp_sk = NULL;
#ifndef GPAC_DISABLE_STREAMING
	GF_RTPChannel *ts_output_rtp = NULL;
	GF_RTSPTransport tr;
	GF_RTPHeader hdr;
#endif
	char *video_buffer;
	u32 video_buffer_size;
	u16 output_port = 0, audio_input_port = 0;
	u32 output_type, audio_input_type, pcr_offset;
	char *audio_input_buffer = NULL;
	u32 audio_input_buffer_length=65536;
	char *src_name;
	M2TSProgram progs[MAX_MUX_SRC_PROG];
	u32 segment_duration, segment_index, segment_number;
	char segment_manifest_default[GF_MAX_PATH];
	char *segment_manifest, *segment_http_prefix, *segment_dir;
	char segment_prefix[GF_MAX_PATH];
	char segment_name[GF_MAX_PATH];
	GF_M2TS_Time prev_seg_time;
	GF_M2TS_Mux *muxer;
	
	/*****************/
	/*   gpac init   */
	/*****************/
	gf_sys_init(0);
	gf_log_set_tool_level(GF_LOG_ALL, GF_LOG_WARNING);
	
	/***********************/
	/*   initialisations   */
	/***********************/
	real_time = 0;	
	ts_output_file = NULL;
	video_buffer = NULL;
	last_video_time = 0;
	audio_input_type = 0;
	ts_output_udp_sk = NULL;
	udp_out = NULL;
#ifndef GPAC_DISABLE_STREAMING
	ts_output_rtp = NULL;
	rtp_out = NULL;
#endif
	ts_out = NULL;
	src_name = NULL;
	nb_progs = 0;
	mux_rate = 0;
	run_time = 0;
	carrousel_rate = 500;
	output_port = 1234;
	segment_duration = 0;
	segment_number = 10; /* by default, we keep the 10 previous segments */
	segment_index = 0;
	segment_manifest = NULL;
	segment_http_prefix = NULL;
	segment_dir = NULL;
	prev_seg_time.sec = 0;
	prev_seg_time.nanosec = 0;
	video_buffer_size = 0;
#ifndef GPAC_DISABLE_PLAYER
	aac_reader = AAC_Reader_new();
#endif
	muxer = NULL;
	single_au_pes = 0;
	bifs_use_pes = 0;
	split_rap = 0;
	psi_refresh_rate = GF_M2TS_PSI_DEFAULT_REFRESH_RATE;
	pcr_offset = DEFAULT_PCR_OFFSET;

	/***********************/
	/*   parse arguments   */
	/***********************/
	if (GF_OK != parse_args(argc, argv, &mux_rate, &carrousel_rate, &pcr_init_val, &pcr_offset, &psi_refresh_rate, &single_au_pes, &bifs_use_pes, progs, &nb_progs, &src_name, 
							&real_time, &run_time, &video_buffer, &video_buffer_size,
							&audio_input_type, &audio_input_ip, &audio_input_port,
							&output_type, &ts_out, &udp_out, &rtp_out, &output_port, 
							&segment_dir, &segment_duration, &segment_manifest, &segment_number, &segment_http_prefix, &split_rap)) {
		goto exit;
	}
	
	if (run_time && !mux_rate) {
		fprintf(stderr, "Cannot specify TS run time for VBR multiplex - disabling run time\n");
		run_time = 0;
	}

	/***************************/
	/*   create mp42ts muxer   */
	/***************************/
	muxer = gf_m2ts_mux_new(mux_rate, psi_refresh_rate, real_time);
	if (muxer) gf_m2ts_mux_use_single_au_pes_mode(muxer, single_au_pes);
	if (pcr_init_val) gf_m2ts_mux_set_initial_pcr(muxer, pcr_init_val);

	if (ts_out != NULL) {
		if (segment_duration) {
			char *dot;
			strcpy(segment_prefix, ts_out);
			dot = strrchr(segment_prefix, '.');
			dot[0] = 0;
			if (segment_dir) {
				if (strchr("\\/", segment_name[strlen(segment_name)-1])) {
					sprintf(segment_name, "%s%s_%d.ts", segment_dir, segment_prefix, segment_index);
				} else {
					sprintf(segment_name, "%s/%s_%d.ts", segment_dir, segment_prefix, segment_index);
				}
			} else {
				sprintf(segment_name, "%s_%d.ts", segment_prefix, segment_index);
			}
			ts_out = gf_strdup(segment_name);
			if (!segment_manifest) { 
				sprintf(segment_manifest_default, "%s.m3u8", segment_prefix);
				segment_manifest = segment_manifest_default;
			}
			//write_manifest(segment_manifest, segment_dir, segment_duration, segment_prefix, segment_http_prefix, segment_index, 0, 0);
		} 
		ts_output_file = fopen(ts_out, "wb");
		if (!ts_output_file) {
			fprintf(stderr, "Error opening %s\n", ts_out);
			goto exit;
		}
	}
	if (udp_out != NULL) {
		ts_output_udp_sk = gf_sk_new(GF_SOCK_TYPE_UDP);
		if (gf_sk_is_multicast_address((char *)udp_out)) {
			e = gf_sk_setup_multicast(ts_output_udp_sk, (char *)udp_out, output_port, 32, 0, NULL);
		} else {
			e = gf_sk_bind(ts_output_udp_sk, NULL, output_port, (char *)udp_out, output_port, GF_SOCK_REUSE_PORT);
		}
		if (e) {
			fprintf(stderr, "Error initializing UDP socket: %s\n", gf_error_to_string(e));
			goto exit;
		}
	}
#ifndef GPAC_DISABLE_STREAMING
	if (rtp_out != NULL) {
		ts_output_rtp = gf_rtp_new();
		gf_rtp_set_ports(ts_output_rtp, output_port);
		memset(&tr, 0, sizeof(GF_RTSPTransport));
		tr.IsUnicast = gf_sk_is_multicast_address((char *)rtp_out) ? 0 : 1;
		tr.Profile="RTP/AVP";
		tr.destination = (char *)rtp_out;
		tr.source = "0.0.0.0";
		tr.IsRecord = 0;
		tr.Append = 0;
		tr.SSRC = rand();
		tr.port_first = output_port;
		tr.port_last = output_port+1;
		if (tr.IsUnicast) {
			tr.client_port_first = output_port;
			tr.client_port_last = output_port+1;
		} else {
			tr.source = (char *)rtp_out;
		}
		e = gf_rtp_setup_transport(ts_output_rtp, &tr, (char *)ts_out);
		if (e != GF_OK) {
			fprintf(stderr, "Cannot setup RTP transport info : %s\n", gf_error_to_string(e));
			goto exit;
		}
		e = gf_rtp_initialize(ts_output_rtp, 0, 1, 1500, 0, 0, NULL);
		if (e != GF_OK) {
			fprintf(stderr, "Cannot initialize RTP sockets : %s\n", gf_error_to_string(e));
			goto exit;
		}
		memset(&hdr, 0, sizeof(GF_RTPHeader));
		hdr.Version = 2;
		hdr.PayloadType = 33;	/*MP2T*/
		hdr.SSRC = tr.SSRC;
		hdr.Marker = 0;
	}
#endif /*GPAC_DISABLE_STREAMING*/

	/************************************/
	/*   create streaming audio input   */
	/************************************/
	if (audio_input_ip)
	switch(audio_input_type) {
		case GF_MP42TS_UDP:
			audio_input_udp_sk = gf_sk_new(GF_SOCK_TYPE_UDP);
			if (gf_sk_is_multicast_address((char *)audio_input_ip)) {
				e = gf_sk_setup_multicast(audio_input_udp_sk, (char *)audio_input_ip, audio_input_port, 32, 0, NULL);
			} else {
				e = gf_sk_bind(audio_input_udp_sk, NULL, audio_input_port, (char *)audio_input_ip, audio_input_port, GF_SOCK_REUSE_PORT);
			}
			if (e) {
				fprintf(stdout, "Error initializing UDP socket for %s:%d : %s\n", audio_input_ip, audio_input_port, gf_error_to_string(e));
				goto exit;
			}
			gf_sk_set_buffer_size(audio_input_udp_sk, 0, UDP_BUFFER_SIZE);
			gf_sk_set_block_mode(audio_input_udp_sk, 0);

			/*allocate data buffer*/
			audio_input_buffer = (char*)gf_malloc(audio_input_buffer_length);
			assert(audio_input_buffer);
			break;
		case GF_MP42TS_RTP:
			/*TODO: not implemented*/
			assert(0);
			break;
#ifndef GPAC_DISABLE_PLAYER
		case GF_MP42TS_HTTP:
			audio_prog = (void*)&progs[nb_progs-1];
			aac_download_file(aac_reader, audio_input_ip);
			break;
#endif
		case GF_MP42TS_FILE:
			assert(0); /*audio live input is restricted to realtime/streaming*/
			break;
		default:
			assert(0);
	}

	if (!nb_progs) {
		fprintf(stderr, "No program to mux, quitting.\n");
	}

	/****************************************/
	/*   declare all streams to the muxer   */
	/****************************************/
	cur_pid = 100;	/*PIDs start from 100*/
	for (i=0; i<nb_progs; i++) {
		GF_M2TS_Mux_Program *program = gf_m2ts_mux_program_add(muxer, i+1, cur_pid, psi_refresh_rate, pcr_offset, progs[i].mpeg4_signaling);
		if (progs[i].mpeg4_signaling) program->iod = progs[i].iod;
		if (progs[i].od_updates) {
			program->loop_descriptors = progs[i].od_updates;
			progs[i].od_updates = NULL;
		}

		for (j=0; j<progs[i].nb_streams; j++) {
			GF_M2TS_Mux_Stream *stream;
			Bool force_pes_mode = 0;
			/*likely an OD stream disabled*/
			if (!progs[i].streams[j].stream_type) continue;

			if (progs[i].streams[j].stream_type==GF_STREAM_SCENE) force_pes_mode = bifs_use_pes ? 1 : 0;

			stream = gf_m2ts_program_stream_add(program, &progs[i].streams[j], cur_pid+j+1, (progs[i].pcr_idx==j) ? 1 : 0, force_pes_mode);
			if (split_rap && (progs[i].streams[j].stream_type==GF_STREAM_VISUAL)) stream->start_pes_at_rap = 1;
		}

		cur_pid += progs[i].nb_streams;
		while (cur_pid % 10)
			cur_pid ++;
	}

	gf_m2ts_mux_update_config(muxer, 1);

	/*****************/
	/*   main loop   */
	/*****************/
	last_print_time = gf_sys_clock();
	while (run) {
		u32 status;

		/*check for some audio input from the network*/
		if (audio_input_ip) {
			u32 read;
			switch (audio_input_type) {
				case GF_MP42TS_UDP:
				case GF_MP42TS_RTP:
					/*e =*/gf_sk_receive(audio_input_udp_sk, audio_input_buffer, audio_input_buffer_length, 0, &read);
					if (read) {
						SampleCallBack((void*)&progs[nb_progs-1], AUDIO_DATA_ESID, audio_input_buffer, read, gf_m2ts_get_sys_clock(muxer));
					}
					break;
#ifndef GPAC_DISABLE_PLAYER
				case GF_MP42TS_HTTP:
					/*nothing to do: AAC_OnLiveData is called automatically*/
					/*check we're still alive*/
					if (gf_dm_is_thread_dead(aac_reader->dnload)) {
						GF_ESD *esd;
						aac_download_file(aac_reader, audio_input_ip);
						esd = AAC_GetESD(aac_reader);
						if (!esd)
							break;
						assert(esd->slConfig->timestampResolution); /*if we don't have this value we won't be able to adjust the timestamps within the MPEG2-TS*/
						if (esd->slConfig->timestampResolution)
							audio_discontinuity_offset = gf_m2ts_get_sys_clock(muxer) * (u64)esd->slConfig->timestampResolution / 1000;
						gf_odf_desc_del((GF_Descriptor *)esd);
					}
					break;
#endif
				default:
					assert(0);
			}
		}

		/*flush all packets*/
		while ((ts_pck = gf_m2ts_mux_process(muxer, &status)) != NULL) {
			if (ts_output_file != NULL) {
				gf_fwrite(ts_pck, 1, 188, ts_output_file); 
				if (segment_duration && (muxer->time.sec > prev_seg_time.sec + segment_duration)) {
					prev_seg_time = muxer->time;
					fclose(ts_output_file);
					segment_index++;
					if (segment_dir) {
						if (strchr("\\/", segment_name[strlen(segment_name)-1])) {
							sprintf(segment_name, "%s%s_%d.ts", segment_dir, segment_prefix, segment_index);
						} else {
							sprintf(segment_name, "%s/%s_%d.ts", segment_dir, segment_prefix, segment_index);
						}
					} else {
						sprintf(segment_name, "%s_%d.ts", segment_prefix, segment_index);
					}
					ts_output_file = fopen(segment_name, "wb");
					if (!ts_output_file) {
						fprintf(stderr, "Error opening %s\n", segment_name);
						goto exit;
					}
					/* delete the oldest segment */
					if (segment_number && ((s32) (segment_index - segment_number - 1) >= 0)){
						char old_segment_name[GF_MAX_PATH];
						if (segment_dir) {
							if (strchr("\\/", segment_name[strlen(segment_name)-1])) {
								sprintf(old_segment_name, "%s%s_%d.ts", segment_dir, segment_prefix, segment_index - segment_number - 1);
							} else {
								sprintf(old_segment_name, "%s/%s_%d.ts", segment_dir, segment_prefix, segment_index - segment_number - 1);
							}
						} else {
							sprintf(old_segment_name, "%s_%d.ts", segment_prefix, segment_index - segment_number - 1);
						}
						gf_delete_file(old_segment_name);
					}
					write_manifest(segment_manifest, segment_dir, segment_duration, segment_prefix, segment_http_prefix, 
//								   (segment_index >= segment_number/2 ? segment_index - segment_number/2 : 0), segment_index >1 ? segment_index-1 : 0, 0);
								   ( (segment_index > segment_number ) ? segment_index - segment_number : 0), segment_index >1 ? segment_index-1 : 0, 0);
				} 
			}

			if (ts_output_udp_sk != NULL) {
				e = gf_sk_send(ts_output_udp_sk, (char*)ts_pck, 188); 
				if (e) {
					fprintf(stderr, "Error %s sending UDP packet\n", gf_error_to_string(e));
				}
			}
#ifndef GPAC_DISABLE_STREAMING
			if (ts_output_rtp != NULL) {
				u32 ts;
				hdr.SequenceNumber++;
				/*muxer clock at 90k*/
				ts = muxer->time.sec*90000 + muxer->time.nanosec*9/100000;
				/*FIXME - better discontinuity check*/
				hdr.Marker = (ts < hdr.TimeStamp) ? 1 : 0;
				hdr.TimeStamp = ts;
				e = gf_rtp_send_packet(ts_output_rtp, &hdr, (char*)ts_pck, 188, 0);
				if (e) {
					fprintf(stderr, "Error %s sending RTP packet\n", gf_error_to_string(e));
				}
			}
#endif
			if (status>=GF_M2TS_STATE_PADDING) {
				break;
			}
		}

		/*push video*/
		{
			u32 now=gf_sys_clock();
			if (now/MP42TS_VIDEO_FREQ != last_video_time/MP42TS_VIDEO_FREQ) {
				/*should use carrousel behaviour instead of being pushed manually*/
				if (video_buffer)
					SampleCallBack((void*)&progs[nb_progs-1], VIDEO_DATA_ESID, video_buffer, video_buffer_size, gf_m2ts_get_sys_clock(muxer)+1000/*try buffering due to VLC msg*/);
				last_video_time = now;
			}
		}

		if (real_time) {
			/*refresh every MP42TS_PRINT_FREQ ms*/
			u32 now=gf_sys_clock();
			if (now/MP42TS_PRINT_FREQ != last_print_time/MP42TS_PRINT_FREQ) {
				last_print_time = now;
				fprintf(stderr, "M2TS: time %d - TS time %d - avg bitrate %d\r", gf_m2ts_get_sys_clock(muxer), gf_m2ts_get_ts_clock(muxer), muxer->avg_br);
			}
			/*cpu load regulation*/
			gf_sleep(1);
		}


		if (run_time) {
			if (gf_m2ts_get_ts_clock(muxer) > run_time) {
				fprintf(stderr, "Stopping multiplex at %d ms (requested runtime %d ms)\n", gf_m2ts_get_ts_clock(muxer), run_time);
				break;
			}
		}
		if (status==GF_M2TS_STATE_EOS) {
			break;
		}
	}

	{
		u64 bits = muxer->tot_pck_sent*8*188;
		u32 dur_sec = gf_m2ts_get_ts_clock(muxer) / 1000;
		fprintf(stdout, "Done muxing - %d sec - average rate %d kbps "LLD" packets written\n", dur_sec, (u32) (bits/dur_sec/1000), muxer->tot_pck_sent);
		fprintf(stdout, "\tPadding: "LLD" packets - "LLD" PES padded bytes (%g kbps)\n", muxer->tot_pad_sent, muxer->tot_pes_pad_bytes, (Double) (muxer->tot_pes_pad_bytes*8.0/dur_sec/1000) );
	}

exit:
	run = 0;
	if (segment_duration) {
		write_manifest(segment_manifest, segment_dir, segment_duration, segment_prefix, segment_http_prefix, segment_index - segment_number, segment_index, 1);
	}
	if (ts_output_file) fclose(ts_output_file);
	if (ts_output_udp_sk) gf_sk_del(ts_output_udp_sk);
#ifndef GPAC_DISABLE_STREAMING
	if (ts_output_rtp) gf_rtp_del(ts_output_rtp);
#endif
	if (ts_out) gf_free(ts_out);
	if (audio_input_udp_sk) gf_sk_del(audio_input_udp_sk);
	if (audio_input_buffer) gf_free (audio_input_buffer);
	if (video_buffer) gf_free(video_buffer);
	if (udp_out) gf_free(udp_out);
#ifndef GPAC_DISABLE_STREAMING
	if (rtp_out) gf_free(rtp_out);
#endif
#ifndef GPAC_DISABLE_PLAYER
	if (aac_reader) AAC_Reader_del(aac_reader);
#endif
	if (muxer) gf_m2ts_mux_del(muxer);
	
	for (i=0; i<nb_progs; i++) {
		for (j=0; j<progs[i].nb_streams; j++) {
			if (progs[i].streams[j].input_ctrl) progs[i].streams[j].input_ctrl(&progs[i].streams[j], GF_ESI_INPUT_DESTROY, NULL);
			if (progs[i].streams[j].input_udta){
			  gf_free(progs[i].streams[j].input_udta);
			}
			if (progs[i].streams[j].decoder_config) {
			  gf_free(progs[i].streams[j].decoder_config);
			}
			if (progs[i].streams[j].sl_config) {
			  gf_free(progs[i].streams[j].sl_config);
			}
		}
		if (progs[i].iod) gf_odf_desc_del((GF_Descriptor*)progs[i].iod);
		if (progs[i].mp4) gf_isom_close(progs[i].mp4);
#ifndef GPAC_DISABLE_SENG
		if (progs[i].seng){
		    gf_seng_terminate(progs[i].seng);
		    progs[i].seng = NULL;
		}
#endif
		if (progs[i].th) gf_th_del(progs[i].th);
	}
	gf_sys_close();
	return 1;
}

