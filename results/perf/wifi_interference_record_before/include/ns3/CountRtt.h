/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef COUNT_RTT_H
#define COUNT_RTT_H

#include "ns3/log.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/internet-apps-module.h"

#include "ns3/ConnectManager.h"

#include <string>

namespace ns3 {

class CountRtt : public Object{

public:
    CountRtt();
    virtual ~CountRtt();
    void Init(Ptr<Node> client, Ptr<ConnectManager> mnj);
    void Start(Ipv4Address remoteA);

private:

    Ptr<Node> m_client;
    Ptr<ConnectManager> m_connectManager;
    void GetRtt(std::string context, Time rtt);

    void COUNTRTT_LOG_INFO(std::string info);

};

}

#endif /* COUNT_RTT_H */
