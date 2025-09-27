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

    cout << "Socket created successfully. Ready for UDP communication." << endl;
    
    close(sock);
    freeaddrinfo(res);
    return 0;
}
