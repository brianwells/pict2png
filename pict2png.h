/*
 *  pict2png.h
 *
 *  Copyright (C) 2010 Brian D. Wells
 *
 *  This file is part of pict2png.
 *
 *  pict2png is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  pict2png is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with pict2png.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Author: Brian D. Wells <spam_brian@me.com>
 *
 */

#include <wand/MagickWand.h>

#define RESULT_OK 0
#define RESULT_ERROR 128
#define RESULT_WARNING 1

#define ALPHA_TYPE_UNKNOWN 0
#define ALPHA_TYPE_NONE 1
#define ALPHA_TYPE_UNASSOCIATED 2
#define ALPHA_TYPE_ASSOCIATED 3

#define BKGND_NONE -1
#define BKGND_BLACK 0
#define BKGND_WHITE 1
#define BKGND_OTHER 2

typedef struct pixel_data {
    unsigned char alp;
    unsigned char red;
    unsigned char grn;
    unsigned char blu;
} PixelData;

typedef struct convert_options {
    int verbose;
    int quiet;
    int dry_run;
    int force;
    int delete_original;
	int manual_alpha;
    double bkgnd_ratio;
} ConvertOptions;

typedef struct convert_results {
    int result;
	char *message;
    int alpha_type;
    int bkgnd_type;
    double bkgnd_ratio;
    unsigned char bkgnd_red;
    unsigned char bkgnd_grn;
    unsigned char bkgnd_blu;
} ConvertResults;

typedef struct convert_context {
    dispatch_queue_t load_queue;
    dispatch_queue_t conv_queue;
    dispatch_queue_t save_queue;
    dispatch_group_t conv_group;
	dispatch_semaphore_t conv_semaphore;
    char *src_path;
    char *dst_path;
    ConvertOptions options;
    ConvertResults results;
    MagickWand *mw;
    unsigned long hasAlphaChannel;
    unsigned long imageWidth;
    unsigned long imageHeight;
    unsigned long pixel_count;
    PixelData *pixels;
} ConvertContext;

void process_image(ConvertContext *context);
void load_image(ConvertContext *context);
void conv_image(ConvertContext *context);
void save_image(ConvertContext *context);
void finish_image(ConvertContext *context);

void initialize_graphics_lib();
void destroy_graphics_lib();

