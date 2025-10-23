/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "ns3/object.h"
#include "ns3/address.h"
#include "ns3/event-id.h"

#include <vector>
#include <string>

namespace ns3 {

class Socket;
class Packet;

class KamedaAppClient;

class ConnectManager : public Object{
public:
    ConnectManager(Address serverAddress, Ptr<KamedaAppClient> client);
private:
    ConnectManager();

public:
    void StartRequest();
    void StopRequest();
    void GetRtt(uint16_t seq, Time rtt);

private:
    bool SendMessage();
    void EnsureSocket();

    Ptr<KamedaAppClient> m_client;
    Address m_serverAddress;
    Ptr<Socket> m_udpSocket;
    std::string m_rttData;

    std::vector<std::string> m_rttDatas;
};

}

#endif /* CONNECTION_MANAGER_H */
