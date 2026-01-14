#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"
#include "calcLib.h"

using namespace std;
using Clock = chrono::steady_clock;

class Job {
public:
    sockaddr_storage addr;
    socklen_t addrlen;
    uint32_t id;
    bool is_float;
    int32_t i_expected;
    double f_expected;
    Clock::time_point ts;

    // Default constructor
    Job() : addrlen(0), id(0), is_float(false), i_expected(0), f_expected(0.0) {}

    // Parameterized constructor
    Job(sockaddr_storage a, socklen_t l, uint32_t tid, bool f, int32_t ei, double ed)
        : addr(a), addrlen(l), id(tid), is_float(f), i_expected(ei), f_expected(ed) {
        ts = Clock::now();
    }
};

static unordered_map<uint32_t, Job> jobs;
static int srv_sock = -1;
static bool stop_server = false;

static auto last_activity_time = Clock::now();
static const int IDLE_TIMEOUT_SECONDS = 60; // Timeout period in seconds

static void handle_sig(int) {
    stop_server = true;
}

static string addr_to_string(const sockaddr_storage &ss) {
    char host[INET6_ADDRSTRLEN] = {0};
    char port[8] = {0};
    if (ss.ss_family == AF_INET) {
        const sockaddr_in *s = (const sockaddr_in*)&ss;
        inet_ntop(AF_INET, &s->sin_addr, host, sizeof(host));
        snprintf(port, sizeof(port), "%u", ntohs(s->sin_port));
    } else {
        const sockaddr_in6 *s6 = (const sockaddr_in6*)&ss;
        inet_ntop(AF_INET6, &s6->sin6_addr, host, sizeof(host));
        snprintf(port, sizeof(port), "%u", ntohs(s6->sin6_port));
    }
    return string(host) + ":" + string(port);
}

static bool same_sockaddr(const sockaddr_storage &a, const sockaddr_storage &b) {
    if (a.ss_family != b.ss_family) return false;
    if (a.ss_family == AF_INET) {
        const sockaddr_in *pa = (const sockaddr_in*)&a;
        const sockaddr_in *pb = (const sockaddr_in*)&b;
        return pa->sin_port == pb->sin_port && pa->sin_addr.s_addr == pb->sin_addr.s_addr;
    } else {
        const sockaddr_in6 *pa = (const sockaddr_in6*)&a;
        const sockaddr_in6 *pb = (const sockaddr_in6*)&b;
        return pa->sin6_port == pb->sin6_port &&
               memcmp(&pa->sin6_addr, &pb->sin6_addr, sizeof(in6_addr)) == 0;
    }
}

static uint32_t new_id() {
    uint32_t id;
    do {
        id = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
    } while (id == 0 || jobs.find(id) != jobs.end());
    return id;
}

static int arith_code_from_name(const string &op) {
    if (op == "add") return 1;
    if (op == "sub") return 2;
    if (op == "mul") return 3;
    if (op == "div") return 4;
    if (op == "fadd") return 5;
    if (op == "fsub") return 6;
    if (op == "fmul") return 7;
    if (op == "fdiv") return 8;
    return 0;
}

void check_idle_timeout() {
    auto now = Clock::now();
    auto diff = chrono::duration_cast<chrono::seconds>(now - last_activity_time).count();
    if (diff >= IDLE_TIMEOUT_SECONDS) {
        cout << "Server has been idle for too long. Shutting down." << endl;
        stop_server = true;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <IP:PORT>" << endl;
        return 1;
    }

    srand((unsigned)time(NULL));
    initCalcLib();

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    // parse host:port or [ipv6]:port
    string arg = argv[1];
    string host, port;
    if (!arg.empty() && arg.front() == '[') {
        auto pos = arg.find("]:");
        if (pos == string::npos) { cerr << "Invalid address\n"; return 1; }
        host = arg.substr(1, pos-1);
        port = arg.substr(pos+2);
    } else {
        auto pos = arg.rfind(':');
        if (pos == string::npos) { cerr << "Invalid address\n"; return 1; }
        host = arg.substr(0, pos);
        port = arg.substr(pos+1);
    }

    // Resolve address (IPv4 & IPv6)
    struct addrinfo hints{}, *res = nullptr, *rp;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;

    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0) {
        cerr << "getaddrinfo: " << gai_strerror(rc) << endl;
        return 1;
    }

    // create and bind socket (try addresses until success)
    srv_sock = -1;
    for (rp = res; rp != nullptr; rp = rp->ai_next) {
        srv_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (srv_sock < 0) continue;

        int yes = 1;
        setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
        setsockopt(srv_sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
        if (bind(srv_sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // bound
        }
        close(srv_sock);
        srv_sock = -1;
    }
    if (srv_sock < 0) {
        perror("bind/socket");
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    cout << "Server started on " << host << ":" << port << endl;
    fflush(stdout);

    const size_t MSG_SZ = sizeof(struct calcMessage);
    const size_t PROTO_SZ = sizeof(struct calcProtocol);
    unsigned char buf[2048];

    while (!stop_server) {
        // Check for idle timeout
        check_idle_timeout();
        if (stop_server) break;

        // cleanup timed out jobs (>=10s)
        auto now = Clock::now();
        vector<uint32_t> expired;
        for (auto &kv : jobs) {
            auto diff = chrono::duration_cast<chrono::seconds>(now - kv.second.ts).count();
            if (diff >= 10) expired.push_back(kv.first);
        }
        for (uint32_t id : expired) {
            cerr << "Job " << id << " timed out and removed." << endl;
            jobs.erase(id);
        }

        // select waiting
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv_sock, &rfds);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000; // 200ms
        int sret = select(srv_sock+1, &rfds, nullptr, nullptr, &tv);
        if (sret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (sret == 0) continue; // loop to cleanup

        // ready to read
        sockaddr_storage cliaddr;
        socklen_t cliaddr_len = sizeof(cliaddr);
        ssize_t n = recvfrom(srv_sock, buf, sizeof(buf), 0, (sockaddr*)&cliaddr, &cliaddr_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            continue;
        }

        string client_str = addr_to_string(cliaddr);
        cout << "Received " << n << " bytes from " << client_str << endl;
        fflush(stdout);

        if ((size_t)n == MSG_SZ) {
            struct calcMessage cm;
            memcpy(&cm, buf, MSG_SZ);

            uint16_t cm_type = ntohs(cm.type);
            uint32_t cm_message = ntohl(cm.message);
            uint16_t cm_protocol = ntohs(cm.protocol);
            uint16_t cm_maj = ntohs(cm.major_version);
            uint16_t cm_min = ntohs(cm.minor_version);

            if (!(cm_type == 22 && cm_message == 0 && cm_protocol == 17 && cm_maj == 1 && cm_min == 0)) {
                cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL from " << client_str << endl;
                fflush(stdout);
                continue;
            }

            struct calcProtocol cp;
            memset(&cp, 0, sizeof(cp));
            cp.type = htons(1); // server -> client
            cp.major_version = htons(1);
            cp.minor_version = htons(0);

            uint32_t id = new_id();
            cp.id = htonl(id);

            string op = randomType(); // "add","fadd",...
            int arith = arith_code_from_name(op);
            cp.arith = htonl(arith);

            Job job(cliaddr, cliaddr_len, id, arith >= 5, 0, 0.0);

            if (arith >= 1 && arith <= 4) {
                int iv1 = randomInt(), iv2 = randomInt(), ires = 0;
                if (arith == 1) ires = iv1 + iv2;
                else if (arith == 2) ires = iv1 - iv2;
                else if (arith == 3) ires = iv1 * iv2;
                else if (arith == 4) ires = (iv2 == 0 ? 0 : (iv1 / iv2));
                job.i_expected = ires;
                cp.inValue1 = htonl(iv1);
                cp.inValue2 = htonl(iv2);
                cp.inResult = htonl(0); // don't reveal expected result
            } else {
                double f1 = randomFloat(), f2 = randomFloat(), fres = 0.0;
                if (arith == 5) fres = f1 + f2;
                else if (arith == 6) fres = f1 - f2;
                else if (arith == 7) fres = f1 * f2;
                else if (arith == 8) fres = (f2 == 0.0 ? 0.0 : (f1 / f2));
                job.f_expected = fres;
                cp.flValue1 = f1;
                cp.flValue2 = f2;
                cp.flResult = 0.0; // don't reveal expected result
            }

            jobs[id] = job;

            ssize_t sent = sendto(srv_sock, &cp, sizeof(cp), 0, (sockaddr*)&cliaddr, cliaddr_len);
            if (sent != (ssize_t)sizeof(cp)) {
                perror("sendto assignment");
            }
            continue;
        }

        if ((size_t)n == PROTO_SZ) {
            struct calcProtocol cp;
            memcpy(&cp, buf, PROTO_SZ);

            uint16_t type = ntohs(cp.type);
            uint16_t maj = ntohs(cp.major_version);
            uint16_t min = ntohs(cp.minor_version);
            uint32_t id = ntohl(cp.id);

            if (!(type == 2 && maj == 1 && min == 0)) {
                cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL from " << client_str << endl;
                continue;
            }

            auto it = jobs.find(id);
            if (it == jobs.end()) {
                calcMessage resp{};
                resp.type = htons(1);
                resp.message = htonl(2); // NOT OK
                resp.protocol = htons(17);
                resp.major_version = htons(1);
                resp.minor_version = htons(0);
                sendto(srv_sock, &resp, sizeof(resp), 0, (sockaddr*)&cliaddr, cliaddr_len);
                continue;
            }

            if (!same_sockaddr(it->second.addr, cliaddr)) {
                calcMessage resp{};
                resp.type = htons(1);
                resp.message = htonl(2); // NOT OK
                resp.protocol = htons(17);
                resp.major_version = htons(1);
                resp.minor_version = htons(0);
                sendto(srv_sock, &resp, sizeof(resp), 0, (sockaddr*)&cliaddr, cliaddr_len);
                continue;
            }

            bool ok = false;
            if (!it->second.is_float) {
                int32_t client_res = (int32_t)ntohl((uint32_t)cp.inResult);
                if (client_res == it->second.i_expected) ok = true;
            } else {
                double client_res = cp.flResult;
                double diff = fabs(client_res - it->second.f_expected);
                if (diff < 0.0001) ok = true;
            }

            jobs.erase(it);

            calcMessage finalm{};
            finalm.type = htons(1);
            finalm.message = htonl(ok ? 1 : 2);
            finalm.protocol = htons(17);
            finalm.major_version = htons(1);
            finalm.minor_version = htons(0);

            ssize_t s = sendto(srv_sock, &finalm, sizeof(finalm), 0, (sockaddr*)&cliaddr, cliaddr_len);
            if (s != (ssize_t)sizeof(finalm)) {
                perror("sendto final");
            }
            continue;
        }

        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL from " << client_str << endl;
    }

    if (srv_sock >= 0) close(srv_sock);
    return 0;
}
