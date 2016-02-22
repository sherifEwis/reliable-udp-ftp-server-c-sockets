#include <netdb.h>
#define h_addr h_addr_list[0] /*backward compat*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/time.h>
#include <ctype.h>
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define DIE_AFTER_TIMEOUTS 14
#define PACKET_SIZE  1400
#define PAYLOAD_SIZE PACKET_SIZE -2
#define REQ_SIZE 283
typedef struct{
        char payload[PACKET_SIZE-2];
        short ack;
        }response;
void swap(char* a, char* b) {
    char temp = *a;
    *a = *b;
    *b = temp;
}
unsigned long delivery_time=1000000;
bool isStatLine=false;
void prepareConsole(){
        if(isStatLine){
                fprintf(stderr, "\033[A\033[2K");
                rewind(stderr);
                isStatLine=false;
        }
}
void parse(response *resp, char* packet);
bool isTicked(unsigned long *ts);
unsigned short window_shift(unsigned short window_base, int index, unsigned int SIZE);
void punch_time(unsigned long ts);
int main (int argc, char *argv[]){
        struct sockaddr_in name;
        struct hostent *hp, *gethostbyname();
        socklen_t length;
        unsigned short ACK, WINDOW_SIZE=2;
        unsigned long ts[2]={0};
        char packet[PACKET_SIZE], req[REQ_SIZE], window[2][PAYLOAD_SIZE];
        unsigned long  SEQ=0, PACKET_COUNT;
        unsigned short  sock, window_base=0, size=PACKET_SIZE-2, last_size=PACKET_SIZE-2;
        short i, k, j=-1;
        int r;
        response resp;
        struct timeval before, after;
        if(argc != 4){
                fprintf(stderr, "%s takes 3 arguments\n", argv[0]);
                exit(EXIT_FAILURE);
        }
        /*preparing connection*/
        sock = socket (AF_INET, SOCK_DGRAM, 0);
        if(sock < 0){
                perror("opening datagram socket");
                exit(EXIT_FAILURE);
        }
        hp = gethostbyname(argv[1]);
        if (hp==0){
                fprintf(stderr, "%s: unknown host", argv[1]);
        }
        bcopy(hp->h_addr , &name.sin_addr , hp->h_length );
        name.sin_family = AF_INET;
        name.sin_port = htons( atoi(argv[2]) );
        length=sizeof(name);
        /* -------------------------------------*/
        /*getting number of packets*/
        if( sendto( sock, argv[3], strlen(argv[3])+1, 0, (struct sockaddr*)&name, sizeof(name) ) < 0 ){
                perror("sending datagram error\n");
                exit(EXIT_FAILURE);
        }
        if(recvfrom(sock, packet, 21, 0, (struct sockaddr *) &name, &length)<0){
                perror("getting packet count\n");
                exit(EXIT_FAILURE);
        }
        if(!(PACKET_COUNT=atoi(packet))){
                fprintf(stderr, "%s\n", packet);
                return EXIT_FAILURE;
        }
        fprintf(stderr, "PACKETS: %lu\n", PACKET_COUNT);
        for(i=0;i<WINDOW_SIZE;i++)
                window[i][0]='\0';/*faster than NULL pointers because no syscalls required (malloc / free)*/
        /*-------------------------------------*/
        /*starting to fetch packets with selective repeate*/
        gettimeofday(&before, NULL);
        while(SEQ<PACKET_COUNT){
                /*WINDOW_SIZE=min(WINDOW_SIZE, PACKET_COUNT-SEQ);*//*in case window is approaching the end of file*/
                /*sending requests for unrequested or timed out requests*/
                for(i=0;i<WINDOW_SIZE;i++){
                        if(window[i][0]=='\0' && isTicked(&ts[i])){
                                ACK=(window_base+i)%(WINDOW_SIZE*3);
                                if(ACK <10)
                                        sprintf(req, "%s %lu 0%hu", argv[3], SEQ+i, ACK);
                                else
                                        sprintf(req, "%s %lu %hu", argv[3], SEQ+i, ACK);
                                /*fprintf(stdout, "out<- %s %lu 0%hu\n", argv[3], SEQ+i, window_base+i);*/
                                if(sendto( sock, req, strlen(req)+1, 0, (struct sockaddr*)&name, sizeof(name) ) < 0){
                                        perror("sending request");
                                        exit(EXIT_FAILURE);
                                }
                        }
                }
                /*--------------------------------------------------------*/
                /*filling Que with recieved responses*/
                while((r=recvfrom(sock, packet, PACKET_SIZE, MSG_DONTWAIT , (struct sockaddr *) &name, &length))>0){
                        parse(&resp, packet);
                        i=(resp.ack-window_base)%(WINDOW_SIZE*3);
                        if(i<0)i+=WINDOW_SIZE*3;
                        if(i>=0 && i< WINDOW_SIZE){/*avoid duplicated packets from previous window see window_base shift below*/
                                punch_time(ts[i]);
                                memcpy(window[i], resp.payload, r-2);
                                prepareConsole();
                                fprintf(stderr, "i: %i, ACK: %hu, window_base: %hu, SEQ:%lu \t%f%% out of %f MB\tspeed: %fMb/s\n", i, resp.ack, window_base, SEQ, 100*(float)SEQ/PACKET_COUNT, (PACKET_COUNT*PACKET_SIZE)/1000000.0, (PACKET_SIZE*8/1000000.0)/(delivery_time/1000000.0));
                                isStatLine=true;
                                if(SEQ+i==PACKET_COUNT-1)
                                        last_size=r-2;
                        }
                        else{
                                prepareConsole();
                                fprintf(stderr, "i: %i, out of order packet SEQ: %lu, window_base: %hu ACK: %hu\n", i, SEQ, window_base, resp.ack);
                        }
                }
                /*-----------------------------------------------*/
               /*writing valid packets to stdout*/
                i=0;
                while(window[i][0]!='\0'){/*write the first packets because they are in order*/
                        if(SEQ+i==PACKET_COUNT-1)
                                size=last_size;
                        if(write(1, window[i], size)<0){
                                perror("writting packet");
                                return EXIT_FAILURE;
                        }
                        i++;
                }
                /*---------------------------------------------*/
                /*shifting window by the number of packets written*/
                if(i){
                        for(k=0;k<WINDOW_SIZE;k++){
                                window[k][0]='\0';
                                if(k+i<WINDOW_SIZE){
                                        swap(window[k], window[k+i]);
                                        /*swaping pointers faster than copying memory, or using free and then changing pointer*/
                                        memcpy(window[k], window[k+i], PAYLOAD_SIZE);
                                        ts[k]=ts[k+i];/*shifting timestamp*/
                                }
                                else
                                        ts[k]=0;
                        }
                        window_base=(window_base+i)%(WINDOW_SIZE*3);/*times 3 to avoid duplicate packets while circulating*/
                        SEQ+=i;
                }
                /*-----------------------------------------------*/
        }
        /*-------------------------------------------*/
        gettimeofday(&after, NULL);
        fprintf(stderr, "window size: %hu\tdelivery_time:%lu usec\tpacket size:%i bytes\ttotal time:%i sec\t\n", WINDOW_SIZE, delivery_time, PACKET_SIZE, (unsigned int)(after.tv_sec -before.tv_sec));
        close(sock);
        return EXIT_SUCCESS;
}
void parse(response *resp, char* packet){
        char tmp[3];
        tmp[2]='\0';
        if(!( isdigit(packet[0]) && isdigit(packet[1]) ) ){
                fprintf(stderr, "error: %s\n", packet);
                exit(EXIT_FAILURE);
        }
        memcpy(tmp, packet, 2);
        resp->ack=atoi(tmp);
        memcpy(resp->payload, &packet[2], PACKET_SIZE-2);
}
void punch_time(unsigned long ts){
        static unsigned long last=0;
        signed long ts_now;
        struct timeval now;
        gettimeofday(&now, NULL);
        ts_now=((now.tv_sec%1000)*1000000)+(now.tv_usec);
        delivery_time=delivery_time*0.9 + (max(0, ts_now-last))*0.1;
        last=ts_now;
}
bool isTicked(unsigned long *ts){
        static unsigned short tries=1;
        static const unsigned long TIMEOUT=2000000;
        signed long ts_now, prev=*ts;
        struct timeval now;
        gettimeofday(&now, NULL);
        ts_now=((now.tv_sec%1000)*1000000)+(now.tv_usec);
        if(*ts==0){
                tries=1;
                *ts=ts_now;
                return true;
        }
        else if(max(0, ts_now-prev) >= TIMEOUT && tries<=(DIE_AFTER_TIMEOUTS)){
                prepareConsole();
                fprintf(stderr, "DIFF: %lu, TIMEOUT at %lu, tries: %hu, delivery_time: %lu\n", ts_now-prev, TIMEOUT, tries, delivery_time);
                tries++;
                *ts=ts_now;
                return true;
        }
        else if(tries>(DIE_AFTER_TIMEOUTS)){
                fprintf(stderr, "%lu>=%lu\n", ts_now-(signed long)prev, TIMEOUT);
                /*fprintf(stderr, "unable to contact server at TIMEOUT: %lu>=%lu\n", *ts, DIE_AFTER_TIMEOUTS);*/
                exit(EXIT_FAILURE);
        }
        /*fprintf(stderr,"DIFF:%i\tts: %lu\tnow: %lu\tTIMEOUT: %lu\n", abs(ts_now-(signed long)prev), *ts, ts_now, TIMEOUT);*/
        return false;
}