/**
 * @biref To demonstrate how to use zutil.c and crc.c functions
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */

#include <stdio.h>    /* for printf(), perror()...   */
#include <stdlib.h>   /* for malloc()                */
#include <errno.h>    /* for errno                   */
#include "crc.h"      /* for crc()                   */
#include "lab_png.h"  /* simple PNG data structures  */

/******************************************************************************
 * DEFINED MACROS
 *****************************************************************************/
#define BUF_LEN  (256*16)
#define BUF_LEN2 (256*32)

/******************************************************************************
 * GLOBALS
 *****************************************************************************/
U8 gp_buf_def[BUF_LEN2]; /* output buffer for mem_def() */
U8 gp_buf_inf[BUF_LEN2]; /* output buffer for mem_inf() */

/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/

void init_data(U8 *buf, int len);

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

/**
 * @brief initialize memory with 256 chars 0 - 255 cyclically
 */
void init_data(U8 *buf, int len)
{
    int i;
    for ( i = 0; i < len; i++) {
        buf[i] = i%256;
    }
}

int main (int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }
    U8 *p_buffer = NULL;  /* a buffer that contains some data to play with */
    char *file_path = argv[1];
    FILE *f = fopen(file_path, "rb");
    if (f == NULL) {
        printf("File not found \n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fln = ftell(f); //length
    
    /* Step 1: Initialize some data in a buffer */
    /* Step 1.1: Allocate a dynamic buffer */
    p_buffer =(U8 *) malloc(fln+1);
    if (p_buffer == NULL) {
        perror("malloc");
        return errno;
    }
    
    /* Step 1.2: Fill the buffer with some data */
    //init_data(p_buffer, fln);
    rewind(f);
    fread(p_buffer, fln, 1, f);
    //printf("crc_val = %c\n", p_buffer[1]);
    //printf("crc_val = %c\n", p_buffer[2]);
    //printf("crc_val = %c\n", p_buffer[3]);
    // is png
    int k = is_png(p_buffer, fln);
    if (k == 1) {
        printf("%s: Not a PNG file \n", argv[1]);
        return 1;
    }
    
    //get IHDR
    struct data_IHDR *out = malloc(sizeof(U8)*13);
    int result = get_png_data_IHDR( out, f, 16, SEEK_SET);
    if ( result == 0 ) {
        printf("%s: %d x %d \n",argv[1], ntohl(out->width), ntohl(out->height));
    }
    fclose(f);
    return 0;
}
