#include "RttForwarderApp.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/nstime.h"

#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("RttForwarderApp");

RttForwarderApp::RttForwarderApp()
    : m_remoteAddress("0.0.0.0"),
      m_remotePort(8080),
      m_listenPort(9000),
      m_udpSocket(nullptr),
      m_tcpSocket(nullptr),
      m_tcpConnected(false)
{
}

RttForwarderApp::~RttForwarderApp()
{
    CancelReconnect();
    m_udpSocket = nullptr;
    m_tcpSocket = nullptr;
}

void RttForwarderApp::SetRemote(Ipv4Address remoteAddress, uint16_t remotePort)
{
    m_remoteAddress = remoteAddress;
    m_remotePort = remotePort;
}

void RttForwarderApp::SetListeningPort(uint16_t port)
{
    m_listenPort = port;
}

void RttForwarderApp::StartApplication()
{
    SetupUdpSocket();
    SetupTcpSocket();
}

void RttForwarderApp::StopApplication()
{
    CancelReconnect();

    if (m_udpSocket)
    {
        m_udpSocket->Close();
        m_udpSocket = nullptr;
    }

    if (m_tcpSocket)
    {
        m_tcpSocket->Close();
        m_tcpSocket = nullptr;
    }

    m_tcpConnected = false;
    std::queue<std::string> empty;
    std::swap(m_pendingMessages, empty);
}

void RttForwarderApp::SetupUdpSocket()
{
    if (m_udpSocket || m_listenPort == 0)
    {
        return;
    }

    m_udpSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_listenPort);
    if (m_udpSocket->Bind(local) != 0)
    {
        NS_LOG_WARN("Failed to bind UDP socket on port " << m_listenPort);
        m_udpSocket = nullptr;
        return;
    }
    m_udpSocket->SetRecvCallback(MakeCallback(&RttForwarderApp::HandleUdpReceive, this));
}

void RttForwarderApp::SetupTcpSocket()
{
    if (m_tcpSocket || m_remoteAddress == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    m_tcpSocket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_tcpSocket->SetConnectCallback(
        MakeCallback(&RttForwarderApp::HandleTcpConnect, this),
        MakeCallback(&RttForwarderApp::HandleTcpError, this));
    m_tcpSocket->SetCloseCallbacks(
        MakeCallback(&RttForwarderApp::HandleTcpClose, this),
        MakeCallback(&RttForwarderApp::HandleTcpError, this));
    m_tcpSocket->Connect(InetSocketAddress(m_remoteAddress, m_remotePort));
}

void RttForwarderApp::HandleUdpReceive(Ptr<Socket> socket)
{
    Address from;
    while (Ptr<Packet> packet = socket->RecvFrom(from))
    {
        if (!packet || packet->GetSize() == 0)
        {
            continue;
        }

        std::string message;
        message.resize(packet->GetSize());
        packet->CopyData(reinterpret_cast<uint8_t*>(&message[0]), message.size());
        auto zeroPos = std::find(message.begin(), message.end(), '\0');
        if (zeroPos != message.end())
        {
            message.erase(zeroPos, message.end());
        }

        if (message.empty())
        {
            continue;
        }

        NS_LOG_INFO("Received RTT payload: " << message);
        m_pendingMessages.push(message);
    }

    TrySendPending();
}

void RttForwarderApp::HandleTcpConnect(Ptr<Socket> socket)
{
    NS_LOG_INFO("Connected to remote host " << m_remoteAddress << ":" << m_remotePort);
    m_tcpConnected = true;
    TrySendPending();
}

void RttForwarderApp::HandleTcpError(Ptr<Socket> socket)
{
    NS_LOG_WARN("TCP connection error, scheduling reconnect");
    m_tcpConnected = false;
    if (m_tcpSocket)
    {
        m_tcpSocket->Close();
        m_tcpSocket = nullptr;
    }
    if (!m_reconnectEvent.IsRunning())
    {
        m_reconnectEvent = Simulator::Schedule(Seconds(1.0), &RttForwarderApp::ReconnectTcp, this);
    }
}

void RttForwarderApp::HandleTcpClose(Ptr<Socket> socket)
{
    NS_LOG_INFO("TCP connection closed by remote host");
    m_tcpConnected = false;
    if (m_tcpSocket)
    {
        m_tcpSocket->Close();
        m_tcpSocket = nullptr;
    }
    if (!m_reconnectEvent.IsRunning())
    {
        m_reconnectEvent = Simulator::Schedule(Seconds(1.0), &RttForwarderApp::ReconnectTcp, this);
    }
}

void RttForwarderApp::TrySendPending()
{
    if (!m_tcpConnected || !m_tcpSocket)
    {
        if (!m_tcpSocket)
        {
            SetupTcpSocket();
        }
        return;
    }

    while (!m_pendingMessages.empty())
    {
        const std::string& msg = m_pendingMessages.front();
        int sent = m_tcpSocket->Send(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size(), 0);
        if (sent <= 0)
        {
            break;
        }
        m_pendingMessages.pop();
    }
}

void RttForwarderApp::ReconnectTcp()
{
    m_tcpConnected = false;
    if (m_tcpSocket)
    {
        m_tcpSocket->Close();
        m_tcpSocket = nullptr;
    }
    SetupTcpSocket();
}

void RttForwarderApp::CancelReconnect()
{
    if (m_reconnectEvent.IsRunning())
    {
        Simulator::Cancel(m_reconnectEvent);
    }
    m_reconnectEvent = EventId();
}

} // namespace ns3
