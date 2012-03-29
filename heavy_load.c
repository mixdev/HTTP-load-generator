#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#define THREADS 1        // spawns this many processes
#define CONNECTIONS 10000   // each thread makes this many socket connections
#define REQUESTS 100000   // each socket connection will make this many requests
//#define DURATION 600       // program runs for this many seconds - now runs forever
#define READ_BUFFER 1024  // buffer for reading http server responses

/* threads[THREADS] is a counter for the number of http requests issued by various forks */
/* each thread would write to its own place, less concurrency */


/* each thread is writing to array thread[1-10000] though */
long *threads = NULL;


int make_socket(char *host, char *port) {
    struct addrinfo hints, *servinfo, *p;
    int sock, r;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if((r=getaddrinfo(host, port, &hints, &servinfo))!=0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
        exit(0);
    }
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        if(connect(sock, p->ai_addr, p->ai_addrlen)==-1) {
            close(sock);
            continue;
        }
        break;
    }
    if(p == NULL) {
        if(servinfo)
            freeaddrinfo(servinfo);
        fprintf(stderr, "No connection could be made\n");
        exit(0);
    }
/* otherwise you got a memleak */
    freeaddrinfo(servinfo);
    return sock;
}

/* This function is called from main(), where is told what thread[tnum] to use.
   The thread[tnum] space gives this particular child process a place
   to store its total number of http requests. */

void httpload(char *host, char *port, int tnum) {
    /* initialize array & variables */ 
    int sockets[CONNECTIONS] = { 0 };
    int x, req, y, response;
    char server_response[READ_BUFFER]; 

    /* http header/s */
    //char http_header[31] = "HEAD /test.htm HTTP/1.0\r\n\r\n \0";
    char http_header[31] = "GET /test.txt HTTP/1.0\r\n\r\n \0"; // debug

    /* ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
    while(1) {
        for(x = 0; x < CONNECTIONS; x++) {
            if(sockets[x] == 0) {
                sockets[x] = make_socket(host, port);
                for (y = 0; y < REQUESTS; y++) {
                    req=send(sockets[x], http_header, 31, MSG_NOSIGNAL);
                    response=read(sockets[x], server_response, READ_BUFFER); 
 
                    /* if the server returns "200", increment number of successful requests */
                    if (strstr(server_response, "200"))  {
                        threads[tnum] += 1; 
                        //printf("%s", server_response);  //debug
                        //fprintf(stderr, "200\n"); // debug
                    }
                    else if (strstr(server_response, "404"))  {                   
                        printf("404 Not Found\n");  //debug
                    }
                    else {
                        printf("HTTP Server Error\n");
                    } 
                }
                if(req == -1) {
                    close(sockets[x]);
                    /* failed connections happen. I only care about tracking successful connections.
                       On failure, simply close socket and continue. */
                }
            }
        }
    }
}

int main(int argc, char **argv) {

    /* Setting up shared memory for the 'requests' variable
    since it needs to be accessed by many forks. */
    int shr = shmget(IPC_PRIVATE, sizeof(long)*THREADS, 0600);
    threads = (long *) shmat(shr, NULL, 0);

    /* starttime of this program */
    struct timeval start, end;
    gettimeofday(&start, NULL);

    /* CHANGEME: to be or not to be +1 */
    pid_t offspring[THREADS+1] = { '\0' };
    int threadnum, i, count;

    /* Create a new thread for each httpload() instance.
       Each instance will have its own space to write
       on the threads[tnum] array. */
    for(threadnum = 0; threadnum < THREADS; threadnum++) {
        pid_t pid = fork();
        if(0 == pid) {                             /* child process */
            offspring[threadnum] = getpid();
            httpload(argv[1], argv[2], threadnum);
            //usleep(10000);                        /* 10 ms delay between spawning processes */
            sleep(60);  // 10 second delay between processes ('users' added every 10 seconds) DEBUG
        }
        if(pid < 0) {                             /* failed to fork */
            fprintf(stderr, "Can't fork\n");
            exit(1);
        }
    }

    /* print number of requests every second */
    /* generate load for the DURATION of the test */
    struct timeval now;
    long long requests;
    //for (count=0; count < DURATION; count++) {
    while (1) { // debug, run forever
        requests = 0;                         // resets each second

        /* grab the number of requests done by each thread */
        for(i = 0; i < THREADS; i++) {
            requests += threads[i];   
        }
            sleep(1);
            gettimeofday(&now, NULL);
            printf("average requests/second: %lld\n", (long long)requests/(((now.tv_sec) - (start.tv_sec)) +1)); 
    }

    /* take commandline options */
    getc(stdin);

    /* kill child pids */
    int pids;

    for(pids = 0; pids < THREADS; pids++) {
        kill(offspring[pids],SIGKILL);
    }
    return 0;
}
