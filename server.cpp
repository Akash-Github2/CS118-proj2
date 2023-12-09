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
#include <vector>
#include <algorithm>

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

    string fileContent = "";
    map<unsigned short, packet> payloadMap;
    map<unsigned short, packet> finOutput;

    while (1) {
        // cout << "START:\n";
        //RECEIVING PACKET
        ssize_t bytes_received = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr_from, &addr_size);
        if (bytes_received <= 0) {
            perror("Received failed");
            continue;
        }

        if (buffer.last == 1) {
            break;
        }
        // cout << "Received message from client: Ack:" << buffer.acknum << " Seqnum:" << buffer.seqnum << "\n";
        // cout << "Received Packet Payload: " << buffer.payload << endl;

        // cout << "RECEIVED: " << buffer.length << ":::" << buffer.payload << endl;
        printRecv(&buffer);
        finOutput.insert({buffer.seqnum, buffer});
        payloadMap.insert({buffer.seqnum, buffer});
        
        //SENDING ACK
        if (payloadMap.size() > 0) {

            unsigned short minElement = payloadMap.begin()->first;

            build_packet(&ack_pkt, 0, minElement, 0, 1, sizeof(ack_pkt), "");

            // cout << "Min Element ACK:::" << minElement << "Buff.last: " << buffer.last << endl;
            ssize_t bytes_sent = sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, sizeof(client_addr_to));
            if (bytes_sent == -1) {
                perror("Send failed");
            } else {
                // cout << "Sent ack to client: Ack Num\n";
                payloadMap.erase(minElement);
            }

        }
    }

    // printPacketMap(finOutput);

    // cout << finOutput.begin()->second.payload << endl;

    for (auto it = finOutput.begin(); it != finOutput.end(); ++it) {
        for (int i = 0; i < it->second.length; ++i) {
            output_file << it->second.payload[i];
        }

        // output_file << it->second.payload;
    }

    output_file.close();
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}