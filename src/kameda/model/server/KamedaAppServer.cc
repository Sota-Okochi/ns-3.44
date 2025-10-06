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

#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("KamedaAppServer");

// RTTデータ構造体（C++/Python間で共有）
struct RTTRecord {
    double timestamp;
    uint32_t terminal_id;
    uint32_t ap_id;
    double rtt_value;
};

KamedaAppServer::KamedaAppServer(const ApSelectionInput& input)
    : m_input(input)
{
    NS_LOG_FUNCTION(this);

    m_listeningPort = 8080;
    m_socket = NULL;
    m_file_out = false; // ファイル出力を無効化
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

    // RTTデータファイルを初期化（古いデータをクリア）
    InitializeRttDataFile(); // ここで関数の呼び出し 
    
    // バッチ処理対応：タイムアウト時間を最適化
    // 監視端末方式: 3台の監視端末からのデータ収集完了を待つ（6.5秒で確実に実行）
    std::cout << "=== Scheduling Ending() function at 6.5s ===" << std::endl;
    Simulator::Schedule(Seconds(6.5), &KamedaAppServer::Ending, this); // 6.5秒後にEnding関数を呼び出す（監視端末対応）
    std::cout << "=== Ending() function scheduled successfully ===" << std::endl;
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

    // RTTデータをバイナリファイルに出力
    WriteRttDataBinary(senderIpAddress, recvMessage);
    
    // オプション: JSON形式でも出力
    // WriteRttDataJSON(senderIpAddress, recvMessage);
    
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
    
    if (parts.size() != 2) {
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
    
    std::cout << "Processed Monitor Data: AP=" << apNo << ", RTT=" << rttValue << "ms" << std::endl;
    
    // APselectionに監視端末データを送信
    if(apselect) {
        // 監視端末データを直接APselectionに送信
        // IPアドレスを監視端末用に変換: "10.0.10.1" -> "10.1.0.1" (AP0の形式)
        std::stringstream apIpAddress;
        apIpAddress << "10.1." << apNo << ".1";
        
        std::stringstream monitorMsg;
        monitorMsg << "MONITOR_" << apNo << "," << rttValue;
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

// RTTデータファイルを初期化（バイナリ形式）
void KamedaAppServer::InitializeRttDataFile(){
    // ディレクトリが存在しない場合は作成
    std::string dir_path("/home/sota/ns-3.44/OUTPUT");
    struct stat info;
    if(stat(dir_path.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
        mkdir(dir_path.c_str(), 0755);
        std::cout << "Created directory: " << dir_path << std::endl;
    }
    
    // バイナリファイル初期化
    std::string bin_filename("/home/sota/ns-3.44/OUTPUT/rtt_output.bin");
    std::ofstream bin_ofs(bin_filename, std::ios::out | std::ios::binary | std::ios::trunc);        
    
    if(bin_ofs.is_open()) {
        // ヘッダー情報をバイナリで書き込み
        uint32_t header_magic = 0x52545444; // "RTTD"
        uint32_t version = 1;
        uint32_t record_size = sizeof(RTTRecord);
        
        bin_ofs.write(reinterpret_cast<const char*>(&header_magic), sizeof(header_magic));
        bin_ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
        bin_ofs.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));
        bin_ofs.close();
        std::cout << "RTT binary data file initialized: " << bin_filename << std::endl;
    } else {
        std::cout << "Failed to initialize RTT binary data file" << std::endl;
    }
    
    /*
    // JSON形式ファイル初期化
    std::string json_filename("/home/sota/ns-3.30/master/data/rtt_output.json");
    std::ofstream json_ofs(json_filename, std::ios::out | std::ios::trunc);
    
    if(json_ofs.is_open()) {
        json_ofs << "{" << std::endl;
        json_ofs << "  \"header\": {" << std::endl;
        json_ofs << "    \"format\": \"RTT_Measurements\"," << std::endl;
        json_ofs << "    \"version\": 1," << std::endl;
        json_ofs << "    \"timestamp\": " << Simulator::Now().GetSeconds() << std::endl;
        json_ofs << "  }," << std::endl;
        json_ofs << "  \"data\": [" << std::endl;
        json_ofs.close();
        std::cout << "RTT JSON data file initialized: " << json_filename << std::endl;
    }
    */
}

// RTTデータをバイナリ形式で出力（高速）
void KamedaAppServer::WriteRttDataBinary(std::string senderIpAddress, std::string recvMessage){
    std::cout << "=== KamedaAppServer::WriteRttDataBinary() called ===" << std::endl;

    // IPアドレスからAP番号を抽出
    std::vector<std::string> ipParts = SplitString(senderIpAddress, ".");
    if(ipParts.size() < 3) {
        std::cout << "Invalid IP address format" << std::endl;
        return;
    }
    
    std::stringstream ss(ipParts[2]);
    int apNo; ss >> apNo;

    // RTT値を抽出
    std::vector<std::string> msgParts = SplitString(recvMessage, ",");
    if(msgParts.size() != 2) {
        std::cout << "Invalid message format" << std::endl;
        return;
    }
    
    std::stringstream ss2(msgParts[1]);
    double rttValue; ss2 >> rttValue;

    // RTTレコード作成
    RTTRecord record;
    record.timestamp = Simulator::Now().GetSeconds();
    
    // 監視端末の場合は特別な処理
    if (msgParts[0].find("MONITOR_AP") == 0) {
        // 監視端末の場合、terminal_idはAP番号をベースにした特別なID
        record.terminal_id = static_cast<uint32_t>(1000 + apNo); // 1000番台を監視端末用に使用
    } else {
        // 通常の端末の場合
        record.terminal_id = static_cast<uint32_t>(std::stoi(msgParts[0]));
    }
    
    record.ap_id = static_cast<uint32_t>(apNo);
    record.rtt_value = rttValue;

    // バイナリファイルに高速書き込み
    std::string filename("/home/sota/ns-3.44/OUTPUT/rtt_output.bin");
    std::ofstream ofs(filename, std::ios::out | std::ios::binary | std::ios::app);
    
    if(ofs.is_open()) {
        ofs.write(reinterpret_cast<const char*>(&record), sizeof(RTTRecord));
        ofs.close();
        std::cout << "Binary RTT data written: Terminal=" << record.terminal_id 
                  << ", AP=" << record.ap_id << ", RTT=" << record.rtt_value << "ms" << std::endl;
    } else {
        std::cout << "Failed to open RTT binary data file" << std::endl;
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
    std::cout << "=== KamedaAppServer::Ending() called at " << Simulator::Now().GetSeconds() << "s ===" << std::endl;
    
    // JSON形式を完成させる
    // FinalizeJSONFile();
    
    // バイナリファイルから統計を出力
    OutputRttStatisticsFromBinary();
    
    // APselectionで最適化処理を実行
    if(apselect) {
        std::cout << "=== Starting AP selection optimization ===" << std::endl;
        apselect->tmain();
        std::cout << "=== AP selection optimization completed ===" << std::endl;
    }
    
    std::cout << "=== KamedaAppServer::Ending() completed ===" << std::endl;
    Simulator::Stop();
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

// バイナリファイルからRTTデータを読み込んで統計を出力
void KamedaAppServer::OutputRttStatisticsFromBinary(){
    std::cout << "=== Reading RTT data from binary file ===" << std::endl;
    
    std::string filename("/home/sota/ns-3.44/OUTPUT/rtt_output.bin");
    std::ifstream ifs(filename, std::ios::in | std::ios::binary);
    
    if(!ifs.is_open()) {
        std::cout << "RTT binary data file not found" << std::endl;
        return;
    }
    
    // ヘッダー読み込み
    uint32_t header_magic, version, record_size;
    ifs.read(reinterpret_cast<char*>(&header_magic), sizeof(header_magic));
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    ifs.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
    
    if(header_magic != 0x52545444 || record_size != sizeof(RTTRecord)) {
        std::cout << "Invalid binary file format" << std::endl;
        ifs.close();
        return;
    }
    
    // AP毎のRTTデータを格納
    std::vector<std::vector<double>> apRttData(3); // 3つのAPを想定
    
    RTTRecord record;
    while(ifs.read(reinterpret_cast<char*>(&record), sizeof(RTTRecord))) {
        if(record.ap_id < 3) {
            apRttData[record.ap_id].push_back(record.rtt_value);
        }
    }
    ifs.close();
    
    // 統計情報を出力
    std::cout << "=== RTT Statistics from Binary File ===" << std::endl;
    for(int i = 0; i < 3; i++) {
        if(apRttData[i].size() > 0) {
            double sum = 0.0;
            for(double rtt : apRttData[i]) {
                sum += rtt;
            }
            double average = sum / apRttData[i].size();
            
            std::cout << "AP:" << i << "\tSUM:" << sum
                      << "\tSIZE:" << apRttData[i].size() << "\tAVE:" << average << std::endl;
        } else {
            std::cout << "AP:" << i << "\tNo data available" << std::endl;
        }
    }
    std::cout << "=== End RTT Statistics ===" << std::endl;
}


}
