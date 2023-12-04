/*
    gameserver.cpp = Code for Problem 1(TicTacToe) server side
    Author = Vikram, CS19B021
    Compilation CMD = g++ gameserver.cpp -o gameserver --std=c++17 -pthread
    Usage = ./gameserver [PORT TO RUN SERVER ON]
    Purpose = Server code for problem 1 
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <poll.h>
#define nloop3(i,j) for(int i=0;i<3;++i) for(int j=0;j<3;++j)
#define MYPORT argv[1]                                                  // server port number
#define BACKLOG 10                                                      // max backlog of pending connects for listen()
#define BUFLEN 100                                                      // size of buffers used for sending and recving data
#define MOVETIMEOUT 15                                                  // timeout for making a move = 15 s
#define ACKTIMEOUT 2                                                    // timeout for getting a response to KEEP_ALIVE msg
#define CHTIMEOUT 30                                                    // timeout for making a choice for the REPLAY question
#define LOGFILE "log_file.txt"                                          // name of log file
#define MAX_PLAYERS 10                                                  // max number of players who may play at any time. may be increased 
using namespace std;

int sockfd;                                                             // fd for the server's socket
atomic_int activeplayers;                                               // num of active players
atomic_uint pidcounter;                                                 // counter for assigning player ids. will be incremented by 1 after a id is assigned
atomic_uint gidcounter;                                                 // counter for assigning game ids. will be incremented by 1 after a id is assigned
mutex logfile_mutex;                                                    // mutex for ensuring log file is not updated by threads simultaneously
const char ackbuffer[BUFLEN] = "I_AM_ALIVE";                            // expected reply from a client for a KEEP_ALIVE msg

// structure to represent a move in the game. p = 1 (player 1 with sym 'X') or 2 (player 2 with sym 'O'),
// r = row index in the game array, c = col index in the game array. r, c both are in {0,1,2}.
struct GMOVE {
    int p, r, c;
    GMOVE(int x, int y, int z) {
        p = x; r = y; c = z; 
    }
};

// structure to represent a game with all the associated data and metadata
struct GAME {
    uint pid1, pid2;                                                    // ids of player 1 and player 2 resp.
    int fd1, fd2;                                                       // connection fds of player 1 and player 2 resp.
    int turn;                                                           // turn = "whose has to make the move now?". turn = 1 or 2
    char array[3][3];                                                   // array for playing the game by placing 'X's and 'O's
    vector<GMOVE> moveSeq;                                              // sequence of moves made in the game so far
    int cause;                                                          // cause for completion. cause = 1 - win , 2 - draw, 3 - timeout, 4 - disconnection
    int winner;                                                         // winner of the game. winner = 1 (player 1) or 2 (player 2)
    time_t starttime, endtime;                                          // start time and end time resp.
    uint gameid;                                                        // id of the game
};

// function for logging a game in LOGFILE.
void logger(GAME* game) {
    logfile_mutex.lock();                                               // get mutex lock to prevent other threads from entering
    ofstream fout;                                                      // ofstream object to write to LOGFILE                                     
    time_t duration = game->endtime - game->starttime;                  // duration of game
    fout.open(LOGFILE,std::ios_base::app);                              // open LOGFILE in append mode
    fout << "_______________________________________________________________";
    fout << "_______________________________________________________________" << endl << "[NEW ENTRY]\n";
    // write game id, player ids, start time, end time and duration to LOGFILE
    fout << "Game ID = " << game->gameid << endl;
    fout << "Player 1 ID = " << game->pid1 << endl;
    fout << "Player 2 ID = " << game->pid2 << endl;
    fout << "Start Time = " << ctime(&game->starttime);
    fout << "End Time = " << ctime(&game->endtime);
    fout << "Duration = " << duration/60 << " m " << duration % 60 << " s" << endl;
    // write the move sequence to LOGFILE
    fout << "Moves Made = <";
    int count = game->moveSeq.size();
    int i = 0;
    for(auto& m : game->moveSeq) {
        fout << "(PLR" << m.p << "," << m.r << "," << m.c << ")";
        ++i;
        if(i != count)
            fout << ", ";
    }
    fout << ">" << endl;
    // write game result to LOGFILE. game result depends on cause and winner
    if(game->cause == 1) {
        fout << "Result = Player " << game->winner << " won the game." << endl;
    }
    else if(game->cause == 2) {
        fout << "Result = The game was a draw." << endl;
    }
    else if(game->cause == 3) {
        fout << "Result = The game was quitted due to inactivity." << endl;
    }
    else {
        fout << "Result = The game was quitted due to disconnection." << endl;
    }
    // close the LOGFILE and release the mutex lock
    fout.close();
    logfile_mutex.unlock();
}

// function to wait for data to be recved at connfd with timeout = tmout seconds.
// if timeout hasn't expired, data recved is put into buf.
int polledrecv(int connfd,void* buf,int tmout) {

    // poll connfd for input data with a timeout as tmout * 1000 milliseconds
    // poll returns <= 0 -> an error or poll has timed out
    pollfd pollstruct = {.fd=connfd,.events=POLL_IN,.revents=0};
    if(poll(&pollstruct,POLL_IN,tmout * 1000) <= 0) {
        return -1;                                      // failed recv
    }
    else {
        // here, we fetch data from connfd and write into buf. also handle any errors
        int ret = recv(connfd,buf,BUFLEN,0);
        if(ret < 0) {
            perror("ERROR - recv failed.");
        }
        return ret;
    }
}

// send a message to connfd with code cd and data = dt
// code : 0 -> KEEP_ALIVE msg; 1 -> print data msg; 2 -> print data and send player response back msg;
//        3 -> game over msg to make client process exit from its loop, close its connection fd and return
int codesend(int connfd, int cd, string dt) {
    string s = "@" + to_string(cd) + "@ " + dt;             // coded msg
    char sendbuf[BUFLEN];                                   // buf containing data to send
    strcpy(sendbuf,s.c_str());                              // fill buf with s
    int ret = send(connfd,sendbuf,BUFLEN,MSG_NOSIGNAL);     // send the coded msg
    // catch failed send and display errno
    if(ret < 0) {
        perror("ERROR - send failed.");
        return ret;
    }
    if(cd == 0) {
        // for KEEP_ALIVE msg, expect a response within ACKTIMEOUT
        // response should be "I_AM_ALIVE"
        char recvbuf[BUFLEN];
        if(polledrecv(connfd,recvbuf,ACKTIMEOUT) < 0 || strcmp(ackbuffer,recvbuf) != 0) {
            return -1;
        }
    }
    return ret;
}

// function to send KEEP_ALIVE msgs to both fd1 and fd2.
// returns true iff both fd1 and fd2 are alive
bool alive(int fd1,int fd2) {
    int r1 = codesend(fd1,0,"ARE YOU ALIVE?");
    int r2 = codesend(fd2,0,"ARE YOU ALIVE?");
    return !(r1 < 0 || r2 < 0);
}

// initialize a game by setting turn = player 1('X')'s turn,
// assigning a new game id and setting all entries in game array as '_'.
// '_' indicates an unfiiled position
void initgame(struct GAME* game) {
    game->turn = 1;
    game->gameid = gidcounter++;
    nloop3(a,b) {
        game->array[a][b] = '_';
    }
}

// function to return the game array as a string in human-friendly form
string gamestring(char array[3][3]) {
    string res("Game Status:-\n");
    nloop3(a,b) {       
        res += array[a][b];
        res += " ";
        if(b != 2)
            res += "| ";
        else
            res += "\n";
    }
    return res;
}

// function to try placing symbol=sym at the position (r,c) in game_array=array
// if (r,c) is not valid, -1 is returned.
// else if (r,c) is already filled, -2 is returned.
// otherwise, the move is made and a game over check is made.
// if game is over, 1 is returned if 'X' won, 2 is returned if 'O' won and 3 is returned for a draw.
// if game is not over, 0 is returned.
int makemove(char array[3][3], char sym, int r, int c) {

    // valid indices check
    if(!(r>=0 && r<=2 && c>=0 && c<=2))
        return -1;
    // unfilled position check
    if(array[r][c] != '_')
        return -2;
    // now, we place the sym at (r,c)
    array[r][c] = sym;

    // row win check
    for(int a=0;a<3;++a)
        if(array[a][0] != '_' && array[a][0] == array[a][1] && array[a][1] == array[a][2])
            return array[a][0] == 'X' ? 1 : 2;
    
    // column win check
    for(int a=0;a<3;++a)
        if(array[0][a] != '_' && array[0][a] == array[1][a] && array[1][a] == array[2][a])
            return array[0][a] == 'X' ? 1 : 2;
    
    // cross win check
    if(array[0][0] != '_' && array[0][0] == array[1][1] && array[1][1] == array[2][2])
        return array[0][0] == 'X' ? 1 : 2;
    if(array[0][2] != '_' && array[0][2] == array[1][1] && array[1][1] == array[2][0])
        return array[0][2] == 'X' ? 1 : 2;

    // check for unfilled positions. if any exist, game is not over
    nloop3(a,b) {
        if(array[a][b] == '_') {
            return 0;
        }
    }
    return 3;                       // here, game is over and it is a draw.
}

// most important function of the code. all threads execute this function after creation
void playgame(struct GAME* game) {

    int r, c;                                   // r - row index, c - col index for a move
    int moveres;                                // result of a move
    bool redo;                                  // = true iff a move was invalid. the relevant player must try again 
    char rbuffer[BUFLEN];                       // buffer to store recved data for a move
    string msg, gamemsg;                        // msg - msg to send, gamemsg -  game status as a string
    int movfd, nonmovfd;                        // movfd = conn fd of player who has to move and nonmovfd = conn fd of movfd-player's partner
    char ch1[BUFLEN], ch2[BUFLEN];              // ch1 and ch2 store choices of player 1 and player 2 for the replay question
    string ch1str, ch2str;                      // string eqvts of ch1 and ch2 buffers resp.
    bool logged = false;                        // flag to indicate if a game has been logged already. useful for handling disconnects

    // initialize the game with a new id, set game->turn = 1 and make the entire game array unfilled
    initgame(game);

    // section to send player id, player symbol and game id msgs to both the players
    msg = "Your partner's ID is "+to_string(game->pid2)+". Your symbol is 'X'.\nStarting the game with ID "+to_string(game->gameid)+" ...";
    codesend(game->fd1,1,msg);
    msg = "Connected to the game server. Your player ID is " + to_string(game->pid2) + ".\n";
    codesend(game->fd2,1,msg);
    msg = "Your partner's ID is "+to_string(game->pid1) + ". Your symbol is 'O'.\nStarting the game  with ID "+to_string(game->gameid)+" ...";
    codesend(game->fd2,1,msg);

    // get starttime and clear the move sequence vector
    game->starttime = time(NULL);
    game->moveSeq.clear();

    while(1) {
        // send game status messages to both the players
        gamemsg = gamestring(game->array); codesend(game->fd1,1,gamemsg); codesend(game->fd2,1,gamemsg);
        // find movfd and nonmovfd. tell non-move player that his partner is playing
        nonmovfd = (game->turn == 1) ? game->fd2 : game->fd1;
        movfd = (game->turn == 1) ? game->fd1 : game->fd2;
        codesend(nonmovfd,1,"Your partner is playing now... ");
        do {
            // initially, we have no redo
            redo = false;     
            // if player 1 or player 2 is not alive, we have a disconnect and the game hasn't been logged
            if(!alive(game->fd1,game->fd2)) {
                logged = false; goto disconnect;
            }
            // prompt movfd for his move and wait for a response with timeout = MOVETIMEOUT
            codesend(movfd,2,"Enter (ROW, COL) for placing your mark: ");
            if (polledrecv(movfd,rbuffer,MOVETIMEOUT) < 0) {
                // here, we have a timeout. so, send relevant msgs to both players, cause = 3 (inactivity),
                // get endtime and jump to timed_out label
                codesend(movfd,1,"You have run out of time.");
                codesend(nonmovfd,1,"Your opponent has timed out.");
                game->cause = 3;
                game->endtime = time(NULL);
                goto timed_out;
            }
            // get recved message and try to read integers r and c from it
            string recvmsg((char*)rbuffer);
            stringstream ss(recvmsg);
            ss >> r >> c;
            // if reading r and c has failed, we send a errmsg and ask the move player to try again
            if(ss.fail()) {
                redo = true;
                string errmsg = "Invalid Move: Enter 2 valid indices in 3x3 array correctly. Try Again!!";
                codesend(movfd,1,errmsg);
                continue;
            }
            // try to fill (r,c) with movfd's symbol after converting them to 0-indexed form
            moveres = makemove(game->array, game->turn == 1 ? 'X':'O', r-1, c-1);
            if(moveres<0) {
                // here, the move is invalid. we send a errmsg and ask the move player to try again
                redo = true;
                string errmsg;
                if(moveres == -1)
                    errmsg = "Invalid Move: Range Check failed. Enter indices in {1,2,3} only. Try Again!!"; 
                else
                    errmsg = "Invalid Move: Position Already filled. Try Again!!";
                codesend(movfd,1,errmsg);
            }
        }while(redo);
        // add the move to move sequence
        game->moveSeq.push_back(GMOVE(game->turn,r,c));

        if(moveres == 0) {  // here, the game is not over. so we switch turn and let the game continue
            game->turn = 3 - game->turn;
        }
        else {
            // the game is over. So, we send game status msg to both players
            gamemsg = gamestring(game->array); codesend(game->fd1,1,gamemsg); codesend(game->fd2,1,gamemsg);

            // if player 1 or player 2 is not alive, we have a disconnect and the game hasn't been logged
            if(!alive(game->fd1,game->fd2)) {
                logged = false; goto disconnect;
            }
            
            // code to set cause and winner fields in the game. it also sets msg to an appropriate result msg
            if(moveres == 1) {  
                // player 1 won -> cause = 1, winner = 1
                msg = "Player "+to_string(game->pid1)+" has won!!"; game->cause = 1; game->winner = 1;
            }
            else if(moveres == 2) { 
                // player 2 won -> cause = 1, winner = 2
                msg = "Player "+to_string(game->pid2)+" has won!!"; game->cause = 1; game->winner = 2;
            }
            else {
                // game was a draw -> cause = 2
                msg = "The game was a draw."; game->cause = 2;
            }
            // get endtime and send game result msgs to both players
            game->endtime = time(NULL);
            codesend(game->fd1,1,msg); codesend(game->fd2,1,msg);
timed_out:
            // log the game
            logger(game);

            // if player 1 or player 2 is not alive, we have a disconnect and the game has already been logged
            if(!alive(game->fd1,game->fd2)) {
                logged = true; goto disconnect;
            }

            // send replay msg to both the players
            msg = "Do you want to replay(YES|NO)?"; codesend(game->fd1,2,msg); codesend(game->fd2,2,msg);

            // wait for replay response from both players. if there is no response within CHTIMEOUT,
            // it is assumed that we have a disconnect and the game has already been logged
            if(polledrecv(game->fd1,ch1,CHTIMEOUT) < 0|| polledrecv(game->fd2,ch2,CHTIMEOUT) < 0) {
                logged = true; goto disconnect;
            }

            ch1str = ch1; ch2str = ch2;
            // start a new game iff both players respond "YES"
            if(ch1str == "YES" && ch2str == "YES") {
                // initialize the game with a new id, set game->turn = 1 and make the entire game array unfilled
                initgame(game);
                // send game id msg to both players
                string idmsg = "Starting a new game with ID "+to_string(game->gameid)+" ...";
                codesend(game->fd1,1,idmsg); codesend(game->fd2,1,idmsg);
                // get starttime and clear the move sequence vector
                game->starttime = time(NULL);
                game->moveSeq.clear();
            }
            else {
                // send no replay msg to both players and goto finish for terminating this thread
                string endmsg = "No Replay... Session Over";
                codesend(game->fd1,1,endmsg); codesend(game->fd2,1,endmsg);
                goto finish;
            }
        }
    }

disconnect:
    // send disconnect msg to both players( only the connected player will recv it)
    codesend(game->fd1,1,"Sorry, Your partner disconnected! "); codesend(game->fd2,1,"Sorry, Your partner disconnected! ");
    if(!logged) {
        // if the game hasn't been logged, set cause = 4(disconnection), get endtime and log the game
        game->cause = 4;
        game->endtime = time(NULL);
        logger(game);
    }

finish:
    // send connection termination(code - 3 msg) to both players in order to make them finish execution
    codesend(game->fd1,3,""); codesend(game->fd2,3,"");
    // close the conn fds of both players, update activeplayers and free the heap memory of game
    close(game->fd1); close(game->fd2);
    activeplayers -= 2;
    delete game;
}

// function to accept a new player connection, assign the player 
// an id and return the pair (connection fd, player id)
// returns (-1,0) if accept() fails
pair<int,uint> acceptplayers() {
    int connfd;
    if((connfd = accept(sockfd,NULL,NULL)) == -1) {
        printf("ERROR: accept connection failed\n");
        return pair<int,uint>(-1,0);                    // failed accept
    }
    ++activeplayers;                                    // update num of active players
    uint playerid = pidcounter++;                       // assign id
    return pair<int,uint> {connfd,playerid};
}

int main(int argc, char** argv) {

    if(argc != 2) {
        cout << "Usage: ./gameserver [PORT TO RUN SERVER ON]";
        exit(-1);
    }

    struct sockaddr_in servaddr;                        // internet address of server 
    memset(&servaddr,0,sizeof servaddr);                // zero out servaddr
    servaddr.sin_family = AF_INET;                      // use IPv4 address family
    servaddr.sin_port = htons((short)atoi(MYPORT));     // set server's port number
    servaddr.sin_addr.s_addr = INADDR_ANY;              // use the ip address of the current machine

    // socket creation with domain as IPv4 family, type as STREAM socket and protocol as 
    // 0 (choose any protocol that supports SOCK_STREAM type)
    if((sockfd = socket(AF_INET,SOCK_STREAM,0)) == -1) {
        perror("ERROR - socket creation failed"); exit(-1);
    }

    // change the socket to allow reuse of ip address and the port number
    int yes = 1;
    if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&yes,sizeof yes) == -1) {
        perror("ERROR - socket reuse failed"); exit(-1);
    }

    // bind the socket to servaddr
    if(bind(sockfd, (struct sockaddr*)&servaddr, sizeof servaddr) != 0) {
        perror("ERROR - binding of socket failed"); exit(-1);
    }

    // listen for incoming connect requests
    if(listen(sockfd,BACKLOG) != 0) {
        perror("ERROR - listen failed"); exit(-1);
    }
    else {
        cout << "Game server started. Waiting for players ... " << endl;
    }

    // create an empty LOGFILE
    ofstream f; f.open(LOGFILE); f.close();

    // initialize the global atomic variables appropriately. Both game and player ids start with 1 
    // and there are no active players initially
    pidcounter = 1; gidcounter = 1;
    activeplayers = 0;

    struct GAME* game;                  // pointer to a newly allocated game
    bool waiting = false;               // true iff there is a player waiting for a partner
    int waitfd;                         // waiting player's connection fd
    uint waitpid;                       // waiting player's id
    while(1) {
        // accept more players only when activeplayers < MAX_PLAYERS
        if(activeplayers.load() < MAX_PLAYERS) {
            pair<int,uint> p = acceptplayers();
            // catch failed accept
            if(p.first == -1)
                continue;
            if(waiting) {
                // here, we can start a new game with the waiting player as player 1 and the
                // newly accepted player as player 2
                game = new GAME();                                      // alloc a new game
                game->fd1 = waitfd; game->fd2 = p.first;                // set conn fds for the new game
                game->pid1 = waitpid; game->pid2 = p.second;            // set player ids for the new game
                // create a thread that will execute playgame function with argument as game and detach the thread for independent execution
                thread newth(playgame,game);
                newth.detach();
                waiting = false;                                        // there is no one waiting now
            }
            else {
                // here, the new player has to wait for a partner. so, update waiting, waitconnfd and waitingpid
                waiting = true;
                waitfd = p.first;
                waitpid = p.second;
                // send a "connected and waiting" msg to the waiting player.
                string msg = "Connected to the game server. Your player ID is " + to_string(waitpid) + ". Waiting for a partner to join...";
                codesend(waitfd,1,msg);
            }
        }
    }
    return 0;
}