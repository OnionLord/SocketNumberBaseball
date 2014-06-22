#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_CLNT 10

#define PACKET_TYPE_MSG                1
#define PACKET_TYPE_INPUT             2
#define PACKET_TYPE_SERV_DISC      3
#define PACKET_TYPE_CLNT_DISC      4

#define GAMEROOM_BLANK            30
#define GAMEROOM_WAITING         31
#define GAMEROOM_PLAYING         32

#define PLAYER_WAITING                  41
#define PLAYER_INPUT_DOING            43
#define PLAYER_INPUT_FINISH            44
#define PLAYER_GUESS_DOING         45
#define PLAYER_GUESS_WAIT            46
#define PLAYER_GUESS_FINISH         47

typedef struct packet {
   int     ptype;
   char  body[100];
} packet;

typedef struct gamePlayer {
   int guess[3];
   int socket;
   int tried;
   int status;
} gamePlayer;

typedef struct clientInfo {
   int client_socket;
   int playing_room;
   char client_ip[16];
   pthread_t send;
   pthread_t recv;
}  clientInfo;

typedef struct gameRoom {
   gamePlayer player[2];
   int roomStatus;
} gameRoom;

int myNumber[3] = { -1, -1, -1};
int myGuess[3] = {-1, -1, -1};

int clnt_cnt=0;
clientInfo clnt_socks[MAX_CLNT];

gameRoom gameRooms[MAX_CLNT/2];

pthread_mutex_t mutx;

void* game_client_recv (void* socketNum)
{
   packet packet_data;
   gamePlayer *me, *opponent;
   clientInfo *mySocket;

   int i,j;
   int ball, strike;

   int mySocketNum = *((int*) socketNum);

   mySocket = &clnt_socks[mySocketNum];

   while(1)
   {
      read(mySocket->client_socket, (char*)&packet_data, sizeof(packet_data));
      switch(packet_data.ptype)
      {
      case PACKET_TYPE_INPUT:
         printf("Client [%d] Inputed [%d %d %d] !! \n", mySocketNum, packet_data.body[0], packet_data.body[1], packet_data.body[2]);

         if (gameRooms[clnt_socks[mySocketNum].playing_room].player[0].socket == clnt_socks[mySocketNum].client_socket)
         {
            me = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[0]);
            opponent = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[1]);
         }
         else {
            me = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[1]);
            opponent = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[0]);
         }

         switch(me->status)
         {
         case PLAYER_INPUT_DOING:
            me->guess[0] = packet_data.body[0];
            me->guess[1] = packet_data.body[1];
            me->guess[2] = packet_data.body[2];

            me->status = PLAYER_INPUT_FINISH;
            // Input
            break;
         case PLAYER_GUESS_DOING:
         case PLAYER_GUESS_WAIT:
            // Doing Guess
            me->tried++;

            ball = 0; strike = 0;

            for (i=0; i<3; i++)
            {
               for (j=0; j<3; j++)
               {
                  if (packet_data.body[i] == opponent->guess[j])
                  {
                     if (i == j) strike++;
                     else ball++;

                     break;
                  }
               }
            }

            if (ball == 0 && strike == 0)
            {
               packet_data.ptype = PACKET_TYPE_MSG    ;
               sprintf(packet_data.body, "[Game Message][%d %d %d] : OUT!",  packet_data.body[0], packet_data.body[1], packet_data.body[2]);
               write(me->socket, (char*)&packet_data, sizeof(packet_data));
               me->status = PLAYER_GUESS_DOING;
               break;
            }
            else if (strike == 3)
            {
               packet_data.ptype = PACKET_TYPE_MSG    ;
               sprintf(packet_data.body, "[Game Message][%d %d %d] : HOME RUN!!",  packet_data.body[0], packet_data.body[1], packet_data.body[2]);
               write(me->socket, (char*)&packet_data, sizeof(packet_data));
               me->status = PLAYER_GUESS_FINISH;
               break;
            }
            else
            {
               packet_data.ptype = PACKET_TYPE_MSG    ;
               sprintf(packet_data.body, "[Game Message][%d %d %d] : %d Strikes, %d Balls!",  packet_data.body[0], packet_data.body[1], packet_data.body[2], strike, ball);
               write(me->socket, (char*)&packet_data, sizeof(packet_data));
               me->status = PLAYER_GUESS_DOING;
               break;
            }
         }
         break;
      case PACKET_TYPE_CLNT_DISC:
         printf("Client [%d] Pressed Ctrl + C !! \n", mySocketNum);

         if (clnt_socks[mySocketNum].playing_room != -1)
         {
            packet_data.ptype = PACKET_TYPE_MSG;
            sprintf(packet_data.body, "Your Opponent Give Up!\n Thanks to play!\n");

            if (gameRooms[clnt_socks[mySocketNum].playing_room].player[0].socket == clnt_socks[mySocketNum].client_socket)
               opponent = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[1]);
            else
               opponent = &(gameRooms[clnt_socks[mySocketNum].playing_room].player[0]);

            write(opponent->socket, (char*)&packet_data, sizeof(packet_data));

            packet_data.ptype = PACKET_TYPE_SERV_DISC;
            packet_data.body[0] = '\0';
            write(opponent->socket, (char*)&packet_data, sizeof(packet_data));
         }

         pthread_mutex_lock(&mutx);

         gameRooms[clnt_socks[mySocketNum].playing_room].roomStatus = GAMEROOM_BLANK;

         for (i=0; i<MAX_CLNT; i++)
         {
            if (clnt_socks[i].client_socket == mySocket->client_socket)
            {
               printf("Game abnormal(Ctrl+c) leaving : [%d] %s / Left %d peoples\n", i, mySocket->client_ip, clnt_cnt-1);
               pthread_cancel(clnt_socks[i].send);
               close(mySocket->client_socket);
               clnt_socks[i].client_socket = 0;
               clnt_socks[i].playing_room = -1;
               break;
            }

         }

         clnt_cnt--;

         for (i=0; i<MAX_CLNT; i++)
         {
            if (clnt_socks[i].client_socket == opponent->socket)
            {
               printf("Game normal leaving : [%d] %s / Left %d peoples\n", i, clnt_socks[i].client_ip, clnt_cnt-1);
               pthread_cancel(clnt_socks[i].send);
               pthread_cancel(clnt_socks[i].recv);
               close(mySocket->client_socket);
               clnt_socks[i].client_socket = 0;
               clnt_socks[i].playing_room = -1;
               break;
            }

         }

         clnt_cnt--;

         pthread_mutex_unlock(&mutx);

         break;
      }
   }
   return (void*) 0;
}

void* game_client_send(void* socketNum)
{

   int mySocketNum = *((int*) socketNum);
   clientInfo *mySocket;
   int i;

   gamePlayer *me, *opponent;
   packet packet_data;

   mySocket = &clnt_socks[mySocketNum];

   packet_data.ptype = PACKET_TYPE_MSG;
   sprintf(packet_data.body, "Waiting For Other Players... (Ctrl + C to exit!)\n");
   write(mySocket->client_socket, (char*)&packet_data, sizeof(packet_data));

   for (i=0; i<MAX_CLNT/2; i++)
   {
      if (gameRooms[i].roomStatus == GAMEROOM_BLANK)
      {
         gameRooms[i].player[0].socket = mySocket->client_socket;
         gameRooms[i].player[0].status = PLAYER_WAITING;
         gameRooms[i].player[0].guess[0] = -1; gameRooms[i].player[0].guess[1] = -1; gameRooms[i].player[0].guess[2] = -1;
         gameRooms[i].roomStatus = GAMEROOM_WAITING;
         mySocket->playing_room =  i;
         me = &(gameRooms[i].player[0]);
         opponent = &(gameRooms[i].player[1]);
         break;
      }
      else if (gameRooms[i].roomStatus == GAMEROOM_WAITING)
      {
         gameRooms[i].player[1].socket = mySocket->client_socket;
         gameRooms[i].player[1].status = PLAYER_WAITING;
         gameRooms[i].player[1].guess[0] = -1; gameRooms[i].player[1].guess[1] = -1; gameRooms[i].player[1].guess[2] = -1;
         gameRooms[i].roomStatus = GAMEROOM_PLAYING;
         mySocket->playing_room =  i;
         me = &(gameRooms[i].player[1]);
         opponent = &(gameRooms[i].player[0]);
         break;
      }
   }

   sprintf(packet_data.body, "Your Room Number is [%d]\n", mySocket->playing_room);
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   while(gameRooms[mySocket->playing_room].roomStatus != GAMEROOM_PLAYING) sleep(1);

   // Wait For Me to Input Finish
   packet_data.ptype = PACKET_TYPE_MSG;
   sprintf(packet_data.body, "Input 3 Numbers, Number's will not be duplicated!\n");
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   me->status = PLAYER_INPUT_DOING;

   packet_data.ptype = PACKET_TYPE_INPUT;
   packet_data.body[0] = '\0';
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   while(me->status != PLAYER_INPUT_FINISH) sleep(1);

   // Wait For Opponent to Input Finish
   packet_data.ptype = PACKET_TYPE_MSG;
   sprintf(packet_data.body, "Wait for opponent to input finished...\n");
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   while(opponent->status == PLAYER_INPUT_DOING ) ;

   // Loop Until Finish
   me->tried = 0;
   me->status = PLAYER_GUESS_DOING;

   while(me->status !=  PLAYER_GUESS_FINISH)
   {
      packet_data.ptype = PACKET_TYPE_MSG;
      sprintf(packet_data.body, "Gussing 3 Numbers (%d try)\n", me->tried);
      write(me->socket, (char*)&packet_data, sizeof(packet_data));

      packet_data.ptype = PACKET_TYPE_INPUT;
      packet_data.body[0] = '\0';
      write(me->socket, (char*)&packet_data, sizeof(packet_data));

      me->status = PLAYER_GUESS_WAIT;

      while(me->status == PLAYER_GUESS_WAIT) ;
   }

   // Wait For opponent to finish guess
   packet_data.ptype = PACKET_TYPE_MSG;
   sprintf(packet_data.body, "Waiting For opponent to finish guess...");
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   while(opponent->status != PLAYER_GUESS_FINISH) ;

   sleep(1);

   // Printing Result And Closing
   if (me->tried < opponent->tried)
   {
      packet_data.ptype = PACKET_TYPE_MSG;
      sprintf(packet_data.body, "[Game Message] Congraulations! You Win!!");
      write(me->socket, (char*)&packet_data, sizeof(packet_data));
   }
   else if (me->tried == opponent->tried)
   {
      packet_data.ptype = PACKET_TYPE_MSG;
      sprintf(packet_data.body, "[Game Message] This game end with draw");
      write(me->socket, (char*)&packet_data, sizeof(packet_data));
   }
   else
   {
      packet_data.ptype = PACKET_TYPE_MSG;
      sprintf(packet_data.body, "[Game Message] You Lose. You have to take more practice!");
      write(me->socket, (char*)&packet_data, sizeof(packet_data));
   }

   packet_data.ptype = PACKET_TYPE_MSG;
   sprintf(packet_data.body, "[Server Message] Closing Connection. Thank you for playing. Good Bye!!!");
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   packet_data.ptype = PACKET_TYPE_SERV_DISC;
   packet_data.body[0] = '\0';
   write(me->socket, (char*)&packet_data, sizeof(packet_data));

   me->status = 0;
   gameRooms[mySocket->playing_room].roomStatus = GAMEROOM_BLANK;

   printf("Game normal leaving : [%d] %s\n", mySocketNum, mySocket->client_ip);

   pthread_mutex_lock(&mutx);

   for (i=0; i<MAX_CLNT; i++)
   {
      if (clnt_socks[i].client_socket == mySocket->client_socket)
      {
         pthread_cancel(clnt_socks[i].recv);
         close(mySocket->client_socket);
         clnt_socks[i].client_socket = 0;
         clnt_socks[i].playing_room = -1;
         break;
      }
   }

   clnt_cnt--;

   pthread_mutex_unlock(&mutx);

   return (void*)0;
}

void error_handling(char* msg)
{
   printf("[Error] %s\n", msg);
   exit(2);
}

int main(int argc, char *argv[])
{
   int serv_sock, clnt_sock;
   struct sockaddr_in serv_adr, clnt_adr;
   socklen_t clnt_adr_sz;
   pthread_t t_id;
   int client_id;
   int i;

   if(argc != 2) {
      printf("Usage : %s <port>\n", argv[0]);
      exit(1);
   }


   // Init Game Rooms
   for (i=0; i<MAX_CLNT/2; i++)
   {
      gameRooms[i].roomStatus = GAMEROOM_BLANK;
      gameRooms[i].player[0].socket = -1;
      gameRooms[i].player[1].socket = -1;
   }

   pthread_mutex_init(&mutx, NULL);

   serv_sock=socket(PF_INET, SOCK_STREAM, 0);

   memset(&serv_adr, 0, sizeof(serv_adr));
   serv_adr.sin_family=AF_INET;
   serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
   serv_adr.sin_port=htons(atoi(argv[1]));

   if(bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr))==-1)
      error_handling("bind() error");
   if(listen(serv_sock, 5)==-1)
      error_handling("listen() error");

   while(1)
   {
      clnt_adr_sz=sizeof(clnt_adr);
      clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_adr,&clnt_adr_sz);

      pthread_mutex_lock(&mutx);

      for (i=0; i<MAX_CLNT; i++)
      {
         if (clnt_socks[i].client_socket == 0)
         {
            clnt_socks[i].client_socket = clnt_sock;
            clnt_socks[i].playing_room = -1;
            sprintf(clnt_socks[i].client_ip, "%s", inet_ntoa(clnt_adr.sin_addr));
            client_id = i;
            break;
         }
      }

      clnt_cnt++;

      pthread_mutex_unlock(&mutx);


      pthread_create(&clnt_socks[i].send, NULL, game_client_send, (void*)&client_id);
      pthread_create(&clnt_socks[i].recv, NULL, game_client_recv, (void*)&client_id);

      pthread_detach(clnt_socks[i].send);
      pthread_detach(clnt_socks[i].recv);

      printf("Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
      printf("Now Connecting : %d persons\n", clnt_cnt);
   }
   close(serv_sock);

   return 0;
}
