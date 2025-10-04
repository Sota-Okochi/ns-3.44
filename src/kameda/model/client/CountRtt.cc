
#include "CountRtt.h"

#include "ns3/ping-helper.h"

#include <iostream>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("CountRtt");

CountRtt::CountRtt(){
    NS_LOG_FUNCTION(this);

    m_client = NULL;
    m_connectManager = NULL;
}

CountRtt::~CountRtt(){
    NS_LOG_FUNCTION(this);

}

void CountRtt::Init(Ptr<Node> client, Ptr<ConnectManager> mnj){
    NS_LOG_FUNCTION(this);
    m_client = client;
    m_connectManager = mnj;
}

void CountRtt::Start(Ipv4Address remoteA){
    NS_LOG_FUNCTION(this);

    COUNTRTT_LOG_INFO("Send Ping");
    PingHelper ping(remoteA);
    ApplicationContainer app = ping.Install(m_client);
    //std::cout << "ID:" << m_client->GetId() << std::endl;
    std::stringstream ss; ss << m_client->GetId();
    int id; ss >> id; id -= 2;
    double sec = 2.0 + ((double)id/5);
    //std::cout << "ID:" << id << "\tSec:" << sec << std::endl;
    app.Start(Seconds(sec));
    app.Stop(Seconds(sec+1.0));

    Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::Ping/Rtt",
                    MakeCallback (&ConnectManager::GetRtt, m_connectManager));

}




void CountRtt::COUNTRTT_LOG_INFO(std::string info){
    if(!m_client){
        NS_LOG_ERROR("CountRtt::m_client is NULL");
        return;
    }
    std::stringstream ss;
    ss << "NODE_ID:" << m_client->GetId();
    NS_LOG_INFO("COUNTRTT_LOG_INFO" << "[" << ss.str() << "] " <<
                Simulator::Now().GetSeconds() << "[s] " << info);
}

}
