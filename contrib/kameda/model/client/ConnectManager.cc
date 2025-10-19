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
    m_retryCount = 0;
    m_maxRetries = 3; // 最大3回リトライ
    m_socket = nullptr;
}

void ConnectManager::StartRequest(){
    m_nextAccess = Simulator::ScheduleNow(&ConnectManager::CreateConnection, this);
    m_continue = true;
}

void ConnectManager::StopRequest(){

    m_continue = false;

    if(m_nextAccess.IsPending()){
        Simulator::Cancel(m_nextAccess);
    }

    if(m_socket){
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
        m_socket = nullptr;
    }
}

void ConnectManager::GetRtt(uint16_t seq, Time rtt){

    std::stringstream ss;
    ss << rtt.GetMilliSeconds();
    m_rttDatas.push_back(ss.str());
    const int TIME = 3; // 5回→3回に短縮して高速化
    if(m_rttDatas.size() == TIME){      //TIME回データをとった平均
        double temp = 0.0;
        for(auto d : m_rttDatas){
            std::stringstream ss; double a = 0.0;
            ss << d; ss >> a;
            temp += a;
        }
        std::stringstream ss2;
        ss2 << temp/(static_cast<double>(TIME));
        ss2 >> m_rttData;
        this->StartRequest();
    }
}

void ConnectManager::CreateConnection(){

    m_socket = m_client->CreateTcpSocket();
    if(!m_socket){
        NS_LOG_ERROR("Failed to create socket");
        return;
    }

    /*std::stringstream ss;
    ss << "ConnectManager::m_serverAddress is " << m_serverAddress;
    NS_LOG_INFO(ss.str());*/
    if(InetSocketAddress::IsMatchingType(m_serverAddress)){
        m_socket->Bind();
        m_socket->Connect(m_serverAddress);
    }else{
        NS_FATAL_ERROR("serverAddress cannot match to InetSocketAddress");
    }

    m_socket->SetConnectCallback(
        MakeCallback(&ConnectManager::ConnectionSucceeded, this),
        MakeCallback(&ConnectManager::ConnectionFailed, this));
    /*m_socket->SetRecvCallback(
        MakeCallback(&ConnectManager::HandleReadResponse, this));*/
}

void ConnectManager::ConnectionSucceeded(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);
    m_retryCount = 0; // 接続成功時にリトライカウンタをリセット
    NS_LOG_INFO("Connection succeeded");
    SendMessage();
}

void ConnectManager::ConnectionFailed(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);

    m_retryCount++;
    NS_LOG_INFO("Connection Failed (Retry " << m_retryCount << "/" << m_maxRetries << ")");
    
    if(m_socket){
        m_socket->Close();
        m_socket = nullptr;
    }
    if(m_retryCount < m_maxRetries && m_continue) {
        // 指数バックオフでリトライ間隔を調整
        double retryDelay = 0.5 * (1 << (m_retryCount - 1)); // 0.5s, 1s, 2s
        NS_LOG_INFO("Retrying connection in " << retryDelay << " seconds");
        m_nextAccess = Simulator::Schedule(Seconds(retryDelay), &ConnectManager::CreateConnection, this);
    } else {
        NS_LOG_INFO("Max retries exceeded, giving up connection");
        m_continue = false;
    }
}

int ConnectManager::SendMessage(){
    NS_LOG_FUNCTION(this);
    if(m_rttData.empty())
        return 0;
    if(!m_socket){
        NS_LOG_WARN("Socket unavailable when trying to send");
        return 0;
    }
    Ptr<Node> node = m_client->GetNode();
    int id = node->GetId();
    std::stringstream ss;
    ss << id << "," << m_rttData;
    std::string mes = ss.str();
    int sent = m_socket->Send(reinterpret_cast<const uint8_t*>(mes.data()), mes.size(), 0);
    m_rttData.clear();
    return sent;
}

}
