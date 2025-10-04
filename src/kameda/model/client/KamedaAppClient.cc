/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "KamedaAppClient.h"
#include "CountRtt.h"

#include "ns3/log.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"

#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("KamedaAppClient");

KamedaAppClient::KamedaAppClient(Ipv4Address server_ping, Ipv4Address server_rtt){
    NS_LOG_FUNCTION(this);

    m_serverPing = server_ping;
    m_serverRtt = server_rtt;
    m_countRtt = Create<CountRtt>();
    Address address = InetSocketAddress (Ipv4Address(m_serverRtt), 8080);
    m_connectManager = Create<ConnectManager>(address, this);
}

KamedaAppClient::~KamedaAppClient(){
    NS_LOG_FUNCTION(this);
}

Ptr<Socket> KamedaAppClient::CreateTcpSocket(){
    return Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
}

Ipv4Address KamedaAppClient::ClientIpv4Address(){
    const uint32_t interfaceNumber = 1;
    const uint32_t addressIndex = 0;
    return GetNode()->GetObject<Ipv4>()->GetAddress(interfaceNumber,addressIndex).GetLocal();
}

void KamedaAppClient::StartApplication(){
    NS_LOG_FUNCTION(this);

    CLIENT_LOG_INFO("Start Application");

    std::cout << "KamedaAppClient::StartApplication" << std::endl;


    /*Ipv4Address addr = GetNode()->GetObject<Ipv4>()->GetAddress(1,0).GetLocal();
    std::cout << "TEST address:";
    addr.Print(std::cout);
    std::cout << std::endl;*/

    //CLIENT_LOG_INFO("Send Ping");
    m_countRtt->Init(GetNode(), m_connectManager);
    m_countRtt->Start(m_serverPing);
    //m_connectManager->StartRequest();

}

void KamedaAppClient::StopApplication(){
    NS_LOG_FUNCTION(this);

    CLIENT_LOG_INFO("Stop Application");

    m_connectManager->StopRequest();
}

void KamedaAppClient::CLIENT_LOG_INFO(std::string info){
    std::stringstream ss;
    ss << "NODE_ID:" << GetNode()->GetId();
    NS_LOG_INFO("CLIENT_LOG_INFO" << "[" << ss.str() << "] " <<
                Simulator::Now().GetSeconds() << "[s] " << info);
}

}
