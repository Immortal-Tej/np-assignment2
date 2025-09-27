#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <calcLib.h>
#include "protocol.h"

using namespace std;

bool splitHostPort(string input, string &host, string &port) {
    if (input.empty()) return false;

    if (input[0] == '[') {
        size_t pos = input.find(']');
        if (pos == string::npos || pos + 1 >= input.size() || input[pos+1] != ':') {
            return false;
        }
        host = input.substr(1, pos - 1);
        port = input.substr(pos + 2);
    } else {
        size_t pos = input.rfind(':');
        if (pos == string::npos) {
            return false;
        }
        host = input.substr(0, pos);
        port = input.substr(pos + 1);
    }
    return true;
}

#ifdef DEBUG
#define DEBUG_PRINT(x) do { cout << x << endl; } while(0)
#else
#define DEBUG_PRINT(x) do {} while(0)
#endif

int sendWithRetry(int sock, const void *msg, size_t msgSize,
                  void *reply, size_t replySize,
                  struct sockaddr *serverAddr, socklen_t addrLen) {
    int tries = 0;
    while (tries < 3) {
        sendto(sock, msg, msgSize, 0, serverAddr, addrLen);
        int bytes = recvfrom(sock, reply, replySize, 0, NULL, NULL);
        if (bytes > 0) {
            return bytes;
        }

        tries++;
        if (tries < 3) {
            cerr << "No reply, retrying..." << endl;
        }
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << "Usage: ./client <host:port>" << endl;
        return 1;
    }

    string host, port;
    if (!splitHostPort(argv[1], host, port)) {
        cout << "Invalid host:port format" << endl;
        return 1;
    }

    cout << "Host " << host << ", and port " << port << "." << endl;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        cout << "Could not resolve host" << endl;
        return 1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        cout << "Failed to create socket" << endl;
        freeaddrinfo(res);
        return 1;
    }

    struct sockaddr_storage local_addr;
    socklen_t local_len = sizeof(local_addr);
    char local_ip[INET6_ADDRSTRLEN];
    int local_port = 0;
    
    if (getsockname(sock, (struct sockaddr*)&local_addr, &local_len) == 0) {
        if (local_addr.ss_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)&local_addr;
            inet_ntop(AF_INET, &(addr_in->sin_addr), local_ip, INET_ADDRSTRLEN);
            local_port = ntohs(addr_in->sin_port);
        } else if (local_addr.ss_family == AF_INET6) {
            struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)&local_addr;
            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), local_ip, INET6_ADDRSTRLEN);
            local_port = ntohs(addr_in6->sin6_port);
        }
    } else {
        strcpy(local_ip, "0.0.0.0");
        local_port = 0;
    }

    DEBUG_PRINT("Connected to " << host << ":" << port << " local " << local_ip << ":" << local_port);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct calcMessage firstMsg;
    memset(&firstMsg, 0, sizeof(firstMsg));
    firstMsg.type = htons(22);
    firstMsg.message = htonl(0);
    firstMsg.protocol = htons(17);
    firstMsg.major_version = htons(1);
    firstMsg.minor_version = htons(0);

    struct calcProtocol assignment;

    int bytes = sendWithRetry(sock, &firstMsg, sizeof(firstMsg),
                              &assignment, sizeof(assignment),
                              res->ai_addr, res->ai_addrlen);

    if (bytes < 0) {
        cout << "ERROR: server did not reply" << endl;
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    if (bytes != sizeof(assignment)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    assignment.type = ntohs(assignment.type);
    assignment.major_version = ntohs(assignment.major_version);
    assignment.minor_version = ntohs(assignment.minor_version);
    assignment.id = ntohl(assignment.id);
    assignment.arith = ntohl(assignment.arith);
    assignment.inValue1 = ntohl(assignment.inValue1);
    assignment.inValue2 = ntohl(assignment.inValue2);
    assignment.inResult = ntohl(assignment.inResult);

    if (assignment.major_version != 1 || assignment.minor_version != 0) {
        cout << "Server replied with unsupported version" << endl;
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    cout << "Protocol negotiation successful. Assignment received." << endl;
    
    close(sock);
    freeaddrinfo(res);
    return 0;
}
