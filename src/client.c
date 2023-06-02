#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <signal.h>

#define NewLineRemover(str){\
    size_t len = strlen(str); \
    if(len > 0 && str[len-1] == '\n') str[len-1] = '\0'; \
}

enum ClientState{
    cs_STANDBY = 0,
    cs_SERVER = 1,
    cs_CLIENT = 2,
    cs_DOWN = 3
};

enum ClientState MyState = cs_STANDBY;

sem_t sem;

/*
TODO : 
Currently, this code only works for a single client and cannot discover the other client.
int MyPort = 9999;
int OtherPort = 9998;
By swapping the values, a second client file (c file) should be created and compiled. Then, a system will be prepared to select whether the program will start as the first or the second client by taking an argument during runtime.
*/

// client -start-
int ClientSocket;
struct sockaddr_in ListenerAddr;
socklen_t ListenerAddrLen = sizeof(ListenerAddr);
int MyPort = 9999;
int OtherPort = 9998;
const char ServerIP[] = "127.0.0.1";
// client -end-

// server -start-
int ListenerSocket;
int ListenerAcceptSocket;
struct sockaddr_in ListenAddr;
socklen_t ListenAddrLen = sizeof(ListenAddr);
// server -end-

//State
void SetState(enum ClientState state){
    MyState = state;
}
bool IsState(enum ClientState state){
    return (MyState == state);
}
//State

void* ListenArea    (void* arg);
void* ClientArea    (void* arg);
void* ReadFromOther (void* arg);
void* SendToOther   (void* arg);

void ExitThreads(pthread_t threadOne, pthread_t threadTwo, int WhichSocket){
    pthread_t threads[2] = {threadOne, threadTwo};
    while (true)
    {
        if(IsState(cs_DOWN)){
            for(int i = 0; i < 2; i++)
                pthread_kill(threads[i], SIGINT);
            close(WhichSocket);
            break;
        }else
            usleep(300000); //300ms
    }
}

void* ListenArea(void* arg){
    int*p = (int*)arg;
    int Port = *p;

    if((ListenerSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Error, server socket.");
        exit(EXIT_FAILURE);
    }

    ListenAddr.sin_family = AF_INET; //IPV4 internet bağlantısı
    ListenAddr.sin_addr.s_addr = INADDR_ANY; //bütün ip leri cevapla
    ListenAddr.sin_port = htons(Port); //16 bit e çevirme işlemi

    if(bind(
        ListenerSocket,
        (struct sockaddr*) &ListenAddr,
        sizeof(ListenAddr)
    ) < 0){
        perror("Bind failed.\n");
        exit(EXIT_FAILURE);
    }

    if(listen(
        ListenerSocket,
        2 //2 bağlantıya izin verir.
    ) < 0){
        perror("Listen failed.\n");
        exit(EXIT_FAILURE);
    }
    
    pthread_t ReadFromOtherID;
    pthread_t SendToOtherID;

    while ( !IsState(cs_CLIENT) )
    {
        if((ListenerAcceptSocket = accept(
            ListenerSocket,
            (struct sockaddr*) &ListenAddr,
            (socklen_t *) &ListenAddrLen
        )) < 0){
            perror("Accept Failed \n");
            exit(EXIT_FAILURE);
        }
        
        sem_wait(&sem);

        if(IsState(cs_STANDBY)){
            printf("Communication started.(S)\n");
            SetState(cs_SERVER);
        }else
            return NULL;

        sem_post(&sem);

        pthread_create(&ReadFromOtherID, NULL, ReadFromOther, &ListenerAcceptSocket);
        pthread_create(&SendToOtherID, NULL, SendToOther, &ListenerAcceptSocket);

        ExitThreads(ReadFromOtherID, SendToOtherID, ListenerSocket);

        pthread_join(ReadFromOtherID, NULL);
        pthread_join(SendToOtherID, NULL);
    }
    
}

void* ClientArea(void* arg){
    int*p = (int*)arg;
    int Port = *p;

    if(
        (ClientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0
    ){
        perror("Socket failed.\n");
        exit(EXIT_FAILURE);
    }

    ListenerAddr.sin_family = AF_INET;
    ListenerAddr.sin_port = htons(Port);

    if(
        inet_pton(AF_INET, ServerIP, &ListenerAddr.sin_addr) <= 0
    ){
        perror("INET Valid Address\n");
        exit(EXIT_FAILURE);
    }

    while(true){
        if(!IsState(cs_STANDBY)) break;
        int c = connect(
                ClientSocket,
                (struct sockaddr*)&ListenerAddr,
                sizeof(ListenerAddr)
            );
        printf("Waiting for connection.\n");
        if(c < 0){
            sleep(1);
            continue;
        }
        break;
    }

    sem_wait(&sem);

    if(IsState(cs_STANDBY)){
        SetState(cs_CLIENT);
        printf("Communication started.(c)\n");
    }else
        return NULL;

    sem_post(&sem);

    pthread_t ReadThreadID;
    pthread_t SendThreadID;
    pthread_create(&ReadThreadID, NULL, ReadFromOther, &ClientSocket);
    pthread_create(&SendThreadID, NULL, SendToOther, &ClientSocket);

    ExitThreads(ReadThreadID, SendThreadID, ClientSocket);
    
    pthread_join(ReadThreadID, NULL);
    pthread_join(SendThreadID, NULL);

}

void* ReadFromOther(void* arg){
    int* socket = (int*)arg;
    int socketID = *socket;
    char message[1024] = {0};
    while (true)
    {
        memset(message, 0, sizeof(message));
        int valRead = read(socketID, message, 1024);
        if(valRead > 0)
            printf("Incoming Message : %s \n", message);
        else if(valRead == 0){
            printf("Disconnected from other client.\n");
            SetState(cs_DOWN);
            break;
        }
    }
    close(socketID);
    shutdown(socketID, SHUT_RDWR);
    return NULL;
}

void* SendToOther(void * arg){
    int* socket = (int*)arg;
    int socketID = *socket;
    char sendMessage[1204] = {0};
    while (true)
    {
        if(IsState(cs_DOWN)) break;

        memset(sendMessage, 0, sizeof(sendMessage));
        fgets(sendMessage, sizeof(sendMessage), stdin);

        if(IsState(cs_DOWN)) break;

        NewLineRemover(sendMessage);

        send(socketID, sendMessage, strlen(sendMessage), 0);
    }
    return NULL;
}

int main(){

    SetState(cs_STANDBY);
    sem_init(&sem, 0, 1);

    pthread_t ClientThreadID;
    pthread_t ListenThreadID;
    pthread_create(&ClientThreadID, NULL, ClientArea, &OtherPort);
    pthread_create(&ListenThreadID, NULL, ListenArea, &MyPort);

    pthread_join(ClientThreadID, NULL);
    pthread_join(ListenThreadID, NULL);

    sem_destroy(&sem);
    
    return 0;
}
