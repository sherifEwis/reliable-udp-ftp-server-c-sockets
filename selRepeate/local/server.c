#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <strings.h>
#include <sys/stat.h>
#include <math.h>
#define REQ_SIZE 283 /*filesize=258 SEQ=20, ACK=2 SPACES=2*/
#define PACKET_SIZE 1400
typedef enum {PACKET_COUNT, PACKET, INVALID} request_type;
typedef struct{
        char fileName[258];
        unsigned long SEQ;
        request_type type;
        char ACK[3];
        }request;
void parse(request *R, char* Rstring);
off_t fsize(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}
const unsigned long PAYLOAD_SIZE=PACKET_SIZE-2;
int main(int argc, char *argv[]){
        unsigned short sock;
        socklen_t length;/*, lengthClient;*/
        struct sockaddr_in name, client;
        char req_buf[REQ_SIZE];
        char resp[PACKET_SIZE];
        int resp_size, FNO, r;
        off_t f_size;
        request req;
        if(argc!=2){
                fprintf(stderr, "%s take one argument [port]\n", argv[0]);
                return EXIT_FAILURE;
        }
        sock=socket(AF_INET, SOCK_DGRAM, 0);
        if(sock < 0){
                perror("opening datagram socket");
                exit(EXIT_FAILURE);
        }
        name.sin_family = AF_INET;
        name.sin_addr.s_addr = INADDR_ANY;
        name.sin_port = ntohs(atoi(argv[1]));
        if(bind(sock, (struct sockaddr*)&name, sizeof(name))){
                perror("binding datagram socket");
                exit(EXIT_FAILURE);
        }
        length = sizeof(name);
        if(getsockname(sock, (struct sockaddr*)&name, &length)){
                perror("getting socket name");
                exit(EXIT_FAILURE);
        }
        printf("Socket has port #%d\n", ntohs(name.sin_port));
        while(1){
                if((r=recvfrom(sock, req_buf, REQ_SIZE, 0, (struct sockaddr *) &client, &length))==-1)
                        perror("receiving datagram packet");
                fprintf(stderr, "request: %s\n", req_buf);
                parse(&req, req_buf);
                switch(req.type){
                        case PACKET_COUNT:
                                puts("count\n");
                                if((f_size=fsize(req.fileName))<0){
                                        fprintf(stderr, "%s: unable to get size\n", req.fileName);
                                        strcpy(resp, "unable to get size\n");
                                        resp_size=20;
                                        break;
                                }
                                f_size=(off_t)ceil((long double)f_size/(PAYLOAD_SIZE));
                                sprintf(resp, "%lu", f_size);
                                resp_size=strlen(resp)+1;
                                break;
                        case PACKET:
                                puts("reading packet");
                                memcpy(resp, req.ACK, 2);
                                if((FNO=open(req.fileName, O_RDWR))<0){
                                        perror(req.fileName);
                                        strcpy(resp, "unable to open file\n");
                                        resp_size=21;
                                        break;
                                }
                                if(lseek(FNO, req.SEQ*(off_t)PAYLOAD_SIZE, SEEK_SET)<0){
                                        fprintf(stdout, "lseek(%i, %lu, %i)\n\t SEQ:%lu\n\tpatload: %lu\n",FNO, req.SEQ*(off_t)PAYLOAD_SIZE, SEEK_SET, req.SEQ, (off_t)PAYLOAD_SIZE);
                                        perror(req.fileName);
                                        strcpy(resp, "unable to seek file\n");
                                        resp_size=21;
                                        break;
                                }
                                if((r=read(FNO, &resp[2], PAYLOAD_SIZE))<0){
                                        perror(req.fileName);
                                        strcpy(resp, "unable to read file\n");
                                        resp_size=21;
                                        break;
                                }
                                resp_size=r+2;
                                close(FNO);
                                break;
                        default:
                                strcpy(resp, "INVALID REQUEST\n");
                                resp_size=17;
                                break;
                }
                if(sendto( sock, resp, resp_size, 0, (struct sockaddr*)&client, sizeof(name) ) < 0 )
                        perror("responding to client");
                puts("sent\n");
        }
        close(sock);
        return EXIT_SUCCESS;
}
void parse(request *R, char* Rstring){
        char *tok=strtok(Rstring, " ");
        if(tok[0]=='.' && tok[1]=='/'){
                R->type=INVALID;
                return;
        }
        strcpy(R->fileName, tok);
        if((tok=strtok(NULL, " "))==NULL){
                R->type=PACKET_COUNT;
                return;
        }
        R->SEQ=atoi(tok);
        if((tok=strtok(NULL, " "))==NULL || strlen(tok)>2){
                R->type=INVALID;
                return;
        }
        strcpy(R->ACK, tok);
        if(strtok(NULL, " ")!=NULL)
                R->type=INVALID;
        else
                R->type=PACKET;
}