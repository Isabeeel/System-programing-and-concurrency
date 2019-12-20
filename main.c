/* curl_multi_test.c

   Clemens Gruber, 2013
   <clemens.gruber@pqgruber.com>

   Code description:
    Requests 4 Web pages via the CURL multi interface
    and checks if the HTTP status code is 200.

   Update: Fixed! The check for !numfds was the problem.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <curl/multi.h>
#include <libxml2/libxml/HTMLparser.h>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/uri.h>
#include <libxml2/libxml/xpath.h>
#include <search.h>
#include <sys/types.h>
#include "lab_png.h"

#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */

#define SEED_URL "http://ece252-1.uwaterloo.ca/~yqhuang/lab4"
#define CNT 1
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define ECE252_HEADER "X-Ece252-Fragment: "
#define CT_PNG "image/png"
#define CT_HTML "text/html"
#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
    /* <0 indicates an invalid seq number */
} RECV_BUF;

CURL *easy_handle_init(RECV_BUF *ptr, char *url);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
int find_http(char *fname, int size, int follow_relative_links,
              const char *base_url);
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf);

RECV_BUF recv_buf[1000];
ENTRY e, *ep;
int url_index =0;
int used_url=0;
char p_url_all[1000][256];
char log_file[256] = "log.txt";
int png_num = 0;
int t = 1;
int m = 50;
int init_index=0;

static size_t cb(char *d, size_t n, size_t l, void *p)
{
    /* take care of the data here, ignored in this example */
    (void)d;
    (void)p;
    return n*l;
}
void cleanup(CURL *curl, RECV_BUF *ptr)
{
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    recv_buf_cleanup(ptr);
}
htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

    if ( doc == NULL ) {
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        printf("No result\n");
        return NULL;
    }
    return result;
}

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "a");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

//    fseek(fp, 0, SEEK_END);
    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3;
    }
    return fclose(fp);
}

CURL *easy_handle_init(RECV_BUF *ptr, char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }
//    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
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

    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, ptr);
    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
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
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
        return 1;
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

/**
 * @brief  cURL header call back function to extract image sequence number from
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    printf("%s", p_recv);
#endif /* DEBUG1_ */
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

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */
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

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{

    CURLcode res;

    long response_code;
    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
        printf("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) {
        fprintf(stderr, "Error.\n");
        return 1;
    }
    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( ct != NULL ) {
        printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        int result_http= process_html(curl_handle, p_recv_buf);
        return result_http;
    } else if ( strstr(ct, CT_PNG) ) {
        int return_png= process_png(curl_handle, p_recv_buf);
        return return_png;
    }
    return 0;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char fname[256];
    int follow_relative_link = 1;
    char *url = NULL;
    pid_t pid =getpid();

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url);
    //sprintf(fname, "./output_%d.html", pid);
    //return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
    return 0;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
    char logurl[256];

    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                e.key = href;
                /* data is just an integer, instead of a
                   pointer to something */
                e.data = (void *) url_index;
                ep = hsearch(e, FIND);
                /* there should be no failures */
                if (ep == NULL) {
                    strcpy(p_url_all[url_index],href);
                    e.key=p_url_all[url_index];
                    e.data=url_index;
                    ep = hsearch(e, ENTER);
                    if (ep != NULL){
//                        deque_push_back(&visited_url_queue, url_index);
//                        printf("enqueue: %d \n", visited_url_queue.items[visited_url_queue.tail-1]);
//                        printf("enqueue: %s\n", p_url_all[url_index]);
                        //push(&visited_url_stack, url_index);
                        url_index += 1;
//                        pthread_mutex_unlock(&lock_thread);
                        //write log.txt
                        sprintf(logurl, "%s\n", href);
                        write_file(log_file, logurl, strlen(logurl));
                    }else{
                        perror("error hash\n");
                    }
                }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
//    pthread_mutex_lock(&lock_png);
//    if(m <= png_num){
////            printf("thread joined\n");
//        pthread_mutex_unlock(&lock_png);
//        return 0;
//    }
//    pthread_mutex_unlock(&lock_png);
    pid_t pid =getpid();
    char fname[256];
    char url[256];
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ( eurl != NULL) {
        if(is_png(p_recv_buf->buf,p_recv_buf->size)==0 ){
            sprintf(fname, "./png_urls.txt");
            sprintf(url, "%s\n", eurl);
            write_file(fname, url, strlen(url));
            png_num += 1;
        }
    }
    //hit_url("");
    return 0;
}

static void init(CURLM *cm)
{
//    CURL *eh = NULL;
    /* init user defined call back function buffer */
    if(init_index<url_index){
        CURL *eh = easy_handle_init(&recv_buf[init_index], p_url_all[init_index]);
        curl_multi_add_handle(cm, eh);
        init_index+=1;
    }
//    eh = easy_handle_init(&recv_buf[i], urls[i]);
//    curl_multi_add_handle(cm, eh);
}

int main(int argc, char** argv )
{
    CURLM *cm=NULL;
    CURL *eh=NULL;
    CURLMsg *msg=NULL;
    CURLcode return_code=0;
    int still_running=0, i=0, msgs_left=0;
    int http_status_code;
    const RECV_BUF *ret_buf;
    char logurl[256];
    int c;
    int arg_num = 0;
    char url_need[256];

    while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
        switch (c) {
            case 't':
                arg_num += 2;
                t = strtoul(optarg, NULL, 10);

                if (t <= 0) {
                    return -1;
                }
                break;
            case 'm':
                arg_num += 2;
                m = strtoul(optarg, NULL, 10);
                if (m <= 0 ) {
                    return -1;
                }
                break;
            case 'v':
                arg_num += 2;
                strcpy(log_file, optarg);
#ifdef DEBUG_1
                printf("option -v specifies a value of %d.\n", t);
#endif /* DEBUG_1 */
                if (log_file[0] < 0) {
//                    fprintf(stderr, "%s: %s > 0 -- 'v'\n", argv[0], str);
                    return -1;
                }
                break;
            default:
                return -1;
        }
    }
    double times[2];
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        abort();
    }

    times[0] = (tv.tv_sec) + tv.tv_usec / 1000000.;
    if (argc == 1) {
        strcpy(url_need, SEED_URL);
    } else if(arg_num == 2){
        strcpy(url_need, argv[3]);
    }else if(arg_num == 4) {
        strcpy(url_need, argv[5]);
    }else if(arg_num == 6){
        strcpy(url_need, argv[7]);
    }

    curl_global_init(CURL_GLOBAL_ALL);

    cm = curl_multi_init();
    hcreate(1000);
    strcpy(p_url_all[url_index],url_need);
//    sprintf(logurl, "%s \n", urls[0]);
//    write_file(log_file, logurl, strlen(urls[0]));
    url_index+=1;
    init(cm);

//    curl_multi_perform(cm, &still_running);

    do {
        curl_multi_perform(cm, &still_running);
        /*
         if(!numfds) {
            fprintf(stderr, "error: curl_multi_wait() numfds=%d\n", numfds);
            return EXIT_FAILURE;
         }
        */
   //     curl_multi_perform(cm, &still_running);
        while((msg = curl_multi_info_read(cm, &msgs_left))){
            if (msg->msg == CURLMSG_DONE) {
                eh = msg->easy_handle;
                return_code = msg->data.result;
                if(return_code!=CURLE_OK) {
                    fprintf(stderr, "CURL error code: %d\n", msg->data.result);
		    ret_buf = NULL;
		    curl_easy_getinfo(eh, CURLINFO_PRIVATE, &ret_buf);
                    curl_multi_remove_handle(cm, eh);
		    cleanup(eh, ret_buf);
		    continue;
                }

                // Get HTTP status code
                http_status_code=0;
                ret_buf = NULL;

                curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
                curl_easy_getinfo(eh, CURLINFO_PRIVATE, &ret_buf);

                if(http_status_code==200) {
//                printf("200 OK for %s\n", ret_buf->buf);
                } else if(http_status_code!=400) {
//                    fprintf(stderr, "GET of %s returned http status code %d\n", ret_buf->buf, http_status_code);
                }
                process_data(eh, ret_buf);
                curl_multi_remove_handle(cm, eh);
	        cleanup(eh, ret_buf);
	    }
        }
//        curl_multi_perform(cm, &still_running);
        if(m<=png_num){
	while(still_running){
	    curl_multi_perform(cm, &still_running);
            while((msg = curl_multi_info_read(cm, &msgs_left))){
		if (msg->msg == CURLMSG_DONE) {
                    eh = msg->easy_handle;
                    ret_buf = NULL;
                    curl_easy_getinfo(eh, CURLINFO_PRIVATE, &ret_buf);
                    curl_multi_remove_handle(cm, eh);
                    cleanup(eh, ret_buf);
		}
            }
	}
	    break;
        }
        if((init_index==url_index) && still_running==0){
            break;
        }
        int w=t-still_running;
        for (i = 0; i < w; ++i) {
            init(cm);
        }
        curl_multi_perform(cm, &still_running);
    } while(1);

    curl_multi_cleanup(cm);
    //time
    if (gettimeofday(&tv, NULL) != 0) {
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec / 1000000.;
    printf("findpng2 execution time: %lf seconds\n", times[1] - times[0]);

    return EXIT_SUCCESS;
}
