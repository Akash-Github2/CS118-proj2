#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <bits/stdc++.h>
#include "utils.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>

using namespace std;

void printPacketMap(map<unsigned short, packet> currWindow) {
    // cout << "\n{";
    // for (auto it = currWindow.begin(); it != currWindow.end(); ++it) {
    //     cout << it->first << ": " << it->second.payload << " ::: ";
    //     printRecv(&it->second);
    // }
    // cout << "}\n";
}

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (::bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // TODO: Receive file from the client and save it as output.txt

    // cout << "server: testing..." << endl;

    ofstream output_file("output.txt");
    if (!output_file.is_open()) {
        cerr << "Error opening output file\n";
        close(listen_sockfd);
        return 1;
    }

    /* ----- START IMPLEMENTATION ----- */
    string fileContent = "";
    map<unsigned short, packet> serverBuffer; // seqNum -> packet
    int lastContiguous = 0;
    int largestReceived = 0;
    unordered_set<int> received;

    while (true) {
        // RECEIVE PACKET
        ssize_t bytes_received = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr_from, &addr_size);
        if (bytes_received <= 0) {
            perror("Received failed");
            continue;
        }
        cout << "Received seqNum: " << buffer.seqnum << endl;

        // Last packet
        if (buffer.last == 1) break;

        largestReceived = max(largestReceived, (int)buffer.seqnum);

        // New packet
        if (received.find(buffer.seqnum) == received.end()) {
            received.insert(buffer.seqnum);
            serverBuffer.insert({buffer.seqnum, buffer});

            // Update last contiguously received packet
            for (int i = lastContiguous; i <= largestReceived; i++) {
                if (serverBuffer.find(i) == serverBuffer.end())
                    break;
                lastContiguous = i;
            }
        }
        
        // SEND ACK (sends ack for last contiguously received packet)
        build_packet(&ack_pkt, 0, lastContiguous + 1, 0, 1, sizeof(ack_pkt), "");
        cout << "ACK'd " << lastContiguous + 1 << endl;
        ssize_t bytes_sent = sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, sizeof(client_addr_to));
        if (bytes_sent == -1) {
            perror("Send failed");
        }
    }

    struct packet finack;
    build_packet(&finack, 0, 0, 1, 1, 0, "");
    for (int i = 0; i < 10; i++) {
        ssize_t finack_sent = sendto(send_sockfd, &finack, sizeof(finack), 0, (struct sockaddr*)&client_addr_to, sizeof(client_addr_to));
        if (finack_sent == -1) {
            perror("Send failed");
        }
    }

    for (auto it : serverBuffer) {
        for (int i = 0; i < it.second.length; ++i)
            output_file << it.second.payload[i];
    }

    output_file.close();
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}