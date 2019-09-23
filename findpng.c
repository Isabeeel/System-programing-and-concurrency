/**
 * @biref To demonstrate how to use zutil.c and crc.c functions
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */

#include <errno.h>  /* for errno                   */
#include <stdio.h>  /* for printf(), perror()...   */
#include <stdlib.h> /* for malloc()                */
//#include "crc.h"      /* for crc()                   */
#include <dirent.h>
#include <string.h> /* for strcat().  man strcat   */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "lab_png.h" /* simple PNG data structures  */

/******************************************************************************
 * DEFINED MACROS
 *****************************************************************************/
#define BUF_LEN (256 * 16)
#define BUF_LEN2 (256 * 32)

/******************************************************************************
 * GLOBALS
 *****************************************************************************/
U8 gp_buf_def[BUF_LEN2]; /* output buffer for mem_def() */
U8 gp_buf_inf[BUF_LEN2]; /* output buffer for mem_inf() */

/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/

void init_data(U8 *buf, int len);
int ls_ftype(char *argv);
int ls_fname(char *argv);

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * @brief initialize memory with 256 chars 0 - 255 cyclically
 */
void init_data(U8 *buf, int len) {
    int i;
    for (i = 0; i < len; i++) {
        buf[i] = i % 256;
    }
}

int ls_ftype(char *argv) {
    char *ptr;
    struct stat buf;
    // printf(" 333333 %s \n", argv );
    if (lstat(argv, &buf) < 0) {
        perror("lstat error");
        return 0;
    }

    if (S_ISREG(buf.st_mode)) {
        ptr = "regular";
        // printf(" 333333 %s \n", ptr );
        return 1;
    } else if (S_ISDIR(buf.st_mode)) {
        ptr = "directory";
        // printf(" 333333 %s \n", ptr );
        return 2;
    } else if (S_ISCHR(buf.st_mode))
        ptr = "character special";
    else if (S_ISBLK(buf.st_mode))
        ptr = "block special";
    else if (S_ISFIFO(buf.st_mode))
        ptr = "fifo";
#ifdef S_ISLNK
    else if (S_ISLNK(buf.st_mode))
        ptr = "symbolic link";
#endif
#ifdef S_ISSOCK
    else if (S_ISSOCK(buf.st_mode))
        ptr = "socket";
#endif
    else
        ptr = "**unknown mode**";
    // printf(" 333333 %s \n", ptr );
    return 3;
}

int ls_fname(char *argv) {
    DIR *p_dir;
    struct dirent *p_dirent;
    char *last_path = malloc(sizeof(char) );
    if ((p_dir = opendir(argv)) == NULL) {
        return 1;
    }

    while ((p_dirent = readdir(p_dir)) != NULL) {
        char *absolute_path = argv;

        if (absolute_path == NULL) {
            return 1;
        } else {
            char *str_path = p_dirent->d_name; /* relative path name! */
            char *abs_path =
                    (char *)malloc(strlen(absolute_path) + strlen(str_path) + 1);
            // abs_path += str_path;
            strcpy(abs_path, absolute_path);
            strcat(abs_path, "/");
            strcat(abs_path, str_path);  // path of sub
            // printf("%s 999999 \n", abs_path);
            U8 *p_buffer =
                    NULL; /* a buffer that contains some data to play with */
            last_path = str_path;
            if (str_path[0] == '.') {
                continue;
            }

            int ftype = ls_ftype(abs_path);

            if (ftype == 2) {
                // printf("%s 111111 \n", abs_path);
                ls_fname(abs_path);

            } else if (ftype == 1) {
                FILE *f = fopen(abs_path, "rb");
                // printf("%s 222222 \n", abs_path);
                if (f == NULL) {
                    printf("findpng: No PNG file found \n");
                    continue;
                }
                fseek(f, 0, SEEK_END);
                long fln = ftell(f);  // length
                p_buffer = (U8 *)malloc(fln + 1);
                if (p_buffer == NULL) {
                    perror("malloc");
                    return errno;
                }
                rewind(f);
                fread(p_buffer, fln, 1, f);
                // is png
                int k = is_png(p_buffer, fln);
                if (k == 0) {
                    printf(" %s \n", abs_path);  // print png path
                }/*else{
                    return 1;
                }*/
                fclose(f);
                free(p_buffer);
            }
        }
    }

    if (last_path[0] == '.') {
        return 1;
    }

    if (closedir(p_dir) != 0) {
        perror("closedir");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }

    char *direct = argv[1];
    int k = ls_ftype(direct);

    // checkerror?
    if (k == 2) {
        int l = ls_fname(direct);
        if (l == 0) {
            return 0;
        } else {
            printf("findpng: No PNG file found \n");
            return 1;
        }
    }else{
        printf( "not a directory \n" );
    }
    return 1;
}
