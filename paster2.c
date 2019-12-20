#include <curl/curl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "catpng.c"

#define BUF_SIZE 10240 /* 1024*10 = 10K */
#define ECE252_HEADER "X-Ece252-Fragment: "
#define IMG_URL_BASE "http://ece252-1.uwaterloo.ca:2530/image?"
#define URL_LEN 256
#define TOTAL_PART 50
#define SEM_PROC 1  // shared in procs
#define MAX_CONSUMER 100

/* for log */
// abort when cond != 0
#define LOG_ABORT(cond)    \
    do {                   \
        if ((cond)) {      \
            perror(#cond); \
            printf("abort\n");\
	    abort();       \
        }                  \
    } while (0)
/* for shm */
// new shm variable
#define NEWSHM(id, key, size, flag)   \
    do {                              \
        id = shmget(key, size, flag); \
        if (id == -1) {               \
            perror("shmget");         \
            abort();                  \
        }                             \
    } while (0)
// connect
#define CONNECT(p, id, addr, flag) \
    do {                           \
        p = shmat(id, addr, flag); \
        if (p == (void *)-1) {     \
            perror("shmat");       \
            abort();               \
        }                          \
    } while (0)
// disconnect
#define DETACH(p)        \
    if (shmdt(p) != 0) { \
        perror("shmdt"); \
        abort();         \
    }
/* for sem */
// init sem variable
#define INITSEM(p, value)                        \
    if (sem_init(p, SEM_PROC, value) != 0) {     \
        perror("sem_init [" #p "][" #value "]"); \
        abort();                                 \
    }

// Buffer begin
typedef struct Buffer {
    char buf[BUF_SIZE];
    int size;
    int seq;
} Buffer;
int buffer_init(Buffer *p_buf) {
    LOG_ABORT(p_buf == NULL);
    p_buf->size = 0;
    p_buf->seq = -1;
    return 0;
}
// Buffer end

// Deque begin
typedef struct Deque {
    int capacity;
    int size;
    int head, tail;  // [head,tail)
    Buffer *items;
} Deque;
int sizeof_shm_deque(int queue_size) {
    return sizeof(Deque) + sizeof(Buffer) * queue_size;
}
int update_shm_deque(Deque *q) {
    q->items = (Buffer *)((char *)q + sizeof(Deque));
    return 0;
}
int init_shm_deque(Deque *q, int queue_size) {
    if (q == NULL || queue_size == 0) {
        return 1;
    }
    q->capacity = queue_size;
    q->size = 0;
    q->head = 0;
    q->tail = 0;
    update_shm_deque(q);
    return 0;
}
// sem_slots and sem_exists to ensure the operate valid
/*
int deque_full(Deque *q) {
    LOG_ABORT(q == NULL);
    return (q->size == q->capacity);
}
int deque_empty(Deque *q) {
    LOG_ABORT(q == NULL);
    return (q->size == 0);
}*/
int deque_push_back(Deque *q, Buffer *item) {
    LOG_ABORT(q == NULL);
    // LOG_ABORT(deque_full(q));
    // q->size++;
    q->items[q->tail++] = *item;
    if (q->tail >= q->capacity) q->tail -= q->capacity;
    return 0;
}
int deque_pop_front(Deque *q, Buffer *item) {
    LOG_ABORT(q == NULL);
    // LOG_ABORT(deque_empty(q));
    // q->size--;
    *item = q->items[q->head++];
    if (q->head >= q->capacity) q->head -= q->capacity;
    return 0;
}
// Deque end

// global variables
int producers, consumers, queue_size, random_wait, pic_number;
// id of shared memory variables
int si_now_downloaded;
int si_sem_get_task, si_sem_need_consume, si_sem_push, si_sem_pop,
        si_sem_exists, si_sem_slots;
int si_queue;
int si_buf_all;
// shared memory variables
int *p_now_downloaded;
sem_t *p_sem_get_task, *p_sem_need_consume, *p_sem_push, *p_sem_pop,
        *p_sem_exists, *p_sem_slots;
Deque *p_queue;
Buffer *p_buf_all;

int gen_url(char *res_url, int part) {
    if (snprintf(res_url, URL_LEN, "%simg=%d&part=%d", IMG_URL_BASE, pic_number,
                 part) < 0)
        return -1;
    return 0;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int write_file(const char *path, const void *in, size_t len);

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
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb,
                      void *p_userdata) {
    int realsize = size * nmemb;
    Buffer *p_recv_buf = (Buffer *)p_userdata;

    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* extract img sequence number */
        p_recv_buf->seq = atoi(p_recv + strlen(ECE252_HEADER));
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
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb,
                     void *p_userdata) {
    size_t realsize = size * nmemb;
    struct Buffer *p_recv_buf = (struct Buffer *)p_userdata;
    // LOG_ABORT(p_recv_buf->size + realsize > BUF_SIZE);
    memcpy(p_recv_buf->buf + p_recv_buf->size, p_recv, realsize);
    p_recv_buf->size += realsize;
    return realsize;
}

/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len) {
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3;
    }
    return fclose(fp);
}

int producer(int id) {
    int now_task = 0;
    char url[URL_LEN];
    CURL *curl_handle;
    CURLcode res;

    Buffer *p_recv_buf = malloc(sizeof(Buffer));
    printf("Producer ID[%d]: init pid[%d] ppid[%d]\n", id, getpid(), getppid());
    CONNECT(p_sem_get_task, si_sem_get_task, NULL, 0);
    CONNECT(p_sem_push, si_sem_push, NULL, 0);
    CONNECT(p_sem_slots, si_sem_slots, NULL, 0);
    CONNECT(p_sem_exists, si_sem_exists, NULL, 0);
    CONNECT(p_queue, si_queue, NULL, 0);
    update_shm_deque(p_queue);
    while (1) {
        // init buffer
        buffer_init(p_recv_buf);
        // get a task
        LOG_ABORT(sem_wait(p_sem_get_task));
        if (*p_now_downloaded == TOTAL_PART) {
            now_task = 0;
        } else {
            (*p_now_downloaded)++;
            now_task = *p_now_downloaded;
        }
        LOG_ABORT(sem_post(p_sem_get_task));
        if (!now_task) break;
        LOG_ABORT(gen_url(url, now_task - 1));
        printf("Producer ID[%d]: run task[%d] url[%s]\n", id, now_task, url);
        curl_handle = curl_easy_init();
        LOG_ABORT(curl_handle == NULL);
        /* specify URL to get */
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        /* register write call back function to process received data */
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
        /* user defined data structure passed to the call back function */
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)p_recv_buf);

        /* register header call back function to process received header data */
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        /* user defined data structure passed to the call back function */
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)p_recv_buf);

        /* some servers requires a user-agent field */
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        /* get it! */
        res = curl_easy_perform(curl_handle);

        LOG_ABORT(res != CURLE_OK);
        printf("Producer ID[%d]: got buffer size[%d] seq[%d]\n", id,
               p_recv_buf->size, p_recv_buf->seq);
	LOG_ABORT(sem_wait(p_sem_push));
        LOG_ABORT(sem_wait(p_sem_slots));
        LOG_ABORT(deque_push_back(p_queue, p_recv_buf));
        printf("Producer ID[%d]: push buffer seq[%d] to queue\n", id,
               p_recv_buf->seq);
        LOG_ABORT(sem_post(p_sem_exists));
        LOG_ABORT(sem_post(p_sem_push));
    }
    /* cleaning up */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    DETACH(p_sem_get_task)
    DETACH(p_sem_push);
    DETACH(p_sem_slots);
    DETACH(p_sem_exists);
    DETACH(p_queue);
    printf("Producer ID[%d]: exit\n", id);
    return 0;
}

int consumer(int id) {
    CONNECT(p_sem_need_consume, si_sem_need_consume, NULL, 0);
    CONNECT(p_sem_pop, si_sem_pop, NULL, 0);
    CONNECT(p_sem_slots, si_sem_slots, NULL, 0);
    CONNECT(p_sem_exists, si_sem_exists, NULL, 0);
    CONNECT(p_queue, si_queue, NULL, 0);
    update_shm_deque(p_queue);
    CONNECT(p_buf_all, si_buf_all, NULL, 0);

    Buffer *p_buf = (Buffer *)malloc(sizeof(Buffer));

    printf("Consumer ID[%d]: init pid[%d] ppid[%d]\n", id, getpid(), getppid());
    while (1) { /* get Buffer from queue and concat */
        if (sem_trywait(p_sem_need_consume)) {  // all down
            return 0;
        }
        printf("Consumer ID[%d]: ready to get buffer\n", id);
        LOG_ABORT(sem_wait(p_sem_pop));
        LOG_ABORT(sem_wait(p_sem_exists));
        deque_pop_front(p_queue, p_buf);
        printf("Consumer ID[%d]: got buffer seq[%d]\n", id, p_buf->seq);
        LOG_ABORT(sem_post(p_sem_slots));
        LOG_ABORT(sem_post(p_sem_pop));
       // usleep(random_wait);
        printf("Consumer ID[%d]: processing buffer seq[%d]\n", id, p_buf->seq);
        // now have Buffer * p_buf
        // process downloaded image data and copy the processed data to
        // a global data structure for generating the concatenated image
        p_buf_all[p_buf->seq] = *p_buf;
    }
    DETACH(p_sem_need_consume);
    DETACH(p_sem_pop);
    DETACH(p_sem_slots);
    DETACH(p_sem_exists);
    DETACH(p_queue);
    DETACH(p_buf_all);
    printf("Consumer ID[%d]: exit\n", id);
    return 0;
}

// make clean && make && ./paster2 10 1 1 3 2
// make clean && make && ./paster2 10 2 2 3 2
int main(int argc, char **argv) {
    // parse args
    if (argc == 6) {
        queue_size = atoi(argv[1]);
        producers = atoi(argv[2]);
        consumers = atoi(argv[3]);
        LOG_ABORT(consumers > MAX_CONSUMER);
        random_wait = atoi(argv[4]);
        pic_number = atoi(argv[5]);
    } else {
        printf("invalid parameter\n");
        return 0;
    }
    double times[2];
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec / 1000000.;
    // create shm variables, get shmid
    NEWSHM(si_now_downloaded, IPC_PRIVATE, sizeof(int),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_get_task, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_need_consume, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_push, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_pop, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_exists, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    NEWSHM(si_sem_slots, IPC_PRIVATE, sizeof(sem_t),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    NEWSHM(si_queue, IPC_PRIVATE, sizeof_shm_deque(queue_size),
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    NEWSHM(si_buf_all, IPC_PRIVATE, sizeof(Buffer) * TOTAL_PART,
           IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    // connect shm variables
    CONNECT(p_now_downloaded, si_now_downloaded, NULL, 0);
    CONNECT(p_sem_get_task, si_sem_get_task, NULL, 0);
    CONNECT(p_sem_need_consume, si_sem_need_consume, NULL, 0);
    CONNECT(p_sem_push, si_sem_push, NULL, 0);
    CONNECT(p_sem_pop, si_sem_pop, NULL, 0);
    CONNECT(p_sem_exists, si_sem_exists, NULL, 0);
    CONNECT(p_sem_slots, si_sem_slots, NULL, 0);
    CONNECT(p_queue, si_queue, NULL, 0);
    // CONNECT(p_buf_all, si_buf_all, NULL, 0);

    // init shm variables
    *p_now_downloaded = 0;
    INITSEM(p_sem_get_task, 1);
    INITSEM(p_sem_need_consume, TOTAL_PART);
    INITSEM(p_sem_push, 1);
    INITSEM(p_sem_pop, 1);
    INITSEM(p_sem_exists, 0);
    INITSEM(p_sem_slots, queue_size);
    init_shm_deque(p_queue, queue_size);

    // DETACH
    DETACH(p_now_downloaded);
    DETACH(p_sem_get_task);
    DETACH(p_sem_need_consume);
    DETACH(p_sem_push);
    DETACH(p_sem_pop);
    DETACH(p_sem_exists);
    DETACH(p_sem_slots);
    DETACH(p_queue);

    // main process
    printf("Main ID[%d]\n", getpid());
    pid_t cpid = 0;
    // pid_t prod_pids[100];
    pid_t cons_pids[MAX_CONSUMER];
    // init curl
    curl_global_init(CURL_GLOBAL_ALL);
    // fork producers
    int i;
    for (i = 0; i < producers; i++) {
        cpid = fork();
        if (cpid > 0) {
            // prod_pids[i] = cpid;
        } else if (cpid == 0) {
            producer(i);
            return 0;
        } else {
            perror("fork producer");
            abort();
        }
    }
    // fork consumers
    for (i = 0; i < consumers; i++) {
        cpid = fork();
        if (cpid > 0) {
            cons_pids[i] = cpid;
        } else if (cpid == 0) {
            consumer(i);
            return 0;
        } else {
            perror("fork consumer");
            abort();
        }
    }
    // now in parent process
    int state;
    for (i = 0; i < consumers; i++) {  // wait consumers end
        waitpid(cons_pids[i], &state, 0);
        if (WIFEXITED(state)) {
            printf("Consumer cpid[%d]=%d terminated with state: %d.\n", i,
                   cons_pids[i], state);
        }
    }
    // generate the concatenated all.png file
    CONNECT(p_buf_all, si_buf_all, NULL, 0);
    char **bufs = (char **)malloc(sizeof(char *) * TOTAL_PART);
    int *lens = (int *)malloc(sizeof(int) * TOTAL_PART);
    for (i = 0; i < TOTAL_PART; i++) {
        bufs[i] = p_buf_all[i].buf;
        lens[i] = p_buf_all[i].size;
        printf("%d buf %p size %d\n", i, &bufs[i], lens[i]);
	usleep(random_wait);
    }
    catpng(TOTAL_PART, bufs, lens);
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec / 1000000.;
    printf("paster2 execution time: %lf seconds\n", times[1] - times[0]);
    return 0;
}
