/*
 *  pict2png.c
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <dispatch/dispatch.h>

#include "pict2png.h"

#define BKGND_GROWTH 3

#define BKGND_NONE -1
#define BKGND_BLACK 0
#define BKGND_WHITE 1

typedef struct bkgnd_metric {
    unsigned long count;
    double red;
    double grn;
    double blu;
} BackgroundMetric;

void process_image(ConvertContext *context) {
	context->results.result = RESULT_OK;
	context->mw = NewMagickWand();
	dispatch_group_async_f(context->conv_group, context->load_queue, context, (void (*)(void *))load_image);
}

void load_image(ConvertContext *context) {
    int result = RESULT_OK;
    char *error_desc;
    ExceptionType error_type;

	// wait for resources to be available
	dispatch_semaphore_wait(context->conv_semaphore, DISPATCH_TIME_FOREVER);
	
	// load image
	if (MagickReadImage(context->mw, context->src_path) == MagickFalse) {
		// deal with error
        error_desc = MagickGetException(context->mw, &error_type);
        asprintf(&context->results.message,"Error loading image (%s): %s\n",error_desc,context->src_path);
        error_desc = (char *)MagickRelinquishMemory(error_desc);
        result += RESULT_ERROR;
    }
    if (result == RESULT_OK) {
        // get image info
        context->hasAlphaChannel = MagickGetImageAlphaChannel(context->mw);
        if  (context->hasAlphaChannel == MagickTrue) {
            context->imageWidth = MagickGetImageWidth(context->mw);
            context->imageHeight = MagickGetImageHeight(context->mw);

            // get pixel data
            context->pixel_count = context->imageWidth * context->imageHeight;
            context->pixels = malloc(context->pixel_count * sizeof(PixelData));
            if (context->pixels == NULL) {
                asprintf(&context->results.message, "Error allocating memory for pixel data: %s\n",context->src_path);
                result += RESULT_ERROR;
            } else {
                if (MagickExportImagePixels(context->mw, 0, 0, context->imageWidth, context->imageHeight, "ARGB", DoublePixel, context->pixels) == MagickFalse) {
                    error_desc = MagickGetException(context->mw, &error_type);
                    asprintf(&context->results.message, "Error exporting pixel data (%s): %s\n",error_desc,context->src_path);
                    error_desc = (char *)MagickRelinquishMemory(error_desc);
                    result += RESULT_ERROR;
                }
            }
        }
    }
    if (result != RESULT_OK) {
        // clean up mess
		context->results.result = result;
		dispatch_group_async_f(context->conv_group, dispatch_get_main_queue(), context, (void (*)(void *))finish_image);
    } else {
		// move to next step
		dispatch_group_async_f(context->conv_group, context->conv_queue, context, (void (*)(void *))conv_image);
	}
}

void conv_image(ConvertContext *context) {
    int result = RESULT_OK;
    BackgroundMetric *backgrounds = NULL;
    int bkgnd_size = BKGND_GROWTH;
    int bkgnd_count;
    int bkgnd_index;
    int bkgnd_selected = BKGND_NONE;
    double bkgnd_ratio = 0.0;
    unsigned long bkgnd_pixels;
    
    unsigned long pixel_index;
    double red;
    double grn;
    double blu;
    double alp;
    double inv;
    int alpha_other = 0;
    int alpha_match = 0;
    int alpha_type = ALPHA_TYPE_NONE;

    // check alpha
    if (result == RESULT_OK && context->hasAlphaChannel == MagickTrue) {

        // allocate buffer for background metrics
        backgrounds = malloc(sizeof(BackgroundMetric) * bkgnd_size);
        if (backgrounds == NULL) {
            asprintf(&context->results.message, "Error allocating memory for background metrics");
			result += RESULT_ERROR;
        }
        
        // load starting background metrics
        backgrounds[BKGND_BLACK].red = 0.0;
        backgrounds[BKGND_BLACK].grn = 0.0;
        backgrounds[BKGND_BLACK].blu = 0.0;
        backgrounds[BKGND_BLACK].count = 0;        
        backgrounds[BKGND_WHITE].red = 1.0;
        backgrounds[BKGND_WHITE].grn = 1.0;
        backgrounds[BKGND_WHITE].blu = 1.0;
        backgrounds[BKGND_WHITE].count = 0;        
        bkgnd_count = 2;
        
        // analyze image
        
        // get background color
        if (result == RESULT_OK) {
            bkgnd_pixels = 0;
            pixel_index = 0;
            while (result == RESULT_OK && pixel_index < context->pixel_count) {
                red = context->pixels[pixel_index].red;
                grn = context->pixels[pixel_index].grn;
                blu = context->pixels[pixel_index].blu;
                alp = context->pixels[pixel_index].alp;
                // transparent pixels only...
                if (alp == 0.0) {
                    bkgnd_pixels++;
                    // look for color in metrics
                    for (bkgnd_index = 0; bkgnd_index < bkgnd_count; bkgnd_index++) {
                        if (red == backgrounds[bkgnd_index].red && grn == backgrounds[bkgnd_index].grn && blu == backgrounds[bkgnd_index].blu) {
                            backgrounds[bkgnd_index].count++;
                            break;
                        }
                    }
                    if (bkgnd_index == bkgnd_count) {
                        // add another metric
                        if (bkgnd_count == bkgnd_size) {
                            bkgnd_size += BKGND_GROWTH;
                            backgrounds = realloc(backgrounds, sizeof(BackgroundMetric) * bkgnd_size);
                            if (backgrounds == NULL) {
                                asprintf(&context->results.message,"Error allocating memory for background metrics");
                                result += RESULT_ERROR;
                                break;
                            }
                        }
                    backgrounds[bkgnd_index].red = red;
                    backgrounds[bkgnd_index].grn = grn;
                    backgrounds[bkgnd_index].blu = blu;
                    backgrounds[bkgnd_index].count = 1;
                    bkgnd_count++;
                    }
                }
                pixel_index++;
            }
        }
        
        // find background color in metrics
        if (result == RESULT_OK) {
            for (bkgnd_index = 0; bkgnd_index < bkgnd_count; bkgnd_index++) {
                if (backgrounds[bkgnd_index].count > (bkgnd_selected == BKGND_NONE ? 0 : backgrounds[bkgnd_selected].count))
                    bkgnd_selected = bkgnd_index;
            }
            // make sure the selected color wins by a good margin
            if (bkgnd_selected != BKGND_NONE) {
                
                bkgnd_ratio = (double)backgrounds[bkgnd_selected].count / (double)bkgnd_pixels;
                if (bkgnd_ratio < context->options.bkgnd_ratio && !context->options.force) {
                    asprintf(&context->results.message, "Inconsistent background color (ratio at %g; should be %g or greater): %s\n",bkgnd_ratio, context->options.bkgnd_ratio, context->src_path);
                    result += RESULT_WARNING;
                }
            }
        }
        
        if (result == RESULT_OK && bkgnd_selected != BKGND_NONE) {
            // check translucent pixels
            pixel_index = 0;
            while (pixel_index < context->pixel_count) {
                alp = context->pixels[pixel_index].alp;

                if (alp > 0.0 && alp < 1.0) {
                    red = context->pixels[pixel_index].red;
                    grn = context->pixels[pixel_index].grn;
                    blu = context->pixels[pixel_index].blu;
                    inv = 1.0 - alp;
                    
                    // would just subtract inverse of alpha, but rounding
                    // issues force us to special-case a value of 1.0
                    if (backgrounds[bkgnd_selected].red == 1.0) {
                        red = (red == 1.0 ? alp : red - inv);
                    } else if (backgrounds[bkgnd_selected].red != 0.0) {
                        red = red - (inv * backgrounds[bkgnd_selected].red);
                    }
                    if (backgrounds[bkgnd_selected].grn == 1.0) {
                        grn = (grn == 1.0 ? alp : grn - inv);
                    } else if (backgrounds[bkgnd_selected].grn != 0.0) {
                        grn = grn - (inv * backgrounds[bkgnd_selected].grn);
                    }
                    if (backgrounds[bkgnd_selected].blu == 1.0) {
                        blu = (blu == 1.0 ? alp : blu - inv);
                    } else if (backgrounds[bkgnd_selected].blu != 0.0) {
                        blu = blu - (inv * backgrounds[bkgnd_selected].blu);
                    }
                    // check range of pixel
                    if (red <= alp && red >= 0.0 &&
                        blu <= alp && blu >= 0.0 &&
                        grn <= alp && grn >= 0.0) {
                        alpha_match++;
                    } else {
                        alpha_other++;
                    }
                }
                pixel_index++;
            }
            
            // If all the translucent pixels fall within the proper range, then
            // this the alpha is likely pre-multiplied (associated) over background
            if (alpha_match > 0 && alpha_other == 0) {
                alpha_type = ALPHA_TYPE_ASSOCIATED;
                if (bkgnd_selected != BKGND_BLACK && bkgnd_selected != BKGND_WHITE) {
                    if (!context->options.force) {
                        asprintf(&context->results.message,
								 "Invalid background - must be black or white (R:%g G:%g B:%g): %s\n",
                                 backgrounds[bkgnd_selected].red,
                                 backgrounds[bkgnd_selected].grn,
                                 backgrounds[bkgnd_selected].blu,
                                 context->src_path);
                        result += RESULT_WARNING;
                    }
                }
            } else if (alpha_match == 0 && alpha_other == 0) {
                // no translucent pixels!!!
                alpha_type = ALPHA_TYPE_UNKNOWN;
                asprintf(&context->results.message, "Unable to determine alpha type (no translucent pixels): %s\n", context->src_path);
                result += RESULT_WARNING;
            } else {
                // straight (unassociated) alpha
                alpha_type = ALPHA_TYPE_UNASSOCIATED;
            }
        }
        
        if (result == RESULT_OK && (alpha_type == ALPHA_TYPE_ASSOCIATED)) {
            // correct image
            pixel_index = 0;
            while (result == RESULT_OK && pixel_index < context->pixel_count) {
                red = context->pixels[pixel_index].red;
                grn = context->pixels[pixel_index].grn;
                blu = context->pixels[pixel_index].blu;
                alp = context->pixels[pixel_index].alp;
                inv = 1.0 - alp;

                if (alp > 0.0 && alp < 1.0) {
                    /*

                     Standard image composition formula
                     (foreground through a mask over a background)

                     Comp = (Fg * A) + ((1 - A) * Bg)

                     So here is how we would get the foreground back...

                     Fg = (Comp - ((1 - A) * Bg)) / A

                     */
                    if (bkgnd_selected == BKGND_BLACK) {
                        // this one is easy...
                        // Fg = Comp / A
                        if (red != 0.0)
                            context->pixels[pixel_index].red = red / alp;
                        if (grn != 0.0)
                            context->pixels[pixel_index].grn = grn / alp;
                        if (blu != 0.0)
                            context->pixels[pixel_index].blu = blu / alp;
                    } else if (bkgnd_selected == BKGND_WHITE) {
                        // much harder...
                        // Fg = (Comp - (1 - A)) / A
                        if (red != 1.0)
                            context->pixels[pixel_index].red = (red - inv)/alp;
                        if (grn != 1.0)
                            context->pixels[pixel_index].grn = (grn - inv)/alp;
                        if (blu != 1.0)
                            context->pixels[pixel_index].blu = (blu - inv)/alp;
                    } else {
                        // way harder!  (not sure if this really works)
                        // Fg = (Comp - ((1 - A) * Bg)) / A
                        context->pixels[pixel_index].red = (red - (inv * backgrounds[bkgnd_selected].red))/alp;
                        context->pixels[pixel_index].grn = (grn - (inv * backgrounds[bkgnd_selected].grn))/alp;
                        context->pixels[pixel_index].blu = (blu - (inv * backgrounds[bkgnd_selected].blu))/alp;
                    }
                    // verify pixel data is within range
                    if (context->pixels[pixel_index].red < 0.0 || context->pixels[pixel_index].red > 1.0 ||
                        context->pixels[pixel_index].grn < 0.0 || context->pixels[pixel_index].grn > 1.0 ||
                        context->pixels[pixel_index].blu < 0.0 || context->pixels[pixel_index].blu > 1.0 ||
                        context->pixels[pixel_index].alp < 0.0 || context->pixels[pixel_index].red > 1.0) {
                        asprintf(&context->results.message, "pixel %06ld out of range (R:%g G:%g B:%g A:%g) for image: %s\n",
                                 pixel_index,
                                 context->pixels[pixel_index].red,
                                 context->pixels[pixel_index].grn,
                                 context->pixels[pixel_index].blu,
                                 context->pixels[pixel_index].alp,
                                 context->src_path);
                        result += RESULT_ERROR;
                    }
                }
                pixel_index++;
            }
        }
    }

	// fill in results
	context->results.alpha_type  = alpha_type;
	context->results.bkgnd_type  = (bkgnd_selected == BKGND_NONE || bkgnd_selected == BKGND_BLACK || bkgnd_selected == BKGND_WHITE ? bkgnd_selected : BKGND_OTHER);
	context->results.bkgnd_ratio = bkgnd_ratio;
	context->results.bkgnd_red   = (bkgnd_selected == BKGND_NONE ? 0.0 : backgrounds[bkgnd_selected].red);
	context->results.bkgnd_grn   = (bkgnd_selected == BKGND_NONE ? 0.0 : backgrounds[bkgnd_selected].grn);
	context->results.bkgnd_blu   = (bkgnd_selected == BKGND_NONE ? 0.0 : backgrounds[bkgnd_selected].blu);

    if (backgrounds != NULL) {
        free(backgrounds);
    }
    
	if (result != RESULT_OK || context->options.dry_run != 0) {
		// clean up mess
		context->results.result = result;
		dispatch_group_async_f(context->conv_group, dispatch_get_main_queue(), context, (void (*)(void *))finish_image);
    } else {
		// move to next step
		dispatch_group_async_f(context->conv_group, context->save_queue, context, (void (*)(void *))save_image);
	}
}

void save_image(ConvertContext *context) {
    int result = RESULT_OK;
    char *error_desc;
    ExceptionType error_type;
	
    // get pixel data
    if (context->hasAlphaChannel == MagickTrue) {
        if (MagickImportImagePixels(context->mw, 0, 0, context->imageWidth, context->imageHeight, "ARGB", DoublePixel, context->pixels)  == MagickFalse) {
            error_desc = MagickGetException(context->mw, &error_type);
            asprintf(&context->results.message, "Error exporting pixel data (%s): %s\n",error_desc,context->src_path);
            error_desc = (char *)MagickRelinquishMemory(error_desc);
            result += RESULT_ERROR;
        }
    }
    
    // convert image to PNG
    if (result == RESULT_OK) {
        if (MagickSetImageFormat(context->mw,"PNG") == MagickFalse) {
            error_desc = MagickGetException(context->mw, &error_type);
			asprintf(&context->results.message,"Error converting image (%s): %s\n",error_desc,context->src_path);
            error_desc = (char *)MagickRelinquishMemory(error_desc);
            result += RESULT_ERROR;
        }
    }
	
    // make sure image is saved as RGB and not crunched down to grayscale
	MagickSetType(context->mw, (context->hasAlphaChannel == MagickTrue ? TrueColorMatteType : TrueColorType));
	
	// save image to disk
    if (result == RESULT_OK) {
        if (MagickWriteImage(context->mw, context->dst_path) == MagickFalse) {
            error_desc = MagickGetException(context->mw, &error_type);
            asprintf(&context->results.message, "Error saving image (%s): %s\n",error_desc,context->dst_path);
            error_desc = (char *)MagickRelinquishMemory(error_desc);
            result += RESULT_ERROR;
        }
    }
	
	if (result == RESULT_OK && context->options.delete_original != 0) {
		if (unlink(context->src_path) == -1) {
			asprintf(&context->results.message, "Unable to delete original image (%s): %s\n", strerror(errno), context->src_path);
			result += RESULT_ERROR;
		}
    }
    
	// cleanup and report results
	context->results.result = result;
	dispatch_group_async_f(context->conv_group, dispatch_get_main_queue(), context, (void (*)(void *))finish_image);
}

void initialize_graphics_lib() {
    MagickWandGenesis();
}

void destroy_graphics_lib() {
    MagickWandTerminus();
}

