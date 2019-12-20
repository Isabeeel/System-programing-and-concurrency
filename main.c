/*
 * The code is derived from cURL example and paster.c base code.
 * The cURL example is at URL:
 * https://curl.haxx.se/libcurl/c/getinmemory.html
 * Copyright (C) 1998 - 2018, Daniel Stenberg, <daniel@haxx.se>, et al..
 *
 * The xml example code is
 * http://www.xmlsoft.org/tutorial/ape.html
 *
 * The paster.c code is
 * Copyright 2013 Patrick Lam, <p23lam@uwaterloo.ca>.
 *
 * Modifications to the code are
 * Copyright 2018-2019, Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 license.
 */

/**
 * @file main_wirte_read_cb.c
 * @brief cURL write call back to save received data in a user defined memory
 * first and then write the data to a file for verification purpose. cURL header
 * call back extracts data sequence number from header if there is a sequence
 * number.
 * @see https://curl.haxx.se/libcurl/c/getinmemory.html
 * @see https://curl.haxx.se/libcurl/using/
 * @see https://ec.haxx.se/callback-write.html
 */

#include <curl/curl.h>
#include <errno.h>
#include <libxml2/libxml/HTMLparser.h>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/uri.h>
#include <libxml2/libxml/xpath.h>
#include <pthread.h>
#include <search.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "lab_png.h"

#define URL_LEN 256
#define FILE_LEN 256
#define THREAD_MAX 100
#define HASH_SIZE 1000

#define DEFAULT_SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576 /* 1024*1024 = 1M */
#define BUF_INC 524288   /* 1024*512  = 0.5M */

#define CT_PNG "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN 9
#define CT_HTML_LEN 9

#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
    /* <0 indicates an invalid seq number */
} RECV_BUF;

/* for log */
// abort when cond != 0
#define LOG_ABORT(cond)        \
    do {                       \
        if ((cond)) {          \
            perror(#cond);     \
            printf("abort\n"); \
            abort();           \
        }                      \
    } while (0)

//#define LOGING

#ifdef LOGING
#define LOG_INFO(args...) \
    do {                  \
        printf(args);     \
    } while (0)
#else
#define LOG_INFO(args...) while (0)
#endif
#define LOG_THREAD(fmt, args...) LOG_INFO("[%2d] " fmt, thread_id, ##args)
/**
 * @brief thread-safe logger, need init
 *
 */
typedef struct logger_t {
    pthread_mutex_t *lock;
    FILE *log_file;
} logger_t;
logger_t *logger;
void logger_init(char *file_path) {  // should use once
    if (logger == NULL) {
        logger = (logger_t *)malloc(sizeof(logger_t));
        logger->lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    }
    if (*file_path == '\0')
        logger->log_file = NULL;
    else {
        logger->log_file = fopen(file_path, "w");
        LOG_ABORT(logger->log_file == NULL);
    }
    pthread_mutex_init(logger->lock, NULL);
}
void logger_destruct() {
    if (logger == NULL) return;
    if (logger->log_file != NULL) fclose(logger->log_file);
    pthread_mutex_destroy(logger->lock);
    free(logger->lock);
    free(logger);
}
#define LOG(args...)                            \
    do {                                        \
        if (logger->log_file == NULL) {         \
            pthread_mutex_lock(logger->lock);   \
            printf("[LOG] " args);              \
            pthread_mutex_unlock(logger->lock); \
        } else {                                \
            pthread_mutex_lock(logger->lock);   \
            fprintf(logger->log_file, args);    \
            pthread_mutex_unlock(logger->lock); \
        }                                       \
    } while (0)

/**
 * @brief message_t, type=0 a new url is enqueued; type=1 finished
 *
 */
typedef struct message_t {
    int type;
    char url[URL_LEN];
} message_t;
message_t *message_new(int type, char *url) {
    message_t *message = (message_t *)malloc(sizeof(message_t));
    message->type = type;
    if (url == NULL) return message;
    strncpy(message->url, url, URL_LEN);
    return message;
}
/**
 * @brief simple queue, not thread safe, need_init
 *
 */
typedef struct node_t {
    message_t *message;
    struct node_t *nxt;
} node_t;
typedef struct queue_t {
    int size;
    node_t *head, *tail;
} queue_t;
queue_t msg_queue;
void msg_queue_init() {
    msg_queue.head = msg_queue.tail = NULL;
    msg_queue.size = 0;
}
int msg_queue_size() { return msg_queue.size; }
void msg_queue_push_back(message_t *message) {  // just push, not copy
    if (message->type == 0) {
        LOG_INFO("enqueue %s\n", message->url);
    } else {
        LOG_INFO("enqueue exit\n");
    }
    node_t *o = (node_t *)malloc(sizeof(node_t));
    o->message = message;
    o->nxt = NULL;
    if (msg_queue.head == NULL) {
        msg_queue.head = msg_queue.tail = o;
    } else {
        msg_queue.tail->nxt = o;
        msg_queue.tail = o;
    }
    msg_queue.size++;
}
void msg_queue_front(message_t **message) {
    LOG_ABORT(msg_queue.head == NULL);
    *message = msg_queue.head->message;
}
void msg_queue_pop_front() {
    LOG_ABORT(msg_queue.head == NULL);
    node_t *newhead = msg_queue.head->nxt;
    free(msg_queue.head);
    if (newhead == NULL)
        msg_queue.head = msg_queue.tail = NULL;
    else
        msg_queue.head = newhead;
    msg_queue.size--;
}
void msg_queue_destruct() {
    node_t *p, *q;
    q = msg_queue.head;
    while (q != NULL) {
        p = q;
        q = q->nxt;
        free(p->message);
        free(p);
    }
}

/* global args */
int t, m;
char SEED_URL[URL_LEN], LOG_FILE[URL_LEN];
/* global variables */
int cnt_crawing = 0, cnt_image = 0;
FILE *FILE_IMG_RESULT;
/* semaphores */
sem_t *sem_queue;
/* mutex */
pthread_mutex_t *lock_hash, *lock_add_image;
// there should be always just 1 thread listen to queue
// lock queue size and cnt_crawing together
// state is {msg_queue.size, cnt_crawing}
pthread_mutex_t *lock_state;

/* hash table operations, should be locked */
int url_index = 0;
char *url_pool[HASH_SIZE];
void hash_init() {
    hcreate(HASH_SIZE);
    url_index = 0;
}
int hash_exist(char *url) {
    ENTRY e;
    e.data = NULL;
    e.key = url;
    return hsearch(e, FIND) != NULL;
}
int hash_insert(char *url) {
    // url should not exist in table!
    ENTRY e;
    url_index++;
    e.data = NULL;
    url_pool[url_index++] = e.key = (char *)malloc(URL_LEN);
    strncpy(e.key, url, URL_LEN);
    return hsearch(e, ENTER) == NULL;
}
void hash_destroy() {
    hdestroy();
    for (int i = 0; i < url_index; i++) free(url_pool[i]);
    url_index = 0;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath);
int find_http(int thread_id, char *fname, int size, int follow_relative_links,
              const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb,
                      void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(int thread_id, CURL *curl_handle, RECV_BUF *p_recv_buf);

/* html parse functions */
htmlDocPtr mem_getdoc(char *buf, int size, const char *url) {
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
               HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

    if (doc == NULL) {
        LOG_INFO("Document not parsed successfully.\n");
        // fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath) {
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        LOG_INFO("Error in xmlXPathNewContext\n");
        // printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        LOG_INFO("Error in xmlXPathEvalExpression\n");
        // printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
        xmlXPathFreeObject(result);
        LOG_INFO("No result\n");
        // printf("No result\n");
        return NULL;
    }
    return result;
}

/**
 * @brief check in hash table
 *        make message of url
 *        enqueue the message
 *
 * @param url
 */
void update_url_result(int thread_id, char *url) {
    pthread_mutex_lock(lock_hash);

    if (hash_exist(url)) {
        LOG_THREAD("found repeat url %s\n", url);
    } else {
        LOG_ABORT(hash_insert(url));
        LOG_THREAD("add url %s\n", url);
        pthread_mutex_lock(lock_state);
        message_t *message = message_new(0, url);
        msg_queue_push_back(message);
        sem_post(sem_queue);
        pthread_mutex_unlock(lock_state);
    }
    pthread_mutex_unlock(lock_hash);
}

int find_http(int thread_id, char *buf, int size, int follow_relative_links,
              const char *base_url) {
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar *)"//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset(doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(
                doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *)base_url);
                xmlFree(old);
            }
            if (href != NULL && !strncmp((const char *)href, "http", 4)) {
                update_url_result(thread_id, (char *)href);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    return 0;
}

/**
 * @brief  cURL header call back function to extract image sequence number from
 *         http header data. An example header for image part n (assume n = 2)
 * is: X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the
 * full header data are received.  we are only interested in the ECE252_HEADER
 * line received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;
    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv,
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb,
                      void *p_userdata) {
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
    if (p->size + realsize + 1 > p->max_size) { /* hope this rarely happens */
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }
    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;
    return realsize;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
    void *p = NULL;
    if (ptr == NULL) {
        return 1;
    }
    p = malloc(max_size);
    if (p == NULL) {
        return 2;
    }
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr) {
    if (ptr == NULL) {
        return 1;
    }
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

int process_html(int thread_id, CURL *curl_handle, RECV_BUF *p_recv_buf) {
    int follow_relative_link = 1;
    char *url = NULL;

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(thread_id, p_recv_buf->buf, p_recv_buf->size,
              follow_relative_link, url);
    return 0;
}

/**
 * @brief check in hash table
 *        add cnt_image
 *        output to png_urls.txt
 *
 * @param url image url
 */
void update_image_result(int thread_id, char *url) {
    pthread_mutex_lock(lock_add_image);
    if (cnt_image < m) {
        LOG_THREAD("add image %s\n", url);
        cnt_image++;
        fprintf(FILE_IMG_RESULT, "%s\n", url);
    }
    pthread_mutex_unlock(lock_add_image);
}

int process_png(int thread_id, CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL; /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    LOG_THREAD("\n>[          processing png %s          ]<\n\n", eurl);
    if (eurl != NULL) {
        if (is_png((U8 *)p_recv_buf->buf, p_recv_buf->size) == 0) {
            update_image_result(thread_id, eurl);
        }
    }
    return 0;
}

/**
 * @brief process teh download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data.
 * @return 0 on success; non-zero otherwise
 */
int process_data(int thread_id, CURL *curl_handle, RECV_BUF *p_recv_buf) {
    CURLcode res;
    long response_code;

    res =
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);

    // if (res == CURLE_OK) {
    LOG_THREAD("Response code: %ld\n", response_code);
    // }

    if (response_code >= 400) {
        LOG_THREAD("error, return\n");
        // fprintf(stderr, "Error.\n");
        return 0;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (res == CURLE_OK && ct != NULL) {
        LOG_THREAD("Content-Type: %s, len=%ld\n", ct, strlen(ct));
        // printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        LOG_THREAD("Failed obtain Content-Type\n");
        // fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if (strstr(ct, CT_HTML)) {
        int result_http = process_html(thread_id, curl_handle, p_recv_buf);
        return result_http;
    } else if (strstr(ct, CT_PNG)) {
        int return_png = process_png(thread_id, curl_handle, p_recv_buf);
        return return_png;
    }
    return 0;
}

extern char *optarg;
extern int optind;
/**
 * @brief parse args
 *
 * @param argc
 * @param argv
 * @param SEED_URL Start from the SEED URL and search for PNG file URLs on the
 *                 world wide web and return the search results to a plain text
 *                 file named png urls.txt in the current working directory.
 *                 Output the execution time in seconds to the standard output.
 * @param t create NUM threads simultaneously crawling the web. Each thread uses
 *          the curl blocking I/O to download the data and then process the
 *          downloaded data. The total number of pthread create() invocations
 *          should equal to NUM specified by the -t option. When this option is
 *          not specified, assumes a single-threaded implementation
 * @param m find up to NUM of unique PNG URLs on the web. It is possible that
 *          the search results is less than NUM of URLs. When this option is not
 *          specified, assumes NUM=50.
 * @param v log all the visited URLs by the crawler, one URL per line in
 *          LOGFILE. When this option is not specified, do not log any visited
 *          URLs by the crawler and do not create any visited URLs log file.
 */
void argsparser(int argc, char **argv, char *SEED_URL, int *t, int *m,
                char *v) {
    int c;
    // default values
    strncpy(SEED_URL, DEFAULT_SEED_URL, URL_LEN);
    *t = 1;
    *m = 50;
    *v = '\0';
    while ((c = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (c) {
            case 't':
                *t = strtoul(optarg, NULL, 10);
                LOG_ABORT(*t <= 0);
                break;
            case 'm':
                *m = strtoul(optarg, NULL, 10);
                LOG_ABORT(*m <= 0);
                break;
            case 'v':
                strncpy(v, optarg, FILE_LEN);
                break;
            default:
                abort();
        }
    }
    if (optind == argc) return;
    LOG_ABORT(optind != argc - 1);
    strncpy(SEED_URL, argv[optind], URL_LEN);
}

/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back
 * function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */
CURL *easy_handle_init(RECV_BUF *ptr, const char *url) {
    CURL *curl_handle = NULL;

    if (ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if (recv_buf_init(ptr, BUF_SIZE) != 0) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        LOG_INFO("curl_easy_init: returned NULL\n");
        // fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    // curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    // curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    // curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

/**
 * @brief child thread
 *
 * @return int
 */
void *child_thread(void *arg) {
    int thread_id = (int)(intptr_t)arg;
    LOG_THREAD("enter\n");
    message_t *message;
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    while (1) {
        pthread_mutex_lock(lock_add_image);
        if (cnt_image == m) {  // check, but not necessary
            LOG_THREAD("found total_image is %d, exit\n", m);
            pthread_mutex_unlock(lock_add_image);
            return NULL;
        }
        pthread_mutex_unlock(lock_add_image);
        LOG_THREAD("waiting sem_queue\n");
        sem_wait(sem_queue);  // wait until queue has something
        LOG_THREAD("waiting lock_state to inc cnt_crawing\n");
        pthread_mutex_lock(lock_state);  // lock state
        LOG_THREAD("reading message\n");
        msg_queue_front(&message);
        if (message->type == 1) {  // someone found all work had done
            LOG_THREAD("got exit message, exit\n");
            pthread_mutex_unlock(lock_state);
            sem_post(sem_queue);  // return the message (not pop)
            LOG_THREAD("return lock_state and sem_queue\n");
            return NULL;
        }
        LOG_THREAD("pop message and add cnt_crawing\n");
        msg_queue_pop_front();
        cnt_crawing++;
        LOG_THREAD("return lock_state\n");
        pthread_mutex_unlock(lock_state);

        // begin craw
        // now url is a image or a html
        // for image, update image results
        // for html, get a url list, and push new url to queue
        // not forget to free message
        // after doing this, if queue is empty and no thread crawing
        // send a 'exit' message to queue, and let all thread exit

        // now url is message->url
        LOG_THREAD("got url %s\n", message->url);
        LOG("%s\n", message->url);

        LOG_THREAD("init curl_handle\n");
        curl_handle = easy_handle_init(&recv_buf, message->url);

        if (curl_handle == NULL) {
            LOG_THREAD("curl_handle is NULL, abort\n");
            curl_global_cleanup();
            abort();
        }

        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            LOG_THREAD("request %s failed, ignored!\n", message->url);
            // abort();
        } else {
            LOG_THREAD("begin process data\n");
            LOG_ABORT(process_data(thread_id, curl_handle, &recv_buf));
            LOG_THREAD("end process data\n");
        }
        // clean
        free(message);
        curl_easy_cleanup(curl_handle);
        recv_buf_cleanup(&recv_buf);
        // finish task and update state
        LOG_THREAD("waiting lock_state to dec cnt_crawing\n");
        pthread_mutex_lock(lock_state);
        cnt_crawing--;
        if (cnt_crawing == 0 && msg_queue_size() == 0) {
            // send 'exit' message
            LOG_THREAD("send exit message\n");
            message = message_new(1, NULL);
            msg_queue_push_back(message);
            sem_post(sem_queue);
        }
        LOG_THREAD("return lock_state\n");
        pthread_mutex_unlock(lock_state);
    }
    LOG_THREAD("exit\n");
    return NULL;
}

int main(int argc, char **argv) {
    // begin time
    double times[2];
    struct timeval tv;
    LOG_ABORT(gettimeofday(&tv, NULL) != 0);
    times[0] = (tv.tv_sec) + tv.tv_usec / 1000000.;

    // parse args
    argsparser(argc, argv, SEED_URL, &t, &m, LOG_FILE);
    LOG_INFO("SEED_URL %s\n", SEED_URL);
    LOG_INFO("t %d m %d\n", t, m);
    LOG_INFO("LOG_FILE %s\n", LOG_FILE);

    // init sems
    LOG_INFO("initing semaphores\n");
    sem_queue = (sem_t *)malloc(sizeof(sem_t));
    LOG_ABORT(sem_init(sem_queue, 0, 0));
    /*
    sem_queue = sem_open("/sems", O_CREAT | O_EXCL, 0644, 0);
    if (sem_queue == SEM_FAILED && errno == 17) {
        sem_close(sem_queue);
        sem_unlink("/sems");
        sem_queue = sem_open("/sems", O_CREAT | O_EXCL, 0644, 0);
        if (sem_queue == SEM_FAILED) {
            sem_close(sem_queue);
            sem_unlink("/sems");
            perror("sems open failed");
            abort();
        }
    }
    */

    // init locks
    LOG_INFO("initing locks\n");
    lock_state = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    lock_add_image = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    lock_hash = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(lock_state, NULL);
    pthread_mutex_init(lock_add_image, NULL);
    pthread_mutex_init(lock_hash, NULL);

    // init logger
    LOG_INFO("initing logger\n");
    logger_init(LOG_FILE);

    // init msg_queue
    LOG_INFO("initing msg_queue\n");
    msg_queue_init();

    // init curl
    LOG_INFO("initing curl\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // init xml2
    LOG_INFO("initing xml2\n");
    xmlInitParser();

    // init hashtable
    LOG_INFO("initing hash table\n");
    hash_init();

    // init png_urls.txt
    LOG_INFO("open png_urls.txt\n");
    FILE_IMG_RESULT = fopen("png_urls.txt", "w");
    LOG_ABORT(FILE_IMG_RESULT == NULL);

    // add SEED_URL
    LOG_INFO("add SEED_URL\n");
    LOG_INFO("new message\n");
    message_t *message = message_new(0, SEED_URL);
    LOG_INFO("message->url %s\n", message->url);
    LOG_INFO("push back message\n");
    msg_queue_push_back(message);
    LOG_INFO("sem post queue\n");
    sem_post(sem_queue);
    LOG_ABORT(hash_insert(SEED_URL));

    LOG_INFO("creating threads\n");
    // create t child threads
    pthread_t p_tids[THREAD_MAX];
    for (int i = 0; i < t; i++) {
        pthread_create(&p_tids[i], NULL, child_thread, (void *)(intptr_t)i);
    }

    LOG_INFO("join threads\n");
    // join t child threads
    for (int i = 0; i < t; i++) {
        pthread_join(p_tids[i], NULL);
        LOG_INFO("Thread %d (%lu) joined.\n", i, p_tids[i]);
    }

    LOG_INFO("clean\n");
    // destroy
    pthread_mutex_destroy(lock_hash);
    pthread_mutex_destroy(lock_state);
    pthread_mutex_destroy(lock_add_image);
    free(lock_hash);
    free(lock_state);
    free(lock_add_image);
    sem_destroy(sem_queue);
    free(sem_queue);
    xmlCleanupParser();
    curl_global_cleanup();
    hash_destroy();
    logger_destruct();
    msg_queue_destruct();
    fclose(FILE_IMG_RESULT);

    // end time
    LOG_ABORT(gettimeofday(&tv, NULL) != 0);
    times[1] = (tv.tv_sec) + tv.tv_usec / 1000000.;
    printf("findpng2 execution time: %lf seconds\n", times[1] - times[0]);
    return 0;
}
