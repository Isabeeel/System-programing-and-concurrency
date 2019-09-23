#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT "2520"
#define PLANS_FILE "deathstarplans.dat"

typedef struct {
    char * data;
    int length;
} buffer;

extern int errno;

/* This function loads the file of the Death Star
 plans so that they can be transmitted to the
 awaiting Rebel Fleet. It takes no arguments, but
 returns a buffer structure with the data. It is the
 responsibility of the caller to deallocate the
 data element inside that structure.
 */
buffer load_plans( );
/*int socket( int domain, int type, int protocol );
 int getaddrinfo(const char *node,     // e.g. "www.example.com" or IP
 const char *service,  // e.g. "http" or port number
 const struct addrinfo *hints,
 struct addrinfo **res);
 int connect( int sockfd, struct sockaddr *addr, socklen_t len);
 int recv( int sockfd, void * buffer, int length, int flags );*/
int sendall( int socket, char *buf, int *len );

int main( int argc, char** argv ) {
    
    if ( argc != 2 ) {
        printf( "Usage: %s IP-Address\n", argv[0] );
        return -1;
    }
    printf("Planning to connect to %s.\n", argv[1]);
    
    //struct sockaddr_in client_addr;
    struct addrinfo hints;
    struct addrinfo *res;
    int sockfd;
    char return_mess[64];
    //getinfo
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_INET;     // Choose IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
    
    int result = getaddrinfo( argv[1], PORT, &hints, &res);
    if (result != 0) {
        return -1;
    }
    struct sockaddr_in * client_addr = (struct sockaddr_in*) res->ai_addr;
    /* Do things with this */
    
    //freeaddrinfo( res );
    
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    
    int status = connect(sockfd, res->ai_addr, res->ai_addrlen);
    
    buffer buf = load_plans();
    
    int sent = sendall( sockfd, buf.data, &buf.length );
    if (sent == -1){
        printf("send out fail");
    }
    
    int rev = recv( sockfd, return_mess, 64, 0 );
    printf("%s\n",return_mess);
    //    if (rev == -1){
    //        printf("receive out fail");
    //    }else{
    //        printf("receive success");
    //    }
    
    
    /*struct addrinfo hints;
     struct addrinfo *res;*/
    
    
    
    return 0;
}

buffer load_plans( ) {
    struct stat st;
    stat( PLANS_FILE, &st );
    ssize_t filesize = st.st_size;
    char* plansdata = malloc( filesize );
    int fd = open( PLANS_FILE, O_RDONLY );
    memset( plansdata, 0, filesize );
    read( fd, plansdata, filesize );
    close( fd );
    
    buffer buf;
    buf.data = plansdata;
    buf.length = filesize;
    
    return buf;
}

int sendall( int socket, char *buf, int *len ) {
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;
    
    while( total < *len ) {
        n = send( socket, buf + total, bytesleft, 0 );
        if (n == -1) {
            break;
        }
        total += n;
        bytesleft -= n;
    }
    *len = total; // return number actually sent here
    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}
