/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2002  Josh Coalson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h> /* for malloc() */
#include <string.h> /* for memcpy() */
#include "FLAC/assert.h"
#include "protected/file_encoder.h"
#include "protected/seekable_stream_encoder.h"

/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_(FLAC__FileEncoder *encoder);
static FLAC__SeekableStreamEncoderSeekStatus seek_callback_(const FLAC__SeekableStreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamEncoderWriteStatus write_callback_(const FLAC__SeekableStreamEncoder *encoder, const FLAC__byte buffer[], unsigned bytes, unsigned samples, unsigned current_frame, void *client_data);

/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct FLAC__FileEncoderPrivate {
	FILE *file;
	char *filename;
	FLAC__SeekableStreamEncoder *seekable_stream_encoder;
} FLAC__FileEncoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

const char * const FLAC__FileEncoderStateString[] = {
	"FLAC__FILE_ENCODER_OK",
	"FLAC__FILE_ENCODER_NO_FILENAME",
	"FLAC__FILE_ENCODER_SEEKABLE_STREAM_ENCODER_ERROR",
	"FLAC__FILE_ENCODER_FATAL_ERROR_WHILE_WRITING",
	"FLAC__FILE_ENCODER_ERROR_OPENING_FILE",
	"FLAC__FILE_ENCODER_MEMORY_ALLOCATION_ERROR",
	"FLAC__FILE_ENCODER_ALREADY_INITIALIZED",
	"FLAC__FILE_ENCODER_UNINITIALIZED"
};


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

FLAC__FileEncoder *FLAC__file_encoder_new()
{
	FLAC__FileEncoder *encoder;

	FLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	encoder = (FLAC__FileEncoder*)malloc(sizeof(FLAC__FileEncoder));
	if(encoder == 0) {
		return 0;
	}
	encoder->protected_ = (FLAC__FileEncoderProtected*)malloc(sizeof(FLAC__FileEncoderProtected));
	if(encoder->protected_ == 0) {
		free(encoder);
		return 0;
	}
	encoder->private_ = (FLAC__FileEncoderPrivate*)malloc(sizeof(FLAC__FileEncoderPrivate));
	if(encoder->private_ == 0) {
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->seekable_stream_encoder = FLAC__seekable_stream_encoder_new();

	if(0 == encoder->private_->seekable_stream_encoder) {
		free(encoder->private_);
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->file = 0;

	set_defaults_(encoder);

	encoder->protected_->state = FLAC__FILE_ENCODER_UNINITIALIZED;

	return encoder;
}

void FLAC__file_encoder_delete(FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);

	(void)FLAC__file_encoder_finish(encoder);

	FLAC__seekable_stream_encoder_delete(encoder->private_->seekable_stream_encoder);

	free(encoder->private_);
	free(encoder->protected_);
	free(encoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

FLAC__FileEncoderState FLAC__file_encoder_init(FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return encoder->protected_->state = FLAC__FILE_ENCODER_ALREADY_INITIALIZED;

	if(0 == encoder->private_->filename)
		return encoder->protected_->state = FLAC__FILE_ENCODER_NO_FILENAME;

	encoder->private_->file = fopen(encoder->private_->filename, "w+b");

	if(encoder->private_->file == 0)
		return encoder->protected_->state = FLAC__FILE_ENCODER_ERROR_OPENING_FILE;

	FLAC__seekable_stream_encoder_set_seek_callback(encoder->private_->seekable_stream_encoder, seek_callback_);
	FLAC__seekable_stream_encoder_set_write_callback(encoder->private_->seekable_stream_encoder, write_callback_);
	FLAC__seekable_stream_encoder_set_client_data(encoder->private_->seekable_stream_encoder, encoder);

	if(FLAC__seekable_stream_encoder_init(encoder->private_->seekable_stream_encoder) != FLAC__SEEKABLE_STREAM_ENCODER_OK)
		return encoder->protected_->state = FLAC__FILE_ENCODER_SEEKABLE_STREAM_ENCODER_ERROR;

	return encoder->protected_->state = FLAC__FILE_ENCODER_OK;
}

void FLAC__file_encoder_finish(FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state == FLAC__FILE_ENCODER_UNINITIALIZED)
		return;

	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);

	/* FLAC__seekable_stream_encoder_finish() might write data so we must close the file after it. */

	FLAC__seekable_stream_encoder_finish(encoder->private_->seekable_stream_encoder);

	if(0 != encoder->private_->file) {
		fclose(encoder->private_->file);
		encoder->private_->file = 0;
	}

	if(0 != encoder->private_->filename) {
		free(encoder->private_->filename);
		encoder->private_->filename = 0;
	}

	set_defaults_(encoder);

	encoder->protected_->state = FLAC__FILE_ENCODER_UNINITIALIZED;
}

FLAC__bool FLAC__file_encoder_set_streamable_subset(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_streamable_subset(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_do_mid_side_stereo(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_do_mid_side_stereo(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_loose_mid_side_stereo(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_loose_mid_side_stereo(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_channels(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_channels(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_bits_per_sample(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_bits_per_sample(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_sample_rate(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_sample_rate(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_blocksize(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_blocksize(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_max_lpc_order(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_max_lpc_order(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_qlp_coeff_precision(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_qlp_coeff_precision(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_do_qlp_coeff_prec_search(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_do_qlp_coeff_prec_search(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_do_escape_coding(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_do_escape_coding(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_do_exhaustive_model_search(FLAC__FileEncoder *encoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_do_exhaustive_model_search(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_min_residual_partition_order(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_min_residual_partition_order(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_max_residual_partition_order(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_max_residual_partition_order(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_rice_parameter_search_dist(FLAC__FileEncoder *encoder, unsigned value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_rice_parameter_search_dist(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_total_samples_estimate(FLAC__FileEncoder *encoder, FLAC__uint64 value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_total_samples_estimate(encoder->private_->seekable_stream_encoder, value);
}

FLAC__bool FLAC__file_encoder_set_metadata(FLAC__FileEncoder *encoder, FLAC__StreamMetadata **metadata, unsigned num_blocks)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != encoder->private_->seekable_stream_encoder);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	return FLAC__seekable_stream_encoder_set_metadata(encoder->private_->seekable_stream_encoder, metadata, num_blocks);
}

FLAC__bool FLAC__file_encoder_set_filename(FLAC__FileEncoder *encoder, const char *value)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	FLAC__ASSERT(0 != encoder->protected_);
	FLAC__ASSERT(0 != value);
	if(encoder->protected_->state != FLAC__FILE_ENCODER_UNINITIALIZED)
		return false;
	if(0 != encoder->private_->filename) {
		free(encoder->private_->filename);
		encoder->private_->filename = 0;
	}
	if(0 == (encoder->private_->filename = (char*)malloc(strlen(value)+1))) {
		encoder->protected_->state = FLAC__FILE_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	strcpy(encoder->private_->filename, value);
	return true;
}

FLAC__FileEncoderState FLAC__file_encoder_get_state(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->state;
}

FLAC__SeekableStreamEncoderState FLAC__file_encoder_get_seekable_stream_encoder_state(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_state(encoder->private_->seekable_stream_encoder);
}

FLAC__StreamEncoderState FLAC__file_encoder_get_stream_encoder_state(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_stream_encoder_state(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_streamable_subset(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_streamable_subset(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_do_mid_side_stereo(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_do_mid_side_stereo(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_loose_mid_side_stereo(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_loose_mid_side_stereo(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_channels(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_channels(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_bits_per_sample(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_bits_per_sample(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_sample_rate(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_sample_rate(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_blocksize(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_blocksize(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_max_lpc_order(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_max_lpc_order(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_qlp_coeff_precision(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_qlp_coeff_precision(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_do_qlp_coeff_prec_search(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_do_qlp_coeff_prec_search(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_do_escape_coding(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_do_escape_coding(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_get_do_exhaustive_model_search(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_do_exhaustive_model_search(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_min_residual_partition_order(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_min_residual_partition_order(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_max_residual_partition_order(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_max_residual_partition_order(encoder->private_->seekable_stream_encoder);
}

unsigned FLAC__file_encoder_get_rice_parameter_search_dist(const FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_get_rice_parameter_search_dist(encoder->private_->seekable_stream_encoder);
}

FLAC__bool FLAC__file_encoder_process(FLAC__FileEncoder *encoder, const FLAC__int32 * const buffer[], unsigned samples)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_process(encoder->private_->seekable_stream_encoder, buffer, samples);
}

/* 'samples' is channel-wide samples, e.g. for 1 second at 44100Hz, 'samples' = 44100 regardless of the number of channels */
FLAC__bool FLAC__file_encoder_process_interleaved(FLAC__FileEncoder *encoder, const FLAC__int32 buffer[], unsigned samples)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);
	return FLAC__seekable_stream_encoder_process_interleaved(encoder->private_->seekable_stream_encoder, buffer, samples);
}


/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_(FLAC__FileEncoder *encoder)
{
	FLAC__ASSERT(0 != encoder);
	FLAC__ASSERT(0 != encoder->private_);

	encoder->private_->filename = 0;
}

FLAC__SeekableStreamEncoderSeekStatus seek_callback_(const FLAC__SeekableStreamEncoder *encoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	FLAC__FileEncoder *file_encoder = (FLAC__FileEncoder*)client_data;

	(void)encoder;

	FLAC__ASSERT(0 != file_encoder);

	if(fseek(file_encoder->private_->file, absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__SEEKABLE_STREAM_ENCODER_SEEK_STATUS_ERROR;
	else
		return FLAC__SEEKABLE_STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderWriteStatus write_callback_(const FLAC__SeekableStreamEncoder *encoder, const FLAC__byte buffer[], unsigned bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	FLAC__FileEncoder *file_encoder = (FLAC__FileEncoder*)client_data;

	(void)encoder, (void)samples, (void)current_frame;

	FLAC__ASSERT(0 != file_encoder);

	if(fwrite(buffer, sizeof(FLAC__byte), bytes, file_encoder->private_->file) == bytes)
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	else
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}