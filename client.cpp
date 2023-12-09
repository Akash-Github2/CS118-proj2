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
#include <map>
#include "utils.h"

using namespace std;

void transmitPacket(int send_sockfd, packet *pkt, struct sockaddr_in *server_addr_to);
bool receiveAck(int listen_sockfd, packet *ack_pkt, struct sockaddr_in *client_addr, socklen_t addr_size);
void printPacketMap(map<unsigned short, packet> currWindow);

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Set a timeout for receiving ACKs
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;

    setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));


    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (::bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) == -1) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    //Open file for reading (C++)
    ifstream file("input.txt", ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file" << endl;
        return 1;
    }



    // TODO: Read from file, and initiate reliable data transfer to the server

    const int payloadSize = 200;
    map<unsigned short, packet> currWindow; // seqNum : payload
    int windowSize = 8;
    
    map<unsigned short, packet> seqNumToPacket; //remove once it gets successfully transmitted
    int seqNum = 0;
    
    //------PREPROCESSING TO LOAD ALL PACKETS INTO 'seqNumToPacket'------//
    while (!file.eof()) {

        file.read(buffer, payloadSize);
        streamsize bytesRead = file.gcount();

        if (bytesRead > 0) {
            struct packet currPkt;
            // Assuming you want to store the content as a string, you can modify this based on your requirements.
            
            string block(buffer, bytesRead);

            // Map the sequence number to the block of 10 bytes
            build_packet(&currPkt, seqNum, ack_num, 0, 0, bytesRead, block.c_str());
            seqNumToPacket[seqNum] = currPkt;
            // Increment the sequence number
            seqNum++;
        }
    }

    //--------------------//

    // printPacketMap(seqNumToPacket);

    while(true) {
        // printPacketMap(currWindow);

        /* ---------- SENDING PACKET ---------- */
        while (currWindow.size() < windowSize) {
            // cout << "CURR: Seq num: " << seq_num << " Min El: " << minElement << endl;

            if (seqNumToPacket.size() > 0) {
                struct packet minOverallPkt = seqNumToPacket.begin()->second; //need to check if it's empty before
                // printPacketMap(seqNumToPacket);
                // cout << "TRANSMITTING::: Min overall packet: " << minOverallPkt.seqnum << " :: " << minOverallPkt.length << " ::: " << minOverallPkt.payload << "::::\n";
                transmitPacket(send_sockfd, &minOverallPkt, &server_addr_to); // Transmit packet to server
                currWindow.insert({minOverallPkt.seqnum, minOverallPkt}); // Add packet to cwnd
                seqNumToPacket.erase(minOverallPkt.seqnum ); //Remove ack'd packet from overall seqNumToPacket
            } else { // EOF case
                break;
            }
        }

        if (seqNumToPacket.size() == 0) {
            break;
        }

        if (currWindow.size() == windowSize) {
            
            if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size)) {
                // cout << "Received ack from server: Num " << ack_pkt.acknum << "\n";
                currWindow.erase(ack_pkt.acknum); // Remove ack'd packet from cwnd
                
            } else { 
                /* ---------- RETRANSMIT PACKET ---------- */
                
                // cout << "RETRANSMIT:::\n";
                struct packet minWindowPkt = currWindow.begin()->second;
                // printRecv(&minWindowPkt);
                transmitPacket(send_sockfd, &minWindowPkt, &server_addr_to);
            }
        }
    }

    //Receive or retransmit anything from window
    while (currWindow.size() > 0) {

        if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size)) {
            
            currWindow.erase(ack_pkt.acknum); // Remove ack'd packet from cwnd
            // seqNumToPacket.erase(ack_pkt.acknum); //Remove ack'd packet from overall seqNumToPacket
        } else { 
            /* ---------- RETRANSMIT PACKET ---------- */
            struct packet minWindowPkt = currWindow.begin()->second;
            printRecv(&minWindowPkt);
            transmitPacket(send_sockfd, &minWindowPkt, &server_addr_to);
        }
    }

    //Transmit end packet after everything else is already transmitted and received
    struct packet end_pkt;
    build_packet(&end_pkt, 0, 0, 1, 0, 0, "");

    while (true) {
        transmitPacket(send_sockfd, &end_pkt, &server_addr_to);

        if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size)) {
            break;
        }
    }

    file.close();
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void transmitPacket(int send_sockfd, packet *pkt, struct sockaddr_in *server_addr_to) {

    // cout << "TRANSMITTING...\n";
    ssize_t bytes_sent = sendto(send_sockfd, pkt, sizeof(*pkt), 0, (struct sockaddr*)server_addr_to, sizeof(*server_addr_to));
    if (bytes_sent == -1) {
        perror("Send failed");
    } else {
        // cout << "Sent message to server: " << pkt->payload << "\n";
    }

    // cout << "Packet Sent: " << pkt->payload << endl;
    // printRecv(pkt);
}

bool receiveAck(int listen_sockfd, packet *ack_pkt, struct sockaddr_in *client_addr, socklen_t addr_size) { //return true for good, false for didn't work
    ssize_t bytes_received = recvfrom(listen_sockfd, ack_pkt, sizeof(*ack_pkt), 0, (struct sockaddr*)client_addr, &addr_size);
    return bytes_received > 0;
}

void printPacketMap(map<unsigned short, packet> currWindow) {
    // cout << "\n{";
    // for (auto it = currWindow.begin(); it != currWindow.end(); ++it) {
    //     cout << it->first << ": " << it->second.payload << " ::: ";
    //     printRecv(&it->second);
    // }
    // cout << "}\n";
}