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
/*for suffolk computers*/
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
#define max(a,b) ((a > b) ? a : b)
#define DIE_AFTER_TIMEOUTS 5
#define PACKET_SIZE  1400
#define PAYLOAD_SIZE PACKET_SIZE -2
#define REQ_SIZE 283
typedef struct{
        char payload[PACKET_SIZE-2];
        short ack;
        }response;
unsigned long delivery_time=5000000;
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
void punch_time(unsigned long ts);
unsigned short getACK(char* packet);
int main (int argc, char *argv[]){
        struct sockaddr_in name;
        struct hostent *hp, *gethostbyname();
        socklen_t length;
        unsigned short WINDOW_SIZE=2;
        unsigned long ts[2]={0};
        char  packet[PACKET_SIZE], req[REQ_SIZE];
        unsigned long  SEQ=0, PACKET_COUNT;
        unsigned short  sock, window_base=0, ACK;
        short i, count=WINDOW_SIZE-1;
        int r, b4, aft;
        struct timeval before, after;/*UNEXPLAINED BUG, without this all other timers in other scopes get messed up. while with them everthing is fine except the values themselves (before and after). 
					this is why there are b4 and aft variables, they have the same purpose, while the old values are still there for this strange bug*/
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
                fprintf(stderr, "ERROR: %s\n", packet);
                return EXIT_FAILURE;
        }
        fprintf(stderr, "SIZE: %s\n", packet);
        /*-------------------------------------*/
        /*starting to fetch packets with go back N*/
        gettimeofday(&before, NULL);
        b4=time(NULL);
        while(SEQ<PACKET_COUNT){
                /*sending requests for unrequested packets in rest of window or if first timedout*/
                for(i=0;i<=count;i++){/*always check if first packet ticked*/
                        if(isTicked(&ts[i])){
                                ACK=(window_base+i)%(WINDOW_SIZE*3);
                                if(ACK <10)
                                        sprintf(req, "%s %lu 0%hu", argv[3], SEQ+i, ACK);
                                else
                                        sprintf(req, "%s %lu %hu", argv[3], SEQ+i, ACK);
                                /*fprintf(stderr, "out<-%s\n", req);*/
                                if(sendto( sock, req, strlen(req)+1, 0, (struct sockaddr*)&name, sizeof(name) ) < 0){
                                        perror("sending request");
                                        exit(EXIT_FAILURE);
                                }
                        }
                        /*else
                                fprintf(stderr, "waiting\n");*/
                }
                /*--------------------------------------------------------*/
                /*writting packets when they are received in order*/
                count=0;
                while((r=recvfrom(sock, packet, PACKET_SIZE, MSG_DONTWAIT , (struct sockaddr *) &name, &length))>0){
                        ACK=getACK(packet);
                        if(ACK==window_base){/*only accept first packet*/
                                punch_time(ts[count]);
                                SEQ++;
                                prepareConsole();
                                fprintf(stderr, "ACK: %hu, window_base: %hu, SEQ:%lu count: %i\t%f%% out of %f MB\tspeed: %fMb/s\n", ACK, window_base, SEQ, count, 100*(float)SEQ/PACKET_COUNT, ((double)PACKET_COUNT*PACKET_SIZE)/1000000, (PACKET_SIZE*8/1000000.0)/(delivery_time/1000000.0));
                                isStatLine=true;
                                if(write(1, &packet[2], r-2)<0){
                                        perror("writting packet");
                                        return EXIT_FAILURE;
                                }
                                window_base=(window_base+1)%(WINDOW_SIZE*3);
                                count++;
                        }
                        else{
                                prepareConsole();
                                fprintf(stderr, "out of order packet count: %i, SEQ: %lu, window_base: %hu ACK: %hu\n", count, SEQ, window_base, ACK);
                        }
                }
                /*-----------------------------------------------*/
                /*shifting timestamps by the number of packets written*/
                if(count){
                        for(i=0;i<WINDOW_SIZE;i++)
                                ts[i]=(i+count<WINDOW_SIZE)?ts[i+count]:0;
                }
                /*-----------------------------------------------*/
        }
        /*-------------------------------------------*/
        gettimeofday(&after, NULL);
        aft=time(NULL);
        fprintf(stderr, "window size: %hu\tdelivery_time:%lu usec\tpacket size:%i bytes\ttotal time:%i sec\t\n\n", WINDOW_SIZE, delivery_time, PACKET_SIZE, (aft - b4));
        close(sock);
        return EXIT_SUCCESS;
}
unsigned short getACK(char* packet){
        char tmp[3];
        tmp[2]='\0';
        if(!( isdigit(packet[0]) && isdigit(packet[1]) ) ){
                fprintf(stderr, "error??: %s\n", packet);
                exit(EXIT_FAILURE);
        }
        memcpy(tmp, packet, 2);
        return atoi(tmp);
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
        signed long ts_now, prev=ts;
        struct timeval now;
        gettimeofday(&now, NULL);
        ts_now=((now.tv_sec%1000)*1000000)+(now.tv_usec);
        delivery_time=delivery_time*0.8 + (max(0, ts_now-prev))*0.2;
}
bool isTicked(unsigned long *ts){
        static unsigned short tries=1;
        static const unsigned long TIMEOUT=4000000;
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