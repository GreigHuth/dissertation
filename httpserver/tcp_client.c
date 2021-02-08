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

#define PORT 1234
#define BUFFER_SIZE 1024



int main(){

    //set up server address and port
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = htons(PORT);

    char buf[BUFFER_SIZE];

    //set up our socket
    printf("setting up socket...\n");
    int sock;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    printf("socket established, connecting to server...\n");


    //if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))){
	//	perror("setsockopt");
	//	close(sock);
	//}

    if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1){
        perror("cannot connect to server");
        exit(EXIT_FAILURE);
    }

    printf("connected to server\n");
    for (;;){

        memset(buf,0, sizeof(buf));
        
        printf("Enter string\n");
        read(0, buf, sizeof(buf));

        printf("You entered: %s", buf);
        write(sock, buf, sizeof(buf));
        
        memset(buf, 0, sizeof(buf));
        
        while(1){
            read(sock, buf, sizeof(buf));
        }
        
    }






    




} 
