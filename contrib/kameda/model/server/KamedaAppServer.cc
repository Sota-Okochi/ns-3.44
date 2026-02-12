/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/packet.h"
#include "ns3/ipv4.h"

#include "ns3/KamedaAppServer.h"
#include "ns3/APselection.h"

#include <algorithm>
#include <fstream>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("KamedaAppServer");

KamedaAppServer::KamedaAppServer(const ApSelectionInput& input)
    : m_input(input)
{
    NS_LOG_FUNCTION(this);

    m_listeningPort = 8080;
    m_socket = NULL;
    m_file_out = false; // ファイル出力を無効化
    m_cycleCount = 2;
    m_cycleDuration = Seconds(7.0);
    m_cycleIndex = 0;
    // APselectionを有効化（外部から渡された設定を利用）
    apselect = CreateObject<APselection>();
    apselect->init(m_input);
}

KamedaAppServer::~KamedaAppServer(){
    NS_LOG_FUNCTION(this);

}

void KamedaAppServer::DoDispose(){
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_socketList.clear();

    Application::DoDispose();
}

void KamedaAppServer::StartApplication(){
    NS_LOG_FUNCTION(this);

    if (apselect && !m_handoverCallback)
    {
        apselect->SetHandoverCallback(nullptr);
    }
    else if (apselect && m_handoverCallback)
    {
        apselect->SetHandoverCallback(m_handoverCallback);
    }

    if(!m_socket){
        m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId()); // TCP通信用ソケット生成

        Ptr<Ipv4> ipv4List = GetNode()->GetObject<Ipv4>();
        Ipv4Address listeningAddress = ipv4List->GetAddress(1,0).GetLocal();
        InetSocketAddress listeingAddressWithPort = InetSocketAddress(listeningAddress, m_listeningPort);

        uint32_t ipv4 = listeningAddress.Get();
        std::cout << "Server listening on: " << ((ipv4 >> 24) & 0xff) << "."
                                             << ((ipv4 >> 16) & 0xff) << "."
                                             << ((ipv4 >>  8) & 0xff) << "."
                                             << (ipv4 & 0xff) << ":" << m_listeningPort << std::endl;
        NS_LOG_INFO("Listening Address:" << ((ipv4 >> 24) & 0xff) << "."
                                         << ((ipv4 >> 16) & 0xff) << "."
                                         << ((ipv4 >>  8) & 0xff) << "."
                             << "Port:" << m_listeningPort);
        m_socket->Bind(listeingAddressWithPort);
        m_socket->Listen(); //クライアントからの接続要求待機開始
    }

    m_socket->SetRecvCallback(MakeCallback(&KamedaAppServer::HandleRead, this));
    m_socket->SetAcceptCallback(
        MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
        MakeCallback(&KamedaAppServer::HandleAccept, this));
    m_socket->SetCloseCallbacks(
        MakeCallback(&KamedaAppServer::HandleClose, this),
        MakeCallback(&KamedaAppServer::HandleError, this));

    // バッチ処理対応：タイムアウト時間を最適化
    // 監視端末方式: 3台の監視端末からのデータ収集完了を待つ（6.5秒で確実に実行）
    if (apselect)
    {
        apselect->StartNewCycle(1);
    }
    std::cout << "=== Scheduling cycle end handlers ===" << std::endl;
    ScheduleCycleEnd(0);
}

void KamedaAppServer::StopApplication(){
    NS_LOG_FUNCTION(this);

}

// クライアントからのRTTデータ受信
void KamedaAppServer::HandleRead(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);
    std::cout << "=== KamedaAppServer::HandleRead() - Data received ===" << std::endl;

    char buf[1024] = {'\0'};
    Address recvFrom;
    [[maybe_unused]] int recvSize = socket->RecvFrom((uint8_t*)buf, sizeof(buf)/sizeof(char), 0, recvFrom);

    std::string recvMessage = buf;
    
    // 監視端末からのデータかチェック
    if (recvMessage.find("MONITOR_AP") == 0) {
        std::cout << "Received MONITOR message: " << recvMessage << std::endl;
    } else {
        std::cout << "Received message: 端末" << recvMessage << "ms" << std::endl;
    }

    /*if(recvMessge.find("GET ", 0) != 0){
        NS_FATAL_ERROR("")
    }*/

    // Convert sender ipv4 Address
    std::string senderIpAddress;
    if(InetSocketAddress::IsMatchingType(recvFrom)){
        InetSocketAddress inet = InetSocketAddress::ConvertFrom(recvFrom);
        Ipv4Address from = inet.GetIpv4();
        uint32_t ipv4Addr = from.Get();

        std::stringstream ss;
        ss << ((ipv4Addr >> 24) & 0xff) << "."
         << ((ipv4Addr >> 16) & 0xff) << "."
         << ((ipv4Addr >>  8) & 0xff) << "."
         << (ipv4Addr & 0xff);
        senderIpAddress = ss.str();
    }else{
        NS_FATAL_ERROR("Sender IP is not ipv4");
    }

    std::stringstream ss;
    ss << "Sender Ipv4: " << senderIpAddress;
    SERVER_LOG_INFO(ss.str());
    std::stringstream ss2;
    ss2 << "RecvMessage is " << recvMessage;
    SERVER_LOG_INFO(ss2.str());
    
    // 監視端末からのデータか通常端末からのデータかで処理を分岐
    if (recvMessage.find("MONITOR_AP") == 0) {
        // 監視端末からのデータを処理
        ProcessMonitorData(senderIpAddress, recvMessage);
    } else {
        // 通常端末からのデータを処理
        if(apselect) {
            apselect->setData(senderIpAddress, recvMessage);
        }
    }
}

// 監視端末からのデータを処理
void KamedaAppServer::ProcessMonitorData(std::string senderIpAddress, std::string recvMessage) {
    std::cout << "=== KamedaAppServer::ProcessMonitorData() called ===" << std::endl;
    std::cout << "Monitor IP: " << senderIpAddress << std::endl;
    std::cout << "Monitor Message: " << recvMessage << std::endl;

    // メッセージをパース: "MONITOR_AP1,331.667"
    std::cout << "Original message: '" << recvMessage << "'" << std::endl;
    std::vector<std::string> parts = SplitString(recvMessage, ",");
    std::cout << "Split parts count: " << parts.size() << std::endl;
    for (size_t i = 0; i < parts.size(); i++) {
        std::cout << "Part[" << i << "]: '" << parts[i] << "'" << std::endl;
    }
    
    if (parts.size() < 2 || parts.size() > 3) {
        std::cout << "Invalid monitor message format" << std::endl;
        return;
    }

    // AP番号を抽出: "MONITOR_AP1" -> "1"
    std::string apPart = parts[0];
    if (apPart.find("MONITOR_AP") != 0) {
        std::cout << "Invalid monitor AP format" << std::endl;
        return;
    }
    
    std::string apNumStr = apPart.substr(10); // "MONITOR_AP"の長さは10
    std::cout << "Extracted AP number string: '" << apNumStr << "'" << std::endl;
    
    if (apNumStr.empty()) {
        std::cout << "Empty AP number string" << std::endl;
        return;
    }
    
    int apNo = std::stoi(apNumStr);
    
    // RTT値を抽出
    double rttValue = std::stod(parts[1]);
    double throughputBps = 0.0;
    bool hasThroughput = false;
    if (parts.size() >= 3)
    {
        try
        {
            throughputBps = std::stod(parts[2]);
            hasThroughput = true;
        }
        catch (const std::exception& ex)
        {
            std::cout << "Failed to parse throughput: " << ex.what() << std::endl;
        }
    }
    
    std::cout << "Processed Monitor Data: AP=" << apNo << ", RTT=" << rttValue << "ms";
    if (hasThroughput)
    {
        std::cout << ", Throughput=" << throughputBps << "bps";
    }
    std::cout << std::endl;
    
    // APselectionに監視端末データを送信
    if(apselect) {
        // 監視端末データを直接APselectionに送信
        // IPアドレスを監視端末用に変換: "10.0.10.1" -> "10.1.0.1" (AP0の形式)
        std::stringstream apIpAddress;
        apIpAddress << "10.1." << apNo << ".1";
        
        std::stringstream monitorMsg;
        monitorMsg << "MONITOR_" << apNo << "," << rttValue;
        if (hasThroughput)
        {
            monitorMsg << "," << throughputBps;
        }
        apselect->setData(apIpAddress.str(), monitorMsg.str());
        
        // Monitor data forwarded silently to reduce output
    }
}

//新規クライアント接続処理
void KamedaAppServer::HandleAccept(Ptr<Socket> socket, const Address& from){
    NS_LOG_FUNCTION(this << socket << from);
    std::cout << "=== KamedaAppServer::HandleAccept() - Client connected ===" << std::endl;

    socket->SetRecvCallback(MakeCallback(&KamedaAppServer::HandleRead, this));
    m_socketList.push_back(socket);
}

// 接続切断処理
void KamedaAppServer::HandleClose(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);
}

// エラーハンドリング
void KamedaAppServer::HandleError(Ptr<Socket> socket){
    NS_LOG_FUNCTION(this);
}

void KamedaAppServer::SERVER_LOG_INFO(std::string info){
    std::stringstream ss;
    ss << "NODE_ID:" << GetNode()->GetId();

    if(m_file_out){
        std::string filename("/home/sota/ns-3.44/OUTPUT/server_log.txt");
        std::ofstream ofs(filename, std::ios::out | std::ios::app);
        ofs << "SERVER_LOG_INFO" << "[" << ss.str() << "] " <<
                    Simulator::Now().GetSeconds() << "[s] " << info << std::endl;
    }else{
        NS_LOG_INFO("SERVER_LOG_INFO" << "[" << ss.str() << "] " <<
                    Simulator::Now().GetSeconds() << "[s] " << info);
    }
}

// RTTデータをJSON形式で出力（ソケット通信用）
void KamedaAppServer::WriteRttDataJSON(std::string senderIpAddress, std::string recvMessage){
    // IPアドレスからAP番号を抽出
    std::vector<std::string> ipParts = SplitString(senderIpAddress, ".");
    if(ipParts.size() < 3) return;
    
    std::stringstream ss(ipParts[2]);
    int apNo; ss >> apNo;

    // RTT値を抽出
    std::vector<std::string> msgParts = SplitString(recvMessage, ",");
    if(msgParts.size() != 2) return;
    
    std::stringstream ss2(msgParts[1]);
    double rttValue; ss2 >> rttValue;

    // JSON形式で追記
    std::string filename("/home/sota/ns-3.44/master/data/rtt_output.json");
    std::ofstream ofs(filename, std::ios::out | std::ios::app);
    
    if(ofs.is_open()) {
        static bool first_record = true;
        if(!first_record) {
            ofs << "," << std::endl;
        }
        first_record = false;
        
        ofs << "    {" << std::endl;
        ofs << "      \"timestamp\": " << Simulator::Now().GetSeconds() << "," << std::endl;
        ofs << "      \"terminal_id\": " << msgParts[0] << "," << std::endl;
        ofs << "      \"ap_id\": " << apNo << "," << std::endl;
        ofs << "      \"rtt_value\": " << rttValue << std::endl;
        ofs << "    }";
        ofs.close();
    }
}

// 文字列分割用のヘルパー関数
std::vector<std::string> KamedaAppServer::SplitString(const std::string &input, const std::string &delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    std::size_t end = input.find(delimiter);
    
    while (end != std::string::npos) {
        result.push_back(input.substr(start, end - start));
        start = end + delimiter.length();
        end = input.find(delimiter, start);
    }
    
    result.push_back(input.substr(start));
    return result;
}

void KamedaAppServer::Ending(){
    std::cout << "=== KamedaAppServer::Ending() called at " << Simulator::Now().GetSeconds()
              << "s (cycle " << (m_cycleIndex + 1) << ") ===" << std::endl;

    if(apselect) {
        std::cout << "=== Starting AP selection optimization ===" << std::endl;
        apselect->tmain();
        std::cout << "=== AP selection optimization completed ===" << std::endl;
    }

    if (m_handoverCallback && apselect)
    {
        m_handoverCallback(apselect->GetLastAssignment());
    }

    m_cycleIndex++;
    if (m_cycleIndex < m_cycleCount)
    {
        if (apselect)
        {
            apselect->StartNewCycle(m_cycleIndex + 1);
        }
        ScheduleCycleEnd(m_cycleIndex);
    }
    else
    {
        std::cout << "=== All cycles completed; stopping simulator ===" << std::endl;
        Simulator::Stop(m_cycleDuration * m_cycleCount);
    }
}

// JSON形式ファイルを完成させる
void KamedaAppServer::FinalizeJSONFile(){
    std::string filename("/home/sota/ns-3.44/master/data/rtt_output.json");
    std::ofstream ofs(filename, std::ios::out | std::ios::app);
    
    if(ofs.is_open()) {
        ofs << std::endl << "  ]" << std::endl;
        ofs << "}" << std::endl;
        ofs.close();
        std::cout << "JSON file finalized" << std::endl;
    }
}

void KamedaAppServer::ConfigureCycles(uint32_t count, Time duration)
{
    m_cycleCount = std::max<uint32_t>(1, count);
    m_cycleDuration = duration.IsPositive() ? duration : Seconds(7.0);
}

void KamedaAppServer::SetTerminalTp(int termIdx, double tpBps)
{
    if (apselect)
    {
        apselect->setTerminalTp(termIdx, tpBps);
    }
}

void KamedaAppServer::SetHandoverCallback(const std::function<void(const std::vector<int>&)>& cb)
{
    m_handoverCallback = cb;
    if (apselect)
    {
        apselect->SetHandoverCallback(m_handoverCallback);
    }
}

void KamedaAppServer::ScheduleCycleEnd(uint32_t cycleIndex)
{
    // Move the optimization close to the monitor window end (3.6s) with a small guard
    const Time monitorWindowStop = Seconds(3.6);
    const Time guard = Seconds(0.2);

    Time cycleStart = m_cycleDuration * cycleIndex;
    Time preferred = cycleStart + monitorWindowStop + guard;
    Time cycleBoundary = m_cycleDuration * (cycleIndex + 1);

    // Do not let the end event slip past the cycle boundary
    Time when = std::min(preferred, cycleBoundary);
    if (when < Simulator::Now())
    {
        when = Simulator::Now();
    }

    Simulator::Schedule(when - Simulator::Now(), &KamedaAppServer::Ending, this);
    std::cout << "=== Cycle " << (cycleIndex + 1) << " end scheduled at "
              << when.GetSeconds() << "s (after monitor window) ===" << std::endl;
}

}
