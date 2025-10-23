/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/log.h"
#include "ns3/socket.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/node.h"
#include "ns3/nstime.h"

#include "ns3/KamedaAppClient.h"

#include "ConnectManager.h"

#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ConnectManager");

ConnectManager::ConnectManager(Address serverAddress, Ptr<KamedaAppClient> client)
{
    m_serverAddress = serverAddress;
    m_client = client;
    m_udpSocket = nullptr;
    m_rttData = "";
}

void ConnectManager::StartRequest()
{
    SendMessage();
}

void ConnectManager::StopRequest()
{
    if (m_udpSocket)
    {
        m_udpSocket->Close();
        m_udpSocket = nullptr;
    }
}

void ConnectManager::GetRtt(uint16_t seq, Time rtt)
{
    std::stringstream ss;
    ss << rtt.GetMilliSeconds();
    m_rttDatas.push_back(ss.str());
    const int TIME = 3;
    if (m_rttDatas.size() == static_cast<size_t>(TIME))
    {
        double temp = 0.0;
        for (const auto& d : m_rttDatas)
        {
            std::stringstream ss2;
            double value = 0.0;
            ss2 << d;
            ss2 >> value;
            temp += value;
        }
        std::stringstream ss3;
        ss3 << temp / static_cast<double>(TIME);
        ss3 >> m_rttData;
        m_rttDatas.clear();
        StartRequest();
    }
}

void ConnectManager::EnsureSocket()
{
    if (m_udpSocket)
    {
        return;
    }
    if (!InetSocketAddress::IsMatchingType(m_serverAddress))
    {
        NS_FATAL_ERROR("ConnectManager expects InetSocketAddress");
    }
    Ptr<Node> node = m_client ? m_client->GetNode() : nullptr;
    if (!node)
    {
        NS_LOG_ERROR("ConnectManager: client node unavailable");
        return;
    }
    m_udpSocket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
    if (!m_udpSocket)
    {
        NS_LOG_ERROR("ConnectManager: failed to create UDP socket");
        return;
    }
    m_udpSocket->Connect(m_serverAddress);
}

bool ConnectManager::SendMessage()
{
    NS_LOG_FUNCTION(this);
    if (m_rttData.empty())
    {
        return false;
    }

    EnsureSocket();
    if (!m_udpSocket)
    {
        NS_LOG_WARN("ConnectManager UDP socket unavailable, dropping RTT data");
        return false;
    }

    Ptr<Node> node = m_client ? m_client->GetNode() : nullptr;
    if (!node)
    {
        NS_LOG_WARN("ConnectManager: client node missing");
        return false;
    }

    int id = node->GetId();
    std::stringstream ss;
    ss << id << "," << m_rttData;
    std::string message = ss.str();

    int sent = m_udpSocket->Send(reinterpret_cast<const uint8_t*>(message.data()), message.size(), 0);
    if (sent <= 0)
    {
        NS_LOG_WARN("ConnectManager failed to send RTT payload");
        return false;
    }

    NS_LOG_INFO("ConnectManager sent RTT data: " << message);
    m_rttData.clear();
    return true;
}

} // namespace ns3
