#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h> 
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
int DEBUG = 0;

int connections[THREADS];
int sent_bytes[THREADS];
int mode = 1; // 0 -  throughput testing mode
              // 1 -  latency testing mode 

int t_size = 0;

//struct to pass the listen socket to the threads
struct t_args{
    int threadID;
    int listen_sock;
};

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


static void debug_msg(char* message){
	if (DEBUG == 1){
	    printf("%s", message);
    }
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
    int listen_sock = args->listen_sock;
    printf("Thread %d created\n",threadID);

    

    //polling stuff
    int kqfd;
    struct kevent event, t_event[MAX_EVENTS];

    //allocate data for transfer, i do it regardless but i only need it when doing tp testing
    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n";
    char* r_buf;
    int r = asprintf(&r_buf, header, t_size);
    int max_bytes = r+t_size;
    char* reply = (char*) calloc(max_bytes, 1); //allocate memory for bulk file transfer and initialise
    strcat(reply, r_buf);


    //first we need to set up the addresses
    
  
    
    

    //create kqueue
    kqfd = kqueue();
    if (kqfd == -1){
        perror("kq failed");
        exit(EXIT_FAILURE);
    }

    EV_SET(&event, listen_sock, EVFILT_READ, EV_ADD|EV_ENABLE,0,0,NULL);

    if (kevent(kqfd, &event, 1, NULL, 0, NULL) == -1){
        perror("kevent failed");
        exit(EXIT_FAILURE);
    };


    for (;;){


        int nfds = kevent(kqfd, NULL, 0, t_event, MAX_EVENTS, NULL);
        if (nfds == -1){
            perror("kevent");
            exit(EXIT_FAILURE);
        }
        
        //loop through all the fd's to find new connections
        for (int n = 0; n < nfds; n++){

            
            int current_fd = (int)t_event[n].ident;

            if (t_event[n].flags & EV_EOF){
                debug_msg("disconnected\n");
                connections[threadID]--;
                close(current_fd);
            
            } else if (current_fd == listen_sock){//listen socket ready means new connection

                connections[threadID]++;

                struct sockaddr_in c_addr;//address of the client
                int c_addr_len = sizeof(c_addr);

                //assign socket to the new connection
                int conn_sock = accept(listen_sock, (struct sockaddr*)&c_addr, &c_addr_len);
                if (conn_sock == -1){
                    perror("cannot accept new connection");
                    exit(EXIT_FAILURE);
                }


                setnonblocking(current_fd);

                //set up ev for new socket
                EV_SET(&event, conn_sock, EVFILT_READ, EV_ADD, 0, 0, NULL);
                if (kevent(kqfd, &event, 1, NULL, 0, NULL) < 0){
                    perror("kevent");
                    exit(EXIT_FAILURE);
                }

            
            }else if(t_event[n].filter == EVFILT_READ) {

                //make the buffer and 0 it
                char buf[BUFFER_SIZE]; // read buffer
                bzero(buf, sizeof(buf));//this is just sensible

                int bytes_recv = read(current_fd, buf, sizeof(buf));

                //if (bytes_recv <= 0){// if recv buffer empty or error then close fd 
                //    close(current_fd);
                //    connections[threadID]--;
                //    sent_bytes[threadID] = 0;
                //}else{

                    if (mode == 1){ //latency tests
                        char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nHello world!";
                        write(current_fd, header, strlen(header));

                    } else if (mode == 0){ //tp testing
                        sent_bytes[threadID]++;//used for tp tracking
                        write(current_fd, reply, max_bytes);
                        //free(reply); //Freeing it causes it to segfault and it works fine w/o it so :|
                        //free(r_buf);
                    }
                //}
            }
        }
    }
}


int main(int argc, char *argv[]){

    //LOCAL VARIABLES
    struct t_args t_args;

    t_args.listen_sock = setup_listener();

    
    
    //arg handling
    if (argc < 2){
        printf("USAGE: ./new <mode> <data transfer size> <debug 0 or 1>\n");
        exit(0);
    }

    if (*argv[1] == '0'){
        if (argc < 3){
            printf("If using throughput testing mode please supply transfer size\n");
            exit(0);
        }else{
            t_size = atoi(argv[2]);
            mode = 0;
        }
    }

    if (*argv[3] == '1'){//debugging options
        DEBUG = 1;
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
        sleep(1);//the threads dont intialise properly unless i have this here, idk why
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
