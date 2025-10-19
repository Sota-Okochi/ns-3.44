/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef KAMEDA_APP_CLIENT_H
#define KAMEDA_APP_CLIENT_H

#include "ns3/core-module.h"
#include "ns3/application.h"
#include "ns3/socket.h"

#include "ns3/CountRtt.h"
#include "ns3/ConnectManager.h"

namespace ns3 {

class KamedaAppClient : public Application{

public:
    KamedaAppClient(Ipv4Address server_ping, Ipv4Address server_rtt);
    virtual ~KamedaAppClient();
private:
    KamedaAppClient();

public:
    Ptr<Socket> CreateTcpSocket();
    Ipv4Address ClientIpv4Address();

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void CLIENT_LOG_INFO(std::string info);

    Ipv4Address m_serverPing;
    Ipv4Address m_serverRtt;
    Ptr<CountRtt> m_countRtt;
    Ptr<ConnectManager> m_connectManager;

};

}

#endif /* KAMEDA_APP_CLIENT_H */
