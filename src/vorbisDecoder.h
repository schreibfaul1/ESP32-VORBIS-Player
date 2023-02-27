/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2007             *
 * by the Xiph.Org Foundation https://xiph.org/                     *
 *                                                                  *
 ********************************************************************/

#pragma once
#include "Arduino.h"
#include "vorbisfile.h"

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#define CHUNKSIZE     1024
#define VI_TRANSFORMB 1
#define VI_WINDOWB    1
#define VI_TIMEB      1
#define VI_FLOORB     2
#define VI_RESB       3
#define VI_MAPB       1

#define LSP_FRACBITS 14

#define OV_FALSE -1
#define OV_EOF   -2
#define OV_HOLE  -3

#define OV_EREAD      -128
#define OV_EFAULT     -129
#define OV_EIMPL      -130
#define OV_EINVAL     -131
#define OV_ENOTVORBIS -132
#define OV_EBADHEADER -133
#define OV_EVERSION   -134
#define OV_ENOTAUDIO  -135
#define OV_EBADPACKET -136
#define OV_EBADLINK   -137
#define OV_ENOSEEK    -138

#define INVSQ_LOOKUP_I_SHIFT 10
#define INVSQ_LOOKUP_I_MASK  1023
#define COS_LOOKUP_I_SHIFT   9
#define COS_LOOKUP_I_MASK    511
#define COS_LOOKUP_I_SZ      128

#define cPI3_8 (0x30fbc54d)
#define cPI2_8 (0x5a82799a)
#define cPI1_8 (0x7641af3d)

#define floor1_rangedB 140 /* floor 1 fixed at -140dB to 0dB range */
#define VIF_POSIT      63

#ifndef min
    #define min(x, y) ((x) > (y) ? (y) : (x))
#endif

#ifndef max
    #define max(x, y) ((x) < (y) ? (y) : (x))
#endif

#define _lookspan()                             \
    while(!end) {                               \
        head = head->next;                      \
        if(!head) return -1;                    \
        ptr = head->buffer->data + head->begin; \
        end = head->length;                     \
    }
//---------------------------------------------------------------------------------------------------------------------
typedef struct codebook{
    uint8_t  dim;          /* codebook dimensions (elements per vector) */
    int16_t entries;       /* codebook entries */
    uint16_t used_entries; /* populated codebook entries */
    uint32_t dec_maxlength;
    void    *dec_table;
    uint32_t dec_nodeb;
    uint32_t dec_leafw;
    uint32_t dec_type; /* 0 = entry number
                          1 = packed vector of values
                          2 = packed vector of column offsets, maptype 1
                          3 = scalar offset into value array,  maptype 2  */
    int32_t q_min;
    int     q_minp;
    int32_t q_del;
    int     q_delp;
    int     q_seq;
    int     q_bits;
    uint8_t q_pack;
    void   *q_val;
} codebook;

struct vorbis_dsp_state;
typedef struct vorbis_dsp_state vorbis_dsp_state;

typedef struct coupling_step{  // Mapping backend generic
    uint8_t mag;
    uint8_t ang;
} coupling_step;

typedef struct submap
{
    char floor;
    char residue;
} submap;

typedef struct vorbis_info_mapping
{
    int            submaps;
    uint8_t       *chmuxlist;
    submap        *submaplist;
    int            coupling_steps;
    coupling_step *coupling;
} vorbis_info_mapping;

typedef struct codec_setup_info{         // Vorbis supports only short and int32_t blocks, but allows the
    uint32_t             blocksizes[2];  // encoder to choose the sizes
    uint32_t             modes;          // modes are the primary means of supporting on-the-fly different
    uint32_t             maps;           // blocksizes, different channel mappings (LR or M/A),
    uint32_t             floors;         // different residue backends, etc.  Each mode consists of a
    uint32_t             residues;       // blocksize flag and a mapping (aint32_t with the mapping setup
    uint32_t             books;
    vorbis_info_mode    *mode_param;
    vorbis_info_mapping *map_param;
    int8_t              *floor_type;
    vorbis_info_floor  **floor_param;
    vorbis_info_residue *residue_param;
    codebook            *book_param;
} codec_setup_info;

//---------------------------------------------------------------------------------------------------------------------

union magic{
    struct{
        int32_t lo;
        int32_t hi;
    } halves;
    int64_t whole;
};

inline int32_t MULT32(int32_t x, int32_t y) {
    union magic magic;
    magic.whole = (int64_t)x * y;
    return magic.halves.hi;
}

inline int32_t MULT31_SHIFT15(int32_t x, int32_t y) {
    union magic magic;
    magic.whole = (int64_t)x * y;
    return ((uint32_t)(magic.halves.lo) >> 15) | ((magic.halves.hi) << 17);
}

inline int32_t MULT31(int32_t x, int32_t y) { return MULT32(x, y) << 1; }

inline void XPROD31(int32_t a, int32_t b, int32_t t, int32_t v, int32_t *x, int32_t *y) {
    *x = MULT31(a, t) + MULT31(b, v);
    *y = MULT31(b, t) - MULT31(a, v);
}

inline void XNPROD31(int32_t a, int32_t b, int32_t t, int32_t v, int32_t *x, int32_t *y) {
    *x = MULT31(a, t) - MULT31(b, v);
    *y = MULT31(b, t) + MULT31(a, v);
}

inline int32_t CLIP_TO_15(int32_t x) {
    int ret = x;
    ret -= ((x <= 32767) - 1) & (x - 32767);
    ret -= ((x >= -32768) - 1) & (x + 32768);
    return (ret);
}

//---------------------------------------------------------------------------------------------------------------------
void     _span(oggpack_buffer *b);
uint8_t  _ilog(uint32_t v);
uint32_t decpack(int32_t entry, int32_t used_entry, uint8_t quantvals, codebook *b, oggpack_buffer *opb, int maptype);
int32_t  _float32_unpack(int32_t val, int *point);
int      _determine_node_bytes(uint32_t used, uint8_t leafwidth);
int      _determine_leaf_words(int nodeb, int leafwidth);
int      _make_words(char *l, uint16_t n, uint32_t *r, uint8_t quantvals, codebook *b, oggpack_buffer *opb, int maptype);
int      _make_decode_table(codebook *s, char *lengthlist, uint8_t quantvals, oggpack_buffer *opb, int maptype);
uint8_t  _book_maptype1_quantvals(codebook *b);
void     vorbis_book_clear(codebook *b);
int      vorbis_book_unpack(oggpack_buffer *opb, codebook *s);
uint32_t decode_packed_entry_number(codebook *book, oggpack_buffer *b);
int32_t  vorbis_book_decode(codebook *book, oggpack_buffer *b);
int      decode_map(codebook *s, oggpack_buffer *b, int32_t *v, int point);
int32_t  vorbis_book_decodevs_add(codebook *book, int32_t *a, oggpack_buffer *b, int n, int point);
int32_t  vorbis_book_decodev_add(codebook *book, int32_t *a, oggpack_buffer *b, int n, int point);
int32_t  vorbis_book_decodev_set(codebook *book, int32_t *a, oggpack_buffer *b, int n, int point);
int32_t  vorbis_book_decodevv_add(codebook *book, int32_t **a, int32_t offset, uint8_t ch, oggpack_buffer *b, int n, int point);
int      vorbis_dsp_restart(vorbis_dsp_state *v);
vorbis_dsp_state *vorbis_dsp_create(vorbis_info *vi);
void              vorbis_dsp_destroy(vorbis_dsp_state *v);
int32_t          *_vorbis_window(int left);
int               vorbis_dsp_pcmout(vorbis_dsp_state *v, int16_t *outBuff, int samples);
int               vorbis_dsp_read(vorbis_dsp_state *v, int s);
int32_t           vorbis_packet_blocksize(vorbis_info *vi, ogg_packet *op);
int               vorbis_dsp_synthesis(vorbis_dsp_state *vd, ogg_packet *op, int decodep);
int32_t           vorbis_fromdBlook_i(int32_t a);
void              render_line(int n, int x0, int x1, int y0, int y1, int32_t *d);
int32_t           vorbis_coslook_i(int32_t a);
int32_t           vorbis_coslook2_i(int32_t a);
int32_t           toBARK(int n);
int32_t           vorbis_invsqlook_i(int32_t a, int32_t e);
void vorbis_lsp_to_curve(int32_t *curve, int n, int ln, int32_t *lsp, int m, int32_t amp, int32_t ampoffset,
                         int32_t nyq);
void floor0_free_info(vorbis_info_floor *i);
vorbis_info_floor *floor0_info_unpack(vorbis_info *vi, oggpack_buffer *opb);
int                floor0_memosize(vorbis_info_floor *i);
int32_t           *floor0_inverse1(vorbis_dsp_state *vd, vorbis_info_floor *i, int32_t *lsp);
int                floor0_inverse2(vorbis_dsp_state *vd, vorbis_info_floor *i, int32_t *lsp, int32_t *out);
void               floor1_free_info(vorbis_info_floor *i);
void               vorbis_mergesort(uint8_t *index, uint16_t *vals, uint16_t n);
vorbis_info_floor *floor1_info_unpack(vorbis_info *vi, oggpack_buffer *opb);
int                render_point(int x0, int x1, int y0, int y1, int x);
int                floor1_memosize(vorbis_info_floor *i);
int32_t           *floor1_inverse1(vorbis_dsp_state *vd, vorbis_info_floor *in, int32_t *fit_value);
int                floor1_inverse2(vorbis_dsp_state *vd, vorbis_info_floor *in, int32_t *fit_value, int32_t *out);
void               mapping_clear_info(vorbis_info_mapping *info);
int                mapping_info_unpack(vorbis_info_mapping *info, vorbis_info *vi, oggpack_buffer *opb);
int                mapping_inverse(vorbis_dsp_state *vd, vorbis_info_mapping *info);
void               _v_readstring(oggpack_buffer *o, char *buf, int bytes);
void               vorbis_comment_init(vorbis_comment *vc);
int                tagcompare(const char *s1, const char *s2, int n);
char              *vorbis_comment_query(vorbis_comment *vc, char *tag, int count);
int                vorbis_comment_query_count(vorbis_comment *vc, char *tag);
void               vorbis_comment_clear(vorbis_comment *vc);
int                vorbis_info_blocksize(vorbis_info *vi, int zo);
void               vorbis_info_init(vorbis_info *vi);
void               vorbis_info_clear(vorbis_info *vi);
int                _vorbis_unpack_info(vorbis_info *vi, oggpack_buffer *opb);
int                _vorbis_unpack_comment(vorbis_comment *vc, oggpack_buffer *opb);
int                _vorbis_unpack_books(vorbis_info *vi, oggpack_buffer *opb);
int                vorbis_dsp_headerin(vorbis_info *vi, vorbis_comment *vc, ogg_packet *op);
void               presymmetry(int32_t *in, int n2, int step);
void               mdct_butterfly_8(int32_t *x);
void               mdct_butterfly_16(int32_t *x);
void               mdct_butterfly_32(int32_t *x);
void               mdct_butterfly_generic(int32_t *x, int points, int step);
void               mdct_butterflies(int32_t *x, int points, int shift);
int                bitrev12(int x);
void               mdct_bitreverse(int32_t *x, int n, int shift);
void               mdct_step7(int32_t *x, int n, int step);
void               mdct_step8(int32_t *x, int n, int step);
void               mdct_backward(int n, int32_t *in);
void               mdct_shift_right(int n, int32_t *in, int32_t *right);
void mdct_unroll_lap(int n0, int n1, int lW, int W, int *in, int *right, const int *w0, const int *w1, short int *out,
                     int step, int start, /* samples, this frame */
                     int end /* samples, this frame */);
void res_clear_info(vorbis_info_residue *info);
int  res_unpack(vorbis_info_residue *info, vorbis_info *vi, oggpack_buffer *opb);
int  res_inverse(vorbis_dsp_state *vd, vorbis_info_residue *info, int32_t **in, int *nonzero, uint8_t ch);

void    oggpack_readinit(oggpack_buffer *b, ogg_reference *r);
int32_t oggpack_look(oggpack_buffer *b, uint16_t bits);
void    oggpack_adv(oggpack_buffer *b, uint16_t bits);
int     oggpack_eop(oggpack_buffer *b);
int32_t oggpack_read(oggpack_buffer *b, uint16_t bits);
