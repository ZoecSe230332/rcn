#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "include/rcn_types.h"
#include "include/rcn_peer.h"
#include "include/rcn_input_types.h"

int main(int argc, char** argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 9998;

    printf("[Linux Server] Listening on 0.0.0.0:%d...\n", port);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("[Linux Server] Awaiting connection from Windows client...\n");
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("[Linux Server] Connection accepted from %s:%d!\n", client_ip, ntohs(client_addr.sin_port));

    int devices_received = 0;
    while (1) {
        struct stream_header hdr = { 0 };
        ssize_t n = recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL);
        if (n <= 0) {
            printf("[Linux Server] Client disconnected (recv returned %zd)\n", n);
            break;
        }

        printf("[Linux Server] Received header: value=%d, size=%zu\n", hdr.value, hdr.size);

        if (hdr.value == PEER_HEADER_DEV_CRT) {
            if (hdr.size != sizeof(struct device_info)) {
                fprintf(stderr, "[Linux Server] ERROR: size mismatch! Expected %zu, got %zu\n",
                        sizeof(struct device_info), hdr.size);
                close(client_fd);
                close(listen_fd);
                return 1;
            }
            struct device_info dev = { 0 };
            n = recv(client_fd, &dev, sizeof(dev), MSG_WAITALL);
            if (n != sizeof(dev)) {
                fprintf(stderr, "[Linux Server] ERROR: incomplete device_info payload!\n");
                close(client_fd);
                close(listen_fd);
                return 1;
            }
            printf("[Linux Server] Verified device registration:\n");
            printf("  - Device Name: '%s'\n", dev.name);
            printf("  - Random ID: 0x%016zx\n", dev.random_id);
            printf("  - EV_KEY bit: %d\n", HAS_BIT(dev.evtbit, EV_KEY));
            printf("  - EV_REL bit: %d\n", HAS_BIT(dev.evtbit, EV_REL));
            devices_received++;

            if (devices_received >= 2) {
                // Once 2 devices received (e.g. keyboard & mouse), send PAUSE then RESUME then exit test
                printf("[Linux Server] Received all expected devices! Sending PAUSE and RESUME controls...\n");
                struct stream_header ctrl = { 0 };
                ctrl.value = PEER_HEADER_PAUSE;
                ctrl.size = 0;
                send(client_fd, &ctrl, sizeof(ctrl), 0);
                usleep(50000);

                ctrl.value = PEER_HEADER_RESUME;
                send(client_fd, &ctrl, sizeof(ctrl), 0);
                usleep(50000);

                ctrl.value = PEER_HEADER_STOP;
                send(client_fd, &ctrl, sizeof(ctrl), 0);
                usleep(50000);

                printf("[Linux Server] Verification SUCCESSFUL! All checks passed.\n");
                break;
            }
        } else if (hdr.value == PEER_HEADER_EVENT) {
            struct peer_msg_event evt_msg = { 0 };
            recv(client_fd, &evt_msg, sizeof(evt_msg), MSG_WAITALL);
            printf("[Linux Server] Event received: dev=0x%016zx type=%u code=%u val=%d\n",
                   evt_msg.random_id, evt_msg.evt_data.type, evt_msg.evt_data.code, evt_msg.evt_data.value);
        } else if (hdr.value == PEER_HEADER_STOP) {
            printf("[Linux Server] Received PEER_HEADER_STOP\n");
            break;
        } else {
            // Drain payload if any
            if (hdr.size > 0) {
                char* buf = malloc(hdr.size);
                recv(client_fd, buf, hdr.size, MSG_WAITALL);
                free(buf);
            }
        }
    }

    close(client_fd);
    close(listen_fd);
    return 0;
}
