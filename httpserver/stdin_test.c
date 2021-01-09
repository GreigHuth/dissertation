#include <unistd.h>
#include <stdio.h>
#include <string.h>
int main(){

    int c;

    char buf[100];

    char zero = '0';

    for(;;){
        memset(buf,0,sizeof(buf));
        printf("Enter value\n");
        int n = read(0, buf, sizeof(buf));
        printf("# of bytes read: %d\n", n);
        while(n < sizeof(buf)){
            strncat(buf, &zero, 1);
            n++;
        }

        printf("you typed %s\n", buf);//buf already has \n in it so we dont need it
    }


}
