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

    int op = assignment.arith;
    bool isFloat = (op >= 5);
    int intRes = 0;
    double floatRes = 0.0;

    if (!isFloat) {
        int a = assignment.inValue1;
        int b = assignment.inValue2;
        string opName;
        if (op == 1) { intRes = a + b; opName = "add"; }
        else if (op == 2) { intRes = a - b; opName = "sub"; }
        else if (op == 3) { intRes = a * b; opName = "mul"; }
        else if (op == 4) { intRes = a / b; opName = "div"; }
        cout << "ASSIGNMENT: " << opName << " " << a << " " << b << endl;
        DEBUG_PRINT("Calculated the result to " << intRes);
    } else {
        double a = assignment.flValue1;
        double b = assignment.flValue2;
        string opName;
        if (op == 5) { floatRes = a + b; opName = "fadd"; }
        else if (op == 6) { floatRes = a - b; opName = "fsub"; }
        else if (op == 7) { floatRes = a * b; opName = "fmul"; }
        else if (op == 8) { floatRes = a / b; opName = "fdiv"; }
        cout << "ASSIGNMENT: " << opName << " " << a << " " << b << endl;
        DEBUG_PRINT("Calculated the result to " << floatRes);
    }

    struct calcProtocol reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = htons(2);
    reply.major_version = htons(1);
    reply.minor_version = htons(0);
    reply.id = htonl(assignment.id);
    reply.arith = htonl(assignment.arith);
    reply.inValue1 = htonl(assignment.inValue1);
    reply.inValue2 = htonl(assignment.inValue2);

    if (!isFloat) {
        reply.inResult = htonl(intRes);
    } else {
        reply.flValue1 = assignment.flValue1;
        reply.flValue2 = assignment.flValue2;
        reply.flResult = floatRes;
    }

    struct calcMessage finalMsg;

    int bytes2 = sendWithRetry(sock, &reply, sizeof(reply),
                               &finalMsg, sizeof(finalMsg),
                               res->ai_addr, res->ai_addrlen);

    if (bytes2 < 0) {
        cout << "ERROR: server did not reply after result" << endl;
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    if (bytes2 != sizeof(finalMsg)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sock);
        freeaddrinfo(res);
        return 1;
    }

    finalMsg.type = ntohs(finalMsg.type);
    finalMsg.message = ntohl(finalMsg.message);
    finalMsg.protocol = ntohs(finalMsg.protocol);
    finalMsg.major_version = ntohs(finalMsg.major_version);
    finalMsg.minor_version = ntohs(finalMsg.minor_version);

    if (finalMsg.message == 1) {
        cout << "OK (myresult=";
        if (!isFloat) cout << intRes;
        else cout << floatRes;
        cout << ")" << endl;
    } else {
        cout << "NOT OK (myresult=";
        if (!isFloat) cout << intRes;
        else cout << floatRes;
        cout << ")" << endl;
    }

    close(sock);
    freeaddrinfo(res);
    return 0;
}
