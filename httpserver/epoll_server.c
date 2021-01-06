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

#define PORT 8080
#define Q_LEN 16
#define MAX_EVENTS 32
#define BUFFER_SIZE 1024


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

int main(){
    
    struct epoll_event ev, events[MAX_EVENTS];
    int listen_sock, conn_sock, nfds, epollfd;
    char buf[BUFFER_SIZE];


    //first we need to set up the address and port struct

    struct sockaddr_in s_addr;//addr we want to bind the socket to
    struct sockaddr_in c_addr;//address of the client 

    set_sockaddr(&s_addr);

    int s_addr_len = sizeof(s_addr);
    int c_addr_len = sizeof(c_addr);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0); //creating fd for socket

    if (listen_sock == 0){ 
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    printf("Socket created, binding socket...\n");

    //bind socket to addr and port given by addr
    if ( bind(listen_sock, (struct sockaddr *) &s_addr, s_addr_len) < 0){

        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    printf("socket bound, setting up listener...\n");

    //this is how the socket can accept incoming connections
    if (listen(listen_sock, Q_LEN) < 0){

        perror("listening failed");
        exit(EXIT_FAILURE);
    }

    printf("listener listening, we gucci\n");

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
               perror("cant create epoll instance");
               exit(EXIT_FAILURE);
    }

    //populate ev with data needed for epoll_ctl and epoll_wait
    ev.events = EPOLLIN; //type of event we are looking for
    ev.data.fd = listen_sock; //populate ev with data about socket, not sure why this is needed

    //this syscall adds entries to the interest list 
    //      (list of fd's your server cares about)
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1){
        perror("adding listen_sock to epoll_ctl failed");
        exit(EXIT_FAILURE);
    }


    //now on to the actual polling
    for (;;){

        //returns the # of fd's ready for I/O
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);

        if (nfds == -1){
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        //loop through all the fd's to find new connections
        for (int n = 0; n < nfds; ++n){

            //if the listen socket is ready to read then make a new connection
            if (events[n].data.fd == listen_sock){
                
                //accept connection to the given socket and set it to non blocking 
                conn_sock = accept(listen_sock, (struct sockaddr*)&c_addr, &c_addr_len);
                if (conn_sock == -1){
                    perror("cannot accept new connection");
                    exit(EXIT_FAILURE);
                }

                //get info about client
                printf("new connection from client:%s\n",inet_ntoa(c_addr.sin_addr));

                //add connection to the list of polling 
                setnonblocking(conn_sock); //make sure new socket is non blocking
                ev.events = EPOLLIN;
                ev.data.fd = conn_sock;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1){
                    perror("adding new connection to epoll_ctl failed");
                    exit(EXIT_FAILURE);
                }

            //if any other fd is available for IO then we do stuff
            }else {
                
                //read from socket until its empty
                for (;;){
                    bzero(buf, sizeof(buf));
                    if (read(events[n].data.fd, buf, sizeof(buf)) <=0){
                        //EOF
                        break;
                    } else{
                        //THIS IS WHERE WE DO STUFF WITH THE DATA
                        printf("[+] data: %s\n", buf);
                        sleep(1);
                    }
                }
            }
        }
    }

    








    
    
}
