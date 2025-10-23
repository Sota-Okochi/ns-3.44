#ifndef MASTER_RTT_FORWARDER_APP_H
#define MASTER_RTT_FORWARDER_APP_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/socket.h"

#include <queue>
#include <string>

namespace ns3 {

class RttForwarderApp : public Application
{
public:
    RttForwarderApp();
    ~RttForwarderApp() override;

    void SetRemote(Ipv4Address remoteAddress, uint16_t remotePort);
    void SetListeningPort(uint16_t port);

protected:
    void StartApplication() override;
    void StopApplication() override;

private:
    void SetupUdpSocket();
    void SetupTcpSocket();
    void HandleUdpReceive(Ptr<Socket> socket);
    void HandleTcpConnect(Ptr<Socket> socket);
    void HandleTcpError(Ptr<Socket> socket);
    void HandleTcpClose(Ptr<Socket> socket);
    void TrySendPending();
    void ReconnectTcp();
    void CancelReconnect();

    Ipv4Address m_remoteAddress;
    uint16_t m_remotePort;
    uint16_t m_listenPort;

    Ptr<Socket> m_udpSocket;
    Ptr<Socket> m_tcpSocket;
    bool m_tcpConnected;

    std::queue<std::string> m_pendingMessages;
    EventId m_reconnectEvent;
};

} // namespace ns3

#endif // MASTER_RTT_FORWARDER_APP_H

