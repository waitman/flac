/* metaflac - Command-line FLAC metadata editor
 * Copyright (C) 2001-2009  Josh Coalson
 * Copyright (C) 2011-2014  Xiph.Org Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "operations.h"
#include "usage.h"
#include "utils.h"
#include "FLAC/assert.h"
#include "FLAC/metadata.h"
#include "share/alloc.h"
#include "share/grabbag.h"
#include "share/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "operations_shorthand.h"
#include "share/json-c/json.h"

static void show_version(void);
static FLAC__bool do_major_operation(const CommandLineOptions *options);
static FLAC__bool do_major_operation_on_file(const char *filename, const CommandLineOptions *options);
static FLAC__bool do_major_operation__list(const char *filename, FLAC__Metadata_Chain *chain, const CommandLineOptions *options);
static FLAC__bool do_major_operation__append(FLAC__Metadata_Chain *chain, const CommandLineOptions *options);
static FLAC__bool do_major_operation__remove(FLAC__Metadata_Chain *chain, const CommandLineOptions *options);
static FLAC__bool do_major_operation__remove_all(FLAC__Metadata_Chain *chain, const CommandLineOptions *options);
static FLAC__bool do_shorthand_operations(const CommandLineOptions *options);
static FLAC__bool do_shorthand_operations_on_file(const char *filename, const CommandLineOptions *options);
static FLAC__bool do_shorthand_operation(const char *filename, FLAC__bool prefix_with_filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write, FLAC__bool utf8_convert);
static FLAC__bool do_shorthand_operation__add_replay_gain(char **filenames, unsigned num_files, FLAC__bool preserve_modtime);
static FLAC__bool do_shorthand_operation__add_padding(const char *filename, FLAC__Metadata_Chain *chain, unsigned length, FLAC__bool *needs_write);

static FLAC__bool passes_filter(const CommandLineOptions *options, const FLAC__StreamMetadata *block, unsigned block_number);
static void write_metadata(const char *filename, FLAC__StreamMetadata *block, unsigned block_number, FLAC__bool raw, FLAC__bool hexdump_application);
static json_object * write_metadata_json(FLAC__StreamMetadata *block, unsigned block_number); /* mod wcg */

/* from operations_shorthand_seektable.c */
extern FLAC__bool do_shorthand_operation__add_seekpoints(const char *filename, FLAC__Metadata_Chain *chain, const char *specification, FLAC__bool *needs_write);

/* from operations_shorthand_streaminfo.c */
extern FLAC__bool do_shorthand_operation__streaminfo(const char *filename, FLAC__bool prefix_with_filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write);

/* from operations_shorthand_vorbiscomment.c */
extern FLAC__bool do_shorthand_operation__vorbis_comment(const char *filename, FLAC__bool prefix_with_filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write, FLAC__bool raw);

/* from operations_shorthand_cuesheet.c */
extern FLAC__bool do_shorthand_operation__cuesheet(const char *filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write);

/* from operations_shorthand_picture.c */
extern FLAC__bool do_shorthand_operation__picture(const char *filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write);


FLAC__bool do_operations(const CommandLineOptions *options)
{
	FLAC__bool ok = true;

	if(options->show_long_help) {
		long_usage(0);
	}
	if(options->show_version) {
		show_version();
	}
	else if(options->args.checks.num_major_ops > 0) {
		FLAC__ASSERT(options->args.checks.num_shorthand_ops == 0);
		FLAC__ASSERT(options->args.checks.num_major_ops == 1);
		FLAC__ASSERT(options->args.checks.num_major_ops == options->ops.num_operations);
		ok = do_major_operation(options);
	}
	else if(options->args.checks.num_shorthand_ops > 0) {
		FLAC__ASSERT(options->args.checks.num_shorthand_ops == options->ops.num_operations);
		ok = do_shorthand_operations(options);
	}

	return ok;
}

/*
 * local routines
 */

void show_version(void)
{
	printf("metaflac %s\n", FLAC__VERSION_STRING);
}

FLAC__bool do_major_operation(const CommandLineOptions *options)
{
	unsigned i;
	FLAC__bool ok = true;

	/* to die after first error,     v---  add '&& ok' here */
	for(i = 0; i < options->num_files; i++)
		ok &= do_major_operation_on_file(options->filenames[i], options);

	return ok;
}

FLAC__bool do_major_operation_on_file(const char *filename, const CommandLineOptions *options)
{
	FLAC__bool ok = true, needs_write = false, is_ogg = false;
	FLAC__Metadata_Chain *chain = FLAC__metadata_chain_new();

	if(0 == chain)
		die("out of memory allocating chain");

	/*@@@@ lame way of guessing the file type */
	if(strlen(filename) >= 4 && (0 == strcmp(filename+strlen(filename)-4, ".oga") || 0 == strcmp(filename+strlen(filename)-4, ".ogg")))
		is_ogg = true;

	if(! (is_ogg? FLAC__metadata_chain_read_ogg(chain, filename) : FLAC__metadata_chain_read(chain, filename)) ) {
		print_error_with_chain_status(chain, "%s: ERROR: reading metadata", filename);
		FLAC__metadata_chain_delete(chain);
		return false;
	}

	switch(options->ops.operations[0].type) {
		case OP__LIST:
			ok = do_major_operation__list(options->prefix_with_filename? filename : 0, chain, options);
			break;
		case OP__APPEND:
			ok = do_major_operation__append(chain, options);
			needs_write = true;
			break;
		case OP__REMOVE:
			ok = do_major_operation__remove(chain, options);
			needs_write = true;
			break;
		case OP__REMOVE_ALL:
			ok = do_major_operation__remove_all(chain, options);
			needs_write = true;
			break;
		case OP__MERGE_PADDING:
			FLAC__metadata_chain_merge_padding(chain);
			needs_write = true;
			break;
		case OP__SORT_PADDING:
			FLAC__metadata_chain_sort_padding(chain);
			needs_write = true;
			break;
		default:
			FLAC__ASSERT(0);
			return false;
	}

	if(ok && needs_write) {
		if(options->use_padding)
			FLAC__metadata_chain_sort_padding(chain);
		ok = FLAC__metadata_chain_write(chain, options->use_padding, options->preserve_modtime);
		if(!ok)
			print_error_with_chain_status(chain, "%s: ERROR: writing FLAC file", filename);
	}

	FLAC__metadata_chain_delete(chain);

	return ok;
}

FLAC__bool do_major_operation__list(const char *filename, FLAC__Metadata_Chain *chain, const CommandLineOptions *options)
{
  
  	FLAC__Metadata_Iterator *iterator = FLAC__metadata_iterator_new();
	FLAC__StreamMetadata *block;
	FLAC__bool ok = true;
	unsigned block_number;
	json_object *flac_out_json, *jsobj, *jblock_id, *jblock_object;
	

	
	if(0 == iterator)
		die("out of memory allocating iterator");

	FLAC__metadata_iterator_init(iterator, chain);

	if (options->output_json) {
	  flac_out_json = json_object_new_array();
	}
	
	block_number = 0;
	do {
		block = FLAC__metadata_iterator_get_block(iterator);
		ok &= (0 != block);
		if(!ok)
		{
			flac_fprintf(stderr, "%s: ERROR: couldn't get block from chain\n", filename);
		} else if(passes_filter(options, FLAC__metadata_iterator_get_block(iterator), block_number))
		{
		   if (!options->output_json) {
			write_metadata(filename, block, block_number, !options->utf8_convert, options->application_data_format_is_hexdump);
		   } else {
			/* mod wcg */
			jblock_id = json_object_new_int(block_number);
			jblock_object = json_object_new_object();
			json_object_object_add(jblock_object,"Block ID",jblock_id);
			jsobj = write_metadata_json(block, block_number);
			json_object_object_add(jblock_object,"Block",jsobj); 
			json_object_array_add(flac_out_json,jblock_object);
	
		   }
		}
		block_number++;
	} while(ok && FLAC__metadata_iterator_next(iterator));

	FLAC__metadata_iterator_delete(iterator);

	if (options->output_json) 
	  printf("%s",json_object_to_json_string(flac_out_json));
	
	
	return ok;
}

FLAC__bool do_major_operation__append(FLAC__Metadata_Chain *chain, const CommandLineOptions *options)
{
	(void) chain, (void) options;
	flac_fprintf(stderr, "ERROR: --append not implemented yet\n");
	return false;
}

FLAC__bool do_major_operation__remove(FLAC__Metadata_Chain *chain, const CommandLineOptions *options)
{
	FLAC__Metadata_Iterator *iterator = FLAC__metadata_iterator_new();
	FLAC__bool ok = true;
	unsigned block_number;

	if(0 == iterator)
		die("out of memory allocating iterator");

	FLAC__metadata_iterator_init(iterator, chain);

	block_number = 0;
	while(ok && FLAC__metadata_iterator_next(iterator)) {
		block_number++;
		if(passes_filter(options, FLAC__metadata_iterator_get_block(iterator), block_number)) {
			ok &= FLAC__metadata_iterator_delete_block(iterator, options->use_padding);
			if(options->use_padding)
				ok &= FLAC__metadata_iterator_next(iterator);
		}
	}

	FLAC__metadata_iterator_delete(iterator);

	return ok;
}

FLAC__bool do_major_operation__remove_all(FLAC__Metadata_Chain *chain, const CommandLineOptions *options)
{
	FLAC__Metadata_Iterator *iterator = FLAC__metadata_iterator_new();
	FLAC__bool ok = true;

	if(0 == iterator)
		die("out of memory allocating iterator");

	FLAC__metadata_iterator_init(iterator, chain);

	while(ok && FLAC__metadata_iterator_next(iterator)) {
		ok &= FLAC__metadata_iterator_delete_block(iterator, options->use_padding);
		if(options->use_padding)
			ok &= FLAC__metadata_iterator_next(iterator);
	}

	FLAC__metadata_iterator_delete(iterator);

	return ok;
}

FLAC__bool do_shorthand_operations(const CommandLineOptions *options)
{
	unsigned i;
	FLAC__bool ok = true;

	/* to die after first error,     v---  add '&& ok' here */
	for(i = 0; i < options->num_files; i++)
		ok &= do_shorthand_operations_on_file(options->filenames[i], options);

	/* check if OP__ADD_REPLAY_GAIN requested */
	if(ok && options->num_files > 0) {
		for(i = 0; i < options->ops.num_operations; i++) {
			if(options->ops.operations[i].type == OP__ADD_REPLAY_GAIN)
				ok = do_shorthand_operation__add_replay_gain(options->filenames, options->num_files, options->preserve_modtime);
		}
	}

	return ok;
}

FLAC__bool do_shorthand_operations_on_file(const char *filename, const CommandLineOptions *options)
{
	unsigned i;
	FLAC__bool ok = true, needs_write = false, use_padding = options->use_padding;
	FLAC__Metadata_Chain *chain = FLAC__metadata_chain_new();

	if(0 == chain)
		die("out of memory allocating chain");

	if(!FLAC__metadata_chain_read(chain, filename)) {
		print_error_with_chain_status(chain, "%s: ERROR: reading metadata", filename);
		return false;
	}

	for(i = 0; i < options->ops.num_operations && ok; i++) {
		/*
		 * Do OP__ADD_SEEKPOINT last to avoid decoding twice if both
		 * --add-seekpoint and --import-cuesheet-from are used.
		 */
		if(options->ops.operations[i].type != OP__ADD_SEEKPOINT)
			ok &= do_shorthand_operation(filename, options->prefix_with_filename, chain, &options->ops.operations[i], &needs_write, options->utf8_convert);

		/* The following seems counterintuitive but the meaning
		 * of 'use_padding' is 'try to keep the overall metadata
		 * to its original size, adding or truncating extra
		 * padding if necessary' which is why we need to turn it
		 * off in this case.  If we don't, the extra padding block
		 * will just be truncated.
		 */
		if(options->ops.operations[i].type == OP__ADD_PADDING)
			use_padding = false;
	}

	/*
	 * Do OP__ADD_SEEKPOINT last to avoid decoding twice if both
	 * --add-seekpoint and --import-cuesheet-from are used.
	 */
	for(i = 0; i < options->ops.num_operations && ok; i++) {
		if(options->ops.operations[i].type == OP__ADD_SEEKPOINT)
			ok &= do_shorthand_operation(filename, options->prefix_with_filename, chain, &options->ops.operations[i], &needs_write, options->utf8_convert);
	}

	if(ok && needs_write) {
		if(use_padding)
			FLAC__metadata_chain_sort_padding(chain);
		ok = FLAC__metadata_chain_write(chain, use_padding, options->preserve_modtime);
		if(!ok)
			print_error_with_chain_status(chain, "%s: ERROR: writing FLAC file", filename);
	}

	FLAC__metadata_chain_delete(chain);

	return ok;
}

FLAC__bool do_shorthand_operation(const char *filename, FLAC__bool prefix_with_filename, FLAC__Metadata_Chain *chain, const Operation *operation, FLAC__bool *needs_write, FLAC__bool utf8_convert)
{
	FLAC__bool ok = true;

	switch(operation->type) {
		case OP__SHOW_MD5SUM:
		case OP__SHOW_MIN_BLOCKSIZE:
		case OP__SHOW_MAX_BLOCKSIZE:
		case OP__SHOW_MIN_FRAMESIZE:
		case OP__SHOW_MAX_FRAMESIZE:
		case OP__SHOW_SAMPLE_RATE:
		case OP__SHOW_CHANNELS:
		case OP__SHOW_BPS:
		case OP__SHOW_TOTAL_SAMPLES:
		case OP__SET_MD5SUM:
		case OP__SET_MIN_BLOCKSIZE:
		case OP__SET_MAX_BLOCKSIZE:
		case OP__SET_MIN_FRAMESIZE:
		case OP__SET_MAX_FRAMESIZE:
		case OP__SET_SAMPLE_RATE:
		case OP__SET_CHANNELS:
		case OP__SET_BPS:
		case OP__SET_TOTAL_SAMPLES:
			ok = do_shorthand_operation__streaminfo(filename, prefix_with_filename, chain, operation, needs_write);
			break;
		case OP__SHOW_VC_VENDOR:
		case OP__SHOW_VC_FIELD:
		case OP__REMOVE_VC_ALL:
		case OP__REMOVE_VC_FIELD:
		case OP__REMOVE_VC_FIRSTFIELD:
		case OP__SET_VC_FIELD:
		case OP__IMPORT_VC_FROM:
		case OP__EXPORT_VC_TO:
			ok = do_shorthand_operation__vorbis_comment(filename, prefix_with_filename, chain, operation, needs_write, !utf8_convert);
			break;
		case OP__IMPORT_CUESHEET_FROM:
		case OP__EXPORT_CUESHEET_TO:
			ok = do_shorthand_operation__cuesheet(filename, chain, operation, needs_write);
			break;
		case OP__IMPORT_PICTURE_FROM:
		case OP__EXPORT_PICTURE_TO:
			ok = do_shorthand_operation__picture(filename, chain, operation, needs_write);
			break;
		case OP__ADD_SEEKPOINT:
			ok = do_shorthand_operation__add_seekpoints(filename, chain, operation->argument.add_seekpoint.specification, needs_write);
			break;
		case OP__ADD_REPLAY_GAIN:
			/* this command is always executed last */
			ok = true;
			break;
		case OP__ADD_PADDING:
			ok = do_shorthand_operation__add_padding(filename, chain, operation->argument.add_padding.length, needs_write);
			break;
		default:
			ok = false;
			FLAC__ASSERT(0);
			break;
	};

	return ok;
}

FLAC__bool do_shorthand_operation__add_replay_gain(char **filenames, unsigned num_files, FLAC__bool preserve_modtime)
{
	FLAC__StreamMetadata streaminfo;
	float *title_gains = 0, *title_peaks = 0;
	float album_gain, album_peak;
	unsigned sample_rate = 0;
	unsigned bits_per_sample = 0;
	unsigned channels = 0;
	unsigned i;
	const char *error;
	FLAC__bool first = true;

	FLAC__ASSERT(num_files > 0);

	for(i = 0; i < num_files; i++) {
		FLAC__ASSERT(0 != filenames[i]);
		if(!FLAC__metadata_get_streaminfo(filenames[i], &streaminfo)) {
			flac_fprintf(stderr, "%s: ERROR: can't open file or get STREAMINFO block\n", filenames[i]);
			return false;
		}
		if(first) {
			first = false;
			sample_rate = streaminfo.data.stream_info.sample_rate;
			bits_per_sample = streaminfo.data.stream_info.bits_per_sample;
			channels = streaminfo.data.stream_info.channels;
		}
		else {
			if(sample_rate != streaminfo.data.stream_info.sample_rate) {
				flac_fprintf(stderr, "%s: ERROR: sample rate of %u Hz does not match previous files' %u Hz\n", filenames[i], streaminfo.data.stream_info.sample_rate, sample_rate);
				return false;
			}
			if(bits_per_sample != streaminfo.data.stream_info.bits_per_sample) {
				flac_fprintf(stderr, "%s: ERROR: resolution of %u bps does not match previous files' %u bps\n", filenames[i], streaminfo.data.stream_info.bits_per_sample, bits_per_sample);
				return false;
			}
			if(channels != streaminfo.data.stream_info.channels) {
				flac_fprintf(stderr, "%s: ERROR: # channels (%u) does not match previous files' (%u)\n", filenames[i], streaminfo.data.stream_info.channels, channels);
				return false;
			}
		}
		if(!grabbag__replaygain_is_valid_sample_frequency(sample_rate)) {
			flac_fprintf(stderr, "%s: ERROR: sample rate of %u Hz is not supported\n", filenames[i], sample_rate);
			return false;
		}
		if(channels != 1 && channels != 2) {
			flac_fprintf(stderr, "%s: ERROR: # of channels (%u) is not supported, must be 1 or 2\n", filenames[i], channels);
			return false;
		}
	}
	FLAC__ASSERT(bits_per_sample >= FLAC__MIN_BITS_PER_SAMPLE && bits_per_sample <= FLAC__MAX_BITS_PER_SAMPLE);

	if(!grabbag__replaygain_init(sample_rate)) {
		FLAC__ASSERT(0);
		/* double protection */
		flac_fprintf(stderr, "internal error\n");
		return false;
	}

	if(
		0 == (title_gains = safe_malloc_mul_2op_(sizeof(float), /*times*/num_files)) ||
		0 == (title_peaks = safe_malloc_mul_2op_(sizeof(float), /*times*/num_files))
	)
		die("out of memory allocating space for title gains/peaks");

	for(i = 0; i < num_files; i++) {
		if(0 != (error = grabbag__replaygain_analyze_file(filenames[i], title_gains+i, title_peaks+i))) {
			flac_fprintf(stderr, "%s: ERROR: during analysis (%s)\n", filenames[i], error);
			free(title_gains);
			free(title_peaks);
			return false;
		}
	}
	grabbag__replaygain_get_album(&album_gain, &album_peak);

	for(i = 0; i < num_files; i++) {
		if(0 != (error = grabbag__replaygain_store_to_file(filenames[i], album_gain, album_peak, title_gains[i], title_peaks[i], preserve_modtime))) {
			flac_fprintf(stderr, "%s: ERROR: writing tags (%s)\n", filenames[i], error);
			free(title_gains);
			free(title_peaks);
			return false;
		}
	}

	free(title_gains);
	free(title_peaks);
	return true;
}

FLAC__bool do_shorthand_operation__add_padding(const char *filename, FLAC__Metadata_Chain *chain, unsigned length, FLAC__bool *needs_write)
{
	FLAC__StreamMetadata *padding = 0;
	FLAC__Metadata_Iterator *iterator = FLAC__metadata_iterator_new();

	if(0 == iterator)
		die("out of memory allocating iterator");

	FLAC__metadata_iterator_init(iterator, chain);

	while(FLAC__metadata_iterator_next(iterator))
		;

	padding = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
	if(0 == padding)
		die("out of memory allocating PADDING block");

	padding->length = length;

	if(!FLAC__metadata_iterator_insert_block_after(iterator, padding)) {
		print_error_with_chain_status(chain, "%s: ERROR: adding new PADDING block to metadata", filename);
		FLAC__metadata_object_delete(padding);
		FLAC__metadata_iterator_delete(iterator);
		return false;
	}

	FLAC__metadata_iterator_delete(iterator);
	*needs_write = true;
	return true;
}

FLAC__bool passes_filter(const CommandLineOptions *options, const FLAC__StreamMetadata *block, unsigned block_number)
{
	unsigned i, j;
	FLAC__bool matches_number = false, matches_type = false;
	FLAC__bool has_block_number_arg = false;

	for(i = 0; i < options->args.num_arguments; i++) {
		if(options->args.arguments[i].type == ARG__BLOCK_TYPE || options->args.arguments[i].type == ARG__EXCEPT_BLOCK_TYPE) {
			for(j = 0; j < options->args.arguments[i].value.block_type.num_entries; j++) {
				if(options->args.arguments[i].value.block_type.entries[j].type == block->type) {
					if(block->type != FLAC__METADATA_TYPE_APPLICATION || !options->args.arguments[i].value.block_type.entries[j].filter_application_by_id || 0 == memcmp(options->args.arguments[i].value.block_type.entries[j].application_id, block->data.application.id, FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8))
						matches_type = true;
				}
			}
		}
		else if(options->args.arguments[i].type == ARG__BLOCK_NUMBER) {
			has_block_number_arg = true;
			for(j = 0; j < options->args.arguments[i].value.block_number.num_entries; j++) {
				if(options->args.arguments[i].value.block_number.entries[j] == block_number)
					matches_number = true;
			}
		}
	}

	if(!has_block_number_arg)
		matches_number = true;

	if(options->args.checks.has_block_type) {
		FLAC__ASSERT(!options->args.checks.has_except_block_type);
	}
	else if(options->args.checks.has_except_block_type)
		matches_type = !matches_type;
	else
		matches_type = true;

	return matches_number && matches_type;
}

void write_metadata(const char *filename, FLAC__StreamMetadata *block, unsigned block_number, FLAC__bool raw, FLAC__bool hexdump_application)
{
	unsigned i, j;

/*@@@ yuck, should do this with a varargs function or something: */
#define PPR if(filename) { if(raw) printf("%s:",filename); else flac_printf("%s:",filename); }
	PPR; printf("METADATA block #%u\n", block_number);
	PPR; printf("  type: %u (%s)\n", (unsigned)block->type, block->type < FLAC__METADATA_TYPE_UNDEFINED? FLAC__MetadataTypeString[block->type] : "UNKNOWN");
	PPR; printf("  is last: %s\n", block->is_last? "true":"false");
	PPR; printf("  length: %u\n", block->length);

	switch(block->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			PPR; printf("  minimum blocksize: %u samples\n", block->data.stream_info.min_blocksize);
			PPR; printf("  maximum blocksize: %u samples\n", block->data.stream_info.max_blocksize);
			PPR; printf("  minimum framesize: %u bytes\n", block->data.stream_info.min_framesize);
			PPR; printf("  maximum framesize: %u bytes\n", block->data.stream_info.max_framesize);
			PPR; printf("  sample_rate: %u Hz\n", block->data.stream_info.sample_rate);
			PPR; printf("  channels: %u\n", block->data.stream_info.channels);
			PPR; printf("  bits-per-sample: %u\n", block->data.stream_info.bits_per_sample);
			PPR; printf("  total samples: %" PRIu64 "\n", block->data.stream_info.total_samples);
			PPR; printf("  MD5 signature: ");
			for(i = 0; i < 16; i++) {
				printf("%02x", (unsigned)block->data.stream_info.md5sum[i]);
			}
			printf("\n");
			break;
		case FLAC__METADATA_TYPE_PADDING:
			/* nothing to print */
			break;
		case FLAC__METADATA_TYPE_APPLICATION:
			PPR; printf("  application ID: ");
			for(i = 0; i < 4; i++)
				printf("%02x", block->data.application.id[i]);
			printf("\n");
			PPR; printf("  data contents:\n");
			if(0 != block->data.application.data) {
				if(hexdump_application)
					hexdump(filename, block->data.application.data, block->length - FLAC__STREAM_METADATA_HEADER_LENGTH, "    ");
				else
					(void) local_fwrite(block->data.application.data, 1, block->length - FLAC__STREAM_METADATA_HEADER_LENGTH, stdout);
			}
			break;
		case FLAC__METADATA_TYPE_SEEKTABLE:
			PPR; printf("  seek points: %u\n", block->data.seek_table.num_points);
			for(i = 0; i < block->data.seek_table.num_points; i++) {
				if(block->data.seek_table.points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER) {
					PPR; printf("    point %u: sample_number=%" PRIu64 ", stream_offset=%" PRIu64 ", frame_samples=%u\n", i, block->data.seek_table.points[i].sample_number, block->data.seek_table.points[i].stream_offset, block->data.seek_table.points[i].frame_samples);
				}
				else {
					PPR; printf("    point %u: PLACEHOLDER\n", i);
				}
			}
			break;
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			PPR; printf("  vendor string: ");
			write_vc_field(0, &block->data.vorbis_comment.vendor_string, raw, stdout);
			PPR; printf("  comments: %u\n", block->data.vorbis_comment.num_comments);
			for(i = 0; i < block->data.vorbis_comment.num_comments; i++) {
				PPR; printf("    comment[%u]: ", i);
				write_vc_field(0, &block->data.vorbis_comment.comments[i], raw, stdout);
			}
			break;
		case FLAC__METADATA_TYPE_CUESHEET:
			PPR; printf("  media catalog number: %s\n", block->data.cue_sheet.media_catalog_number);
			PPR; printf("  lead-in: %" PRIu64 "\n", block->data.cue_sheet.lead_in);
			PPR; printf("  is CD: %s\n", block->data.cue_sheet.is_cd? "true":"false");
			PPR; printf("  number of tracks: %u\n", block->data.cue_sheet.num_tracks);
			for(i = 0; i < block->data.cue_sheet.num_tracks; i++) {
				const FLAC__StreamMetadata_CueSheet_Track *track = block->data.cue_sheet.tracks+i;
				const FLAC__bool is_last = (i == block->data.cue_sheet.num_tracks-1);
				const FLAC__bool is_leadout = is_last && track->num_indices == 0;
				PPR; printf("    track[%u]\n", i);
				PPR; printf("      offset: %" PRIu64 "\n", track->offset);
				if(is_last) {
					PPR; printf("      number: %u (%s)\n", (unsigned)track->number, is_leadout? "LEAD-OUT" : "INVALID");
				}
				else {
					PPR; printf("      number: %u\n", (unsigned)track->number);
				}
				if(!is_leadout) {
					PPR; printf("      ISRC: %s\n", track->isrc);
					PPR; printf("      type: %s\n", track->type == 1? "DATA" : "AUDIO");
					PPR; printf("      pre-emphasis: %s\n", track->pre_emphasis? "true":"false");
					PPR; printf("      number of index points: %u\n", track->num_indices);
					for(j = 0; j < track->num_indices; j++) {
						const FLAC__StreamMetadata_CueSheet_Index *indx = track->indices+j;
						PPR; printf("        index[%u]\n", j);
						PPR; printf("          offset: %" PRIu64 "\n", indx->offset);
						PPR; printf("          number: %u\n", (unsigned)indx->number);
					}
				}
			}
			break;
		case FLAC__METADATA_TYPE_PICTURE:
			PPR; printf("  type: %u (%s)\n", block->data.picture.type, block->data.picture.type < FLAC__STREAM_METADATA_PICTURE_TYPE_UNDEFINED? FLAC__StreamMetadata_Picture_TypeString[block->data.picture.type] : "UNDEFINED");
			PPR; printf("  MIME type: %s\n", block->data.picture.mime_type);
			PPR; printf("  description: %s\n", block->data.picture.description);
			PPR; printf("  width: %u\n", (unsigned)block->data.picture.width);
			PPR; printf("  height: %u\n", (unsigned)block->data.picture.height);
			PPR; printf("  depth: %u\n", (unsigned)block->data.picture.depth);
			PPR; printf("  colors: %u%s\n", (unsigned)block->data.picture.colors, block->data.picture.colors? "" : " (unindexed)");
			PPR; printf("  data length: %u\n", (unsigned)block->data.picture.data_length);
			PPR; printf("  data:\n");
			if(0 != block->data.picture.data)
				hexdump(filename, block->data.picture.data, block->data.picture.data_length, "    ");
			break;
		default:
			PPR; printf("  data contents:\n");
			if(0 != block->data.unknown.data)
				hexdump(filename, block->data.unknown.data, block->length, "    ");
			break;
	}
#undef PPR
}


/* mod wcg 
 NO PPR here, raw output not an option with --output-json
 remove FLAC__bool raw, FLAC__bool hexdump_application
 */

json_object * write_metadata_json(FLAC__StreamMetadata *block, unsigned block_number)
{
	unsigned i, j;
	FLAC__bool raw = false;
	FLAC__bool hexdump_application = false;
	
	char hbuf[64] = {0};
	  
	json_object * jobj = json_object_new_object();
	
	json_object *jmin_blocksize, *jmax_blocksize, *jmin_framesize, *jmax_framesize, *jsample_rate, 
		    *jchannels, *jbps, *jtotal_samples, *jmd5, *jcomments_array, *jcomment, *jvendor,
		    *jcomments_number, *japplication_id, *japplication_data, *jseek_points, *jpoints, *jpoint, *jsample,
		    *jpoint_number, *jsample_number, *jstream_offset, *jframe_samples, *jpicture_type, *jmime_type, 
		    *jdescription, *jwidth, *jheight, *jdepth, *jcolors, *jdata_length, *jdata;

	json_object *jblock_number = json_object_new_int(block_number);
	json_object *jblock_type = json_object_new_int(block->type);
	json_object *jblock_length = json_object_new_int(block->length);
	json_object *jblock_last = json_object_new_boolean(block->is_last);
	
	json_object_object_add(jobj,"Block Number", jblock_number);
	json_object_object_add(jobj,"Block Type", jblock_type);
	json_object_object_add(jobj,"Block Length", jblock_length);
	json_object_object_add(jobj,"Is Last", jblock_last);

	
/*	
	 printf("JSON METADATA block #%u\n", block_number);
	 printf("  type: %u (%s)\n", (unsigned)block->type, block->type < FLAC__METADATA_TYPE_UNDEFINED? FLAC__MetadataTypeString[block->type] : "UNKNOWN");
	 printf("  is last: %s\n", block->is_last? "true":"false");
	 printf("  length: %u\n", block->length);
*/
	switch(block->type) {
		case FLAC__METADATA_TYPE_STREAMINFO:
			 
			  jmin_blocksize = json_object_new_int(block->data.stream_info.min_blocksize);
			  jmax_blocksize = json_object_new_int(block->data.stream_info.max_blocksize);
			  jmin_framesize = json_object_new_int(block->data.stream_info.min_framesize);
			  jmax_framesize = json_object_new_int(block->data.stream_info.max_framesize);
			  jsample_rate = json_object_new_int(block->data.stream_info.sample_rate);
			  jchannels = json_object_new_int(block->data.stream_info.channels);
			  jbps = json_object_new_int(block->data.stream_info.bits_per_sample);
			  jtotal_samples = json_object_new_int(block->data.stream_info.total_samples);
			
			  for(i = 0; i < 16; i++) {
			  	sprintf(hbuf+strlen(hbuf),"%02x", (unsigned)block->data.stream_info.md5sum[i]);
			  }
			  jmd5 = json_object_new_string(hbuf);
			  
			  json_object_object_add(jobj,"Min Blocksize",jmin_blocksize);
			  json_object_object_add(jobj,"Max Blocksize",jmax_blocksize);
			  json_object_object_add(jobj,"Min Framesize",jmin_framesize);
			  json_object_object_add(jobj,"Max Framesize",jmax_framesize);
			  json_object_object_add(jobj,"Sample Rate",jsample_rate);
			  json_object_object_add(jobj,"Channels",jchannels);
			  json_object_object_add(jobj,"Bits Per Sample",jbps);
			  json_object_object_add(jobj,"Total Samples",jtotal_samples);
			  json_object_object_add(jobj,"MD5 Signature",jmd5);
						 

			  
/*			 printf("  minimum blocksize: %u samples\n", block->data.stream_info.min_blocksize);
			 printf("  maximum blocksize: %u samples\n", block->data.stream_info.max_blocksize);
			 printf("  minimum framesize: %u bytes\n", block->data.stream_info.min_framesize);
			 printf("  maximum framesize: %u bytes\n", block->data.stream_info.max_framesize);
			 printf("  sample_rate: %u Hz\n", block->data.stream_info.sample_rate);
			 printf("  channels: %u\n", block->data.stream_info.channels);
			 printf("  bits-per-sample: %u\n", block->data.stream_info.bits_per_sample);
			 printf("  total samples: %" PRIu64 "\n", block->data.stream_info.total_samples);
			 printf("  MD5 signature: "); 
			 */

			//printf("\n");
			break;
		case FLAC__METADATA_TYPE_PADDING:
			/* nothing to print */
			break;
		case FLAC__METADATA_TYPE_APPLICATION:

			for(i = 0; i < 4; i++)
				sprintf(hbuf+strlen(hbuf),"%02x", block->data.application.id[i]);
			
			japplication_id = json_object_new_string(hbuf);
			
			if(0 != block->data.application.data) {
			  japplication_data = json_object_new_string(block->data.application.data);
			}
			json_object_object_add(jobj,"Application ID", japplication_id);
			json_object_object_add(jobj,"Application Data", japplication_data);
			
			break;
		case FLAC__METADATA_TYPE_SEEKTABLE:
			 jseek_points = json_object_new_int(block->data.seek_table.num_points);
			 jpoints = json_object_new_array();
			 
			for(i = 0; i < block->data.seek_table.num_points; i++) {
			  
			      jpoint = json_object_new_object();
				if(block->data.seek_table.points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER) {
				  jpoint_number = json_object_new_int(i);
				  jsample_number = json_object_new_int(block->data.seek_table.points[i].sample_number);
				  jstream_offset = json_object_new_int(block->data.seek_table.points[i].stream_offset);
				  jframe_samples = json_object_new_int(block->data.seek_table.points[i].frame_samples);
				  json_object_object_add(jpoint,"Point Number",jpoint_number);
				  json_object_object_add(jpoint,"Sample Number",jsample_number);
				  json_object_object_add(jpoint,"Stream Offset",jstream_offset);
				  json_object_object_add(jpoint,"Frame Samples",jframe_samples);
				}
				else {
				  jpoint_number = json_object_new_int(i);
				  json_object_object_add(jpoint,"Point Number",jpoint_number);
				}
				json_object_array_add(jpoints,jpoint);
			}
			json_object_object_add(jobj,"Seek Points", jseek_points);
			json_object_object_add(jobj,"Seek Data",jpoints);
			break;
		case FLAC__METADATA_TYPE_VORBIS_COMMENT:
			jvendor = json_object_new_string(block->data.vorbis_comment.vendor_string.entry);
			jcomments_number = json_object_new_int(block->data.vorbis_comment.num_comments);
			jcomments_array = json_object_new_array();
			for(i = 0; i < block->data.vorbis_comment.num_comments; i++) {
				jcomment = json_object_new_string(block->data.vorbis_comment.comments[i].entry);
				json_object_array_add(jcomments_array,jcomment);
			}
			json_object_object_add(jobj,"Vendor String",jvendor);
			json_object_object_add(jobj,"Number of Comments",jcomments_number);
			json_object_object_add(jobj,"Comments",jcomments_array);
			
			break;
		case FLAC__METADATA_TYPE_CUESHEET:
		  /*
			 printf("  media catalog number: %s\n", block->data.cue_sheet.media_catalog_number);
			 printf("  lead-in: %" PRIu64 "\n", block->data.cue_sheet.lead_in);
			 printf("  is CD: %s\n", block->data.cue_sheet.is_cd? "true":"false");
			 printf("  number of tracks: %u\n", block->data.cue_sheet.num_tracks);
			for(i = 0; i < block->data.cue_sheet.num_tracks; i++) {
				const FLAC__StreamMetadata_CueSheet_Track *track = block->data.cue_sheet.tracks+i;
				const FLAC__bool is_last = (i == block->data.cue_sheet.num_tracks-1);
				const FLAC__bool is_leadout = is_last && track->num_indices == 0;
				 printf("    track[%u]\n", i);
				 printf("      offset: %" PRIu64 "\n", track->offset);
				if(is_last) {
					 printf("      number: %u (%s)\n", (unsigned)track->number, is_leadout? "LEAD-OUT" : "INVALID");
				}
				else {
					 printf("      number: %u\n", (unsigned)track->number);
				}
				if(!is_leadout) {
					 printf("      ISRC: %s\n", track->isrc);
					 printf("      type: %s\n", track->type == 1? "DATA" : "AUDIO");
					 printf("      pre-emphasis: %s\n", track->pre_emphasis? "true":"false");
					 printf("      number of index points: %u\n", track->num_indices);
					for(j = 0; j < track->num_indices; j++) {
						const FLAC__StreamMetadata_CueSheet_Index *indx = track->indices+j;
						 printf("        index[%u]\n", j);
						 printf("          offset: %" PRIu64 "\n", indx->offset);
						 printf("          number: %u\n", (unsigned)indx->number);
					}
				}
			}
			LATER wcg */
			break;
		case FLAC__METADATA_TYPE_PICTURE:
			 jpicture_type = json_object_new_int(block->data.picture.type);
			 jmime_type = json_object_new_string(block->data.picture.mime_type);
			 jdescription = json_object_new_string(block->data.picture.description);
			 jwidth = json_object_new_int(block->data.picture.width);
			 jheight = json_object_new_int(block->data.picture.height);
			 jdepth = json_object_new_int(block->data.picture.depth);
			 jcolors = json_object_new_int(block->data.picture.colors);
			 jdata_length = json_object_new_int(block->data.picture.data_length);
			 
			 json_object_object_add(jobj,"Picture Type",jpicture_type);
			 json_object_object_add(jobj,"MIME Type",jmime_type);
			 json_object_object_add(jobj,"Description",jdescription);
			 json_object_object_add(jobj,"Width",jwidth);
			 json_object_object_add(jobj,"Height",jheight);
			 json_object_object_add(jobj,"Depth",jdepth);
			 json_object_object_add(jobj,"Colors",jcolors);
			 json_object_object_add(jobj,"Data Length",jdata_length);
			 
			 
			 if(0 != block->data.picture.data)
			 {
			  char * image_buffer = (char*) malloc ((block->data.picture.data_length*2)+1);
			  for (int i=0;i<block->data.picture.data_length;i++) {
			     sprintf(image_buffer+strlen(image_buffer),"%02X",block->data.picture.data[i]);
			  }
			  jdata = json_object_new_string(image_buffer);
			  json_object_object_add(jobj,"Image Data",jdata);
			 }
				//hexdump(filename, block->data.picture.data, block->data.picture.data_length, "    ");
			break;
		default:
		  /*
			 printf("  data contents:\n");
			if(0 != block->data.unknown.data)
				hexdump(filename, block->data.unknown.data, block->length, "    ");
			 LATER wcg */
			break;
	}

	return (jobj);
}
