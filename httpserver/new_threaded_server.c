#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/in.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>


#define PORT 1234
#define Q_LEN 16
#define MAX_EVENTS 32
#define MAX_CLIENTS 10000
#define BUFFER_SIZE 1024
#define EPOLL_TIMEOUT 0
#define THREADS 4


//Global variables 
int connections[THREADS];
int sent_bytes[THREADS];
int mode = 1; // 0 -  throughput testing mode
              // 1 -  latency testing mode 

int t_size = 0;

//struct to pass the listen socket to the threads
struct t_args{
    int threadID;
    char* response;
};


//this gets done a lot so a function makes sense 
static void add_epoll_ctl(int epollfd, int socket, struct epoll_event ev){
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, socket, &ev) == -1){
        perror("adding listen_sock to epoll_ctl failed");
        exit(EXIT_FAILURE);
    }
}


//set address and port for socket
static void set_sockaddr(struct sockaddr_in * addr){
    addr->sin_family = AF_INET;

    addr->sin_addr.s_addr = INADDR_ANY;//binds socket to all available interfaces
    //manual addr assignment: inet_aton('127.0.0.1', addr->sin_addr.s_addr);

    //htons convers byte order from host to network order 
    addr->sin_port = htons(PORT);
}


//set fd to non blocking, more portable than doing it in the socket definition
static int setnonblocking(int sockfd){
    if (fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) ==-1) {
	return -1;
    }
    return 0;
}


//using void as a pointer lets you point to anything you like,
//and for some reason when threading you need to pass the arg struct as void
void *polling_thread(void *data){
    //unpack arguments
    struct t_args *args = data;
    int threadID = args->threadID;
    printf("Thread %d created\n",threadID);

    //LOCAL VARIABLES
    //socket stuff
    int listen_sock;
    struct sockaddr_in s_addr;//addr we want to bind the socket to
    int s_addr_len;

    //epoll stuff
    int epollfd;
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds;
    

    //allocate data for transfer, i do it regardless but i only need it when doing tp testing
    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n";
    char* r_buf;
    int r = asprintf(&r_buf, header, t_size);
    int max_bytes = r+t_size;
    char* reply = (char*) calloc(max_bytes, 1); //allocate memory for bulk file transfer and initialise
    strcat(reply, r_buf);


    //first we need to set up the addresses
    
    set_sockaddr(&s_addr);
    s_addr_len = sizeof(s_addr);

    //set up listener socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (listen_sock == 0){ 
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    

    //set sock options that allow you to reuse addresses and ports
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &(int){1}, sizeof(int))){
		perror("setsockopt");
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
    

    //create epoll instance
    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
        perror("cant create epoll instance");
        exit(EXIT_FAILURE);
    }

    
    //add listen socket to interest list
    ev.events = EPOLLIN; //type of event we are looking for
    ev.data.fd = listen_sock;
    add_epoll_ctl(epollfd, listen_sock, ev);

    for (;;){

        nfds = epoll_wait(epollfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if (nfds == -1){
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        //loop through all the fd's to find new connections
        for (int n = 0; n < nfds; ++n){

            
            int current_fd = events[n].data.fd;
            if (current_fd == listen_sock){//listen socket ready means new connection

                connections[threadID]++;

                struct sockaddr_in c_addr;//address of the client
                int c_addr_len = sizeof(c_addr);

                //assign socket to the new connection
                int conn_sock = accept(listen_sock, (struct sockaddr*)&c_addr, &c_addr_len);
                if (conn_sock == -1){
                    perror("cannot accept new connection");
                    exit(EXIT_FAILURE);
                }


                //set up ev for new socket
                ev.events = EPOLLIN;
                setnonblocking(conn_sock);
                ev.data.fd = conn_sock;
                add_epoll_ctl(epollfd, conn_sock, ev);


            
            }else {//if current_fd is not the listener we can do stuff

                //make the buffer and 0 it
                char buf[BUFFER_SIZE]; // read buffer
                bzero(buf, sizeof(buf));//this is just sensible

                int bytes_recv = read(events[n].data.fd, buf, sizeof(buf));
                if (bytes_recv <= 0){// if recv buffer empty or error then close fd 
                    epoll_ctl(epollfd,EPOLL_CTL_DEL, current_fd, NULL);
                    close(current_fd);
                    connections[threadID]--;
                    sent_bytes[threadID] = 0;
                }else{

                    if (mode == 1){ //latency tests
                        char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nHello world!";
                        write(current_fd, header, strlen(header));

                    } else if (mode == 0){ //tp testing
                        sent_bytes[threadID]++;//used for tp tracking
                        write(current_fd, reply, max_bytes);
                        //free(reply); //Freeing it causes it to segfault and it works fine w/o it so :|
                        //free(r_buf);
                    }
                }
            }
        }
    }
}


int main(int argc, char *argv[]){

    //LOCAL VARIABLES
    struct t_args t_args;
    
    //arg handling
    if (argc < 2){
        printf("USAGE: ./new <mode> <data transfer size>\n");
        exit(0);
    }

    if (*argv[1] == '0'){
        if (argc < 3){
            printf("If using throughput testing mode please supply trasfer size\n");
            exit(0);
        }else{
            t_size = atoi(argv[2]);
            mode = 0;
        }
    }

    //print various configuration settings
    printf("PORT: %d\n", PORT);
    printf("EPOLL_Q_LENGTH: %d\n", Q_LEN);
    printf("MAX_CLIENTS: %d\n", MAX_CLIENTS);
    printf("EPOLL_TIMEOUT: %d\n", EPOLL_TIMEOUT);


    //each thread has its own listener and epoll instance, the only thing they share is the port
    pthread_t threads[THREADS];
    for (int i=0; i < THREADS; i++){
        t_args.threadID = i;
        int rc = pthread_create(&threads[i], NULL, polling_thread, &t_args);
        sleep(.25);//the threads dont intialise properly unless i have this here, idk why
        if (rc){
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(0);
        }
    }

    for (;;){
        for (int i=0; i < THREADS; i++){
            printf("thread %d connections: %d\n",i, connections[i]);
            
        }
        for (int i=0; i < THREADS; i++){
            printf("thread %d requests: %d\n",i, sent_bytes[i]);            
        }

        
        sleep(1);
        system("clear");
    }
}
