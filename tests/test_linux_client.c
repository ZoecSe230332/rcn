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
    const char* server_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 9999;

    printf("[Linux Client] Connecting to %s:%d...\n", server_ip, port);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    int no_delay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    printf("[Linux Client] Connected successfully!\n");

    // 1. Send PEER_HEADER_DEV_CRT
    struct stream_header hdr = { 0 };
    hdr.value = PEER_HEADER_DEV_CRT;
    hdr.size = sizeof(struct device_info);

    struct device_info dev = { 0 };
    strncpy(dev.name, "Linux Test Keyboard", sizeof(dev.name) - 1);
    SET_BIT(dev.evtbit, EV_KEY);
    SET_BIT(dev.evtbit, EV_SYN);
    SET_BIT(dev.keybit, KEY_A);
    SET_BIT(dev.keybit, KEY_B);
    SET_BIT(dev.keybit, KEY_ENTER);
    dev.dev_id.bustype = 0x0003; // BUS_USB
    dev.dev_id.vendor  = 0x1234;
    dev.dev_id.product = 0x5678;
    dev.random_id = 0xDEADBEEFCAFE1234ULL;

    printf("[Linux Client] Sending PEER_HEADER_DEV_CRT (size=%zu, sizeof(device_info)=%zu)...\n",
           hdr.size, sizeof(struct device_info));
    if (send(sock, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
        perror("send header");
        close(sock);
        return 1;
    }
    if (send(sock, &dev, sizeof(dev), 0) != sizeof(dev)) {
        perror("send device_info");
        close(sock);
        return 1;
    }
    printf("[Linux Client] Device creation message sent!\n");
    usleep(100000); // 100ms

    // 2. Send PEER_HEADER_EVENT (KEY_A down)
    hdr.value = PEER_HEADER_EVENT;
    hdr.size = sizeof(struct peer_msg_event);

    struct peer_msg_event evt_msg = { 0 };
    evt_msg.random_id = dev.random_id;
    evt_msg.evt_data.type = EV_KEY;
    evt_msg.evt_data.code = KEY_A;
    evt_msg.evt_data.value = 1; // Down

    printf("[Linux Client] Sending KEY_A down (sizeof(peer_msg_event)=%zu)...\n", sizeof(struct peer_msg_event));
    if (send(sock, &hdr, sizeof(hdr), 0) != sizeof(hdr) ||
        send(sock, &evt_msg, sizeof(evt_msg), 0) != sizeof(evt_msg)) {
        perror("send event");
        close(sock);
        return 1;
    }

    // SYN_REPORT
    evt_msg.evt_data.type = EV_SYN;
    evt_msg.evt_data.code = SYN_REPORT;
    evt_msg.evt_data.value = 0;
    send(sock, &hdr, sizeof(hdr), 0);
    send(sock, &evt_msg, sizeof(evt_msg), 0);
    usleep(50000); // 50ms

    // 3. Send PEER_HEADER_EVENT (KEY_A up)
    evt_msg.evt_data.type = EV_KEY;
    evt_msg.evt_data.code = KEY_A;
    evt_msg.evt_data.value = 0; // Up
    printf("[Linux Client] Sending KEY_A up...\n");
    send(sock, &hdr, sizeof(hdr), 0);
    send(sock, &evt_msg, sizeof(evt_msg), 0);

    // SYN_REPORT
    evt_msg.evt_data.type = EV_SYN;
    evt_msg.evt_data.code = SYN_REPORT;
    evt_msg.evt_data.value = 0;
    send(sock, &hdr, sizeof(hdr), 0);
    send(sock, &evt_msg, sizeof(evt_msg), 0);
    usleep(100000);

    // 4. Send PEER_HEADER_PAUSE
    hdr.value = PEER_HEADER_PAUSE;
    hdr.size = 0;
    printf("[Linux Client] Sending PEER_HEADER_PAUSE...\n");
    send(sock, &hdr, sizeof(hdr), 0);
    usleep(50000);

    // 5. Send PEER_HEADER_RESUME
    hdr.value = PEER_HEADER_RESUME;
    hdr.size = 0;
    printf("[Linux Client] Sending PEER_HEADER_RESUME...\n");
    send(sock, &hdr, sizeof(hdr), 0);
    usleep(50000);

    // 6. Send PEER_HEADER_STOP
    hdr.value = PEER_HEADER_STOP;
    hdr.size = 0;
    printf("[Linux Client] Sending PEER_HEADER_STOP...\n");
    send(sock, &hdr, sizeof(hdr), 0);
    usleep(50000);

    close(sock);
    printf("[Linux Client] All messages sent successfully! Test PASSED.\n");
    return 0;
}
