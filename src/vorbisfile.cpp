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

#include "Arduino.h"
#include "SD_MMC.h"
#include "vorbisDecoder.h"

#define NOTOPEN   0
#define PARTOPEN  1
#define OPENED    2
#define STREAMSET 3 /* serialno and link set, but not to current link */
#define LINKSET   4 /* serialno and link set to current link */
#define INITSET   5

File audiofile;

#define __malloc_heap_psram(size) \
    heap_caps_malloc_prefer(size, 2, MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT|MALLOC_CAP_INTERNAL)
#define __calloc_heap_psram(ch, size) \
    heap_caps_calloc_prefer(ch, size, 2, MALLOC_CAP_DEFAULT|MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT|MALLOC_CAP_INTERNAL)


ogg_sync_state_t*   s_oggSyncState = NULL;
ogg_buffer_state_t* s_oggBufferState = NULL;
//---------------------------------------------------------------------------------------------------------------------
bool VORBISDecoder_AllocateBuffers(void){

    if(!s_oggSyncState)   {s_oggSyncState   = (ogg_sync_state_t*)   __calloc_heap_psram(1, sizeof(ogg_sync_state_t));}
    if(!s_oggBufferState) {s_oggBufferState = (ogg_buffer_state_t*) __calloc_heap_psram(1, sizeof(ogg_buffer_state_t));}

    if(!s_oggSyncState || !s_oggBufferState){
        log_e("not enough memory to allocate vorbisdecoder buffers");
        return false;
    }
    log_i("size %i", sizeof(ogg_sync_state_t));
//    VORBISDecoder_ClearBuffer();
    return true;
}

void VORBISDecoder_ClearBuffer(){
    memset(s_oggSyncState,   0, sizeof(ogg_sync_state_t));
    return;
}

void VORBISDecoder_FreeBuffers(){
    if(s_oggSyncState)    {free(s_oggSyncState);    s_oggSyncState    = NULL;}
    if(s_oggBufferState)  {free(s_oggBufferState);  s_oggBufferState  = NULL;}
}
//---------------------------------------------------------------------------------------------------------------------

/* A 'chained bitstream' is a Vorbis bitstream that contains more than one logical bitstream arranged end to end (the
 only form of Ogg multiplexing allowed in a Vorbis bitstream; grouping [parallel  multiplexing] is not allowed in
 Vorbis). A Vorbis file can be played beginning to end (streamed) without worrying ahead of time about chaining (see
 decoder_example.c). If we have the whole file, however, and want random access (seeking/scrubbing) or desire to know
 the total length/time of a file, we need to account for the possibility of chaining. */

/* We can handle things a number of ways; we can determine the entire bitstream structure right off the bat, or find
 pieces on demand. This example determines and caches structure for the entire bitstream, but builds a virtual decoder
 on the fly when moving between links in the chain. */

/* There are also different ways to implement seeking. Enough information exists in an Ogg bitstream to seek to
 sample-granularity positions in the output. Or, one can seek by picking some portion of the stream roughly in the
 desired area if we only want coarse navigation through the stream. */

/* returns the number of packets that are completed on this page (if the leading packet is begun on a previous page,
 but ends on this page, it's counted */

/* Static CRC calculation table.  See older code in CVS for dead run-time initialization code. */
//---------------------------------------------------------------------------------------------------------------------
const uint32_t crc_lookup[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005, 0x2608edb8,
    0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
    0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f, 0x639b0da6,
    0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84,
    0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
    0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a,
    0xec7dd02d, 0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
    0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
    0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba, 0xaca5c697, 0xa864db20, 0xa527fdf9,
    0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b,
    0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
    0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c,
    0x774bb0eb, 0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b, 0x0315d626,
    0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
    0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
    0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a,
    0x8cf30bad, 0x81b02d74, 0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
    0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093,
    0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679,
    0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
    0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09, 0x8d79e0be, 0x803ac667,
    0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};
//---------------------------------------------------------------------------------------------------------------------

/* Many, many internal helpers.  The intention is not to be confusing; rampant duplication and monolithic function
 implementation would be harder to understand anyway. The high level functions are last. Begin grokking near the end
 of the file. Read a little more data from the file/pipe into the ogg_sync framer */

int32_t _get_data(OggVorbis_File *vf) {
    errno = 0;
    if(vf->datasource) {
        uint8_t *buffer = ogg_sync_bufferin(CHUNKSIZE);

        int32_t  bytes = vf->datasource->readBytes((char*)buffer, CHUNKSIZE);
        if(bytes != CHUNKSIZE) {log_e("eof"); return -2;} // esp32 (-1) if eof
        if(bytes <  0){log_e("eof"); return -2;}          // esp32 (-1) if eof
        if(bytes > 0) ogg_sync_wrote(bytes);
        if(bytes == 0) return -1;

        return bytes;
    }
    else
        return 0;
}

//---------------------------------------------------------------------------------------------------------------------
/* The read/seek functions track absolute position within the stream from the head of the stream, get the next page.
 Boundary specifies if the function is allowed to fetch more data from the stream (and how much) or only use internally
 buffered data.
 boundary: -1) unbounded search
 0) read no additional data; use cached only
 n) search for a new page beginning for n bytes

 return:   <0) did not find a page (OV_FALSE, OV_EOF, OV_EREAD)
 n) found a page at absolute offset n produces a refcounted page */

int64_t _get_next_page(OggVorbis_File *vf, ogg_page *og, int64_t boundary) {
    if(boundary > 0) boundary += vf->offset;
    while(1) {
        int32_t more;

        if(boundary > 0 && vf->offset >= boundary) return OV_FALSE;
        more = ogg_sync_pageseek(og);

        if(more < 0) {          /* skipped n bytes */
            vf->offset -= more;
        }
        else {
            if(more == 0) {  /* send more paramedics */
                if(!boundary) return OV_FALSE;
                {
                    int32_t ret = _get_data(vf);
                    if(ret == 0) return OV_EOF;
                //    if(ret < 0) return OV_EREAD;
                    if(ret == -2) return OV_EOF;
                }
            }
            else {
                /* got a page.  Return the offset at the page beginning,
                 advance the internal offset past the page end */
                int64_t ret = vf->offset;
                vf->offset += more;
                return ret;
            }
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
/* find the latest page beginning before the current stream cursor position. Much dirtier than the above as Ogg doesn't
 have any backward search linkage.  no 'readp' as it will certainly have to read. */
/* returns offset or OV_EREAD, OV_FAULT and produces a refcounted page */

int64_t _get_prev_page(OggVorbis_File *vf, ogg_page *og) {
    int64_t begin = vf->offset;
    int64_t end = begin;
    int64_t ret;
    int64_t offset = -1;

    while(offset == -1) {
        begin -= CHUNKSIZE;
        if(begin < 0) begin = 0;
        while(vf->offset < end) {
            ret = _get_next_page(vf, og, end - vf->offset);
            if(ret == OV_EREAD) return OV_EREAD;
            if(ret < 0) { break; }
            else { offset = ret; }
        }
    }

    /* we have the offset.  Actually snork and hold the page now */
    ret = _get_next_page(vf, og, CHUNKSIZE);
    if(ret < 0) /* this shouldn't be possible */
        return OV_EFAULT;

    return offset;
}
//---------------------------------------------------------------------------------------------------------------------
/* finds each bitstream link one at a time using a bisection search (has to begin by knowing the offset of the lb's
 initial page). Recurses for each link so it can alloc the link storage after finding them all, then unroll and fill
 the cache at the same time */
int _bisect_forward_serialno(OggVorbis_File *vf, int64_t begin, int64_t searched, int64_t end, uint32_t currentno,
                             int32_t m) {
    int64_t  endsearched = end;
    int64_t  next = end;
    ogg_page og = {0, 0, 0, 0};
    int64_t  ret;

    /* the below guards against garbage seperating the last and first pages of two links. */
    while(searched < endsearched) {
        int64_t bisect;

        if(endsearched - searched < CHUNKSIZE) { bisect = searched; }
        else { bisect = (searched + endsearched) / 2; }

        ret = _get_next_page(vf, &og, -1);
        if(ret == OV_EREAD) return OV_EREAD;
        if(ret < 0 || ogg_page_serialno(&og) != currentno) {
            endsearched = bisect;
            if(ret >= 0) next = ret;
        }
        else { searched = ret + og.header_len + og.body_len; }
        ogg_page_release(&og);
    }

    ret = _get_next_page(vf, &og, -1);
    if(ret == OV_EREAD) return OV_EREAD;

    if(searched >= end || ret < 0) {
        ogg_page_release(&og);
        vf->links = m + 1;
        vf->offsets = (int64_t *)__malloc_heap_psram((vf->links + 1) * sizeof(*vf->offsets));
        vf->serialnos = (uint32_t *)__malloc_heap_psram(vf->links * sizeof(*vf->serialnos));
        vf->offsets[m + 1] = searched;
    }
    else {
        ret = _bisect_forward_serialno(vf, next, vf->offset, end, ogg_page_serialno(&og), m + 1);
        ogg_page_release(&og);
        if(ret == OV_EREAD) return OV_EREAD;
    }

    vf->offsets[m] = begin;
    vf->serialnos[m] = currentno;
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
/* uses the local ogg_stream storage in vf; this is important for non-streaming input sources */
/* consumes the page that's passed in (if any) state is LINKSET upon successful return */

int _fetch_headers(OggVorbis_File *vf, vorbis_info *vi, vorbis_comment *vc, uint32_t *serialno, ogg_page *og_ptr) {
    ogg_page   og = {0, 0, 0, 0};
    ogg_packet op = {0, 0, 0, 0, 0, 0};
    int        i, ret;

    if(!og_ptr) {
        int64_t llret = _get_next_page(vf, &og, CHUNKSIZE);
        if(llret == OV_EREAD) return OV_EREAD;
        if(llret < 0) return OV_ENOTVORBIS;
        og_ptr = &og;
    }

    ogg_stream_reset_serialno(vf->os, ogg_page_serialno(og_ptr));
    if(serialno) *serialno = vf->os->serialno;

    /* extract the initial header from the first page and verify that the
     Ogg bitstream is in fact Vorbis data */

    vorbis_info_init(vi);
    vorbis_comment_init(vc);




    i = 0;
    while(i < 3) {

        ogg_stream_pagein(vf->os, og_ptr);

        while(i < 3) {
            int result = ogg_stream_packetout(vf->os, &op);
            if(result == 0) break;
            if(result == -1) {
                ret = OV_EBADHEADER;
                goto bail_header;
            }
            if((ret = vorbis_dsp_headerin(vi, vc, &op))) { goto bail_header; }
            i++;
        }

        if(i < 3)
            if(_get_next_page(vf, og_ptr, CHUNKSIZE) < 0) {
                ret = OV_EBADHEADER;
                goto bail_header;
            }
    }



    ogg_packet_release(&op);
    ogg_page_release(&og);
    vf->ready_state = LINKSET;
    return 0;

bail_header:
    ogg_packet_release(&op);
    ogg_page_release(&og);
    vorbis_info_clear(vi);
    vorbis_comment_clear(vc);
    vf->ready_state = OPENED;

    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
/* last step of the OggVorbis_File initialization; get all the offset positions.  Only called by the seekable
initialization (local stream storage is hacked slightly; pay attention to how that's done) */

/* this is void and does not propogate errors up because we want to be able to open and use damaged bitstreams as well
as we can. Just watch out for missing information for links in the OggVorbis_File struct */
void _prefetch_all_offsets(OggVorbis_File *vf, int64_t dataoffset) {
    ogg_page og = {0, 0, 0, 0};
    int      i;
    int64_t  ret;

    vf->dataoffsets = (int64_t *)__malloc_heap_psram(vf->links * sizeof(*vf->dataoffsets));
    vf->pcmlengths = (int64_t *)__malloc_heap_psram(vf->links * 2 * sizeof(*vf->pcmlengths));

    for(i = 0; i < vf->links; i++) {
        if(i == 0) {
            /* we already grabbed the initial header earlier.  Just set the offset */
            vf->dataoffsets[i] = dataoffset;
        }
        else {
            /* seek to the location of the initial header */

            if(_fetch_headers(vf, &vf->vi, &vf->vc, NULL, NULL) < 0) { vf->dataoffsets[i] = -1; }
            else { vf->dataoffsets[i] = vf->offset; }
        }

        /* fetch beginning PCM offset */

        if(vf->dataoffsets[i] != -1) {
            int64_t accumulated = 0, pos;
            int32_t lastblock = -1;
            int     result;

            ogg_stream_reset_serialno(vf->os, vf->serialnos[i]);

            while(1) {
                ogg_packet op = {0, 0, 0, 0, 0, 0};

                ret = _get_next_page(vf, &og, -1);
                if(ret < 0)
                    /* this should not be possible unless the file is
                     truncated/mangled */
                    break;

                if(ogg_page_serialno(&og) != vf->serialnos[i]) break;

                pos = ogg_page_granulepos(&og);

                /* count blocksizes of all frames in the page */
                ogg_stream_pagein(vf->os, &og);
                while((result = ogg_stream_packetout(vf->os, &op))) {
                    if(result > 0) { /* ignore holes */
                        int32_t thisblock = vorbis_packet_blocksize(&vf->vi, &op);
                        if(lastblock != -1) accumulated += (lastblock + thisblock) >> 2;
                        lastblock = thisblock;
                    }
                }
                ogg_packet_release(&op);

                if(pos != -1) {
                    /* pcm offset of last packet on the first audio page */
                    accumulated = pos - accumulated;
                    break;
                }
            }

            /* less than zero?  This is a stream with samples trimmed off
             the beginning, a normal occurrence; set the offset to zero */
            if(accumulated < 0) accumulated = 0;

            vf->pcmlengths[i * 2] = accumulated;
        }

        /* get the PCM length of this link. To do this, get the last page of the stream */
        {
            while(1) {
                ret = _get_prev_page(vf, &og);
                if(ret < 0) {
                    /* this should not be possible */
                    vorbis_info_clear(&vf->vi);
                    vorbis_comment_clear(&vf->vc);
                    break;
                }
                if(ogg_page_granulepos(&og) != -1) {
                    vf->pcmlengths[i * 2 + 1] = ogg_page_granulepos(&og) - vf->pcmlengths[i * 2];
                    break;
                }
                vf->offset = ret;
            }
        }
    }
    ogg_page_release(&og);
}
//---------------------------------------------------------------------------------------------------------------------
int _make_decode_ready(OggVorbis_File *vf) {
    int i;
    switch(vf->ready_state) {
        case OPENED:
        case STREAMSET:
            for(i = 0; i < vf->links; i++)
                if(vf->offsets[i + 1] >= vf->offset) break;
            if(i == vf->links) return -1;
            /* fall through */
        case LINKSET:
            vf->vd = vorbis_dsp_create(&vf->vi);
            vf->ready_state = INITSET;
            vf->bittrack = 0;
            vf->samptrack = 0;
            __attribute__ ((fallthrough));
        case INITSET:
            return 0;
        default:
            return -1;
    }
}
//---------------------------------------------------------------------------------------------------------------------
/* fetch and process a packet.  Handles the case where we're at a bitstream boundary and dumps the decoding machine.
 If the decoding machine is unloaded, it loads it.  It also keeps pcm_offset up to date (seek and read both use this.
 seek uses a special hack with readp).

 return: <0) error, OV_HOLE (lost packet) or OV_EOF
 0) need more data (only if readp==0)
 1) got a packet
 */

int _fetch_and_process_packet(OggVorbis_File *vf, int readp, int spanp) {
    ogg_page   og = {0, 0, 0, 0};
    ogg_packet op = {0, 0, 0, 0, 0, 0};
    int        ret = 0;

    /* handle one packet.  Try to fetch it from current stream state */
    /* extract packets from page */
    while(1) {
        /* process a packet if we can.  If the machine isn't loaded,
         neither is a page */
        if(vf->ready_state == INITSET) {
            while(1) {
                int     result = ogg_stream_packetout(vf->os, &op);
                int64_t granulepos;

                if(result < 0) {
                    ret = OV_HOLE; /* hole in the data. */
                    goto cleanup;
                }
                if(result > 0) {
                    /* got a packet.  process it */
                    granulepos = op.granulepos;
                    if(!vorbis_dsp_synthesis(vf->vd, &op, 1)) { /* lazy check for lazy     header handling.  The
                    header packets aren't audio, so if/when we submit them, vorbis_synthesis will reject them */

                        int pco = vorbis_dsp_pcmout(vf->vd, NULL, 0);
                        vf->samptrack += pco;

                        vf->bittrack += op.bytes * 8;
//                        printf("%i   %i\n", op.bytes, pco);
                        /* update the pcm offset. */
                        if(granulepos != -1 && !op.e_o_s) {
                            int link = 0;
                            int i, samples;

                            /* this packet has a pcm_offset on it (the last packet
                             completed on a page carries the offset) After processing
                             (above), we know the pcm position of the *last* sample
                             ready to be returned. Find the offset of the *first*

                             As an aside, this trick is inaccurate if we begin
                             reading anew right at the last page; the end-of-stream
                             granulepos declares the last frame in the stream, and the
                             last packet of the last page may be a partial frame.
                             So, we need a previous granulepos from an in-sequence page
                             to have a reference point.  Thus the !op.e_o_s clause
                             above */

                            if(granulepos < 0)
                                granulepos = 0; /* actually, this
                                 shouldn't be possible
                                 here unless the stream
                                 is very broken */

                            samples = vorbis_dsp_pcmout(vf->vd, NULL, 0);

                            granulepos -= samples;
                            for(i = 0; i < link; i++) granulepos += vf->pcmlengths[i * 2 + 1];
                            vf->pcm_offset = granulepos;
                        }
                        ret = 1;
                        goto cleanup;
                    }
                }
                else
                    break;
            }
        }

        if(vf->ready_state >= OPENED) {
            int ret;
            if(!readp) {
                ret = 0;
                goto cleanup;
            }
            if((ret = _get_next_page(vf, &og, -1)) < 0) {
                ret = OV_EOF; /* eof. leave unitialized */
                goto cleanup;
            }

            /* bitrate tracking; add the header's bytes here, the body bytes
             are done by packet above */
            vf->bittrack += og.header_len * 8;

            /* has our decoding just traversed a bitstream boundary? */
            if(vf->ready_state == INITSET) {
                if(vf->current_serialno != ogg_page_serialno(&og)) {
                    if(!spanp) {
                        ret = OV_EOF;
                        goto cleanup;
                    }
                }
            }
        }

        /* Do we need to load a new machine before submitting the page? This is different in the seekable and
         non-seekable cases. In the seekable case, we already have all the header information loaded and cached;
         we just initialize the machine with it and continue on our merry way. In the non-seekable (streaming) case,
         we'll only be at a boundary if we just left the previous logical bitstream and we're now nominally at the
         header of the next bitstream */

        if(vf->ready_state != INITSET) {

            if(vf->ready_state < STREAMSET) {

                /* we're streaming */
                /* fetch the three header packets, build the info struct */
                int ret = _fetch_headers(vf, &vf->vi, &vf->vc, &vf->current_serialno, &og);
                if(ret) goto cleanup;
                vf->current_link++;

            }

            if(_make_decode_ready(vf)) return OV_EBADLINK;
        }
        ogg_stream_pagein(vf->os, &og);
    }
cleanup:
    ogg_packet_release(&op);
    ogg_page_release(&og);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
/* clear out the OggVorbis_File struct */
int ov_clear(OggVorbis_File *vf) {
    if(vf) {
        vorbis_dsp_destroy(vf->vd);
        vf->vd = 0;
        ogg_stream_destroy(vf->os);
        vorbis_info_clear(&vf->vi);
        vorbis_comment_clear(&vf->vc);
        if(vf->dataoffsets) free(vf->dataoffsets);
        if(vf->pcmlengths) free(vf->pcmlengths);
        if(vf->serialnos) free(vf->serialnos);
        if(vf->offsets) free(vf->offsets);
        ogg_sync_destroy();

        if(vf->datasource)  vf->datasource->close();
        memset(vf, 0, sizeof(*vf));
    }
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ov_open(File* fIn, OggVorbis_File *vf) {

    int ret;
    memset(vf, 0, sizeof(*vf));

    vf->datasource = fIn;

    /* No seeking yet; Set up a 'single' (current) logical bitstream entry for partial open */
    vf->links = 1;

    vf->os =(ogg_stream_state_t *)__calloc_heap_psram(1, sizeof(ogg_stream_state_t));
    vf->os->serialno = -1; // serialno;
    vf->os->pageno = -1;

    /* Try to fetch the headers, maintaining all the storage */
    if((ret = _fetch_headers(vf, &vf->vi, &vf->vc, &vf->current_serialno, NULL)) < 0) {
        vf->datasource = NULL;
        ov_clear(vf);
    }
    else if(vf->ready_state < PARTOPEN)
        vf->ready_state = PARTOPEN;

    if(ret) return ret;
    if(vf->ready_state < OPENED) vf->ready_state = OPENED;
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
/* returns the bitrate for a given logical bitstream or the entire physical bitstream.  If the file is open for random
 access, it will find the *actual* average bitrate. If the file is streaming, it returns the nominal bitrate (if set)
 else the average of the upper/lower bounds (if set) else -1 (unset).
 If you want the actual bitrate field settings, get them from the vorbis_info structs */

int32_t ov_bitrate(OggVorbis_File *vf, int i) {
    if(vf->ready_state < OPENED) return OV_EINVAL;
    if(i >= vf->links) return OV_EINVAL;
    if(i < 0) {
        int64_t bits = 0;
        int     i;
        for(i = 0; i < vf->links; i++) bits += (vf->offsets[i + 1] - vf->dataoffsets[i]) * 8;
        /* This once read: return(rint(bits/ov_time_total(vf,-1)));
         * gcc 3.x on x86 miscompiled this at optimisation level 2 and above,
         * so this is slightly transformed to make it work.
         */
        return bits * 1000 / ov_time_total(vf, -1);
    }
    else {
        /* return nominal if set */
        if(vf->vi.bitrate_nominal > 0) { return vf->vi.bitrate_nominal; }
        else {
            if(vf->vi.bitrate_upper > 0) {
                if(vf->vi.bitrate_lower > 0) { return (vf->vi.bitrate_upper + vf->vi.bitrate_lower) / 2; }
                else { return vf->vi.bitrate_upper; }
            }
            return OV_FALSE;
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
/* returns the actual bitrate since last call.  returns -1 if no additional data to offer since last call (or at
 beginning of stream), EINVAL if stream is only partially open */
int32_t ov_bitrate_instant(OggVorbis_File *vf) {
    int32_t ret;
    if(vf->ready_state < OPENED) return OV_EINVAL;
    if(vf->samptrack == 0) return OV_FALSE;
    ret = vf->bittrack / vf->samptrack * vf->vi.rate;
    vf->bittrack = 0;
    vf->samptrack = 0;
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
/* Guess */
int32_t ov_serialnumber(OggVorbis_File *vf, int i) {
    if(i >= vf->links) return ov_serialnumber(vf, vf->links - 1);
    if(i >= 0) return ov_serialnumber(vf, -1);
    if(i < 0) { return vf->current_serialno; }
    else { return vf->serialnos[i]; }
}
//---------------------------------------------------------------------------------------------------------------------
/* returns: total PCM length (samples) of content if i==-1 PCM length (samples) of that logical bitstream for i==0 to
 n OV_EINVAL if the stream is not seekable (we can't know the length) or only partially open */
int64_t ov_pcm_total(OggVorbis_File *vf, int i) {
    if(vf->ready_state < OPENED) return OV_EINVAL;
    return OV_EINVAL;
}
//---------------------------------------------------------------------------------------------------------------------
/* returns: total milliseconds of content if i==-1 milliseconds in that logical bitstream for i==0 to n OV_EINVAL if
 the stream is not seekable (we can't know the length) or only partially open */
int64_t ov_time_total(OggVorbis_File *vf, int i) {
    if(vf->ready_state < OPENED) return OV_EINVAL;
    return OV_EINVAL;
}
//---------------------------------------------------------------------------------------------------------------------
/*  link:   -1) return the vorbis_info struct for the bitstream section currently being decoded
 0-n) to request information for a specific bitstream section

 In the case of a non-seekable bitstream, any call returns the current bitstream.  NULL in the case that the machine is
 not initialized */

vorbis_info *ov_info(OggVorbis_File *vf, int link) {
    return &vf->vi;
}
//---------------------------------------------------------------------------------------------------------------------
/* grr, strong typing, grr, no templates/inheritence, grr */
vorbis_comment *ov_comment(OggVorbis_File *vf) {
    return &vf->vc;
}
//---------------------------------------------------------------------------------------------------------------------
/* up to this point, everything could more or less hide the multiple logical bitstream nature of chaining from the
 toplevel application if the toplevel application didn't particularly care. However, at the point that we actually read
 audio back, the multiple-section nature must surface: Multiple bitstream sections do not necessarily have to have the
 same number of channels or sampling rate. ov_read returns the sequential logical bitstream number currently being
 decoded along with the PCM data in order that the toplevel application can take action on channel/sample rate changes.
 This number will be incremented even for streamed (non-seekable) streams (for seekable streams, it represents the
 actual logical bitstream index within the physical bitstream. Note that the accessor functions above are aware of this
 dichotomy).
 input values: buffer) a buffer to hold packed PCM data for return
 length) the byte length requested to be placed into buffer

 return values: <0) error/hole in data (OV_HOLE), partial open (OV_EINVAL)
 0) EOF
 n) number of bytes of PCM actually returned. The below works on a packet-by-packet basis, so the return length is not
 related to the 'length' passed in, just guaranteed to fit.
 *section) set to the logical bitstream number */

int32_t ov_read(OggVorbis_File *vf, void *outBuff, int bytes_req) {
    int32_t samples;
    int32_t channels;

    if(vf->ready_state < OPENED) return OV_EINVAL;

    while(1) {
        if(vf->ready_state == INITSET) {
            channels = vf->vi.channels;
            samples = vorbis_dsp_pcmout(vf->vd, (int16_t *)outBuff, (bytes_req >> 1) / channels);
            if(samples) {
                if(samples > 0) {
                    vorbis_dsp_read(vf->vd, samples);
                    vf->pcm_offset += samples;
                    return samples * 2 * channels;
                }
                return samples;
            }
        }

        /* suck in another packet */
        {
            int ret = _fetch_and_process_packet(vf, 1, 1);
            if(ret == OV_EOF) return 0;
            if(ret <= 0) return ret;
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
uint8_t *ogg_sync_bufferin(int32_t bytes) {
    /* [allocate and] expose a buffer for data submission.

     If there is no head fragment allocate one and expose it else if the current head fragment has sufficient unused
     space expose it else if the current head fragment is unused resize and expose it else allocate new fragment and
     expose it
     */

    /* base case; fifo uninitialized */
    if(!s_oggSyncState->fifo_head) {
        s_oggSyncState->fifo_head = s_oggSyncState->fifo_tail = ogg_buffer_alloc(s_oggBufferState, bytes);
        return s_oggSyncState->fifo_head->buffer->data;
    }

    /* space left in current fragment case */
    if(s_oggSyncState->fifo_head->buffer->size - s_oggSyncState->fifo_head->length - s_oggSyncState->fifo_head->begin >= bytes)
        return s_oggSyncState->fifo_head->buffer->data + s_oggSyncState->fifo_head->length + s_oggSyncState->fifo_head->begin;

    /* current fragment is unused, but too small */
    if(!s_oggSyncState->fifo_head->length) {
        ogg_buffer_realloc(s_oggSyncState->fifo_head, bytes);
        return s_oggSyncState->fifo_head->buffer->data + s_oggSyncState->fifo_head->begin;
    }

    /* current fragment used/full; get new fragment */
    {
        ogg_reference_t *_new = ogg_buffer_alloc(s_oggBufferState, bytes);
        s_oggSyncState->fifo_head->next = _new;
        s_oggSyncState->fifo_head = _new;
    }
    return s_oggSyncState->fifo_head->buffer->data;
}
//---------------------------------------------------------------------------------------------------------------------
/* fetch a reference pointing to a fresh, initially continguous buffer
 of at least [bytes] length */
ogg_reference_t *ogg_buffer_alloc(ogg_buffer_state_t *bs, int32_t bytes) {
    ogg_buffer_t    *ob = _fetch_buffer(bs, bytes);
    ogg_reference_t *_or = _fetch_ref(bs);
    _or->buffer = ob;
    return _or;
}
//---------------------------------------------------------------------------------------------------------------------
/* enlarge the data buffer in the current link */
void ogg_buffer_realloc(ogg_reference_t *_or, int32_t bytes) {
    ogg_buffer_t *ob = _or->buffer;

    /* if the unused buffer is too small, grow it */
    if(ob->size < bytes) {
        ob->data = (uint8_t *)realloc(ob->data, bytes);
        ob->size = bytes;
    }
}
//---------------------------------------------------------------------------------------------------------------------
ogg_reference_t *_fetch_ref(ogg_buffer_state_t *bs) {
    ogg_reference_t *_or;
    bs->outstanding++;

    /* do we have an unused reference sitting in the pool? */
    if(bs->unused_references) {
        _or = bs->unused_references;
        bs->unused_references = _or->next;
    }
    else {
        /* allocate a new reference */
        _or = (ogg_reference_t *)__malloc_heap_psram(sizeof(*_or));
    }

    _or->begin = 0;
    _or->length = 0;
    _or->next = 0;
    return _or;
}
//---------------------------------------------------------------------------------------------------------------------
ogg_buffer_t *_fetch_buffer(ogg_buffer_state_t *bs, int32_t bytes) {
    ogg_buffer_t *ob;
    bs->outstanding++;

    /* do we have an unused buffer sitting in the pool? */
    if(bs->unused_buffers) {
        ob = bs->unused_buffers;
        bs->unused_buffers = ob->next;

        /* if the unused buffer is too small, grow it */
        if(ob->size < bytes) {
            ob->data = (uint8_t *)realloc(ob->data, bytes);
            ob->size = bytes;
        }
    }
    else {
        /* allocate a new buffer */
        ob = (ogg_buffer_t *)__malloc_heap_psram(sizeof(*ob));
        ob->data = (uint8_t *)__malloc_heap_psram(bytes < 16 ? 16 : bytes);
        ob->size = bytes;
    }

    ob->refcount = 1;
    ob->owner = bs;
    return ob;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_sync_wrote(int32_t bytes) {
    if(!s_oggSyncState->fifo_head) return OGG_EINVAL;
    if(s_oggSyncState->fifo_head->buffer->size - s_oggSyncState->fifo_head->length - s_oggSyncState->fifo_head->begin < bytes) return OGG_EINVAL;
    s_oggSyncState->fifo_head->length += bytes;
    s_oggSyncState->fifo_fill += bytes;
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
/* clear things to an initial state.  Good to call, eg, before seeking */
int ogg_sync_reset() {
    ogg_buffer_release(s_oggSyncState->fifo_tail);
    s_oggSyncState->fifo_tail = 0;
    s_oggSyncState->fifo_head = 0;
    s_oggSyncState->fifo_fill = 0;

    s_oggSyncState->unsynced = 0;
    s_oggSyncState->headerbytes = 0;
    s_oggSyncState->bodybytes = 0;
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
/* release the references, decrease the refcounts of buffers to which
 they point, release any buffers with a refcount that drops to zero */
void ogg_buffer_release(ogg_reference_t *_or) {
    while(_or) {
        ogg_reference_t *next = _or->next;
        ogg_buffer_release_one(_or);
        _or = next;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ogg_buffer_release_one(ogg_reference_t *_or) {
    ogg_buffer_t       *ob = _or->buffer;
    ogg_buffer_state_t *bs = ob->owner;

    ob->refcount--;
    if(ob->refcount == 0) {
        bs->outstanding--; /* for the returned buffer */
        ob->next = bs->unused_buffers;
        bs->unused_buffers = ob;
    }

    bs->outstanding--; /* for the returned reference */
    _or->next = bs->unused_references;
    bs->unused_references = _or;

    _ogg_buffer_destroy(bs); /* lazy cleanup (if needed) */
}
//---------------------------------------------------------------------------------------------------------------------
// destruction is 'lazy'; there may be memory references outstanding, and yanking the buffer state
// out from underneath would be antisocial.  Dealloc what is currently unused and have _release_one
// watch for the stragglers to come in.  When they do, finish destruction.

/* call the helper while holding lock */
void _ogg_buffer_destroy(ogg_buffer_state_t *bs) {
    ogg_buffer_t    *bt;
    ogg_reference_t *rt;

    if(bs->shutdown) {
        bt = bs->unused_buffers;
        rt = bs->unused_references;

        while(bt) {
            ogg_buffer_t *b = bt;
            bt = b->next;
            if(b->data) free(b->data);
            free(b);
        }
        bs->unused_buffers = 0;
        while(rt) {
            ogg_reference_t *r = rt;
            rt = r->next;
            free(r);
        }
        bs->unused_references = 0;

        if(!bs->outstanding) free(bs);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ogg_buffer_destroy(ogg_buffer_state_t *bs) {
    bs->shutdown = 1;
    _ogg_buffer_destroy(bs);
}
//---------------------------------------------------------------------------------------------------------------------
/* sync the stream.  This is meant to be useful for finding page boundaries.

 return values for this:
 -n) skipped n bytes
 0) page not ready; more data (no bytes skipped)
 n) page synced at current location; page length n bytes */

int32_t ogg_sync_pageseek(ogg_page *og) {
    oggbyte_buffer_t page;
    int32_t          bytes, ret = 0;

    ogg_page_release(og);

    bytes = s_oggSyncState->fifo_fill;
    oggbyte_init(&page, s_oggSyncState->fifo_tail);

    if(s_oggSyncState->headerbytes == 0) {
        if(bytes < 27) goto sync_out;
        /* not enough for even a minimal header */

        /* verify capture pattern */
        if(oggbyte_read1(&page, 0) != (int)'O' || oggbyte_read1(&page, 1) != (int)'g' ||
           oggbyte_read1(&page, 2) != (int)'g' || oggbyte_read1(&page, 3) != (int)'S')
            goto sync_fail;

        s_oggSyncState->headerbytes = oggbyte_read1(&page, 26) + 27;
    }
    if(bytes < s_oggSyncState->headerbytes) goto sync_out;
    /* not enough for header +
     seg table */
    if(s_oggSyncState->bodybytes == 0) {
        int i;
        /* count up body length in the segment table */
        for(i = 0; i < s_oggSyncState->headerbytes - 27; i++) s_oggSyncState->bodybytes += oggbyte_read1(&page, 27 + i);
    }

    if(s_oggSyncState->bodybytes + s_oggSyncState->headerbytes > bytes) goto sync_out;

    /* we have what appears to be a complete page; last test: verify checksum */
    {
        uint32_t chksum = oggbyte_read4(&page, 22);
        oggbyte_set4(&page, 0, 22);

        /* Compare checksums; memory continues to be common access */
        if(chksum != _checksum(s_oggSyncState->fifo_tail, s_oggSyncState->bodybytes + s_oggSyncState->headerbytes)) {
            /* D'oh.  Mismatch! Corrupt page (_or miscapture and not a page at all). replace the computed checksum with
             the one actually read in; remember all the memory is common access */

            oggbyte_set4(&page, chksum, 22);
            goto sync_fail;
        }
        oggbyte_set4(&page, chksum, 22);
    }

    /* We have a page.  Set up page return. */
    if(og) {
        /* set up page output */
        og->header = ogg_buffer_split(&s_oggSyncState->fifo_tail, &s_oggSyncState->fifo_head, s_oggSyncState->headerbytes);
        og->header_len = s_oggSyncState->headerbytes;
        og->body = ogg_buffer_split(&s_oggSyncState->fifo_tail, &s_oggSyncState->fifo_head, s_oggSyncState->bodybytes);
        og->body_len = s_oggSyncState->bodybytes;
    }
    else {
        /* simply advance */
        s_oggSyncState->fifo_tail = ogg_buffer_pretruncate(s_oggSyncState->fifo_tail, s_oggSyncState->headerbytes + s_oggSyncState->bodybytes);
        if(!s_oggSyncState->fifo_tail) s_oggSyncState->fifo_head = 0;
    }

    ret = s_oggSyncState->headerbytes + s_oggSyncState->bodybytes;
    s_oggSyncState->unsynced = 0;
    s_oggSyncState->headerbytes = 0;
    s_oggSyncState->bodybytes = 0;
    s_oggSyncState->fifo_fill -= ret;

    return ret;

sync_fail:

    s_oggSyncState->headerbytes = 0;
    s_oggSyncState->bodybytes = 0;
    s_oggSyncState->fifo_tail = ogg_buffer_pretruncate(s_oggSyncState->fifo_tail, 1);
    ret--;

    /* search forward through fragments for possible capture */
    while(s_oggSyncState->fifo_tail) {
        /* invariant: fifo_cursor points to a position in fifo_tail */
        uint8_t *now = s_oggSyncState->fifo_tail->buffer->data + s_oggSyncState->fifo_tail->begin;
        uint8_t *next = (uint8_t *)memchr(now, 'O', s_oggSyncState->fifo_tail->length);

        if(next) {
            /* possible capture in this segment */
            int32_t bytes = next - now;
            s_oggSyncState->fifo_tail = ogg_buffer_pretruncate(s_oggSyncState->fifo_tail, bytes);
            ret -= bytes;
            break;
        }
        else {
            /* no capture.  advance to next segment */
            int32_t bytes = s_oggSyncState->fifo_tail->length;
            ret -= bytes;
            s_oggSyncState->fifo_tail = ogg_buffer_pretruncate(s_oggSyncState->fifo_tail, bytes);
        }
    }
    if(!s_oggSyncState->fifo_tail) s_oggSyncState->fifo_head = 0;
    s_oggSyncState->fifo_fill += ret;

sync_out:
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
int oggbyte_init(oggbyte_buffer_t *b, ogg_reference_t *_or) {
    memset(b, 0, sizeof(*b));
    if(_or) {
        b->ref = b->baseref = _or;
        b->pos = 0;
        b->end = b->ref->length;
        b->ptr = b->ref->buffer->data + b->ref->begin;
        return 0;
    }
    else
        return -1;
}
//---------------------------------------------------------------------------------------------------------------------
uint8_t oggbyte_read1(oggbyte_buffer_t *b, int pos) {
    _positionB(b, pos);
    _positionF(b, pos);
    return b->ptr[pos - b->pos];
}
//---------------------------------------------------------------------------------------------------------------------
int64_t oggbyte_read8(oggbyte_buffer_t *b, int pos) {
    int64_t ret;
    uint8_t t[7];
    int     i;
    _positionB(b, pos);
    for(i = 0; i < 7; i++) {
        _positionF(b, pos);
        t[i] = b->ptr[pos++ - b->pos];
    }

    _positionF(b, pos);
    ret = b->ptr[pos - b->pos];

    for(i = 6; i >= 0; --i) ret = ret << 8 | t[i];

    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
void _positionB(oggbyte_buffer_t *b, int pos) {
    if(pos < b->pos) {
        /* start at beginning, scan forward */
        b->ref = b->baseref;
        b->pos = 0;
        b->end = b->pos + b->ref->length;
        b->ptr = b->ref->buffer->data + b->ref->begin;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void _positionF(oggbyte_buffer_t *b, int pos) {
    /* scan forward for position */
    while(pos >= b->end) {
        /* just seek forward */
        b->pos += b->ref->length;
        b->ref = b->ref->next;
        b->end = b->ref->length + b->pos;
        b->ptr = b->ref->buffer->data + b->ref->begin;
    }
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t oggbyte_read4(oggbyte_buffer_t *b, int pos) {
    uint32_t ret;
    _positionB(b, pos);
    _positionF(b, pos);
    ret = b->ptr[pos - b->pos];
    _positionF(b, ++pos);
    ret |= b->ptr[pos - b->pos] << 8;
    _positionF(b, ++pos);
    ret |= b->ptr[pos - b->pos] << 16;
    _positionF(b, ++pos);
    ret |= b->ptr[pos - b->pos] << 24;
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
void oggbyte_set4(oggbyte_buffer_t *b, uint32_t val, int pos) {
    int i;
    _positionB(b, pos);
    for(i = 0; i < 4; i++) {
        _positionF(b, pos);
        b->ptr[pos - b->pos] = val;
        val >>= 8;
        ++pos;
    }
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t _checksum(ogg_reference_t *_or, int bytes) {
    uint32_t crc_reg = 0;
    int      j, post;

    while(_or) {
        uint8_t *data = _or->buffer->data + _or->begin;
        post = (bytes < _or->length ? bytes : _or->length);
        for(j = 0; j < post; ++j) crc_reg = (crc_reg << 8) ^ crc_lookup[((crc_reg >> 24) & 0xff) ^ data[j]];
        bytes -= j;
        _or = _or->next;
    }

    return crc_reg;
}
//---------------------------------------------------------------------------------------------------------------------
/* split a reference into two references; 'return' is a reference to the buffer preceeding pos and 'head'/'tail' are
 the buffer past the split. If pos is at _or past the end of the passed in segment, 'head/tail' are NULL */
ogg_reference_t *ogg_buffer_split(ogg_reference_t **tail, ogg_reference_t **head, int32_t pos) {
    /* walk past any preceeding fragments to one of:
     a) the exact boundary that seps two fragments
     b) the fragment that needs split somewhere in the middle */
    ogg_reference_t *ret = *tail;
    ogg_reference_t *_or = *tail;

    while(_or && pos > _or->length) {
        pos -= _or->length;
        _or = _or->next;
    }

    if(!_or || pos == 0) { return 0; }
    else {
        if(pos >= _or->length) {
            /* exact split, _or off the end? */
            if(_or->next) {
                /* a split */
                *tail = _or->next;
                _or->next = 0;
            }
            else {
                /* off _or at the end */
                *tail = *head = 0;
            }
        }
        else {
            /* split within a fragment */
            int32_t lengthA = pos;
            int32_t beginB = _or->begin + pos;
            int32_t lengthB = _or->length - pos;

            /* make a new reference to tail the second piece */
            *tail = _fetch_ref(_or->buffer->owner);

            (*tail)->buffer = _or->buffer;
            (*tail)->begin = beginB;
            (*tail)->length = lengthB;
            (*tail)->next = _or->next;
            _ogg_buffer_mark_one(*tail);
            if(head && _or == *head) *head = *tail;

            /* update the first piece */
            _or->next = 0;
            _or->length = lengthA;
        }
    }
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
void _ogg_buffer_mark_one(ogg_reference_t *_or) { _or->buffer->refcount++; }
//---------------------------------------------------------------------------------------------------------------------
/* increase the refcount of the buffers to which the reference points */
void ogg_buffer_mark(ogg_reference_t *_or) {
    while(_or) {
        _ogg_buffer_mark_one(_or);
        _or = _or->next;
    }
}
//---------------------------------------------------------------------------------------------------------------------
ogg_reference_t *ogg_buffer_pretruncate(ogg_reference_t *_or, int32_t pos) {
    /* release preceeding fragments we don't want */
    while(_or && pos >= _or->length) {
        ogg_reference_t *next = _or->next;
        pos -= _or->length;
        ogg_buffer_release_one(_or);
        _or = next;
    }
    if(_or) {
        _or->begin += pos;
        _or->length -= pos;
    }
    return _or;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_page_version(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return -1;
    return oggbyte_read1(&ob, 4);
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_page_continued(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return -1;
    return oggbyte_read1(&ob, 5) & 0x01;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_page_bos(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return -1;
    return oggbyte_read1(&ob, 5) & 0x02;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_page_eos(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return -1;
    return oggbyte_read1(&ob, 5) & 0x04;
}
//---------------------------------------------------------------------------------------------------------------------
int64_t ogg_page_granulepos(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return -1;
    return oggbyte_read8(&ob, 6);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t ogg_page_serialno(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return 0xffffffffUL;
    return oggbyte_read4(&ob, 14);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t ogg_page_pageno(ogg_page *og) {
    oggbyte_buffer_t ob;
    if(oggbyte_init(&ob, og->header)) return 0xffffffffUL;
    return oggbyte_read4(&ob, 18);
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_page_release(ogg_page *og) {
    if(og) {
        ogg_buffer_release(og->header);
        ogg_buffer_release(og->body);
        memset(og, 0, sizeof(*og));
    }
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_stream_reset_serialno(ogg_stream_state_t *os, int serialno) {
    ogg_stream_reset(os);
    os->serialno = serialno;
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
/* add the incoming page to the stream state; we decompose the page into packet segments here as well. */

int ogg_stream_pagein(ogg_stream_state_t *os, ogg_page *og) {
    int serialno = ogg_page_serialno(og);
    int version = ogg_page_version(og);

    /* check the serial number */
    if(serialno != os->serialno) {
        ogg_page_release(og);
        return OGG_ESERIAL;
    }
    if(version > 0) {
        ogg_page_release(og);
        return OGG_EVERSION;
    }

    /* add to fifos */
    if(!os->body_tail) {
        os->body_tail = og->body;
        os->body_head = ogg_buffer_walk(og->body);
    }
    else { os->body_head = ogg_buffer_cat(os->body_head, og->body); }
    if(!os->header_tail) {
        os->header_tail = og->header;
        os->header_head = ogg_buffer_walk(og->header);
        os->lacing_fill = -27;
    }
    else { os->header_head = ogg_buffer_cat(os->header_head, og->header); }

    memset(og, 0, sizeof(*og));
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
ogg_reference_t *ogg_buffer_walk(ogg_reference_t *_or) {
    if(!_or) return NULL;
    while(_or->next) { _or = _or->next; }
    return (_or);
}
//---------------------------------------------------------------------------------------------------------------------
/* *head is appended to the front end (head) of *tail; both continue to
 be valid pointers, with *tail at the tail and *head at the head */
ogg_reference_t *ogg_buffer_cat(ogg_reference_t *tail, ogg_reference_t *head) {
    if(!tail) return head;

    while(tail->next) { tail = tail->next; }
    tail->next = head;
    return ogg_buffer_walk(head);
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_stream_packetout(ogg_stream_state_t *os, ogg_packet *op) { return _packetout(os, op, 1); }
//---------------------------------------------------------------------------------------------------------------------
int ogg_stream_packetpeek(ogg_stream_state_t *os, ogg_packet *op) { return _packetout(os, op, 0); }
//---------------------------------------------------------------------------------------------------------------------
int ogg_packet_release(ogg_packet *op) {
    if(op) {
        ogg_buffer_release(op->packet);
        memset(op, 0, sizeof(*op));
    }
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
int _packetout(ogg_stream_state_t *os, ogg_packet *op, int adv) {
    ogg_packet_release(op);
    _span_queued_page(os);

    if(os->holeflag) {
        int temp = os->holeflag;
        if(os->clearflag) os->holeflag = 0;
        else
            os->holeflag = 1;
        if(temp == 2) {
            os->packetno++;
            return OGG_HOLE;
        }
    }
    if(os->spanflag) {
        int temp = os->spanflag;
        if(os->clearflag) os->spanflag = 0;
        else
            os->spanflag = 1;
        if(temp == 2) {
            os->packetno++;
            return OGG_SPAN;
        }
    }

    if(!(os->body_fill & FINFLAG)) return 0;
    if(!op && !adv)
        return 1; /* just using peek as an inexpensive way
         to ask if there's a whole packet waiting */
    if(op) {
        op->b_o_s = os->b_o_s;
        if(os->e_o_s && os->body_fill_next == 0) op->e_o_s = os->e_o_s;
        else
            op->e_o_s = 0;
        if((os->body_fill & FINFLAG) && !(os->body_fill_next & FINFLAG)) op->granulepos = os->granulepos;
        else
            op->granulepos = -1;
        op->packetno = os->packetno;
    }

    if(adv) {
        oggbyte_buffer_t ob;
        oggbyte_init(&ob, os->header_tail);

        /* split the body contents off */
        if(op) {
            op->packet = ogg_buffer_split(&os->body_tail, &os->body_head, os->body_fill & FINMASK);
            op->bytes = os->body_fill & FINMASK;
        }
        else {
            os->body_tail = ogg_buffer_pretruncate(os->body_tail, os->body_fill & FINMASK);
            if(os->body_tail == 0) os->body_head = 0;
        }

        /* update lacing pointers */
        os->body_fill = os->body_fill_next;
        _next_lace(&ob, os);
    }

    if(adv) {
        os->packetno++;
        os->b_o_s = 0;
    }

    return 1;
}
//---------------------------------------------------------------------------------------------------------------------
void _span_queued_page(ogg_stream_state_t *os) {
    while(!(os->body_fill & FINFLAG)) {
        if(!os->header_tail) break;

        /* first flush out preceeding page header (if any).  Body is
         flushed as it's consumed, so that's not done here. */

        if(os->lacing_fill >= 0) os->header_tail = ogg_buffer_pretruncate(os->header_tail, os->lacing_fill + 27);
        os->lacing_fill = 0;
        os->laceptr = 0;
        os->clearflag = 0;

        if(!os->header_tail) {
            os->header_head = 0;
            break;
        }
        else {
            /* process/prepare next page, if any */

            int32_t          pageno;
            oggbyte_buffer_t ob;
            ogg_page         og;         /* only for parsing header values */
            og.header = os->header_tail; /* only for parsing header values */
            pageno = ogg_page_pageno(&og);

            oggbyte_init(&ob, os->header_tail);
            os->lacing_fill = oggbyte_read1(&ob, 26);

            /* are we in sequence? */
            if(pageno != os->pageno) {
                if(os->pageno == -1)  /* indicates seek _or reset */
                    os->holeflag = 1; /* set for internal use */
                else
                    os->holeflag = 2; /* set for external reporting */

                os->body_tail = ogg_buffer_pretruncate(os->body_tail, os->body_fill);
                if(os->body_tail == 0) os->body_head = 0;
                os->body_fill = 0;
            }

            if(ogg_page_continued(&og)) {
                if(os->body_fill == 0) {
                    /* continued packet, but no preceeding data to continue */
                    /* dump the first partial packet on the page */
                    _next_lace(&ob, os);
                    os->body_tail = ogg_buffer_pretruncate(os->body_tail, os->body_fill_next & FINMASK);
                    if(os->body_tail == 0) os->body_head = 0;
                    /* set span flag */
                    if(!os->spanflag && !os->holeflag) os->spanflag = 2;
                }
            }
            else {
                if(os->body_fill > 0) {
                    /* preceeding data to continue, but not a continued page */
                    /* dump body_fill */
                    os->body_tail = ogg_buffer_pretruncate(os->body_tail, os->body_fill);
                    if(os->body_tail == 0) os->body_head = 0;
                    os->body_fill = 0;

                    /* set espan flag */
                    if(!os->spanflag && !os->holeflag) os->spanflag = 2;
                }
            }

            if(os->laceptr < os->lacing_fill) {
                os->granulepos = ogg_page_granulepos(&og);

                /* get current packet size & flag */
                _next_lace(&ob, os);
                os->body_fill += os->body_fill_next; /* addition handles the flag fine;
                 unsigned on purpose */
                /* ...and next packet size & flag */
                _next_lace(&ob, os);
            }

            os->pageno = pageno + 1;
            os->e_o_s = ogg_page_eos(&og);
            os->b_o_s = ogg_page_bos(&og);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void _next_lace(oggbyte_buffer_t *ob, ogg_stream_state_t *os) {
    /* search ahead one lace */
    os->body_fill_next = 0;
    while(os->laceptr < os->lacing_fill) {
        int val = oggbyte_read1(ob, 27 + os->laceptr++);
        os->body_fill_next += val;
        if(val < 255) {
            os->body_fill_next |= FINFLAG;
            os->clearflag = 1;
            break;
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_sync_destroy() {
    if(s_oggSyncState) {
        ogg_sync_reset();
        ogg_buffer_destroy(s_oggBufferState);
        memset(s_oggSyncState, 0, sizeof(*s_oggSyncState));
        free(s_oggSyncState);
    }
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_stream_destroy(ogg_stream_state_t *os) {
    if(os) {
        ogg_buffer_release(os->header_tail);
        ogg_buffer_release(os->body_tail);
        memset(os, 0, sizeof(*os));
        free(os);
    }
    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
int ogg_stream_reset(ogg_stream_state_t *os) {
    ogg_buffer_release(os->header_tail);
    ogg_buffer_release(os->body_tail);
    os->header_tail = os->header_head = 0;
    os->body_tail = os->body_head = 0;

    os->e_o_s = 0;
    os->b_o_s = 0;
    os->pageno = -1;
    os->packetno = 0;
    os->granulepos = 0;

    os->body_fill = 0;
    os->lacing_fill = 0;

    os->holeflag = 0;
    os->spanflag = 0;
    os->clearflag = 0;
    os->laceptr = 0;
    os->body_fill_next = 0;

    return OGG_SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
void ogg_page_dup(ogg_page *dup, ogg_page *orig) {
    dup->header_len = orig->header_len;
    dup->body_len = orig->body_len;
    dup->header = ogg_buffer_dup(orig->header);
    dup->body = ogg_buffer_dup(orig->body);
}
//---------------------------------------------------------------------------------------------------------------------
ogg_reference_t *ogg_buffer_dup(ogg_reference_t *_or) {
    ogg_reference_t *ret = 0, *head = 0;
    /* duplicate the reference chain; increment refcounts */
    while(_or) {
        ogg_reference_t *temp = _fetch_ref(_or->buffer->owner);
        if(head) head->next = temp;
        else
            ret = temp;
        head = temp;
        head->buffer = _or->buffer;
        head->begin = _or->begin;
        head->length = _or->length;
        _or = _or->next;
    }

    ogg_buffer_mark(ret);
    return ret;
}
