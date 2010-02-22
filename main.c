/*
 *  main.c
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

#define PICT2PNG_VERSION   "1.0"
#define PICT2PNG_COPYRIGHT "\n  Copyright (C) 2010 Brian D. Wells\n"
#define PICT2PNG_CONTACT   "\n  Author: Brian D. Wells <spam_brian@me.com>\n"
#define PICT2PNG_LICENSE   "\n  pict2png comes with ABSOLUTELY NO WARRANTY. It is free software,\n  and you are welcome to redistribute it under certain conditions.\n  See the man page (type 'man pict2png') for details.\n"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <getopt.h>
#include <sys/syslimits.h>
#include <dirent.h>
#include <sys/errno.h>
#include <dispatch/dispatch.h>

#include "pict2png.h"

static ConvertOptions convert_options = { 
    0,      // verbose OFF
    0,      // quiet   OFF
    0,      // dry_run OFF
    0,      // force   OFF
    0,      // delete  OFF
    0.8     // background at least 80%
};

static int images_converted    = 0;
static int images_skipped      = 0;
static int images_alpha_none   = 0;
static int images_alpha_plain  = 0;
static int images_alpha_black  = 0;
static int images_alpha_white  = 0;
static int images_alpha_other  = 0;
static int images_result       = RESULT_OK;

static dispatch_queue_t load_queue;
static dispatch_queue_t conv_queue;
static dispatch_queue_t save_queue;
static dispatch_group_t conv_group;
static dispatch_semaphore_t conv_semaphore;

int process_path(char *src_path, char *dst_path, char *tmp_path, int complain) {
    struct stat finfo;
    int result = RESULT_OK;
    char *new_src = NULL;
    char *new_dst = NULL;
    char *dir_path = NULL;
    char *file_name = NULL;
    char *file_ext = NULL;
    char *file_type = NULL;
    DIR *directory;
    struct dirent *entry;
    ConvertContext *convert_context;
        
    char *valid_exts[] = { "pict", "pct", "pic", 0 };
    int idx;
        
    // check source path
    if (lstat(src_path, &finfo) == -1) {
        fprintf(stderr, "Unable to access source (%s): %s\n",strerror(errno), src_path);
        result += RESULT_ERROR;
    } else {
        if (S_ISDIR(finfo.st_mode)) {
            // directory
            // check destination path
            if (dst_path == NULL) {
                // same as src_path
                dst_path = src_path;
            } else {
                if (lstat(dst_path, &finfo) == -1) {
                    // create dir
                    if (convert_options.verbose)
                        printf("creating destination folder: %s\n",dst_path);
                    if (!convert_options.dry_run && mkdir(dst_path, 0777) == -1) {
                        fprintf(stderr, "Unable to create destination (%s): %s\n", strerror(errno), dst_path);
                        result += RESULT_ERROR;
                        dst_path = NULL;
                    }
                } else {
                    if (!S_ISDIR(finfo.st_mode)) {
                        // complain about bad destination
                        fprintf(stderr, "Unable to write to %s\n", dst_path);
                        result += RESULT_ERROR;
                        dst_path = NULL;                        
                    }            
                }
            }
            if (dst_path != NULL) {
                // open dir and loop through entries
                directory = opendir(src_path);
                if (directory == NULL) {
                    fprintf(stderr, "Unable to access %s\n", src_path);
                    result += RESULT_ERROR;
                } else {
                    while (result < RESULT_ERROR && (entry = readdir(directory)) != NULL) {
                        if (entry->d_namlen > 0 && *(entry->d_name) != '.') {
                            // got an entry...
                            switch (entry->d_type) {
                                case DT_DIR:
                                    // generate new src_path and dst_path
                                    
                                    if ((entry->d_namlen + 2) > (PATH_MAX - strlen(src_path))) {
                                        // buffer overflow
                                        fprintf(stderr, "Buffer overflow appending: %s\n", src_path);
                                        result += RESULT_ERROR;
                                    } 
                                    if ((entry->d_namlen + 2) > (PATH_MAX - strlen(dst_path))) {
                                        // buffer overflow
                                        fprintf(stderr, "Buffer overflow appending: %s\n", dst_path);
                                        result += RESULT_ERROR;
                                    } 
                                    if (result < RESULT_ERROR) {
                                        strncpy(tmp_path, src_path, PATH_MAX - 1);
                                        strncat(tmp_path, "/", 1);
                                        strncat(tmp_path, entry->d_name, entry->d_namlen);
                                        new_src = strdup(tmp_path);
                                        strncpy(tmp_path, dst_path, PATH_MAX - 1);
                                        strncat(tmp_path, "/", 1);
                                        strncat(tmp_path, entry->d_name, entry->d_namlen);
                                        new_dst = strdup(tmp_path);
                                        result += process_path(new_src, new_dst, tmp_path, 0);
                                        free(new_src);
                                        free(new_dst);
                                    }
                                    
                                    break;
                                case DT_REG:
                                    // generate new src_path
                                    if ((entry->d_namlen + 2) > (PATH_MAX - strlen(src_path))) {
                                        // buffer overflow
                                        fprintf(stderr, "Buffer overflow appending: %s\n", src_path);
                                        result += RESULT_ERROR;
                                    } else {
                                        strncpy(tmp_path, src_path, PATH_MAX - 1);
                                        strncat(tmp_path, "/", 1);
                                        strncat(tmp_path, entry->d_name, entry->d_namlen);
                                        new_src = strdup(tmp_path);
                                        result += process_path(new_src, dst_path, tmp_path, 0);
                                        free(new_src);
                                    }
                                    break;
                                default:
                                    // ignore other types of entries
                                    break;
                            }
                            
                        }
                    }
                    closedir(directory);
                }
            }
        } else if (S_ISREG(finfo.st_mode)) {
            // file
            strncpy(tmp_path, src_path, PATH_MAX - 1);
            file_name = strdup(basename(tmp_path));
            strncpy(tmp_path, src_path, PATH_MAX - 1);
            dir_path = strdup(dirname(tmp_path));
            file_ext = strrchr(file_name, '.');
            // check file extension
            idx = 0;
            while (file_ext != NULL) {
                if (valid_exts[idx] == 0) {
                    file_ext = NULL;
                } else if (strcasecmp(valid_exts[idx], file_ext + 1) == 0) {
                    break;
                }
                idx++;
            }
            if (file_ext == NULL) {
                // check file type
                if (getxattr(src_path, "com.apple.FinderInfo", tmp_path, PATH_MAX, 0, XATTR_NOFOLLOW) >= 4) {
                    if (strncmp(tmp_path, "PICT", 4) == 0) {
                        tmp_path[4] = '\0';
                        file_type = tmp_path;
                    }
                }
            }
            
            if (file_ext != NULL || file_type != NULL) {
                // check destination path
                if (dst_path == NULL) {
                    // same as src_path, but with proper extension
                    if ((strlen(file_name) - strlen(file_ext) + 6) > (PATH_MAX - strlen(dir_path))) {
                        // buffer overflow
                        fprintf(stderr, "Buffer overflow appending: %s\n", src_path);
                        result += RESULT_ERROR;
                    } else {
                        strncpy(tmp_path, dir_path, PATH_MAX - 1);
                        strncat(tmp_path, "/", 1);
                        strncat(tmp_path, file_name, strlen(file_name) - strlen(file_ext));
                        strncat(tmp_path, ".png", 4);
                        dst_path = tmp_path;
                    }
                }
                while (dst_path != NULL && lstat(dst_path, &finfo) != -1) {
                    if (S_ISDIR(finfo.st_mode)) {
                        // directory, so put file inside
                        if ((strlen(file_name) - strlen(file_ext) + 6) > (PATH_MAX - strlen(dst_path))) {
                            // buffer overflow
                            fprintf(stderr, "Buffer overflow appending: %s\n", dst_path);
                            result += RESULT_ERROR;
                        } else {
                            strncpy(tmp_path, dst_path, PATH_MAX - 1);
                            strncat(tmp_path, "/", 1);
                            strncat(tmp_path, file_name, strlen(file_name) - strlen(file_ext));
                            strncat(tmp_path, ".png", 4);
                            dst_path = tmp_path;
                        }
                    } else if (S_ISREG(finfo.st_mode)) {
                        // got a file... will try to overwrite
                        break;
                    } else {
                        // not a file
                        fprintf(stderr, "Unable to write to %s\n", dst_path);
                        result += RESULT_ERROR;
                        dst_path = NULL;
                    }   
                }
                                
                // convert file
                if (dst_path != NULL) {
                    // init context
                    convert_context = calloc(1, sizeof(ConvertContext));
                    convert_context->load_queue = load_queue;
                    convert_context->conv_queue = conv_queue;
                    convert_context->save_queue = save_queue;
                    convert_context->conv_group = conv_group;
					convert_context->conv_semaphore = conv_semaphore;
                    convert_context->src_path = strdup(src_path);
                    convert_context->dst_path = strdup(dst_path);
                    convert_context->options = convert_options;

                    process_image(convert_context);
                }
            
            } else {
                if (complain) {
                    fprintf(stderr, "Not a PICT file: %s\n", src_path);
                    result += RESULT_ERROR;
                }
            }
            
            free(file_name);
            free(dir_path);
        } else {
            // unknown type
            if (complain) {
                fprintf(stderr, "Unknown type: %s\n", src_path);
                result += RESULT_ERROR;
            }
        }
    }

    return result;
}

int main (int argc, const char * argv[]) {
    int result = 0;
    int show_usage = 0;
    int show_version = 0;
    char *src_path = NULL;
    char *dst_path = NULL;
    char *tmp_path = NULL;
    char *dir_path = NULL;
    char *file_name = NULL;
    char *endp;

    static struct option options[] = {
        { "bkgnd-ratio", required_argument, NULL, 'b' },
        { "delete",         no_argument,    NULL, 'd' },
        { "force",          no_argument,    NULL, 'f' },
        { "quiet",          no_argument,    NULL, 'q' },
        { "verbose",        no_argument,    NULL, 'v' },
        { "dry-run",        no_argument,    NULL, 'n' },
		{ "version",        no_argument,    NULL, 'V' },
        { "help",           no_argument,    NULL, 'h' },
        { NULL,             0,              NULL, 0 }
    };
    static char *options_str = "b:dfqvnVh";
    
    int opt = 0;
    while ((opt = getopt_long(argc, (char **)argv, options_str, options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                convert_options.bkgnd_ratio = strtod(optarg, &endp);
                if (*endp != '\0' || convert_options.bkgnd_ratio <= 0.0 || convert_options.bkgnd_ratio > 1.0) {
                    printf("Background ratio out of range (0.1 to 1.0): %g\n", convert_options.bkgnd_ratio);
                    show_usage++;
                }
                break;
            case 'd':
                convert_options.delete_original++;
                break;
            case 'f':
                convert_options.force++;
                break;
            case 'q':
                convert_options.quiet++;
                break;
            case 'v':
                convert_options.verbose++;
                break;
            case 'n':
                convert_options.dry_run++;
                break;
            case 'V':
                show_version++;
                break;
            case 'h':
                show_usage++;
                break;
            default:
                show_usage++;
                break;
        }
    }

    // allocate buffer
    tmp_path = malloc(PATH_MAX);
    
    // get path arguments
    int path_count = argc - optind;
    if (path_count < 1 || path_count > 2) {
        show_usage++;
    } else {
        // get source path
        src_path = realpath(*(argv + optind), NULL);
        if (src_path == NULL) {
            printf("Source not found: '%s'\n\n",*(argv + optind));
            show_usage++;
        }
        if (path_count > 1) {
            // get destination path
            if (realpath(*(argv + optind + 1), tmp_path) != NULL) {
                dst_path = strdup(tmp_path);                
            } else {
                strncpy(tmp_path, *(argv + optind + 1), PATH_MAX - 1);
                file_name = strdup(basename(tmp_path));
                strncpy(tmp_path, *(argv + optind + 1), PATH_MAX - 1);
                dir_path = strdup(dirname(tmp_path));
                if (realpath(dir_path, tmp_path) == NULL) {
                    printf("Destination not found '%s'\n\n",dir_path);
                    show_usage++;                    
                } else {
                    asprintf(&dst_path, "%s/%s",tmp_path,file_name);
                }
                free(file_name);
                free(dir_path);
            }
        }
    }

    // process file(s)
	if (show_version) {
		printf("pict2png v%s\n", PICT2PNG_VERSION);
		printf(PICT2PNG_COPYRIGHT);
		printf(PICT2PNG_LICENSE);
		printf(PICT2PNG_CONTACT);
	} else if (show_usage) {
        printf("usage: pict2png [flags] <src> [<dst>]\n");
        printf("    --verbose        Increase status messages\n");
        printf("    --quiet          Do not show summary at end of process\n");
        printf("    --dry-run        Do not write converted filesto disk\n");
        printf("    --delete         Delete original PICT files (use with caution)\n");
        printf("    --force          Force conversion of files that have issues\n");
        printf("    --bkgnd-ratio=x  Adjust required ratio of background color\n");
        printf("    --help           Display usage information.\n");
        printf("    --version        Display version information.\n");
        result = 1;

        // cleanup
		if (src_path != NULL)
			free(src_path);
		if (dst_path != NULL)
			free(dst_path);
		if (tmp_path != NULL)
			free(tmp_path);
	} else {

        initialize_graphics_lib();

        // setup GCD
        load_queue = dispatch_queue_create("com.briandwells.pict2png.load", NULL);
        save_queue = dispatch_queue_create("com.briandwells.pict2png.save", NULL);
        conv_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
        conv_group = dispatch_group_create();
		conv_semaphore = dispatch_semaphore_create(32);

        // start processing files
        if (process_path(src_path, dst_path, tmp_path, 1) != 0) {
            // ignore errors here, but increment result value.
            images_result = 2;
        }

		// prepare to clean up when all files are processed
		dispatch_group_notify(conv_group, dispatch_get_main_queue(), ^{
			// cleanup
			if (src_path != NULL)
				free(src_path);
			if (dst_path != NULL)
				free(dst_path);
			if (tmp_path != NULL)
				free(tmp_path);

			dispatch_release(load_queue);
			dispatch_release(conv_queue);	// does nothing to global queue
			dispatch_release(save_queue);
			dispatch_release(conv_semaphore);

			destroy_graphics_lib();

			// show summary
			if (convert_options.quiet == 0) {
				printf("\npict2png: %d image%c converted",images_converted,(images_converted == 1 ? ' ' : 's'));
				if (images_skipped > 0)
					printf(", %d image%c skipped",images_skipped,(images_skipped == 1 ? ' ' : 's'));
				printf("\n");
				if (images_alpha_none > 0)
					printf("          %d image%c with no alpha channel\n",images_alpha_none,(images_alpha_none == 1 ? ' ' : 's'));
				if (images_alpha_plain > 0)
					printf("          %d image%c with an unassociated alpha channel\n",images_alpha_plain,(images_alpha_plain == 1 ? ' ' : 's'));
				if (images_alpha_black > 0)
					printf("          %d image%c with an associated alpha channel and black background\n",images_alpha_black,(images_alpha_black == 1 ? ' ' : 's'));
				if (images_alpha_white > 0)
					printf("          %d image%c with an associated alpha channel and white background\n",images_alpha_white,(images_alpha_white == 1 ? ' ' : 's'));
				if (images_alpha_other > 0)
					printf("          %d image%c with an associated alpha channel and other background\n\n",images_alpha_other,(images_alpha_other == 1 ? ' ' : 's'));
				if (convert_options.dry_run) {
					printf("          The 'dry run' option prevented any changes from being written to disk.\n");
				} else if (convert_options.delete_original) {
					printf("          The original files were deleted after a successful image conversion.\n");
				}
			}

			// exit with result
			exit(images_result);
		});
		dispatch_release(conv_group);

		// start main loop (does not return)
		dispatch_main();
    }
    
    return result;
}

void finish_image(ConvertContext *context) {

	// free up resources
	dispatch_semaphore_signal(context->conv_semaphore);

	// report results
	if (context->results.message != NULL) {
		fprintf((context->results.result == RESULT_OK ? stdout : stderr), "%s", context->results.message);
		free(context->results.message);
	}

	if (context->results.result == RESULT_OK) {
		images_converted++;
		if (context->options.verbose)
			printf("converted image with ");
		if (context->results.alpha_type == ALPHA_TYPE_NONE) {
			images_alpha_none++;
			if (context->options.verbose)
				printf("no");
		} else if (context->results.alpha_type == ALPHA_TYPE_UNASSOCIATED) {
			images_alpha_plain++;
			if (context->options.verbose)
				printf("unassociated");
		} else if (context->results.alpha_type == ALPHA_TYPE_ASSOCIATED) {
			if (context->options.verbose)
				printf("associated ");
			switch (context->results.bkgnd_type) {
				case BKGND_BLACK:
					images_alpha_black++;
					if (context->options.verbose)
						printf("BLACK");
					break;
				case BKGND_WHITE:
					images_alpha_white++;
					if (context->options.verbose)
						printf("WHITE");
					break;
				case BKGND_OTHER:
					images_alpha_other++;
					if (context->options.verbose)
						printf("(%hhu %hhu %hhu)",context->results.bkgnd_red, context->results.bkgnd_grn, context->results.bkgnd_blu);
					break;
				default:
					break;
			}
		}
		if (context->options.verbose)
			printf(" alpha channel: %s to %s\n",context->src_path, context->dst_path);
	} else {
		images_skipped++;
		images_result = 2;
	}

	// clean up
    if (context->pixels)
        free(context->pixels);
	context->mw = DestroyMagickWand(context->mw);
	free(context->src_path);
	free(context->dst_path);
	free(context);
}


