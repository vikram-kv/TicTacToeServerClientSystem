# TicTacToeServerClientSystem

1) Socket programming in C++ for the client server architecture in a network. The server hosts tic tac toe while the clients are players.
2) The server is programmed to handle timeouts, illegal moves, abrupt disconnection (through heartbeat pings), maintain a detailed log of every game hosted since it began.
3) The server also allows for multiple simultaneous games through C++ threads.

1
a
TicTacToe on a LAN
Compilation
• The version of C++ used is C++17.
• The header files required are
<s y s / s o c k e t . h>
<s y s / t y p e s . h>
<b i t s / s t d c ++.h>
<n e t i n e t / i n . h>
<arpa / i n e t . h>
<u n i s t d . h>
<s y s / time . h>
<p o l l . h>
All header files are normally available on linux systems.
• The compilation command for gameserver is
g++ gameserver.cpp -o gameserver --std=c++17 -pthread
• The compilation command for gameclient is
g++ gameclient.cpp -o gameclient --std=c++17
b
Usage
The server will store the log file in "log_file.txt". The usage of gameserver is ./gameserver [PORT
TO RUN SERVER ON] and the usage of gameclient is ./gameclient [SERVER IP ADDRESS]
[SERVER PORT NO].
c
References
• Manpages for select, poll, setsockopt functions.
• Referenced concurrency control and threading library of C++ at https://www.cplusplus.
com/reference/. Specifically, I read about mutexes, threads, atomic types in C++.
• Basic structure of client-server socket programming in C++ as in https://www.geeksforgeeks.
org/socket-programming-cc/.
