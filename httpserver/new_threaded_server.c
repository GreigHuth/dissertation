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

//struct to pass the listen socket to the threads
struct t_args{
    int threadID;
}t_args;


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
static int setnonblocking(int sockfd)
{
	if (fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) ==-1) {
		return -1;
	}
	return 0;
}


//using void as a pointer lets you point to anything you like,
//and for some reason when threading you need to pass the arg struct as void
void *polling_thread(void *data){

    int listen_sock;

    //first we need to set up the addresses
    struct sockaddr_in s_addr;//addr we want to bind the socket to
    set_sockaddr(&s_addr);
    int s_addr_len = sizeof(s_addr);

    //set up listener socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (listen_sock == 0){ 
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket created, binding socket...\n");

    //set sock options that allow you to reuse addresses
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))){
		perror("setsockopt");
		close(listen_sock);
	}


    //bind listener to addr and port 
    if (bind(listen_sock, (struct sockaddr *) &s_addr, s_addr_len) < 0){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("socket bound, setting up listener...\n");


    //start listening for connections
    if (listen(listen_sock, Q_LEN) < 0){
        perror("listening failed");
        exit(EXIT_FAILURE);
    }
    printf("listener listening, we gucci\n");

    //create epoll instance
    int epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
               perror("cant create epoll instance");
               exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN; //type of event we are looking for
    ev.data.fd = listen_sock;

    //add listen socket to interest list
    add_epoll_ctl(epollfd, listen_sock, ev);

    printf("polling thread started\n");

    for (;;){

        //returns the # of fd's ready for I/O
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if (nfds == -1){
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        //loop through all the fd's to find new connections
        for (int n = 0; n < nfds; ++n){

            //if the listen socket is ready to read then we have a new connection
            int current_fd = events[n].data.fd;
            if (current_fd == listen_sock){

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


            //if current_fd is not the listener we can do stuff
            }else {

                //make the buffer and 0 it
                char buf[BUFFER_SIZE];
                bzero(buf, sizeof(buf));

                //read from socket, if done or there is an error remove the fd from epoll and close it
                int bytes_recv = read(events[n].data.fd, buf, sizeof(buf));
                if (bytes_recv <= 0){
                    epoll_ctl(epollfd,EPOLL_CTL_DEL, current_fd, NULL);
                    close(current_fd);
                }else{
                    char *hello = "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello world!";
                    //write(1, buf, sizeof(buf));
                    write(current_fd, hello, strlen(hello));
                }
            }
        }
    }
}


int main(){
    
    

    //print various configuration settings
    printf("PORT: %d\n", PORT);
    printf("EPOLL_Q_LENGTH: %d\n", Q_LEN);
    printf("MAX_CLIENTS: %d\n", MAX_CLIENTS);
    printf("EPOLL_TIMEOUT: %d\n", EPOLL_TIMEOUT);


    
    


    //now on to the actual polling
    pthread_t threads[THREADS];//4 seems like a good number

    //each thread has its own epoll instance, the only thing they share is the listen socket
    //tried 
    for (int i; i < sizeof(threads); i++){
        printf("creating thread %d\n", i);
        t_args.threadID = i;
        int rc = pthread_create(&threads[i], NULL, polling_thread, &t_args);
        if (rc){
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(0);
        }
    }

    //if you dont ask, i wont tell
    while (1){
    }
}
