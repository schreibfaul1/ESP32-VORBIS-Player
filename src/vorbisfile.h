/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis 'TREMOR' CODEC SOURCE CODE.   *
 *                                                                  *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis 'TREMOR' SOURCE CODE IS (C) COPYRIGHT 1994-2003    *
 * BY THE Xiph.Org FOUNDATION http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************/

#ifndef _OGG_H
#define _OGG_H

#include <stdint.h>
#include <stdio.h>
#include "Arduino.h"
#include "SD_MMC.h"

#define FINFLAG 0x80000000UL
#define FINMASK 0x7fffffffUL

#define OGG_SUCCESS 0

#define OGG_HOLE     -10
#define OGG_SPAN     -11
#define OGG_EVERSION -12
#define OGG_ESERIAL  -13
#define OGG_EINVAL   -14
#define OGG_EEOS     -15

typedef struct ogg_buffer_state
{
	struct ogg_buffer    *unused_buffers;
	struct ogg_reference *unused_references;
	int                   outstanding;
	int                   shutdown;
} ogg_buffer_state_t;

typedef struct ogg_buffer
{
	uint8_t *data;
	int32_t  size;
	int      refcount;
	union
	{
		ogg_buffer_state_t *owner;
		struct ogg_buffer  *next;
	} ptr;
} ogg_buffer_t;

typedef struct ogg_reference
{
	ogg_buffer_t *buffer;
	int32_t       begin;
	int32_t       length;

	struct ogg_reference *next;
} ogg_reference_t;

typedef struct oggpack_buffer
{
	int      headbit;
	uint8_t *headptr;
	int32_t  headend;

	/* memory management */
	ogg_reference_t *head;
	ogg_reference_t *tail;

	/* render the byte/bit counter API constant time */
	int32_t count; /* doesn't count the tail */
} oggpack_buffer_t;

typedef struct oggbyte_buffer
{
	ogg_reference_t *baseref;

	ogg_reference_t *ref;
	uint8_t         *ptr;
	int32_t          pos;
	int32_t          end;
} oggbyte_buffer_t;

typedef struct ogg_sync_state
{
	/* decode memory management pool */
	ogg_buffer_state_t *bufferpool;

	/* stream buffers */
	ogg_reference_t *fifo_head;
	ogg_reference_t *fifo_tail;
	int32_t          fifo_fill;

	/* stream sync management */
	int unsynced;
	int headerbytes;
	int bodybytes;

} ogg_sync_state_t;

typedef struct ogg_stream_state
{
	ogg_reference_t *header_head;
	ogg_reference_t *header_tail;
	ogg_reference_t *body_head;
	ogg_reference_t *body_tail;

	int e_o_s; /* set when we have buffered the last
				  packet in the logical bitstream */
	int b_o_s; /* set after we've written the initial page
				  of a logical bitstream */
	int32_t serialno;
	int32_t pageno;
	int64_t packetno; /* sequence number for decode; the framing
							 knows where there's a hole in the data,
							 but we need coupling so that the codec
							 (which is in a seperate abstraction
							 layer) also knows about the gap */
	int64_t granulepos;

	int      lacing_fill;
	uint32_t body_fill;

	/* decode-side state data */
	int      holeflag;
	int      spanflag;
	int      clearflag;
	int      laceptr;
	uint32_t body_fill_next;

} ogg_stream_state_t;

typedef struct
{
	ogg_reference_t *packet;
	int32_t          bytes;
	int32_t          b_o_s;
	int32_t          e_o_s;
	int64_t          granulepos;
	int64_t          packetno; /* sequence number for decode; the framing
									  knows where there's a hole in the data,
									  but we need coupling so that the codec
									  (which is in a seperate abstraction
									  layer) also knows about the gap */
} ogg_packet;

typedef struct
{
	ogg_reference_t *header;
	int              header_len;
	ogg_reference_t *body;
	int32_t          body_len;
} ogg_page;

typedef struct vorbis_info
{
	int     version;   // The below bitrate declarations are *hints*. Combinations of the three values carry
	int     channels;  // the following implications: all three set to the same value: implies a fixed rate bitstream
	int32_t rate;      // only nominal set:  implies a VBR stream that averages the nominal bitrate.  No hard
	int32_t bitrate_upper;    // upper/lower limit upper and or lower set:  implies a VBR bitstream that obeys the
	int32_t bitrate_nominal;  // bitrate limits. nominal may also be set to give a nominal rate. none set:
	int32_t bitrate_lower;    //  the coder does not care to speculate.
	int32_t bitrate_window;
	void   *codec_setup;
} vorbis_info;

typedef void vorbis_info_floor;

struct vorbis_dsp_state
{                         // vorbis_dsp_state buffers the current vorbis audio analysis/synthesis state.
	vorbis_info     *vi;  // The DSP state be int32_ts to a specific logical bitstream
	oggpack_buffer_t opb;
	int32_t        **work;
	int32_t        **mdctright;
	int              out_begin;
	int              out_end;
	int32_t          lW;
	uint32_t         W;
	int64_t          granulepos;
	int64_t          sequence;
	int64_t          sample_count;
};

typedef struct
{
	int     order;
	int32_t rate;
	int32_t barkmap;
	int     ampbits;
	int     ampdB;
	int     numbooks; /* <= 16 */
	char    books[16];
} vorbis_info_floor0;

typedef struct
{
	char    class_dim;        /* 1 to 8 */
	char    class_subs;       /* 0,1,2,3 (bits: 1<<n poss) */
	uint8_t class_book;       /* subs ^ dim entries */
	uint8_t class_subbook[8]; /* [VIF_CLASS][subs] */
} floor1class;

typedef struct
{
	floor1class *_class;         /* [VIF_CLASS] */
	uint8_t     *partitionclass; /* [VIF_PARTS]; 0 to 15 */
	uint16_t    *postlist;       /* [VIF_POSIT+2]; first two implicit */
	uint8_t     *forward_index;  /* [VIF_POSIT+2]; */
	uint8_t     *hineighbor;     /* [VIF_POSIT]; */
	uint8_t     *loneighbor;     /* [VIF_POSIT]; */
	int          partitions;     /* 0 to 31 */
	int          posts;
	int          mult; /* 1 2 3 or 4 */
} vorbis_info_floor1;

typedef struct vorbis_info_residue{
	int      type;
	uint8_t *stagemasks;
	uint8_t *stagebooks;
	/* block-partitioned VQ coded straight residue */
	uint32_t begin;
	uint32_t end;
	/* first stage (lossless partitioning) */
	uint32_t grouping;   /* group n vectors per partition */
	char     partitions; /* possible codebooks for a partition */
	uint8_t  groupbook;  /* huffbook for partitioning */
	char     stages;
} vorbis_info_residue;

typedef struct{  // mode
	uint8_t blockflag;
	uint8_t mapping;
} vorbis_info_mode;

typedef struct vorbis_comment{
	char **user_comments;
	int   *comment_lengths;
	int    comments;
	char  *vendor;
} vorbis_comment;

struct vorbis_dsp_state;
typedef struct vorbis_dsp_state vorbis_dsp_state;

typedef struct OggVorbis_File
{
	File               *datasource; /* Pointer to a FILE *, etc. */
//	int                 seekable;
	int64_t             offset;
	int64_t             end;
	ogg_sync_state_t   *oy;     // If the FILE handle isn't seekable (eg, a pipe), only the current
	int                 links;  // stream appears */
	int64_t            *offsets;
	int64_t            *dataoffsets;
	uint32_t           *serialnos;
	int64_t            *pcmlengths;
	vorbis_info         vi;
	vorbis_comment      vc;
	int64_t             pcm_offset; /* Decoding working state local storage */
	int                 ready_state;
	uint32_t            current_serialno;
	int                 current_link;
	int64_t             bittrack;
	int64_t             samptrack;
	ogg_stream_state_t *os; /* take physical pages, weld into a logical stream of packets */
	vorbis_dsp_state   *vd; /* central working state for the packet->PCM decoder */
} OggVorbis_File;

//---------------------------------------------------------------------------------------------------------------------
int32_t _get_data(OggVorbis_File *vf);
int64_t _get_next_page(OggVorbis_File *vf, ogg_page *og, int64_t boundary);
int64_t _get_prev_page(OggVorbis_File *vf, ogg_page *og);
int     _bisect_forward_serialno(OggVorbis_File *vf, int64_t begin, int64_t searched, int64_t end, uint32_t currentno,
								 int32_t m);
int     _fetch_headers(OggVorbis_File *vf, vorbis_info *vi, vorbis_comment *vc, uint32_t *serialno, ogg_page *og_ptr);
int     _set_link_number(OggVorbis_File *vf, int link);
void    _prefetch_all_offsets(OggVorbis_File *vf, int64_t dataoffset);
int     _make_decode_ready(OggVorbis_File *vf);
int     _open_seekable2(OggVorbis_File *vf);
int     _fetch_and_process_packet(OggVorbis_File *vf, int readp, int spanp);
int     _ov_open1(File* fIn, OggVorbis_File *vf);
int     _ov_open2(OggVorbis_File *vf);
int     ov_clear(OggVorbis_File *vf);
int     ov_open(File* fIn, OggVorbis_File *vf);
int32_t ov_bitrate(OggVorbis_File *vf, int i);
int32_t ov_bitrate_instant(OggVorbis_File *vf);
int32_t ov_serialnumber(OggVorbis_File *vf, int i);
int64_t ov_raw_total(OggVorbis_File *vf, int i);
int64_t ov_pcm_total(OggVorbis_File *vf, int i);
int64_t ov_time_total(OggVorbis_File *vf, int i);
int     ov_raw_seek(OggVorbis_File *vf, int64_t pos);
int     ov_pcm_seek(OggVorbis_File *vf, int64_t pos);
vorbis_info    *ov_info(OggVorbis_File *vf, int link);
vorbis_comment *ov_comment(OggVorbis_File *vf);
int32_t         ov_read(OggVorbis_File *vf, void *outBuff, int bytes_req);





ogg_buffer_state_t *ogg_buffer_create(void);
void                _ogg_buffer_destroy(ogg_buffer_state_t *bs);
void                ogg_buffer_destroy(ogg_buffer_state_t *bs);
ogg_buffer_t       *_fetch_buffer(ogg_buffer_state_t *bs, int32_t bytes);
uint8_t            *ogg_sync_bufferin(ogg_sync_state_t *oy, int32_t bytes);
ogg_reference_t    *ogg_buffer_alloc(ogg_buffer_state_t *bs, int32_t bytes);
void                ogg_buffer_realloc(ogg_reference_t *_or, int32_t bytes);
ogg_reference_t    *_fetch_ref(ogg_buffer_state_t *bs);
ogg_buffer_t       *_fetch_buffer(ogg_buffer_state_t *bs, int32_t bytes);
int                 ogg_sync_wrote(ogg_sync_state_t *oy, int32_t bytes);
int                 ogg_sync_reset(ogg_sync_state_t *oy);
void                ogg_buffer_release(ogg_reference_t *_or);
void                ogg_buffer_release_one(ogg_reference_t *_or);
void                _ogg_buffer_destroy(ogg_buffer_state_t *bs);
void                ogg_buffer_destroy(ogg_buffer_state_t *bs);
int32_t             ogg_sync_pageseek(ogg_sync_state_t *oy, ogg_page *og);
int                 oggbyte_init(oggbyte_buffer_t *b, ogg_reference_t *_or);
uint8_t             oggbyte_read1(oggbyte_buffer_t *b, int pos);
int64_t             oggbyte_read8(oggbyte_buffer_t *b, int pos);
void                _positionB(oggbyte_buffer_t *b, int pos);
void                _positionF(oggbyte_buffer_t *b, int pos);
uint32_t            oggbyte_read4(oggbyte_buffer_t *b, int pos);
void                oggbyte_set4(oggbyte_buffer_t *b, uint32_t val, int pos);
uint32_t            _checksum(ogg_reference_t *_or, int bytes);
ogg_reference_t    *ogg_buffer_split(ogg_reference_t **tail, ogg_reference_t **head, int32_t pos);
void                _ogg_buffer_mark_one(ogg_reference_t *_or);
void                ogg_buffer_mark(ogg_reference_t *_or);
ogg_reference_t    *ogg_buffer_pretruncate(ogg_reference_t *_or, int32_t pos);
int                 ogg_page_version(ogg_page *og);
int                 ogg_page_continued(ogg_page *og);
int                 ogg_page_bos(ogg_page *og);
int                 ogg_page_eos(ogg_page *og);
int64_t             ogg_page_granulepos(ogg_page *og);
uint32_t            ogg_page_serialno(ogg_page *og);
uint32_t            ogg_page_pageno(ogg_page *og);
int                 ogg_page_release(ogg_page *og);
int                 ogg_stream_reset_serialno(ogg_stream_state_t *os, int serialno);
int                 ogg_stream_pagein(ogg_stream_state_t *os, ogg_page *og);
ogg_reference_t    *ogg_buffer_walk(ogg_reference_t *_or);
ogg_reference_t    *ogg_buffer_cat(ogg_reference_t *tail, ogg_reference_t *head);
int                 ogg_stream_packetout(ogg_stream_state_t *os, ogg_packet *op);
int                 ogg_stream_packetpeek(ogg_stream_state_t *os, ogg_packet *op);
int                 ogg_packet_release(ogg_packet *op);
int                 _packetout(ogg_stream_state_t *os, ogg_packet *op, int adv);
void                _span_queued_page(ogg_stream_state_t *os);
void                _next_lace(oggbyte_buffer_t *ob, ogg_stream_state_t *os);
ogg_buffer_state_t *ogg_buffer_create(void);
ogg_sync_state_t   *ogg_sync_create(void);
int                 ogg_sync_destroy(ogg_sync_state_t *oy);
ogg_stream_state_t *ogg_stream_create(int serialno);
int                 ogg_stream_destroy(ogg_stream_state_t *os);
int                 ogg_stream_reset(ogg_stream_state_t *os);
void                ogg_page_dup(ogg_page *dup, ogg_page *orig);
ogg_reference_t    *ogg_buffer_dup(ogg_reference_t *_or);

#endif /* _OGG_H */
