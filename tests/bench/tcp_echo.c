#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* The C mirror of bench/tcp_echo.r: blocking BSD sockets over loopback in one thread. */
int main(void) {
    const size_t iterations = (size_t)20000u;
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int client;
    int accepted;
    unsigned char payload[4096];
    unsigned char window[4096];
    size_t total = 0u;
    int one = 1;

    if (listener < 0)
        return 66;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 4) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &length) != 0)
        return 66;
    client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0 || connect(client, (struct sockaddr *)&address, sizeof(address)) != 0)
        return 66;
    accepted = accept(listener, NULL, NULL);
    if (accepted < 0)
        return 66;
    close(listener);
    memset(payload, 7, sizeof(payload));
    for (size_t index = 0; index < iterations; ++index) {
        size_t sent = 0u;
        size_t received = 0u;
        while (sent < sizeof(payload)) {
            ssize_t count = write(client, payload + sent, sizeof(payload) - sent);
            if (count <= 0)
                return 66;
            sent += (size_t)count;
        }
        while (received < sizeof(window)) {
            ssize_t count = read(accepted, window + received, sizeof(window) - received);
            if (count <= 0)
                return 70;
            received += (size_t)count;
        }
        total += received;
    }
    close(client);
    close(accepted);
    return (int)(total % 109u);
}
