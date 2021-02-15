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


int main(int argc, char *argv[]){
    char *header ="HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n";
    int t_size = atoi(argv[1]);

    char* data = NULL;


    int r = asprintf(&data, header, t_size);
    
    char *payload = malloc(t_size); //allocate memory for bulk file transfer and initialise
    memset(payload, 1, t_size);
    strcat(data, payload);//concat payload to header for sending

    puts(data);
    free(payload);
    
}

