#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <experimental/filesystem>
#include <fstream> 
#include <dirent.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
string dirname;

///////////////////////////////////////////////////////////////////////////////

void* clientCommunication(void* data);
void signalHandler(int sig);
void* s_threading(void* arg);
int saveMessage(vector<string> msg);
vector<string> listFiles(char* directory);
void listMessages(vector<string> msg, int* current_socket);
void readMessage(vector<string> msg, int* socket);
void delMessage(vector<string> msg, int* socket);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    if (argc < 2) {
        cerr << "Missing arguments!" << endl;
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    dirname = argv[2];

    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;

    ////////////////////////////////////////////////////////////////////////////
    // SIGNAL HANDLER
    // SIGINT (Interrup: ctrl+c)
    // https://man7.org/linux/man-pages/man2/signal.2.html
    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("signal can not be registered");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error"); // errno set by socket()
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    // https://man7.org/linux/man-pages/man2/setsockopt.2.html
    // https://man7.org/linux/man-pages/man7/socket.7.html
    // socket, level, optname, optvalue, optlen
    if (setsockopt(create_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseValue,
        sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
        SOL_SOCKET,
        SO_REUSEPORT,
        &reuseValue,
        sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }


    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    // Attention: network byte order => big endian
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ////////////////////////////////////////////////////////////////////////////
    // ASSIGN AN ADDRESS WITH PORT TO SOCKET
    if (bind(create_socket, (struct sockaddr*)&address, sizeof(address)) == -1)
    {
        perror("bind error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ALLOW CONNECTION ESTABLISHING
    // Socket, Backlog (= count of waiting connections allowed)
    if (listen(create_socket, 5) == -1)
    {
        perror("listen error");
        return EXIT_FAILURE;
    }

    //new thread
    pthread_t thread;
    while (!abortRequested)
    {
        /////////////////////////////////////////////////////////////////////////
        // ignore errors here... because only information message
        // https://linux.die.net/man/3/printf
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPTS CONNECTION SETUP
        // blocking, might have an accept-error on ctrl+c
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket,
            (struct sockaddr*)&cliaddress,
            &addrlen)) == -1)
        {
            if (abortRequested)
            {
                perror("accept error after aborted");
            }
            else
            {
                perror("accept error");
            }
            break;
        }
        //create thread
        pthread_create(&thread, NULL, s_threading, (void*)&new_socket);
    }

    // frees the descriptor
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

void * s_threading(void* arg){
    cout << "A Client connected to the server!" << endl;
    int clientfd = *((int*)arg);
    pthread_detach(pthread_self());

    //client communication
    clientCommunication(&clientfd);
    close(clientfd);
    return nullptr;
}

void sendMessage(int* socket, const char* msg){
    if (send(*socket, msg, strlen(msg), 0) == -1)
    {
        perror("Message send failed!");
    }
}

void* clientCommunication(void* data)
{
    char buffer[BUF];
    int size;
    int* current_socket = (int*)data;

    ////////////////////////////////////////////////////////////////////////////
    // SEND welcome message
    strcpy(buffer, "Welcome to TWMailer!\r\nPlease enter your commands...\r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
    {
        perror("send failed");
        return NULL;
    }

    do
    {
        /////////////////////////////////////////////////////////////////////////
        // RECEIVE
        size = recv(*current_socket, buffer, BUF - 1, 0);
        if (size == -1)
        {
            if (abortRequested)
            {
                perror("recv error after aborted");
            }
            else
            {
                perror("recv error");
            }
            break;
        }

        if (size == 0)
        {
            printf("Client closed remote socket\n"); // ignore error
            break;
        }

        // remove ugly debug message, because of the sent newline of client
        if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
        {
            size -= 2;
        }
        else if (buffer[size - 1] == '\n')
        {
            --size;
        }

        buffer[size] = '\0';
        printf("Message received: %s\n", buffer); // ignore error

        /*
        if (send(*current_socket, "OK", 3, 0) == -1)
        {
            perror("send answer failed");
            return NULL;
        }
        */
        vector<string> msg;
        string line = "";
        stringstream ss;
        ss << buffer << "\n";

        while (getline(ss, line))
        {
            msg.push_back(line);
        }
        

        if(msg[0] =="SEND"){
            cout << "SEND" << endl;
            if(saveMessage(msg) == -1)
                sendMessage(current_socket, "ERR");
            else
                sendMessage(current_socket, "OK");
        }else if(msg[0] =="LIST"){
            listMessages(msg, current_socket);
        }else if(msg[0] =="READ"){
            readMessage(msg, current_socket);
        }else if(msg[0] =="DEL"){
            delMessage(msg, current_socket);
        }else if(msg[0] =="QUIT"){
        }else{
            sendMessage(current_socket, "Wrong Command, try again!");
        }
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    // closes/frees the descriptor if not already
    if (*current_socket != -1)
    {
        if (shutdown(*current_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown new_socket");
        }
        if (close(*current_socket) == -1)
        {
            perror("close new_socket");
        }
        *current_socket = -1;
    }

    return NULL;
}

//Save sent message in given mail spool directory
int saveMessage(vector<string> msg){
    char cdir[256];
    getcwd(cdir, 256);
    char tmp_dir[256];
    strcat(tmp_dir, cdir);
    //change directory to given path
    chdir(dirname.c_str());

    //create new directory
    mkdir(msg[2].c_str(), 0777);

    //get current working directory
    getcwd(cdir, 256);
    
    strcat(cdir, "/");
    strcat(cdir, msg[2].c_str());
    strcat(cdir, "/");

    chdir(cdir);

    ofstream newFile(msg[3] + ".txt");
    newFile << msg[1] << "\n" << msg[2] << "\n" << msg[3] << "\n" << msg[4];
    newFile.close(); 
    chdir(tmp_dir);
    return 1;
}

vector<string> listFiles(char* directory){
    DIR *dir;
    struct dirent *file;
    vector<string> files;

    if ((dir = opendir(directory)) != NULL) {
        while ((file = readdir(dir)) != NULL) {
            if(strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0)
                files.push_back(file->d_name);
        }
        closedir(dir);
    }else{
        cerr << "Error opening directory" << endl;
    }
    return files;
}

void listMessages(vector<string> msg, int* socket){
    char dir[256] = "";
    strcat(dir, dirname.c_str());
    strcat(dir, "/");
    strcat(dir, msg[1].c_str());
    strcat(dir, "/");

    vector<string> files = listFiles(dir);
 
    string size = to_string(files.size()) += "\n";
    sendMessage(socket, size.c_str());
    for(string file : files){
        file += "\n";
        sendMessage(socket, file.c_str());
    }
}

void readMessage(vector<string> msg, int* socket){
    char dir[256] = "";
    char response[500] = "";
    DIR *directory;
    struct dirent *file;
    string fileText;
    strcat(dir, dirname.c_str());
    strcat(dir, "/");
    strcat(dir, msg[1].c_str());
    strcat(dir, "/");

    msg[2] += ".txt";
    chdir(dir);
    getcwd(dir, 256);

    if ((directory = opendir(dir)) != NULL) { 
        while ((file = readdir(directory)) != NULL) {
            if(strcmp(file->d_name, msg[2].c_str()) == 0){
                getcwd(dir, 256);
                cout << dir << endl;
                cout << file->d_name << endl; 
                ifstream newfile;
                newfile.open(file->d_name, ios::in);

                if(newfile.is_open()){
                    strcat(response, "OK\n");
                    while(getline(newfile, fileText)){
                        fileText += "\n";
                        strcat(response, fileText.c_str());
                    }
                    sendMessage(socket, response);
                    newfile.close(); //close the file object
                }else{
                    cerr << "Error opening file" << endl;
                    sendMessage(socket, "ERR");
                }
                break;
            }
        }
        
        closedir(directory);
    }else{
        cerr << "Error opening directory" << endl;
        sendMessage(socket, "ERR");
    }
}

void delMessage(vector<string> msg, int* socket){
    char dir[256] = "";
    strcat(dir, dirname.c_str());
    strcat(dir, "/");
    strcat(dir, msg[1].c_str());
    strcat(dir, "/");
    chdir(dir);

    if(remove(msg[2].c_str()) == 0){
        sendMessage(socket, "OK");
    }else{
        sendMessage(socket, "ERR");
    }
}

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("abort Requested... "); // ignore error
        abortRequested = 1;
        /////////////////////////////////////////////////////////////////////////
        // With shutdown() one can initiate normal TCP close sequence ignoring
        // the reference count.
        // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
        // https://linux.die.net/man/3/shutdown
        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown new_socket");
            }
            if (close(new_socket) == -1)
            {
                perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1)
        {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown create_socket");
            }
            if (close(create_socket) == -1)
            {
                perror("close create_socket");
            }
            create_socket = -1;
        }
    }
    else
    {
        exit(sig);
    }
}
