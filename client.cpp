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
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
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
    timeout.tv_sec = 0;  // x second timeout
    timeout.tv_usec = 10000;

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

    // Open file for reading (C++)
    ifstream file(argv[1], ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening file" << endl;
        return 1;
    }

    /* ------ BEGIN IMPLEMENTATION ------ */
    const int payloadSize = PAYLOAD_SIZE;
    map<unsigned short, packet> currWindow; // seqNum -> payload
    int windowSize = 1;
    
    map<unsigned short, packet> seqNumToPacket; //remove once it gets successfully transmitted
    int seqNum = 0;
    
    /* ------ PREPROCESSING: LOAD ALL PACKETS INTO 'seqNumToPacket' ------ */
    while (!file.eof()) {

        file.read(buffer, payloadSize);
        streamsize bytesRead = file.gcount();

        if (bytesRead > 0) { // Build packet from string read in, add it into map w/ seqNum
            struct packet currPkt;
            string block(buffer, bytesRead);

            // packet, seqNum, ackNum, lastFlag, ackFlag, length, *payload
            build_packet(&currPkt, seqNum, ack_num, 0, 0, bytesRead, block.c_str());
            // Map the sequence number to the block of 10 bytes
            seqNumToPacket[seqNum] = currPkt;
            // Increment the sequence number
            seqNum++;
        }
    }

    unordered_map<int, int> dupAcks;
    int ssthresh = 8;
    int it = 1;
    map<chrono::steady_clock::time_point, unsigned short> rtoToSeqNum; //time packet was sent
    chrono::milliseconds rtoTimeout(500);
    /* ---------- SENDING PACKETS ---------- */

    while (true) {
        //cout << "[";
        // for (auto i : currWindow)
            //cout << i.first << ", ";
        //cout << "]" << endl;
        //cout << "A\n";
        // Send until cwnd full
        while (currWindow.size() < windowSize && seqNumToPacket.size() > 0) {
            it = 0;
            // this_thread::sleep_for(chrono::milliseconds(100));
            struct packet minOverallPkt = seqNumToPacket.begin()->second;
            transmitPacket(send_sockfd, &minOverallPkt, &server_addr_to); // Transmit packet to server
            rtoToSeqNum.insert({chrono::steady_clock::now(), minOverallPkt.seqnum}); //add rto timeout for packet
            currWindow.insert({minOverallPkt.seqnum, minOverallPkt}); // Add packet to cwnd
            seqNumToPacket.erase(minOverallPkt.seqnum); // Remove ack'd packet from overall seqNumToPacket
            //cout << "[Sent " << minOverallPkt.seqnum << "] CWND: " << currWindow.size() << ", WindowSize: " << windowSize << endl;
        }
        //cout << "B\n";
        if (currWindow.size() == 0)  // All packet sent & ack'd
            break;
        //cout << "C\n";

        //Retransmit packets that timed out

        // if (it % 5 == 0) {
            if (it % 5 == 0 || (rtoToSeqNum.size() > 0 && chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - rtoToSeqNum.begin()->first) > rtoTimeout)) {
                it = 0;
                while (currWindow.find(rtoToSeqNum.begin()->second) == currWindow.end()) {
                    rtoToSeqNum.erase(rtoToSeqNum.begin());
                    // continue;
                } 
                if (rtoToSeqNum.size() > 0) {
                    // Send pkt when rto timeout
                    unsigned short retransmitSeqNum = rtoToSeqNum.begin()->second;
                    struct packet retransmitPkt = currWindow.at(retransmitSeqNum);
                    transmitPacket(send_sockfd, &retransmitPkt, &server_addr_to);
                    //cout << "[Sending RTO: " << retransmitPkt.seqnum << "] CWND: " << currWindow.size() << ", WindowSize: " << windowSize << endl;

                    //Remove current RTO Timeout Key : Val and add new one
                    rtoToSeqNum.erase(rtoToSeqNum.begin());
                    rtoToSeqNum.insert({chrono::steady_clock::now(), retransmitSeqNum});
                }
            }
        // }
        
        //cout << "D\n";
        if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size) && ack_pkt.acknum >= currWindow.begin()->first) {
            it = 0;
            //cout << "A\n";
            for (unsigned short i = currWindow.begin()->first; i < ack_pkt.acknum; i++) {
                currWindow.erase(i); // Remove start -> ack'd packet
                dupAcks.erase(i);
            }
            //cout << "[Received Ack " << ack_pkt.acknum << "] CWND: " << currWindow.size() << ", WindowSize: " << windowSize << endl;
            // New Ack
            if (dupAcks.find(ack_pkt.acknum) == dupAcks.end()) {
                dupAcks.insert({ack_pkt.acknum, 0});
                if (windowSize < ssthresh) windowSize *= 2; // Slow start
                else windowSize = min(windowSize + 1, 15); // Additive Increase
            }
            else dupAcks[ack_pkt.acknum]++;

            //cout << "E\n";
            // FAST RETRANSMIT
            if (dupAcks[ack_pkt.acknum] >= 3) {
                //cout << "[Fast Retransmitted " << ack_pkt.acknum << "] CWND: " << currWindow.size() << ", WindowSize: " << windowSize << endl;
                ssthresh = max(windowSize/2, 2);
                windowSize = max(windowSize/2, 1);
                //cout << "F\n";
                struct packet rt = currWindow[ack_pkt.acknum];
                transmitPacket(send_sockfd, &rt, &server_addr_to);
                rtoToSeqNum.insert({chrono::steady_clock::now(), rt.seqnum}); //add rto timeout for packet
                //cout << "G\n";
                dupAcks[ack_pkt.acknum] = 0;
            }
        }
        it++;
    }

    // Receive or retransmit anything from window
    // while (currWindow.size() > 0) {
    //     //cout << "Leftover Retransmitting: " << currWindow.begin()->first << "->" << currWindow.rbegin()->first << endl;
    //     if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size)) {
    //         currWindow.erase(ack_pkt.acknum); // Remove ack'd packet from cwnd
    //     }
    //     struct packet minWindowPkt = currWindow.begin()->second;
    //     transmitPacket(send_sockfd, &minWindowPkt, &server_addr_to);
    // }

    // Transmit end packet after everything else is already transmitted and received
    //cout << "Reached end, transmitting end packet" << endl;
    struct packet end_pkt;
    build_packet(&end_pkt, 0, 0, 1, 0, 0, "");

    while (true) {
        transmitPacket(send_sockfd, &end_pkt, &server_addr_to);
        if (receiveAck(listen_sockfd, &ack_pkt, &client_addr, addr_size) && ack_pkt.last)
            break;
    }

    file.close();
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void transmitPacket(int send_sockfd, packet *pkt, struct sockaddr_in *server_addr_to) {

    // //cout << "TRANSMITTING...\n";
    ssize_t bytes_sent = sendto(send_sockfd, pkt, sizeof(*pkt), 0, (struct sockaddr*)server_addr_to, sizeof(*server_addr_to));
    if (bytes_sent == -1) {
        perror("Send failed");
    } else {
        // //cout << "Sent message to server: " << pkt->payload << "\n";
    }

    // //cout << "Packet Sent: " << pkt->payload << endl;
    // printRecv(pkt);
}

bool receiveAck(int listen_sockfd, packet *ack_pkt, struct sockaddr_in *client_addr, socklen_t addr_size) { //return true for good, false for didn't work
    ssize_t bytes_received = recvfrom(listen_sockfd, ack_pkt, sizeof(*ack_pkt), 0, (struct sockaddr*)client_addr, &addr_size);
    return bytes_received > 0;
}

void printPacketMap(map<unsigned short, packet> currWindow) {
    // //cout << "\n{";
    // for (auto it = currWindow.begin(); it != currWindow.end(); ++it) {
    //     //cout << it->first << ": " << it->second.payload << " ::: ";
    //     printRecv(&it->second);
    // }
    // //cout << "}\n";
}