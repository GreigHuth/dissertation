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


//Global variables 
int DEBUG = 0;
pthread_mutex_t lock;
int mode = 1; // 0 -  throughput testing mode
              // 1 -  latency testing mode 

int t_size = 0;
int TIMEOUT = 0;
int THREADS = 1;

//struct to pass the listen socket to the threads
struct t_args{
    int threadID;
    int listen_sock;
};

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


//static void update_tracker(int threadID, int value){
//    pthread_mutex_lock(&lock);
//    connections[threadID] += value;
//    pthread_mutex_unlock(&lock);
//}


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
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))){
        perror("setsockopt");
        exit(EXIT_FAILURE);
        close(listen_sock);
    }
    if (setsockopt(listen_sock, SOL_SOCKET, TCP_NODELAY, &(int){1}, sizeof(int))){
        perror("setsockopt");
        exit(EXIT_FAILURE);
        close(listen_sock);
    }
    if (ioctl(listen_sock, FIONBIO, &(int){1}) < 0) {
    perror("ioctl");
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

    if (setsockopt(conn_sock, SOL_SOCKET, TCP_NODELAY, &(int){1}, sizeof(int))){
        perror("setsockopt");
        exit(EXIT_FAILURE);
        close(conn_sock);
    }


    struct linger sl = {.l_onoff = 1, .l_linger = 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

    #ifdef linux

        ev.events = EPOLLIN;
        ev.data.fd = conn_sock;
        epoll_ctl(pfd, EPOLL_CTL_ADD, conn_sock, &ev);
        
    #else

        memset(&ev, 0, sizeof(ev));
        EV_SET(&ev, conn_sock, EVFILT_READ, EV_ADD, 0, 0, NULL);
        kevent(pfd, &ev, 1, NULL, 0, NULL);

    #endif

}





//using void as a pointer lets you point to anything you like,
//and for some reason when threading you need to pass the arg struct as void
void *polling_thread(void *data){
    //unpack arguments
    struct t_args *args = data;
    int threadID = args->threadID;
    int listen_sock = args->listen_sock;
    printf("Thread %d created\n",threadID);

    int pfd;  //polling fd
    int nfds; 

    #ifdef linux
    struct epoll_event evts[MAX_EVENTS];
    #else
    struct kevent evts[MAX_EVENTS];
    #endif

    //allocate data for transfer, i do it regardless but i only need it when doing tp testing
    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n";
    char* r_buf;
    int r = asprintf(&r_buf, header, t_size);
    long max_bytes = r+t_size;
    char* reply = (char*) calloc(max_bytes, 1); //allocate memory for bulk file transfer and initialise
    strcat(reply, r_buf);
    int reply_len = max_bytes;



    //first we need to set up the addresses
    
    
    
    #ifdef linux
    //create epoll instance
    struct epoll_event ev;

    pfd = epoll_create1(EPOLL_CLOEXEC);
    if (pfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    
    //add listen socket to interest list
    ev.events = EPOLLIN; //type of event we are looking for
    ev.data.fd = listen_sock;
    epoll_ctl(pfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
    

    #else
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

    #endif

    for (;;){

        #ifdef linux
        nfds = epoll_wait(pfd, evts, MAX_EVENTS, TIMEOUT);
        #else
        struct timespec timeout = {TIMEOUT, 0};
        nfds = kevent(pfd, NULL, 0, evts, MAX_EVENTS, &timeout);
        #endif

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
                //update_tracker(threadID, 1);
                break;

            }else {//if current_fd is not the listener we can do stuff
                //make the buffer and 0 it
                char buf[BUFFER_SIZE]; // read buffer
                bzero(buf, sizeof(buf));//this is just sensible

                int bytes_recv = read(current_fd, buf, sizeof(buf));

                if (bytes_recv <= 0){// if recv buffer empty or error then close fd 
                    close(current_fd);
                    //update_tracker(threadID, -1);
                }else{
                    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nHello world!";
                    write(current_fd, header, strlen(header));
                }
                
            }
        }
    }
}


int main(int argc, char *argv[]){

    //thread arguments
    struct t_args t_args;

    //LOCAL VARIABLES
    //socket stuff
    int listen_sock = setup_listener();
    struct sockaddr_in s_addr;//addr we want to bind the socket to
    int s_addr_len;
    
    set_sockaddr(&s_addr);
    s_addr_len = sizeof(s_addr);


    //arg handling
    if (argc < 3){
        printf("USAGE: <threads> <response size> <timeout>\n");
        exit(0);
    }else{
        THREADS = atoi(argv[1]);
        t_size = atoi(argv[2]);
        TIMEOUT = atoi(argv[3]);
    }


    //print various configuration settings
    #ifdef linux
        printf("Running Linux\n");
    #else
        printf("Running freeBSD\n");
    #endif

    printf("PORT: %d\n", PORT);
    printf("THREADS: %d\n", THREADS);
    printf("REPONSE SIZE: %d bytes\n", t_size);
    printf("TIMEOUT: %d\n", TIMEOUT);


    //each thread has its own listener and epoll instance, the only thing they share is the port
    pthread_t threads[THREADS];
    
    //set cpu affinity

    #ifdef linux

    #else
        cpuset_t cpuset;
        CPU_ZERO(&cpuset);
        for (int j = 0; j < THREADS; j++)
               CPU_SET(j, &cpuset);
    #endif


     for (int i=0; i < THREADS; i++){
   
        struct t_args* args = malloc(sizeof(*args));
        args->threadID = i;
        args->listen_sock = listen_sock;

        int rc = pthread_create(&threads[i], NULL, polling_thread, (void*)args);

        if (rc){
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(0);
        }    

        #ifdef linux
        #else
            rc = pthread_setaffinity_np(threads[i], sizeof(cpuset), &cpuset);
            if (rc != 0)
               perror("pthread_setaffinity_np");
        #endif
    }

    for (;;){

    }

    //for (;;){
    //    for (int i=0; i < THREADS; i++){
    //        printf("thread %d connections: %d\n",i, connections[i]);
    //        
    //    }
    //    for (int i=0; i < THREADS; i++){
    //        printf("thread %d requests: %d\n",i, sent_bytes[i]);            
    //    }
    //
    //    
    //    sleep(1);
    //    system("clear");
    //}
}