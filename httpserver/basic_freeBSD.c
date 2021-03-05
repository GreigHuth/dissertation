#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h> 
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <pthread.h>

#ifdef linux
#include <sys/epoll.h>

#else 
#include <sys/cpuset.h>
#include <pthread_np.h> 
#include <sys/event.h>
#include <sys/sysctl.h>	
#endif
#include <netinet/tcp.h>

#define PORT 1234
#define Q_LEN 16
#define MAX_EVENTS 1024
#define BUFFER_SIZE 1024
#define TIMEOUT 5
#define THREADS 4


//Global variables 
int DEBUG = 0;
int mode = 1; // 0 -  throughput testing mode
              // 1 -  latency testing mode 
int t_size = 0;

//set fd to non blocking, more portable than doing it in the socket definition
static int setnonblocking(int sockfd){
    if (fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) ==-1) {
	return -1;
    }
    return 0;
}

//set address and port for socket
static void set_sockaddr(struct sockaddr_in * addr){
    addr->sin_family = AF_INET;

    addr->sin_addr.s_addr = INADDR_ANY;//binds socket to all available interfaces
    //manual addr assignment: inet_aton('127.0.0.1', addr->sin_addr.s_addr);

    //htons convers byte order from host to network order 
    addr->sin_port = htons(PORT);
}



static int setup_listener(){
    int listen_sock;
    struct sockaddr_in s_addr;//addr we want to bind the socket to
    int s_addr_len;

    set_sockaddr(&s_addr);
    s_addr_len = sizeof(s_addr);

    //set up listener socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (listen_sock == 0){ 
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    
    //set sock options that allow you to reuse addresses and ports
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int))){
	    perror("setsockopt");
        exit(EXIT_FAILURE);
        close(listen_sock);
    }
    //bind listener to addr and port 
    if (bind(listen_sock, (struct sockaddr *) &s_addr, s_addr_len) < 0){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    //start listening for connections
    if (listen(listen_sock, Q_LEN) < 0){
        perror("listening failed");
        exit(EXIT_FAILURE);
    }

    return listen_sock;
}


static void accept_conn(int fd, int pfd){

    #ifdef linux
        struct epoll_event ev;

    #else 
        struct kevent ev;
    
    #endif

    struct sockaddr_in c_addr;//address of the client
    int c_addr_len = sizeof(c_addr);

    //assign socket to the new connection
    int conn_sock = accept(fd, (struct sockaddr*)&c_addr, &c_addr_len);
    if (conn_sock == -1){
        perror("accept");
	    return;
    }
    setnonblocking(conn_sock);

	memset(&ev, 0, sizeof(ev));
    EV_SET(&ev, conn_sock, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(pfd, &ev, 1, NULL, 0, NULL);


}



int main(int argc, char *argv[]){


    struct sockaddr_in s_addr;//addr we want to bind the socket to
    int s_addr_len;
    
    set_sockaddr(&s_addr);
    s_addr_len = sizeof(s_addr);


    //print various configuration settings
    printf("Running freeBSD\n");
    

    printf("PORT: %d\n", PORT);
    printf("EPOLL_Q_LENGTH: %d\n", Q_LEN);
    printf("TIMEOUT: %d\n", TIMEOUT);


    //each thread has its own listener and epoll instance, the only thing they share is the port
    
    int listen_sock = setup_listener();
    int pfd;  //polling fd
    int nfds; 


    struct kevent evts[MAX_EVENTS];


    //allocate data for transfer, i do it regardless but i only need it when doing tp testing
    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n";
    char* r_buf;
    int r = asprintf(&r_buf, header, t_size);
    long max_bytes = r+t_size;
    char* reply = (char*) calloc(max_bytes, 1); //allocate memory for bulk file transfer and initialise
    strcat(reply, r_buf);
    int reply_len = max_bytes;
    
    
    
    //create kqueue
    struct kevent ev;

    pfd = kqueue();
    if (pfd == -1){
        perror("kqueue");
        exit(EXIT_FAILURE);
    }

    EV_SET(&ev, listen_sock, EVFILT_READ, EV_ADD, 0, 0, NULL);
    //attach event to queue
    if (kevent(pfd, &ev, 1, NULL, 0, NULL) == -1){
        perror("kevent failed");
    exit(EXIT_FAILURE);
    }



    for (;;){

        struct timespec timeout = {TIMEOUT, 0};
        nfds = kevent(pfd, NULL, 0, evts, MAX_EVENTS, &timeout);


        //loop through all the fd's to find new connections
        for (int n = 0; n < nfds; ++n){

            #ifdef linux
                int current_fd = evts[n].data.fd;
            #else  
                int current_fd = evts[n].ident;

            #endif
            
            if (current_fd == listen_sock){//listen socket ready means new connection

                //printf("accepting connection\n");
                accept_conn(current_fd, pfd);
                continue;

            }else {//if current_fd is not the listener we can do stuff
                //make the buffer and 0 it
                char buf[BUFFER_SIZE]; // read buffer
                bzero(buf, sizeof(buf));//this is just sensible

                int bytes_recv = read(current_fd, buf, sizeof(buf));

                if (bytes_recv <= 0){// if recv buffer empty or error then close fd 
                    close(current_fd);
                }else{

                    if (mode == 1){ //latency tests
                        char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nHello world!";
                        write(current_fd, header, strlen(header));

                    } else if (mode == 0){ //tp testing
                        write(current_fd, reply, max_bytes);

                        
                    }
                }
            }
        }
    }
}
