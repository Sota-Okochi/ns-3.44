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

#include "ns3/APselection.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <climits>
#include <fstream>
#include <iomanip>
#include <random>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("APselection");

// split関数の定義
std::vector<std::string> splitString(const std::string &input, const std::string &delimiter) {
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

APselection::APselection(){
	std::cout << "=== APselection::APselection ===" << std::endl;
}

APselection::~APselection(){
}

void APselection::init(const ApSelectionInput& input){
    NS_LOG_FUNCTION(this);

    std::cout << "=== APselection::init() START ===" << std::endl;


    // --------APselectionのメンバ変数に受け渡し-------------
    aps = input.baseStations; //基地局数
    terms = input.terminals; // 端末数
    initial_app = input.useAppli; // 各端末の初期アプリ番号
    initial_AP = input.initialAp; // 各端末の初期接続先


    m_monitor_rtt.assign(aps, 0.0);
    m_rtt_sum.assign(aps, 0.0);
    m_rtt_count.assign(aps, 0);
    m_has_rtt.assign(aps, false);
    m_monitor_tp.assign(aps, 0.0);
    m_has_tp.assign(aps, false);

    // --------各端末の初期接続先と初期アプリ種別の表示-----------
    /*if (!initial_AP.empty() && !initial_app.empty()){
            std::cout << "=== 端末初期設定 ===" << std::endl;
            for (int i = 0; i < terms; ++i)
            {
                std::cout << "Term:" << i
                        << "\tInitAP:" << initial_AP[i] - 1
                        << "\tApp:" << initial_app[i] << std::endl;
            }
    }*/

    std::cout << "APs: " << aps << ", Terms: " << terms << std::endl;
}

void APselection::setData(std::string senderIpAddress, std::string recvMessage){
    NS_LOG_FUNCTION(this);
    std::cout << "=== APselection::setData() called ===" << std::endl;
    std::cout << "Sender IP: " << senderIpAddress << std::endl;
    std::cout << "Message: " << recvMessage << std::endl;

    //送られたRTTデータから基地局ごとにRTT平均値を求める ここでは基地局ごとにpush_back
    std::vector<std::string> ret = splitString(senderIpAddress, "."); //IPアドレスを桁ごとに分解
    if(ret.size() < 3) {
        std::cout << "Invalid IP address format" << std::endl;
        return;
    }
    std::stringstream ss(ret[2]);   //前から3つ目の値がAPのナンバー
    int apNo; ss >> apNo;

    std::vector<std::string> ret2 = splitString(recvMessage, ",");
    if( ret2.size() < 2 || ret2.size() > 3 ) {
        std::cout << "Invalid message format" << std::endl;
        return;
    }
    std::stringstream ss2(ret2[1]);
    double d; ss2 >> d;
    if(static_cast<size_t>(apNo) >= m_monitor_rtt.size()) {
        std::cout << "Invalid AP index" << std::endl;
        return;
    }

    m_rtt_sum[apNo] += d;
    m_rtt_count[apNo] += 1;
    m_monitor_rtt[apNo] = m_rtt_sum[apNo] / static_cast<double>(m_rtt_count[apNo]);
    m_has_rtt[apNo] = true;

    if (ret2.size() >= 3)
    {
        std::stringstream ssTp(ret2[2]);
        double tpBps = 0.0;
        ssTp >> tpBps;
        m_monitor_tp[apNo] = tpBps;
        m_has_tp[apNo] = true;
        std::cout << "Monitor TP stored: AP=" << apNo << ", Goodput=" << tpBps << "bps" << std::endl;
    }

    std::cout << "Monitor data stored: AP=" << apNo << ", RTT=" << d
              << "ms, AVG=" << m_monitor_rtt[apNo] << "ms" << std::endl;
}

void APselection::tmain(){
    NS_LOG_FUNCTION(this);
    std::cout << "=== APselection::tmain() START ===" << std::endl;
    std::cout << "APs: " << aps << std::endl;
    std::cout << "monitors: " << m_monitor_rtt.size() << std::endl;

    
    for(int i=0; i<aps; i++){
        if(m_has_rtt[i]){
            double ave_rtt = m_monitor_rtt[i];
            double tpDisplayMbps = 0.0;
            if (m_has_tp[i])
            {
                tpDisplayMbps = m_monitor_tp[i] * ns3::APConstants::BPS_TO_MBPS;
            }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "AP:" << i << "\tRTT:" << ave_rtt << "ms"
                << "\tTP:" << tpDisplayMbps << "Mbps" << std::endl;

        } else {
            std::cout << "実測のRTT値とTP値がありません" << std::endl;
        }
    }


    // アプリ種別の必要TP, RTT
    cal_traffic_request();
    // 割り当て前端末満足度の調和平均の計算
    cal_initial_harmonic_mean();
    
    random_assignment_test();
    
    // std::cout << "=== APselection::tmain() END ===" << std::endl;
}


//必要RTT, TPの算出
void APselection::cal_traffic_request(){
    traffic_request.clear();
    traffic_request.reserve(terms);

    for(int i=0;i<terms;i++){
        if(initial_app.at(i) == static_cast<int>(APConstants::AppType::BROWSER)){
            traffic_request.push_back(APConstants::BROWSER_REQUIRED_TP);   //ブラウザ（TP）
        }
        else if(initial_app.at(i) == static_cast<int>(APConstants::AppType::VIDEO)){
            traffic_request.push_back(APConstants::VIDEO_REQUIRED_TP);  //動画ストリーミング（TP）
        }
        else if(initial_app.at(i) == static_cast<int>(APConstants::AppType::VOICE_CALL)){
            traffic_request.push_back(APConstants::VOICE_CALL_REQUIRED_RTT);  //通話アプリケーション（RTT）
        }
        else if(initial_app.at(i) == static_cast<int>(APConstants::AppType::LIVE_STREAM)){
            traffic_request.push_back(APConstants::LIVE_STREAM_REQUIRED_RTT);  //ライブ配信（RTT）
        }
    }
    
}

void APselection::cal_initial_harmonic_mean(){
    double initial_harmonic_mean = 0.0;
    double sum_satisfaction = 0.0;

    for(int i=0;i<terms;i++){
        int term_index = i;
        int ap_index = initial_AP[term_index] - 1;
        double satisfaction = calculate_satisfaction(term_index, ap_index);
        sum_satisfaction += satisfaction;
    }
    initial_harmonic_mean = terms / sum_satisfaction;
    std::cout << std::fixed << std::setprecision(6)
              << "割り当て前端末満足度の調和平均：" << initial_harmonic_mean << std::endl;
}

// 端末満足度計算（共通ロジック）
double APselection::calculate_satisfaction(int terminal_idx, int ap_idx) {
    int appNum = initial_app[terminal_idx];
    double satis = 0;

    if(appNum == static_cast<int>(APConstants::AppType::BROWSER) || 
        appNum == static_cast<int>(APConstants::AppType::VIDEO)) {
            // TP指標
            double needTp = traffic_request[terminal_idx];
            satis = m_monitor_tp[ap_idx] * APConstants::MIN_SATISFACTION_THRESHOLD / needTp;
        } else {
            // RTT指標
            double needRtt = traffic_request[terminal_idx];  
            satis = needRtt / m_monitor_rtt[ap_idx];
    }

    return satis;
}

// ランダムにAPを割り当てるダミー処理
void APselection::random_assignment_test() {
    std::cout << "=== APselection::random_assignment_test() ===" << std::endl;

    // 割り当て結果用の一次元配列（initial_AP に合わせて 1 ベースで保持）
    std::vector<int> assignment;
    assignment.reserve(terms);

    // AP0〜2の範囲でランダム生成（AP数が3未満の場合は存在するAPの範囲に制限）
    const int maxApIndex = std::min(aps - 1, 2);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, maxApIndex);

    for (int i = 0; i < terms; ++i) {
        int apIndex = dist(gen);
        assignment.push_back(apIndex + 1); // 1 ベースで格納
    }

    std::cout << "割り当て結果（端末ID:AP番号）: ";
    for (size_t i = 0; i < assignment.size(); ++i) {
        std::cout << i + 1 << ":" << assignment[i];
        if (i + 1 != assignment.size()) {
            std::cout << " ";
        }
    }
    std::cout << std::endl;
}
}
