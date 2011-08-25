/**
 * FreeRDP: A Remote Desktop Protocol client.
 * RemoteFX Codec Library
 *
 * Copyright 2011 Vic Lee
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <freerdp/rfx/rfx.h>
#include <freerdp/utils/memory.h>

#include "rfx_constants.h"
#include "rfx_types.h"
#include "rfx_pool.h"
#include "rfx_decode.h"
#include "rfx_encode.h"
#include "rfx_quantization.h"
#include "rfx_dwt.h"

#ifdef WITH_SSE2
#include "rfx_sse2.h"
#endif

#ifdef WITH_NEON
#include "rfx_neon.h"
#endif

#ifndef RFX_INIT_SIMD
#define RFX_INIT_SIMD(_rfx_context) do { } while (0)
#endif

/**
 * The quantization values control the compression rate and quality. The value
 * range is between 6 and 15. The higher value, the higher compression rate
 * and lower quality.
 *
 * This is the default values being use by the MS RDP server, and we will also
 * use it as our default values for the encoder. It can be overrided by setting
 * the context->num_quants and context->quants member.
 *
 * The order of the values are:
 * LL3, LH3, HL3, HH3, LH2, HL2, HH2, LH1, HL1, HH1
 */
static const uint32 rfx_default_quantization_values[] =
{
	6, 6, 6, 6, 7, 7, 8, 8, 8, 9
};

static void rfx_profiler_create(RFX_CONTEXT* context)
{
	PROFILER_CREATE(context->priv->prof_rfx_decode_rgb, "rfx_decode_rgb");
	PROFILER_CREATE(context->priv->prof_rfx_decode_component, "rfx_decode_component");
	PROFILER_CREATE(context->priv->prof_rfx_rlgr_decode, "rfx_rlgr_decode");
	PROFILER_CREATE(context->priv->prof_rfx_differential_decode, "rfx_differential_decode");
	PROFILER_CREATE(context->priv->prof_rfx_quantization_decode, "rfx_quantization_decode");
	PROFILER_CREATE(context->priv->prof_rfx_dwt_2d_decode, "rfx_dwt_2d_decode");
	PROFILER_CREATE(context->priv->prof_rfx_decode_ycbcr_to_rgb, "rfx_decode_ycbcr_to_rgb");
	PROFILER_CREATE(context->priv->prof_rfx_decode_format_rgb, "rfx_decode_format_rgb");

	PROFILER_CREATE(context->priv->prof_rfx_encode_rgb, "rfx_encode_rgb");
	PROFILER_CREATE(context->priv->prof_rfx_encode_component, "rfx_encode_component");
	PROFILER_CREATE(context->priv->prof_rfx_rlgr_encode, "rfx_rlgr_encode");
	PROFILER_CREATE(context->priv->prof_rfx_differential_encode, "rfx_differential_encode");
	PROFILER_CREATE(context->priv->prof_rfx_quantization_encode, "rfx_quantization_encode");
	PROFILER_CREATE(context->priv->prof_rfx_dwt_2d_encode, "rfx_dwt_2d_encode");
	PROFILER_CREATE(context->priv->prof_rfx_encode_rgb_to_ycbcr, "rfx_encode_rgb_to_ycbcr");
	PROFILER_CREATE(context->priv->prof_rfx_encode_format_rgb, "rfx_encode_format_rgb");
}

static void rfx_profiler_free(RFX_CONTEXT* context)
{
	PROFILER_FREE(context->priv->prof_rfx_decode_rgb);
	PROFILER_FREE(context->priv->prof_rfx_decode_component);
	PROFILER_FREE(context->priv->prof_rfx_rlgr_decode);
	PROFILER_FREE(context->priv->prof_rfx_differential_decode);
	PROFILER_FREE(context->priv->prof_rfx_quantization_decode);
	PROFILER_FREE(context->priv->prof_rfx_dwt_2d_decode);
	PROFILER_FREE(context->priv->prof_rfx_decode_ycbcr_to_rgb);
	PROFILER_FREE(context->priv->prof_rfx_decode_format_rgb);

	PROFILER_FREE(context->priv->prof_rfx_encode_rgb);
	PROFILER_FREE(context->priv->prof_rfx_encode_component);
	PROFILER_FREE(context->priv->prof_rfx_rlgr_encode);
	PROFILER_FREE(context->priv->prof_rfx_differential_encode);
	PROFILER_FREE(context->priv->prof_rfx_quantization_encode);
	PROFILER_FREE(context->priv->prof_rfx_dwt_2d_encode);
	PROFILER_FREE(context->priv->prof_rfx_encode_rgb_to_ycbcr);
	PROFILER_FREE(context->priv->prof_rfx_encode_format_rgb);
}

static void rfx_profiler_print(RFX_CONTEXT* context)
{
	PROFILER_PRINT_HEADER;

	PROFILER_PRINT(context->priv->prof_rfx_decode_rgb);
	PROFILER_PRINT(context->priv->prof_rfx_decode_component);
	PROFILER_PRINT(context->priv->prof_rfx_rlgr_decode);
	PROFILER_PRINT(context->priv->prof_rfx_differential_decode);
	PROFILER_PRINT(context->priv->prof_rfx_quantization_decode);
	PROFILER_PRINT(context->priv->prof_rfx_dwt_2d_decode);
	PROFILER_PRINT(context->priv->prof_rfx_decode_ycbcr_to_rgb);
	PROFILER_PRINT(context->priv->prof_rfx_decode_format_rgb);

	PROFILER_PRINT(context->priv->prof_rfx_encode_rgb);
	PROFILER_PRINT(context->priv->prof_rfx_encode_component);
	PROFILER_PRINT(context->priv->prof_rfx_rlgr_encode);
	PROFILER_PRINT(context->priv->prof_rfx_differential_encode);
	PROFILER_PRINT(context->priv->prof_rfx_quantization_encode);
	PROFILER_PRINT(context->priv->prof_rfx_dwt_2d_encode);
	PROFILER_PRINT(context->priv->prof_rfx_encode_rgb_to_ycbcr);
	PROFILER_PRINT(context->priv->prof_rfx_encode_format_rgb);

	PROFILER_PRINT_FOOTER;
}

RFX_CONTEXT* rfx_context_new(void)
{
	RFX_CONTEXT* context;

	context = xnew(RFX_CONTEXT);
	context->priv = xnew(RFX_CONTEXT_PRIV);
	context->priv->pool = rfx_pool_new();

	/* initialize the default pixel format */
	rfx_context_set_pixel_format(context, RFX_PIXEL_FORMAT_BGRA);

	/* align buffers to 16 byte boundary (needed for SSE/SSE2 instructions) */
	context->priv->y_r_buffer = (sint16*)(((uintptr_t)context->priv->y_r_mem + 16) & ~ 0x0F);
	context->priv->cb_g_buffer = (sint16*)(((uintptr_t)context->priv->cb_g_mem + 16) & ~ 0x0F);
	context->priv->cr_b_buffer = (sint16*)(((uintptr_t)context->priv->cr_b_mem + 16) & ~ 0x0F);

	context->priv->dwt_buffer = (sint16*)(((uintptr_t)context->priv->dwt_mem + 16) & ~ 0x0F);

	/* create profilers for default decoding routines */
	rfx_profiler_create(context);
	
	/* set up default routines */
	context->decode_ycbcr_to_rgb = rfx_decode_ycbcr_to_rgb;
	context->encode_rgb_to_ycbcr = rfx_encode_rgb_to_ycbcr;
	context->quantization_decode = rfx_quantization_decode;	
	context->quantization_encode = rfx_quantization_encode;	
	context->dwt_2d_decode = rfx_dwt_2d_decode;
	context->dwt_2d_encode = rfx_dwt_2d_encode;

	/* detect and enable SIMD CPU acceleration */
	RFX_INIT_SIMD(context);

	return context;
}

void rfx_context_free(RFX_CONTEXT* context)
{
	xfree(context->quants);

	rfx_pool_free(context->priv->pool);

	rfx_profiler_print(context);
	rfx_profiler_free(context);

	xfree(context->priv);
	xfree(context);
}

void rfx_context_set_pixel_format(RFX_CONTEXT* context, RFX_PIXEL_FORMAT pixel_format)
{
	context->pixel_format = pixel_format;
	switch (pixel_format)
	{
		case RFX_PIXEL_FORMAT_BGRA:
		case RFX_PIXEL_FORMAT_RGBA:
			context->bytes_per_pixel = 4;
			break;
		case RFX_PIXEL_FORMAT_BGR:
		case RFX_PIXEL_FORMAT_RGB:
			context->bytes_per_pixel = 3;
			break;
		default:
			context->bytes_per_pixel = 0;
			break;
	}
}

static void rfx_process_message_sync(RFX_CONTEXT* context, STREAM* data_in)
{
	uint32 magic;

	/* RFX_SYNC */
	stream_read_uint32(data_in, magic); /* magic (4 bytes), 0xCACCACCA */

	if (magic != WF_MAGIC)
	{
		DEBUG_WARN("invalid magic number 0x%X", magic);
		return;
	}

	stream_read_uint16(data_in, context->version); /* version (2 bytes), WF_VERSION_1_0 (0x0100) */

	if (context->version != WF_VERSION_1_0)
	{
		DEBUG_WARN("unknown version number 0x%X", context->version);
		return;
	}

	DEBUG_RFX("version 0x%X", context->version);
}

static void rfx_process_message_codec_versions(RFX_CONTEXT* context, STREAM* data_in)
{
	uint8 numCodecs;

	stream_read_uint8(data_in, numCodecs); /* numCodecs (1 byte), must be set to 0x01 */

	if (numCodecs != 1)
	{
		DEBUG_WARN("numCodecs: %d, expected:1", numCodecs);
		return;
	}

	/* RFX_CODEC_VERSIONT */
	stream_read_uint8(data_in, context->codec_id); /* codecId (1 byte) */
	stream_read_uint8(data_in, context->codec_version); /* version (2 bytes) */

	DEBUG_RFX("id %d version 0x%X.", context->codec_id, context->codec_version);
}

static void rfx_process_message_channels(RFX_CONTEXT* context, STREAM* data_in)
{
	uint8 channelId;
	uint8 numChannels;

	stream_read_uint8(data_in, numChannels); /* numChannels (1 byte), must bet set to 0x01 */

	if (numChannels != 1)
	{
		DEBUG_WARN("numChannels:%d, expected:1", numChannels);
		return;
	}

	/* RFX_CHANNELT */
	stream_read_uint8(data_in, channelId); /* channelId (1 byte) */
	stream_read_uint16(data_in, context->width); /* width (2 bytes) */
	stream_read_uint16(data_in, context->height); /* height (2 bytes) */

	DEBUG_RFX("numChannels %d id %d, %dx%d.",
		numChannels, channelId, context->width, context->height);
}

static void rfx_process_message_context(RFX_CONTEXT* context, STREAM* data_in)
{
	uint8 ctxId;
	uint16 tileSize;
	uint16 properties;

	stream_read_uint8(data_in, ctxId); /* ctxId (1 byte), must be set to 0x00 */
	stream_read_uint16(data_in, tileSize); /* tileSize (2 bytes), must be set to CT_TILE_64x64 (0x0040) */
	stream_read_uint16(data_in, properties); /* properties (2 bytes) */

	DEBUG_RFX("ctxId %d tileSize %d properties 0x%X.", ctxId, tileSize, properties);

	context->properties = properties;
	context->flags = (properties & 0x0007);

	if (context->flags == CODEC_MODE)
		DEBUG_RFX("codec is in image mode.");
	else
		DEBUG_RFX("codec is in video mode.");

	switch ((properties & 0x1E00) >> 9)
	{
		case CLW_ENTROPY_RLGR1:
			context->mode = RLGR1;
			DEBUG_RFX("RLGR1.");
			break;

		case CLW_ENTROPY_RLGR3:
			context->mode = RLGR3;
			DEBUG_RFX("RLGR3.");
			break;

		default:
			DEBUG_WARN("unknown RLGR algorithm.");
			break;
	}
}

static void rfx_process_message_frame_begin(RFX_CONTEXT* context, RFX_MESSAGE* message, STREAM* data_in)
{
	uint32 frameIdx;
	uint16 numRegions;

	stream_read_uint32(data_in, frameIdx); /* frameIdx (4 bytes), if codec is in video mode, must be ignored */
	stream_read_uint16(data_in, numRegions); /* numRegions (2 bytes) */

	DEBUG_RFX("RFX_FRAME_BEGIN: frameIdx:%d numRegions:%d", frameIdx, numRegions);
}

static void rfx_process_message_frame_end(RFX_CONTEXT* context, RFX_MESSAGE* message, STREAM* data_in)
{
	DEBUG_RFX("RFX_FRAME_END");
}

static void rfx_process_message_region(RFX_CONTEXT* context, RFX_MESSAGE* message, STREAM* data_in)
{
	int i;

	stream_seek_uint8(data_in); /* regionFlags (1 byte) */
	stream_read_uint16(data_in, message->num_rects); /* numRects (2 bytes) */

	if (message->num_rects < 1)
	{
		DEBUG_WARN("no rects.");
		return;
	}

	if (message->rects != NULL)
		message->rects = (RFX_RECT*) xrealloc(message->rects, message->num_rects * sizeof(RFX_RECT));
	else
		message->rects = (RFX_RECT*) xmalloc(message->num_rects * sizeof(RFX_RECT));

	/* rects */
	for (i = 0; i < message->num_rects; i++)
	{
		/* RFX_RECT */
		stream_read_uint16(data_in, message->rects[i].x); /* x (2 bytes) */
		stream_read_uint16(data_in, message->rects[i].y); /* y (2 bytes) */
		stream_read_uint16(data_in, message->rects[i].width); /* width (2 bytes) */
		stream_read_uint16(data_in, message->rects[i].height); /* height (2 bytes) */

		DEBUG_RFX("rect %d (%d %d %d %d).",
			i, message->rects[i].x, message->rects[i].y, message->rects[i].width, message->rects[i].height);
	}
}

static void rfx_process_message_tile(RFX_CONTEXT* context, RFX_TILE* tile, STREAM* data_in)
{
	uint8 quantIdxY;
	uint8 quantIdxCb;
	uint8 quantIdxCr;
	uint16 xIdx, yIdx;
	uint16 YLen, CbLen, CrLen;

	/* RFX_TILE */
	stream_read_uint8(data_in, quantIdxY); /* quantIdxY (1 byte) */
	stream_read_uint8(data_in, quantIdxCb); /* quantIdxCb (1 byte) */
	stream_read_uint8(data_in, quantIdxCr); /* quantIdxCr (1 byte) */
	stream_read_uint16(data_in, xIdx); /* xIdx (2 bytes) */
	stream_read_uint16(data_in, yIdx); /* yIdx (2 bytes) */
	stream_read_uint16(data_in, YLen); /* YLen (2 bytes) */
	stream_read_uint16(data_in, CbLen); /* CbLen (2 bytes) */
	stream_read_uint16(data_in, CrLen); /* CrLen (2 bytes) */

	DEBUG_RFX("quantIdxY:%d quantIdxCb:%d quantIdxCr:%d xIdx:%d yIdx:%d YLen:%d CbLen:%d CrLen:%d",
		quantIdxY, quantIdxCb, quantIdxCr, xIdx, yIdx, YLen, CbLen, CrLen);

	tile->x = xIdx * 64;
	tile->y = yIdx * 64;

	rfx_decode_rgb(context, data_in,
		YLen, context->quants + (quantIdxY * 10),
		CbLen, context->quants + (quantIdxCb * 10),
		CrLen, context->quants + (quantIdxCr * 10),
		tile->data);
}

static void rfx_process_message_tileset(RFX_CONTEXT* context, RFX_MESSAGE* message, STREAM* data_in)
{
	int i;
	uint16 subtype;
	uint32 blockLen;
	uint32 blockType;
	uint32 tilesDataSize;
	uint32* quants;
	uint8 quant;
	int pos;

	stream_read_uint16(data_in, subtype); /* subtype (2 bytes) must be set to CBT_TILESET (0xCAC2) */

	if (subtype != CBT_TILESET)
	{
		DEBUG_WARN("invalid subtype, expected CBT_TILESET.");
		return;
	}

	stream_seek_uint16(data_in); /* idx (2 bytes), must be set to 0x0000 */
	stream_seek_uint16(data_in); /* properties (2 bytes) */

	stream_read_uint8(data_in, context->num_quants); /* numQuant (1 byte) */
	stream_seek_uint8(data_in); /* tileSize (1 byte), must be set to 0x40 */

	if (context->num_quants < 1)
	{
		DEBUG_WARN("no quantization value.");
		return;
	}

	stream_read_uint16(data_in, message->num_tiles); /* numTiles (2 bytes) */

	if (message->num_tiles < 1)
	{
		DEBUG_WARN("no tiles.");
		return;
	}

	stream_read_uint32(data_in, tilesDataSize); /* tilesDataSize (4 bytes) */

	if (context->quants != NULL)
		context->quants = (uint32*) xrealloc((void*) context->quants, context->num_quants * 10 * sizeof(uint32));
	else
		context->quants = (uint32*) xmalloc(context->num_quants * 10 * sizeof(uint32));
	quants = context->quants;

	/* quantVals */
	for (i = 0; i < context->num_quants; i++)
	{
		/* RFX_CODEC_QUANT */
		stream_read_uint8(data_in, quant);
		*quants++ = (quant & 0x0F);
		*quants++ = (quant >> 4);
		stream_read_uint8(data_in, quant);
		*quants++ = (quant & 0x0F);
		*quants++ = (quant >> 4);
		stream_read_uint8(data_in, quant);
		*quants++ = (quant & 0x0F);
		*quants++ = (quant >> 4);
		stream_read_uint8(data_in, quant);
		*quants++ = (quant & 0x0F);
		*quants++ = (quant >> 4);
		stream_read_uint8(data_in, quant);
		*quants++ = (quant & 0x0F);
		*quants++ = (quant >> 4);

		DEBUG_RFX("quant %d (%d %d %d %d %d %d %d %d %d %d).",
			i, context->quants[i * 10], context->quants[i * 10 + 1],
			context->quants[i * 10 + 2], context->quants[i * 10 + 3],
			context->quants[i * 10 + 4], context->quants[i * 10 + 5],
			context->quants[i * 10 + 6], context->quants[i * 10 + 7],
			context->quants[i * 10 + 8], context->quants[i * 10 + 9]);
	}

	message->tiles = rfx_pool_get_tiles(context->priv->pool, message->num_tiles);

	/* tiles */
	for (i = 0; i < message->num_tiles; i++)
	{
		/* RFX_TILE */
		stream_read_uint16(data_in, blockType); /* blockType (2 bytes), must be set to CBT_TILE (0xCAC3) */
		stream_read_uint32(data_in, blockLen); /* blockLen (4 bytes) */

		pos = stream_get_pos(data_in) - 6 + blockLen;

		if (blockType != CBT_TILE)
		{
			DEBUG_WARN("unknown block type 0x%X, expected CBT_TILE (0xCAC3).", blockType);
			break;
		}

		rfx_process_message_tile(context, message->tiles[i], data_in);

		stream_set_pos(data_in, pos);
	}
}

RFX_MESSAGE* rfx_process_message(RFX_CONTEXT* context, STREAM* data_in)
{
	uint32 blockLen;
	uint32 blockType;
	RFX_MESSAGE* message;
	int pos;

	message = xnew(RFX_MESSAGE);

	while (stream_get_left(data_in) > 6)
	{
		/* RFX_BLOCKT */
		stream_read_uint16(data_in, blockType); /* blockType (2 bytes) */
		stream_read_uint32(data_in, blockLen); /* blockLen (4 bytes) */

		DEBUG_RFX("blockType 0x%X blockLen %d", blockType, blockLen);

		if (blockLen == 0)
		{
			DEBUG_WARN("zero blockLen");
			break;
		}

		pos = stream_get_pos(data_in) - 6 + blockLen;

		if (blockType >= WBT_CONTEXT && blockType <= WBT_EXTENSION)
		{
			/* RFX_CODEC_CHANNELT */
			/* codecId (1 byte) must be set to 0x01 */
			/* channelId (1 byte) must be set to 0x00 */
			stream_seek(data_in, 2);
		}

		switch (blockType)
		{
			case WBT_SYNC:
				rfx_process_message_sync(context, data_in);
				break;

			case WBT_CODEC_VERSIONS:
				rfx_process_message_codec_versions(context, data_in);
				break;

			case WBT_CHANNELS:
				rfx_process_message_channels(context, data_in);
				break;

			case WBT_CONTEXT:
				rfx_process_message_context(context, data_in);
				break;

			case WBT_FRAME_BEGIN:
				rfx_process_message_frame_begin(context, message, data_in);
				break;

			case WBT_FRAME_END:
				rfx_process_message_frame_end(context, message, data_in);
				break;

			case WBT_REGION:
				rfx_process_message_region(context, message, data_in);
				break;

			case WBT_EXTENSION:
				rfx_process_message_tileset(context, message, data_in);
				break;

			default:
				DEBUG_WARN("unknown blockType 0x%X", blockType);
				break;
		}

		stream_set_pos(data_in, pos);
	}

	return message;
}

void rfx_message_free(RFX_CONTEXT* context, RFX_MESSAGE* message)
{
	if (message != NULL)
	{
		xfree(message->rects);

		if (message->tiles != NULL)
		{
			rfx_pool_put_tiles(context->priv->pool, message->tiles, message->num_tiles);
			xfree(message->tiles);
		}

		xfree(message);
	}
}

static void rfx_compose_message_sync(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_write_uint16(data_out, WBT_SYNC); /* BlockT.blockType */
	stream_write_uint32(data_out, 12); /* BlockT.blockLen */
	stream_write_uint32(data_out, WF_MAGIC); /* magic */
	stream_write_uint16(data_out, WF_VERSION_1_0); /* version */
}

static void rfx_compose_message_codec_versions(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_write_uint16(data_out, WBT_CODEC_VERSIONS); /* BlockT.blockType */
	stream_write_uint32(data_out, 10); /* BlockT.blockLen */
	stream_write_uint8(data_out, 1); /* numCodecs */
	stream_write_uint8(data_out, 1); /* codecs.codecId */
	stream_write_uint16(data_out, WF_VERSION_1_0); /* codecs.version */
}

static void rfx_compose_message_channels(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_write_uint16(data_out, WBT_CHANNELS); /* BlockT.blockType */
	stream_write_uint32(data_out, 12); /* BlockT.blockLen */
	stream_write_uint8(data_out, 1); /* numChannels */
	stream_write_uint8(data_out, 0); /* Channel.channelId */
	stream_write_uint16(data_out, context->width); /* Channel.width */
	stream_write_uint16(data_out, context->height); /* Channel.height */
}

static void rfx_compose_message_context(RFX_CONTEXT* context, STREAM* data_out)
{
	uint16 properties;

	stream_write_uint16(data_out, WBT_CONTEXT); /* CodecChannelT.blockType */
	stream_write_uint32(data_out, 13); /* CodecChannelT.blockLen */
	stream_write_uint8(data_out, 1); /* CodecChannelT.codecId */
	stream_write_uint8(data_out, 0); /* CodecChannelT.channelId */
	stream_write_uint8(data_out, 0); /* ctxId */
	stream_write_uint16(data_out, CT_TILE_64x64); /* tileSize */

	/* properties */
	properties = context->flags; /* flags */
	properties |= (COL_CONV_ICT << 3); /* cct */
	properties |= (CLW_XFORM_DWT_53_A << 5); /* xft */
	properties |= ((context->mode == RLGR1 ? CLW_ENTROPY_RLGR1 : CLW_ENTROPY_RLGR3) << 9); /* et */
	properties |= (SCALAR_QUANTIZATION << 13); /* qt */
	stream_write_uint16(data_out, properties);

	/* properties in tilesets: note that this has different format from the one in TS_RFX_CONTEXT */
	properties = 1; /* lt */
	properties |= (context->flags << 1); /* flags */
	properties |= (COL_CONV_ICT << 4); /* cct */
	properties |= (CLW_XFORM_DWT_53_A << 6); /* xft */
	properties |= ((context->mode == RLGR1 ? CLW_ENTROPY_RLGR1 : CLW_ENTROPY_RLGR3) << 10); /* et */
	properties |= (SCALAR_QUANTIZATION << 14); /* qt */
	context->properties = properties;
}

void rfx_compose_message_header(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_check_size(data_out, 12 + 10 + 12 + 13);

	rfx_compose_message_sync(context, data_out);
	rfx_compose_message_context(context, data_out);
	rfx_compose_message_codec_versions(context, data_out);
	rfx_compose_message_channels(context, data_out);
}

static void rfx_compose_message_frame_begin(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_check_size(data_out, 14);

	stream_write_uint16(data_out, WBT_FRAME_BEGIN); /* CodecChannelT.blockType */
	stream_write_uint32(data_out, 14); /* CodecChannelT.blockLen */
	stream_write_uint8(data_out, 1); /* CodecChannelT.codecId */
	stream_write_uint8(data_out, 0); /* CodecChannelT.channelId */
	stream_write_uint32(data_out, context->frame_idx); /* frameIdx */
	stream_write_uint16(data_out, 1); /* numRegions */

	context->frame_idx++;
}

static void rfx_compose_message_region(RFX_CONTEXT* context, STREAM* data_out,
	const RFX_RECT* rects, int num_rects)
{
	int size;
	int i;

	size = 15 + num_rects * 8;
	stream_check_size(data_out, size);

	stream_write_uint16(data_out, WBT_REGION); /* CodecChannelT.blockType */
	stream_write_uint32(data_out, size); /* set CodecChannelT.blockLen later */
	stream_write_uint8(data_out, 1); /* CodecChannelT.codecId */
	stream_write_uint8(data_out, 0); /* CodecChannelT.channelId */
	stream_write_uint8(data_out, 1); /* regionFlags */
	stream_write_uint16(data_out, num_rects); /* numRects */

	for (i = 0; i < num_rects; i++)
	{
		stream_write_uint16(data_out, rects[i].x);
		stream_write_uint16(data_out, rects[i].y);
		stream_write_uint16(data_out, rects[i].width);
		stream_write_uint16(data_out, rects[i].height);
	}

	stream_write_uint16(data_out, CBT_REGION); /* regionType */
	stream_write_uint16(data_out, 1); /* numTilesets */
}

static void rfx_compose_message_tile(RFX_CONTEXT* context, STREAM* data_out,
	uint8* tile_data, int tile_width, int tile_height, int rowstride,
	const uint32* quantVals, int quantIdxY, int quantIdxCb, int quantIdxCr,
	int xIdx, int yIdx)
{
	int YLen = 0;
	int CbLen = 0;
	int CrLen = 0;
	int start_pos, end_pos;

	stream_check_size(data_out, 19);
	start_pos = stream_get_pos(data_out);

	stream_write_uint16(data_out, CBT_TILE); /* BlockT.blockType */
	stream_seek_uint32(data_out); /* set BlockT.blockLen later */
	stream_write_uint8(data_out, quantIdxY);
	stream_write_uint8(data_out, quantIdxCb);
	stream_write_uint8(data_out, quantIdxCr);
	stream_write_uint16(data_out, xIdx);
	stream_write_uint16(data_out, yIdx);

	stream_seek(data_out, 6); /* YLen, CbLen, CrLen */

	rfx_encode_rgb(context, tile_data, tile_width, tile_height, rowstride,
		quantVals + quantIdxY * 10, quantVals + quantIdxCb * 10, quantVals + quantIdxCr * 10,
		data_out, &YLen, &CbLen, &CrLen);

	DEBUG_RFX("xIdx=%d yIdx=%d width=%d height=%d YLen=%d CbLen=%d CrLen=%d",
		xIdx, yIdx, tile_width, tile_height, YLen, CbLen, CrLen);

	end_pos = stream_get_pos(data_out);

	stream_set_pos(data_out, start_pos + 2);
	stream_write_uint32(data_out, 19 + YLen + CbLen + CrLen); /* BlockT.blockLen */
	stream_set_pos(data_out, start_pos + 13);
	stream_write_uint16(data_out, YLen);
	stream_write_uint16(data_out, CbLen);
	stream_write_uint16(data_out, CrLen);

	stream_set_pos(data_out, end_pos);
}

static void rfx_compose_message_tileset(RFX_CONTEXT* context, STREAM* data_out,
	uint8* image_data, int width, int height, int rowstride)
{
	int size;
	int start_pos, end_pos;
	int i;
	int numQuants;
	const uint32* quantVals;
	const uint32* quantValsPtr;
	int quantIdxY;
	int quantIdxCb;
	int quantIdxCr;
	int numTiles;
	int numTilesX;
	int numTilesY;
	int xIdx;
	int yIdx;
	int tilesDataSize;

	if (context->num_quants == 0)
	{
		numQuants = 1;
		quantVals = rfx_default_quantization_values;
		quantIdxY = 0;
		quantIdxCb = 0;
		quantIdxCr = 0;
	}
	else
	{
		numQuants = context->num_quants;
		quantVals = context->quants;
		quantIdxY = context->quant_idx_y;
		quantIdxCb = context->quant_idx_cb;
		quantIdxCr = context->quant_idx_cr;
	}

	numTilesX = (width + 63) / 64;
	numTilesY = (height + 63) / 64;
	numTiles = numTilesX * numTilesY;

	size = 22 + numQuants * 5;
	stream_check_size(data_out, size);
	start_pos = stream_get_pos(data_out);

	stream_write_uint16(data_out, WBT_EXTENSION); /* CodecChannelT.blockType */
	stream_seek_uint32(data_out); /* set CodecChannelT.blockLen later */
	stream_write_uint8(data_out, 1); /* CodecChannelT.codecId */
	stream_write_uint8(data_out, 0); /* CodecChannelT.channelId */
	stream_write_uint16(data_out, CBT_TILESET); /* subtype */
	stream_write_uint16(data_out, 0); /* idx */
	stream_write_uint16(data_out, context->properties); /* properties */
	stream_write_uint8(data_out, numQuants); /* numQuants */
	stream_write_uint8(data_out, 0x40); /* tileSize */
	stream_write_uint16(data_out, numTiles); /* numTiles */
	stream_seek_uint32(data_out); /* set tilesDataSize later */

	quantValsPtr = quantVals;
	for (i = 0; i < numQuants * 5; i++)
	{
		stream_write_uint8(data_out, quantValsPtr[0] + (quantValsPtr[1] << 4));
		quantValsPtr += 2;
	}

	DEBUG_RFX("width:%d height:%d rowstride:%d", width, height, rowstride);

	end_pos = stream_get_pos(data_out);
	for (yIdx = 0; yIdx < numTilesY; yIdx++)
	{
		for (xIdx = 0; xIdx < numTilesX; xIdx++)
		{
			rfx_compose_message_tile(context, data_out,
				image_data + yIdx * 64 * rowstride + xIdx * 64 * context->bytes_per_pixel,
				xIdx < numTilesX - 1 ? 64 : width - xIdx * 64,
				yIdx < numTilesY - 1 ? 64 : height - yIdx * 64,
				rowstride, quantVals, quantIdxY, quantIdxCb, quantIdxCr, xIdx, yIdx);
		}
	}
	tilesDataSize = stream_get_pos(data_out) - end_pos;
	size += tilesDataSize;
	end_pos = stream_get_pos(data_out);

	stream_set_pos(data_out, start_pos + 2);
	stream_write_uint32(data_out, size); /* CodecChannelT.blockLen */
	stream_set_pos(data_out, start_pos + 18);
	stream_write_uint32(data_out, tilesDataSize);

	stream_set_pos(data_out, end_pos);
}

static void rfx_compose_message_frame_end(RFX_CONTEXT* context, STREAM* data_out)
{
	stream_check_size(data_out, 8);

	stream_write_uint16(data_out, WBT_FRAME_END); /* CodecChannelT.blockType */
	stream_write_uint32(data_out, 8); /* CodecChannelT.blockLen */
	stream_write_uint8(data_out, 1); /* CodecChannelT.codecId */
	stream_write_uint8(data_out, 0); /* CodecChannelT.channelId */
}

void rfx_compose_message_data(RFX_CONTEXT* context, STREAM* data_out,
	const RFX_RECT* rects, int num_rects, uint8* image_data, int width, int height, int rowstride)
{
	rfx_compose_message_frame_begin(context, data_out);
	rfx_compose_message_region(context, data_out, rects, num_rects);
	rfx_compose_message_tileset(context, data_out, image_data, width, height, rowstride);
	rfx_compose_message_frame_end(context, data_out);
}
