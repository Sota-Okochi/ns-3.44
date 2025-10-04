
#include "CountRtt.h"

#include "ns3/ping-helper.h"

#include <iostream>
#include <random>

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
    
    // 最適化: 送信間隔を0.1s→0.02sに短縮（5倍高速化）
    double base_sec = 2.0 + ((double)id * 0.02);
    
    // ランダムジッター追加でコリジョン回避（±10ms）
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> jitter(-0.01, 0.01);
    double sec = base_sec + jitter(gen);
    
    // バッチ処理: 10台ごとに同時開始でさらに高速化
    int batch_id = id / 10;
    double batch_sec = 2.0 + ((double)batch_id * 0.2) + jitter(gen);
    
    // 最終的な開始時間（バッチ処理優先）
    double final_sec = (id < 100) ? batch_sec : sec;
    
    //std::cout << "ID:" << id << "\tSec:" << final_sec << std::endl;
    app.Start(Seconds(final_sec));
    app.Stop(Seconds(final_sec+1.0));

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
