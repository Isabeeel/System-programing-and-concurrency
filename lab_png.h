/**
 * @brief  micros and structures for a simple PNG file
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */
#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdio.h>
#include <string.h>
#include "crc.h" /* for crc()                   */
/******************************************************************************
 * DEFINED MACROS
 *****************************************************************************/

#define PNG_SIG_SIZE 8    /* number of bytes of png image signature data */
#define CHUNK_LEN_SIZE 4  /* chunk length field size in bytes */
#define CHUNK_TYPE_SIZE 4 /* chunk type field size in bytes */
#define CHUNK_CRC_SIZE 4  /* chunk CRC field size in bytes */
#define DATA_IHDR_SIZE 13 /* IHDR chunk data field size */

/******************************************************************************
 * STRUCTURES and TYPEDEFS
 *****************************************************************************/
typedef unsigned char U8;
typedef unsigned int U32;

typedef struct chunk {
    U32 length; /* length of data in the chunk, host byte order */
    U8 type[4]; /* chunk type */
    U8 *p_data; /* pointer to location where the actual data are */
    U32 crc;    /* CRC field  */
} * chunk_p;

/* note that there are 13 Bytes valid data, compiler will padd 3 bytes to make
   the structure 16 Bytes due to alignment. So do not use the size of this
   structure as the actual data size, use 13 Bytes (i.e DATA_IHDR_SIZE macro).
 */
typedef struct data_IHDR {  // IHDR chunk data
    U32 width;              /* width in pixels, big endian   */
    U32 height;             /* height in pixels, big endian  */
    U8 bit_depth;           /* num of bits per sample or per palette index.
                               valid values are: 1, 2, 4, 8, 16 */
    U8 color_type;          /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                               =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8 compression;         /* only method 0 is defined for now */
    U8 filter;              /* only method 0 is defined for now */
    U8 interlace;           /* =0: no interlace; =1: Adam7 interlace */
} * data_IHDR_p;

/* A simple PNG file format, three chunks only*/
typedef struct simple_PNG {
    struct chunk *p_IHDR;
    struct chunk *p_IDAT; /* only handles one IDAT chunk */
    struct chunk *p_IEND;
} * simple_PNG_p;

/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/
U32 get_8_to_32(U8 *address);
int is_png(U8 *buf, size_t n);
int get_png_height(struct data_IHDR *buf);
int get_png_width(struct data_IHDR *buf);
int get_png_IHDR_data(struct data_IHDR *st_IHDR_data, const U8 *buf_file);
int chunk_to_buffer(struct chunk *st_chunk, U8 *buf_result, U32 *len_result,
                    int calc_crc);
/* declare your own functions prototypes here */
int is_png(U8 *buf, size_t n) {
    if (n == 0) {
        return 1;
    }

    U8 png_header[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    for (int i = 0; i < 8; i++) {
        if (buf[i] != png_header[i]) {
            return 1;
        }
    }
    // struct simple_PNG myPng;
    // IHDR
    U8 *start_p = buf + 12;
    U32 ihdr_cal_crc = crc(start_p, 17);  // type4+data13
    U32 ihdr_exp_crc = get_8_to_32(buf + 12 + 17);
    if (ihdr_cal_crc != ihdr_exp_crc) {
        printf("IHDR chunk CRC error: computed %x, expected %x\n", ihdr_cal_crc,
               ihdr_exp_crc);
        return 0;
    }

    // IDAT
    U8 *start_idat_p = buf + 37;
    U32 idat_cal_crc = crc(start_idat_p, 4 + get_8_to_32(buf + 33));
    // printf("IEND chunk CRC error: computed %d \n", get_8_to_32(buf+33) );
    U32 idat_exp_crc = get_8_to_32(start_idat_p + 4 + get_8_to_32(buf + 33));
    if (idat_cal_crc != idat_exp_crc) {
        printf("IDAT chunk CRC error: computed %x, expected %x\n", idat_cal_crc,
               idat_exp_crc);
        return 0;
    }

    // IEND
    U8 *start_iend_p = buf + 37 + 4 + get_8_to_32(buf + 33) + 8;
    U32 iend_cal_crc = crc(start_iend_p, 4);
    U32 iend_exp_crc = get_8_to_32(start_iend_p + 4);
    if (iend_cal_crc != iend_exp_crc) {
        printf("IEND chunk CRC error: computed %x, expected %x\n", iend_cal_crc,
               iend_exp_crc);
        return 0;
    }

    return 0;
}

int get_png_height(struct data_IHDR *buf) { return buf->height; }

int get_png_width(struct data_IHDR *buf) { return buf->width; }

int get_png_IHDR_data(struct data_IHDR *st_data_IHDR, const U8 *buf_file) {
    // buf_file must be the begin of IHDR data field
    memcpy(&st_data_IHDR->width, buf_file, 4);
    st_data_IHDR->width = ntohl(st_data_IHDR->width);
    memcpy(&st_data_IHDR->height, buf_file + 4, 4);
    st_data_IHDR->height = ntohl(st_data_IHDR->height);
    memcpy(&st_data_IHDR->bit_depth, buf_file + 8, 1);
    memcpy(&st_data_IHDR->color_type, buf_file + 9, 1);
    memcpy(&st_data_IHDR->compression, buf_file + 10, 1);
    memcpy(&st_data_IHDR->filter, buf_file + 11, 1);
    memcpy(&st_data_IHDR->interlace, buf_file + 12, 1);
    return 0;
}

int data_IHDR_to_chunk(struct chunk *st_chunk, struct data_IHDR *st_data_IHDR) {
    struct data_IHDR *st_data_IHDR_net =
            (struct data_IHDR *)malloc(sizeof(struct data_IHDR));
    *st_data_IHDR_net = *st_data_IHDR;
    st_data_IHDR_net->width = htonl(st_data_IHDR->width);
    st_data_IHDR_net->height = htonl(st_data_IHDR->height);
    st_chunk->p_data = (U8 *)st_data_IHDR_net;
    return 0;
}

U32 get_8_to_32(U8 *address) {
    U32 res = 0;
    memcpy(&res, address, 4);
    return ntohl(res);
}

int chunk_to_buffer(struct chunk *st_chunk, U8 *buf_result, U32 *len_result,
                    int calc_crc) {
    // buf_result caller supply
    *len_result = st_chunk->length + 12;
    U32 net_length = htonl(st_chunk->length);
    memcpy(buf_result, &net_length, 4);
    memcpy(buf_result + 4, st_chunk->type, 4);
    memcpy(buf_result + 8, st_chunk->p_data, st_chunk->length);
    if (calc_crc) {
        st_chunk->crc = crc(buf_result + 4, 4 + st_chunk->length);
    }
    U32 net_crc = htonl(st_chunk->crc);
    memcpy(buf_result + 8 + st_chunk->length, &net_crc, 4);
    return 0;
}
