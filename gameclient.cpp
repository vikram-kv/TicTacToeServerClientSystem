/*
    gameclient.cpp = Code for Problem 1(TicTacToe) client side
    Author = Vikram, CS19B021
    Compilation CMD = g++ gameclient.cpp -o gameclient --std=c++17
    Usage = Usage : ./gameclient [SERVER IP ADDRESS] [SERVER PORT NO]
    Purpose = Client code for problem 1 
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#define SERVERPORT argv[2]          // server port number
#define BUFLEN 100                  // size of buffers used for sending and recving data
#define STDINFD 0                   // fd for stdin
#define SERVERIPADDR argv[1]        // server ip address
using namespace std;

// response to code-0 msg from server. This is to reply that the client is alive
const char ackbuffer[BUFLEN] = "I_AM_ALIVE";

int main(int argc, char** argv) {

    if(argc != 3) {
        cout << "Usage : ./gameclient [SERVER IP ADDRESS] [SERVER PORT NO]";
        exit(-1);
    }

    struct sockaddr_in servaddr;                            // internet address of server 
    memset(&servaddr,0,sizeof servaddr);                    // zero out servaddr
    servaddr.sin_family = AF_INET;                          // use IPv4 address family
    servaddr.sin_port = htons((short)atoi(SERVERPORT));     // set port number to SERVERPORT

    // try to convert IPv4 address in argv[1] to binary form and store it in network byte order at servaddr.sin_addr
    // 0 return indicates INVALID IPv4 address in dotted notation and -1 return indicates error
    int err = inet_pton(AF_INET,(const char*)SERVERIPADDR,&(servaddr.sin_addr));
    if(err < 0) {
        perror("ERROR - inet_pton");
        exit(-1);
    }
    else if(err == 0) {
        cout << "Bad Server IP Address" << endl;
        exit(-1);
    }

    int sockfd;                                             // socket fd
    // socket creation with domain as IPv4 family, type as STREAM socket and protocol as 
    // 0 (choose any protocol that supports SOCK_STREAM type)
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("ERROR: socket creation failed"); exit(-1);
    }

    // connect to server
    if(connect(sockfd, (struct sockaddr*)&servaddr,sizeof servaddr) == -1) {
        perror("ERROR: server connection failed"); exit(-1);
    }

    char code;                                  // code of recved data
    char recvbuffer[BUFLEN];                    // buffer for storing recved data
    char sendbuffer[BUFLEN];                    // buffer for storing data to send
    string replay;                              // response to replay request
    fd_set rfds;                                // set of file descriptors to monitor for possible read() using select function
    int maxfd = max(sockfd,STDINFD) + 1;        // the range of fds that must be monitored = max{rfds} + 1
    while(1) {
        // recv server msg. first 4 bytes contain a code in the form 
        // "@i@ " where i is in {0,1,2,3}
        recv(sockfd,recvbuffer,BUFLEN,0);       
        code = recvbuffer[1];                   // get server msg's code

        // code = 0 -> Respond to KEEP_ALIVE msg by sending ackbuffer to server immediately
        if(code == '0') {
            send(sockfd,ackbuffer,BUFLEN,0);
        }
        // code = 1 -> Output server msg without the code
        else if(code == '1') {
            cout << recvbuffer+4 << endl;
        }
        // code = 2 -> Output server msg and accept a line of input and send it to the server
        else if(code == '2') {
            cout << recvbuffer+4 << endl;
            FD_ZERO(&rfds);                         // reset rfds
            FD_SET(sockfd,&rfds);                   // set the bits corr. to sockfd and STDINFD in rfds
            FD_SET(STDINFD,&rfds);
            select(maxfd,&rfds,NULL,NULL,NULL);     // block till data can be read from stdinfd or sockfd
            // if data is not there in sockfd, then we have data at stdin. We can read the player's response and send it to the server
            if(!FD_ISSET(sockfd,&rfds)) {
                cin.getline(sendbuffer,BUFLEN);
                send(sockfd,sendbuffer,BUFLEN,0);
            }
        }
        // code = 3 -> close the socket and finish execution
        else {
            break;
        }
    }
    close(sockfd);
    return 0;
}