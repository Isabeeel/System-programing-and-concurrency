/**
 * @biref To demonstrate how to use zutil.c and crc.c functions
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */

#include <errno.h>   /* for errno                   */
#include <stdio.h>   /* for printf(), perror()...   */
#include <stdlib.h>  /* for malloc()                */
#include "lab_png.h" /* simple PNG data structures  */
#include "zutil.h"   /* simple PNG data structures  */

#include <dirent.h>
#include <string.h> /* for strcat().  man strcat   */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/******************************************************************************
 * DEFINED MACROS
 *****************************************************************************/
/******************************************************************************
 * GLOBALS
 *****************************************************************************/
/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/
/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * @brief initialize memory with 256 chars 0 - 255 cyclically
 */

int main(int argc, char **argv) {
    if (argc == 1) {
        printf("No png file, do nothing \n");
        return 1;
    }
    if (argc == 2) {
        printf("Just one png file, do nothing \n");
        return 1;
    }

    int ret = 0;

    // all.png: chunk IHDR, IDAT, IEND (host)
    struct chunk *ck_ihdr_all = malloc(sizeof(struct chunk));
    struct chunk *ck_idat_all = malloc(sizeof(struct chunk));
    struct chunk *ck_iend_all = malloc(sizeof(struct chunk));
    // all.png: IHDR.data (host)
    struct data_IHDR *ihdr_data_all = malloc(sizeof(struct data_IHDR));
    // temp IHDR.data (host)
    struct data_IHDR *ihdr_data_now = malloc(sizeof(struct data_IHDR));

    // check is png
    for (int i = 1; i < argc; ++i) {
        U8 *p_buffer = NULL;
        char *file_path = argv[i];
        printf("%s \n", file_path);
        FILE *f = fopen(file_path, "rb");
        if (f == NULL) {
            printf("File not found \n");
            return 1;
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
        if (k == 1) {
            printf("%s: Not a PNG file \n", argv[i]);
            return 1;
        }
        fclose(f);
        free(p_buffer);
    }
    // unzipped buffer keeper
    U8 **a_buf_unzip = (U8 **)malloc((argc - 1) * sizeof(U8 *));
    U32 *a_len_unzip = (U32 *)malloc((argc - 1) * sizeof(U32 *));
    // read multi pngs
    for (int i = 1; i < argc; ++i) {
        U8 *p_buffer = NULL;
        char *file_path = argv[i];
        FILE *f = fopen(file_path, "rb");
        if (f == NULL) {
            return 1;
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
        fclose(f);
        // only use buffer to avoid file IO, so close f
        U32 cur = 8;   // cur at IHDR.length
        if (i == 1) {  // copy IHDR.length, IHDR.type once
            ck_ihdr_all->length = get_8_to_32(p_buffer + cur);
            cur += 4;
            memcpy(ck_ihdr_all->type, p_buffer + cur, 4);
            cur += 4;
        } else {
            cur += 8;
        }
        // cur at IHDR.data
        // get IHDR.data
        if ((ret = get_png_IHDR_data(ihdr_data_now, p_buffer + cur))) {
            printf("%s: get IHDR.data failed \n", file_path);
            return ret;
        }
        if (i == 1) {  // copy IHDR.data once
            *ihdr_data_all = *ihdr_data_now;
        } else {  // update height
            ihdr_data_all->height += ihdr_data_now->height;
        }

        cur = 33;  // cur at IDAT.length

        U32 len_idat_now = get_8_to_32(p_buffer + cur);  // get IDAT.length
        if (i == 1) {                                    // copy IDAT.type once
            cur += 4;
            memcpy(ck_idat_all->type, p_buffer + cur, 4);
            cur += 4;
        } else {
            cur += 8;
        }
        // cur at IDAT.data
        U64 len_unzip_idat_data_now_64;
        a_len_unzip[i - 1] = (ihdr_data_now->width * 4 + 1) *
                             ihdr_data_now->height;  // bit-depth must be 8
        a_buf_unzip[i - 1] = (U8 *)malloc(a_len_unzip[i - 1]);
        // unzip IDAT.data
        if ((ret = mem_inf(a_buf_unzip[i - 1], &len_unzip_idat_data_now_64,
                           p_buffer + cur, len_idat_now))) {
            printf("unzip %s's IDAT failed \n", file_path);
            return ret;
        }
        cur += len_idat_now;

        // jump IDAT.crc, calc it later out of loop
        cur += 4;  // cur at IEND.length

        // according to manual, IEND has an empty data field, it has 12 bytes
        if (i == 1) {  // copy IEND once
            ck_iend_all->length = 0;
            cur += 4;
            memcpy(ck_iend_all->type, p_buffer + cur, 4);
            cur += 4;
            ck_iend_all->p_data = NULL;
            ck_iend_all->crc = get_8_to_32(p_buffer + cur);
        }
        free(p_buffer);
    }
    U32 len_unzip_idat_data_all = 0;
    for (int i = 1; i < argc; i++) {
        len_unzip_idat_data_all += a_len_unzip[i - 1];
    }
    U8 *buf_unzip_idat_data_all = (U8 *)malloc(len_unzip_idat_data_all);
    len_unzip_idat_data_all = 0;
    for (int i = 1; i < argc; i++) {
        memcpy(buf_unzip_idat_data_all + len_unzip_idat_data_all,
               a_buf_unzip[i - 1], a_len_unzip[i - 1]);
        len_unzip_idat_data_all += a_len_unzip[i - 1];
        free(a_buf_unzip[i - 1]);
    }
    // prepare IHDR: ihdr_data_all save to ck_ihdr_all
    if ((ret = data_IHDR_to_chunk(ck_ihdr_all, ihdr_data_all))) {
        return ret;
    }
    // prepare IDAT: zip data
    U8 *buf_zip_idat_data_all = (U8 *)malloc(len_unzip_idat_data_all);
    U64 len_zip_idat_data_all;
    U8 gp_buff[len_zip_idat_data_all];
    if ((ret = mem_def(buf_zip_idat_data_all, &len_zip_idat_data_all,
                       buf_unzip_idat_data_all, len_unzip_idat_data_all,
                       Z_BEST_COMPRESSION))) {
        return ret;
    }
    ck_idat_all->length = (U32)len_zip_idat_data_all;
    ck_idat_all->p_data = buf_zip_idat_data_all;

    U32 len_now_ck = 0;
    U32 len_file_all = 8 + ck_ihdr_all->length + 12 + ck_idat_all->length + 12 +
                       ck_iend_all->length + 12;
    U8 *buf_file_all = (U8 *)malloc(len_file_all);
    len_file_all = 0;
    // write png header
    U8 png_header[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(buf_file_all, png_header, 8);
    len_file_all += 8;
    // write IHDR
    if ((ret = chunk_to_buffer(ck_ihdr_all, buf_file_all + len_file_all,
                               &len_now_ck, 1))) {
        printf("IHDR chunk to buffer failed \n");
        return ret;
    }
    len_file_all += len_now_ck;
    // write IDAT
    if ((ret = chunk_to_buffer(ck_idat_all, buf_file_all + len_file_all,
                               &len_now_ck, 1))) {
        printf("IDAT chunk to buffer failed\n");
        return ret;
    }
    len_file_all += len_now_ck;
    // write IEND
    if ((ret = chunk_to_buffer(ck_iend_all, buf_file_all + len_file_all,
                               &len_now_ck, 1))) {
        printf("IEND chunk to buffer failed\n");
        return ret;
    }
    len_file_all += len_now_ck;

    // save to all.png
    FILE *f_all = fopen("all.png", "wb");
    if (f_all == NULL) {
        printf("cannot write to all.png \n");
        return 1;
    }
    fwrite(buf_file_all, 1, len_file_all, f_all);
    fclose(f_all);
    printf("result write to all.png \n");
    free(ck_ihdr_all);
    free(ck_idat_all);
    free(ck_iend_all);
    free(ihdr_data_all);
    free(ihdr_data_now);
    free(a_buf_unzip);
    free(a_len_unzip);
    free(buf_unzip_idat_data_all);
    free(buf_zip_idat_data_all);
    free(buf_file_all);
    return 0;
}
