/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/log.h"
#include "ns3/address-utils.h"
#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/inet-socket-address.h"
#include "ns3/node.h"

#include "ns3/KamedaAppClient.h"

#include "ConnectManager.h"
#include <fstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ConnectManager");

ConnectManager::ConnectManager(Address serverAddress, Ptr<KamedaAppClient> client){
    m_serverAddress = serverAddress;
    m_client = client;
    m_packet = Create<Packet>();
    m_continue = false;
    m_rttData = "";
    m_socket = nullptr;
}

void ConnectManager::StartRequest(){
    m_continue = true;
    if(m_nextAccess.IsRunning()){
        Simulator::Cancel(m_nextAccess);
    }
    m_nextAccess = Simulator::ScheduleNow(&ConnectManager::CreateConnection, this);
}

void ConnectManager::StopRequest(){

    m_continue = false;

    if(m_nextAccess.IsRunning()){
        Simulator::Cancel(m_nextAccess);
    }

    if(m_socket){
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
        m_socket = nullptr;
    }
}

void ConnectManager::GetRtt(uint16_t seq, Time rtt){
    std::ofstream ofs("/home/yy/test.txt", std::ios::app);
    ofs << m_client->GetNode()->GetId() << "\t" << rtt << std::endl;

    std::stringstream ss;
    ss << rtt.GetMilliSeconds();
    m_rttDatas.push_back(ss.str());
    const int TIME = 3;
    if(m_rttDatas.size() == TIME){
        double temp = 0.0;
        for(auto d : m_rttDatas){
            std::stringstream ss; double a = 0.0;
            ss << d; ss >> a;
            temp += a;
        }
        std::stringstream ss2;
        ss2 << temp/(static_cast<double>(TIME));
        ss2 >> m_rttData;
        m_rttDatas.clear();
        this->StartRequest();
    }
}

void ConnectManager::CreateConnection(){

    if(!m_continue){
        return;
    }

    if(m_socket){
        m_socket->Close();
        m_socket = nullptr;
    }

    if(!InetSocketAddress::IsMatchingType(m_serverAddress)){
        NS_FATAL_ERROR("serverAddress cannot match to InetSocketAddress");
    }

    m_socket = m_client->CreateTcpSocket();
    if(!m_socket){
        NS_LOG_ERROR("Failed to create socket");
        return;
    }

    m_socket->Bind();
    m_socket->Connect(m_serverAddress);

    m_socket->SetConnectCallback(
        MakeCallback(&ConnectManager::ConnectionSucceeded, this),
        MakeCallback(&ConnectManager::ConnectionFailed, this));
    /*m_socket->SetRecvCallback(
        MakeCallback(&ConnectManager::HandleReadResponse, this));*/
}

void ConnectManager::ConnectionSucceeded(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);
    if(!m_continue){
        return;
    }
    SendMessage();
}

void ConnectManager::ConnectionFailed(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);

    NS_LOG_INFO("Connection Failed");
    if(m_socket){
        m_socket->Close();
        m_socket = nullptr;
    }
    if(m_continue){
        m_nextAccess = Simulator::Schedule(Seconds(1.0), &ConnectManager::CreateConnection, this);
    }
}

int ConnectManager::SendMessage(){
    NS_LOG_FUNCTION(this);
    if(m_rttData.empty()){
        return 0;
    }
    if(!m_socket){
        NS_LOG_WARN("Socket is not available");
        return 0;
    }
    Ptr<Node> node = m_client->GetNode();
    int id = node->GetId();
    std::stringstream ss;
    ss << id << "," << m_rttData;
    std::string mes = ss.str();
    int sent = m_socket->Send(reinterpret_cast<const uint8_t*>(mes.data()), mes.size(), 0);
    m_socket->Close();
    m_socket = nullptr;
    m_rttData.clear();
    return sent;
}

}
