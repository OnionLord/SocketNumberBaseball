#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>

#define PACKET_TYPE_MSG                1
#define PACKET_TYPE_INPUT             2
#define PACKET_TYPE_SERV_DISC      3
#define PACKET_TYPE_CLNT_DISC      4

typedef struct packet {
   int     ptype;
   char  body[100];
} packet;

void * send_msg(void * arg);
void * recv_msg(void * arg);
void error_handling(char * msg);
void sigint_handler (int signo);

int canSend = 0;
int isConnected = 0;
int sock;
pthread_t snd_thread, rcv_thread;

int main(int argc, char *argv[])
{
   struct sockaddr_in serv_addr;
   void * thread_return;

   if(argc!=3) {
      printf("Usage : %s <IP> <port> \n", argv[0]);
      exit(1);
    }

   signal(SIGINT, sigint_handler);

   sock=socket(PF_INET, SOCK_STREAM, 0);

   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family=AF_INET;
   serv_addr.sin_addr.s_addr=inet_addr(argv[1]);
   serv_addr.sin_port=htons(atoi(argv[2]));

   if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
      error_handling("connect() error");

   isConnected = 1;

   pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
   pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
   pthread_join(snd_thread, &thread_return);
   pthread_join(rcv_thread, &thread_return);

   close(sock);
   return 0;
}

void* send_msg(void * arg)
{
   packet send_packet;
   int i, j, cnt, input[3];
   int sock=*((int*)arg);

   while (!isConnected) sleep(1);

   do
   {
      while(isConnected && !canSend) sleep(1);

      scanf("%d %d %d", &input[0], &input[1], &input[2]);

      if (input[0] < 0 || input[0] > 9 || input[1] < 0 || input[1] > 9 || input[2] <0 || input[2] > 9)
      {
         printf("Out of Range! Only 0 ~ 9 Permitted! \n");
      }
      else if (input[0] == input[1] || input[0] == input[2] || input[1] == input[2])
      {
         printf("Please input not duplication numbers! \n");
      }
      else
      {
         send_packet.ptype = PACKET_TYPE_INPUT;
         send_packet.body[0] = input[0];
         send_packet.body[1] = input[1];
         send_packet.body[2] = input[2];
         send_packet.body[3] = '\0';

         canSend = 0;

         write(sock, (char*)&send_packet, sizeof(packet));
      }
   } while (isConnected);

   return NULL;
}

void * recv_msg(void * arg)   // read thread main
{
   packet recv_packet;
   int sock=*((int*)arg);
   int str_len=0;

   while(isConnected)
   {
      str_len=read(sock, (char*)&recv_packet, sizeof(recv_packet));

      if(str_len==-1)
         return (void*)-1;

      switch(recv_packet.ptype)
      {
      case PACKET_TYPE_MSG:
         printf("%s\n", recv_packet.body);
         break;
      case PACKET_TYPE_INPUT:
         canSend = 1;
         break;
      case PACKET_TYPE_SERV_DISC:
         isConnected = 0;
         pthread_cancel(snd_thread);
         break;
      }
   }
   return NULL;
}

void error_handling(char *msg)
{
   fputs(msg, stderr);
   fputc('\n', stderr);
   exit(1);
}

void sigint_handler (int signo)
{
   packet send_packet;

   if (isConnected)
   {
      printf("\nCtrl + C Pressed! (Give Up!!)\n Closing Connection... \n");
      send_packet.ptype = PACKET_TYPE_CLNT_DISC;
      send_packet.body[0] = '\0';
      write(sock, (char*)&send_packet, sizeof(packet));
      isConnected = 0;
      exit(0);
   }
}
