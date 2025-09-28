// UDP Server for math problems
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>

#include <calcLib.h>
#include "protocol.h"

using namespace std;

int loopCount = 0;
int terminate = 0;

void split_host_port(char* input, char* host, char* port) {
    // IPv6 has brackets
    if (input[0] == '[') {
        char* bracket_end = strchr(input, ']');
        int host_length = bracket_end - input - 1;
        
        int i = 0;
        for (i = 0; i < host_length; i++) {
            host[i] = input[i + 1];
        }
        host[i] = '\0';
        
        strcpy(port, bracket_end + 2);
    } else {
        char* colon = strrchr(input, ':');
        int host_length = colon - input;
        
        int i = 0;
        for (i = 0; i < host_length; i++) {
            host[i] = input[i];
        }
        host[i] = '\0';
        
        strcpy(port, colon + 1);
    }
}

void sendAssignment(int sockfd, struct sockaddr* client_addr, socklen_t client_addr_len) {
    
    struct calcProtocol assignment;
    memset(&assignment, 0, sizeof(assignment));
    
    char* operation = randomType();
    
    assignment.type = htons(1);
    assignment.major_version = htons(1);
    assignment.minor_version = htons(0);
    assignment.id = htonl(rand() % 10000 + 1);
    
    int is_float = 0;
    if (operation[0] == 'f') {
        is_float = 1;
    }
    if (is_float) {
        double num1 = randomFloat();
        double num2 = randomFloat();
        double answer = 0.0;
        
        assignment.flValue1 = num1;
        assignment.flValue2 = num2;
        
        if (strcmp(operation, "fadd") == 0) {
            assignment.arith = htonl(5);
            answer = num1 + num2;
        } else if (strcmp(operation, "fsub") == 0) {
            assignment.arith = htonl(6);
            answer = num1 - num2;
        } else if (strcmp(operation, "fmul") == 0) {
            assignment.arith = htonl(7);
            answer = num1 * num2;
        } else if (strcmp(operation, "fdiv") == 0) {
            assignment.arith = htonl(8);
            answer = num1 / num2;
        }
        
        assignment.flResult = answer;
        
#ifdef DEBUG
        printf("Generated float assignment: %s %.8g %.8g = %.8g\n", 
               operation, num1, num2, answer);
#endif
        
    } else {
        int num1 = randomInt();
        int num2 = randomInt();
        int answer = 0;
        
        assignment.inValue1 = htonl(num1);
        assignment.inValue2 = htonl(num2);
        
        if (strcmp(operation, "add") == 0) {
            assignment.arith = htonl(1);
            answer = num1 + num2;
        } else if (strcmp(operation, "sub") == 0) {
            assignment.arith = htonl(2);
            answer = num1 - num2;
        } else if (strcmp(operation, "mul") == 0) {
            assignment.arith = htonl(3);
            answer = num1 * num2;
        } else if (strcmp(operation, "div") == 0) {
            assignment.arith = htonl(4);
            answer = num1 / num2;
        }
        
        assignment.inResult = htonl(answer);
        
#ifdef DEBUG
        printf("Generated integer assignment: %s %d %d = %d\n", 
               operation, num1, num2, answer);
#endif
    }
    
    int bytes_sent = sendto(sockfd, &assignment, sizeof(assignment), 0, client_addr, client_addr_len);
    
    if (bytes_sent == sizeof(assignment)) {
#ifdef DEBUG
        printf("Assignment sent successfully\n");
#endif
    } else {
        printf("Error sending assignment\n");
    }
}

void checkJobbList(int signum){

  printf("Let me be, I want to sleep, loopCount = %d.\n", loopCount);

  if(loopCount>20){
    printf("I had enough.\n");
    terminate=1;
  }
  
  return;
}





int main(int argc, char *argv[]){
  
  if (argc != 2) {
    printf("Usage: %s <host:port>\n", argv[0]);
    return 1;
  }
  
  char host[256];
  char port[10];
  split_host_port(argv[1], host, port);
  
#ifdef DEBUG
  printf("Host %s, and port %s.\n", host, port);
#endif

  initCalcLib();
  
  struct addrinfo hints;
  struct addrinfo *server_info;
  
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  int status = getaddrinfo(host, port, &hints, &server_info);
  if (status != 0) {
    printf("getaddrinfo error: %s\n", gai_strerror(status));
    return 1;
  }
  
  int sockfd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
  if (sockfd == -1) {
    printf("Socket creation failed\n");
    freeaddrinfo(server_info);
    return 1;
  }
  
  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    printf("setsockopt failed\n");
    close(sockfd);
    freeaddrinfo(server_info);
    return 1;
  }
  
  if (bind(sockfd, server_info->ai_addr, server_info->ai_addrlen) == -1) {
    printf("Bind failed: %s\n", strerror(errno));
    close(sockfd);
    freeaddrinfo(server_info);
    return 1;
  }
  
  freeaddrinfo(server_info);
  
  printf("Server listening on %s:%s\n", host, port);

  struct itimerval alarm_time;
  alarm_time.it_interval.tv_sec = 10;
  alarm_time.it_interval.tv_usec = 10;
  alarm_time.it_value.tv_sec = 10;
  alarm_time.it_value.tv_usec = 10;

  signal(SIGALRM, checkJobbList);
  setitimer(ITIMER_REAL, &alarm_time, NULL);

  while(terminate == 0) {
    
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[sizeof(calcProtocol)];
    
    int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                                 (struct sockaddr*)&client_addr, &client_addr_len);
    
    if (bytes_received > 0) {
      
      printf("Received message, %d bytes\n", bytes_received);
      
      if (bytes_received == sizeof(calcMessage)) {
        
        struct calcMessage* msg = (struct calcMessage*)buffer;
        
        // Fix byte order
        msg->type = ntohs(msg->type);
        msg->message = ntohl(msg->message);
        msg->protocol = ntohs(msg->protocol);
        msg->major_version = ntohs(msg->major_version);
        msg->minor_version = ntohs(msg->minor_version);
        
#ifdef DEBUG
        printf("Received calcMessage: type=%d, message=%d, protocol=%d, version=%d.%d\n",
               msg->type, msg->message, msg->protocol, msg->major_version, msg->minor_version);
#endif
        
        if (msg->type == 22 && msg->message == 0 && msg->protocol == 17 && 
            msg->major_version == 1 && msg->minor_version == 0) {
          
          sendAssignment(sockfd, (struct sockaddr*)&client_addr, client_addr_len);
          
        } else {
          struct calcMessage rejection;
          memset(&rejection, 0, sizeof(rejection));
          rejection.type = htons(2);
          rejection.message = htonl(2);
          rejection.protocol = htons(17);
          rejection.major_version = htons(1);
          rejection.minor_version = htons(0);
          
          sendto(sockfd, &rejection, sizeof(rejection), 0,
                (struct sockaddr*)&client_addr, client_addr_len);
          
#ifdef DEBUG
          printf("Rejected invalid protocol\n");
#endif
        }
      }
    }
  }

  close(sockfd);
  printf("Server shutdown.\n");
  return 0;
}
