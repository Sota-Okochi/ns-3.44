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
    // ファイル出力は無効化（ターミナル出力のみ）
	std::cout << "APselectionコンストラクタ発動" << std::endl;
}

APselection::~APselection(){
    
}

void APselection::init(const ApSelectionInput& input){
    NS_LOG_FUNCTION(this);

    m_input = input;
    m_isInitialized = true;

    m_APNum = input.baseStations;
    aps = m_APNum;
    m_termNum = input.terminals;
    terms = m_termNum;
    m_capa = input.capacities;
    if (m_capa.size() < static_cast<size_t>(aps))
    {
        m_capa.resize(aps, APConstants::DEFAULT_AP_CAPACITY);
    }

    m_initialRtt = input.initialRtt;
    if (m_initialRtt.empty())
    {
        m_initialRtt.assign(aps, 50.0);
    }
    else if (m_initialRtt.size() < static_cast<size_t>(aps))
    {
        m_initialRtt.resize(aps, m_initialRtt.back());
    }

    m_use_appli = input.useAppli;
    if (m_use_appli.size() < static_cast<size_t>(terms))
    {
        m_use_appli.resize(terms, static_cast<int>(APConstants::AppType::BROWSER));
    }

    m_initialAp = input.initialAp;
    if (m_initialAp.size() < static_cast<size_t>(terms))
    {
        m_initialAp.resize(terms, 1); // デフォルトはAP1へ接続扱い
    }

    incRTT.assign(aps, APConstants::RTT_INCREASE_PER_TERMINAL);
    incTP.assign(aps, APConstants::TP_DECREASE_PER_TERMINAL);

    init_rtt.assign(aps, 0.0);
    init_tp.assign(aps, 0.0);
    m_monitor_rtt.assign(aps, 0.0);
    m_rtt_sum.assign(aps, 0.0);
    m_rtt_count.assign(aps, 0);
    m_has_rtt.assign(aps, false);
    m_monitor_tp.assign(aps, 0.0);
    m_has_tp.assign(aps, false);

    std::cout << "=== 初期設定値 ===" << std::endl;
    constexpr double KBPS_TO_MBPS = (1024.0 * 8.0) / 1e6;
    for(int i = 0; i < aps; i++){
        double tpKiloBytesPerSec = APConstants::INITIAL_TP_MULTIPLIER[0] / m_initialRtt[i];
        double tpMbps = tpKiloBytesPerSec * KBPS_TO_MBPS;
        std::cout << "AP:" << i << "\tRTT:" << m_initialRtt[i] << "ms\tTP:" << tpMbps << "Mbps" << std::endl;
    }

    if (!m_initialAp.empty() && !m_use_appli.empty())
    {
        std::cout << "=== 端末初期設定 ===" << std::endl;
        for (int i = 0; i < m_termNum; ++i)
        {
            std::cout << "Term:" << i
                      << "\tInitAP:" << m_initialAp[i] - 1
                      << "\tApp:" << m_use_appli[i] << std::endl;
        }
    }

    std::cout << "=== APselection::init() completed ===" << std::endl;
    std::cout << "APs: " << m_APNum << ", Terms: " << m_termNum << std::endl;
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
    std::cout << "m_APNum: " << m_APNum << std::endl;
    std::cout << "m_monitor_rtt size: " << m_monitor_rtt.size() << std::endl;

    constexpr double KBPS_TO_MBPS = (1024.0 * 8.0) / 1e6;
    constexpr double BPS_TO_MBPS = 1e-6;
    
    // 実測RTTデータから各APの平均RTTとTP値を計算
    m_link_rtt.resize(m_APNum);
    init_rtt.clear();
    init_rtt.resize(m_APNum);
    init_tp.clear();
    init_tp.resize(m_APNum);
    
    for(int i=0; i<m_APNum; i++){
        if(m_has_rtt[i]){
            double ave_rtt = m_monitor_rtt[i];
            m_link_rtt[i] = ave_rtt;
            init_rtt[i] = ave_rtt;
            double tpValue = APConstants::INITIAL_TP_MULTIPLIER[0] / ave_rtt;
            double tpDisplayMbps = tpValue * KBPS_TO_MBPS;
            if (m_has_tp[i])
            {
                tpValue = m_monitor_tp[i] / 1024.0;
                tpDisplayMbps = m_monitor_tp[i] * BPS_TO_MBPS;
            }
            init_tp[i] = tpValue;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "AP:" << i << "\tRTT:" << ave_rtt << "ms"
                << "\tTP:" << tpDisplayMbps << "Mbps" << std::endl;

        } else {
            // データがない場合は設定ファイルの値を使用
            double default_rtt = 50.0; // デフォルト値
            if (!m_initialRtt.empty())
            {
                if (static_cast<size_t>(i) < m_initialRtt.size())
                {
                    default_rtt = m_initialRtt[i];
                }
                else
                {
                    default_rtt = m_initialRtt.back();
                }
            }
            
            m_link_rtt[i] = default_rtt;
            init_rtt[i] = default_rtt;
            double tpValue = APConstants::INITIAL_TP_MULTIPLIER[0] / default_rtt;
            double tpDisplayMbps = tpValue * KBPS_TO_MBPS;
            if (m_has_tp[i])
            {
                tpValue = m_monitor_tp[i] / 1024.0;
                tpDisplayMbps = m_monitor_tp[i] * BPS_TO_MBPS;
            }
            init_tp[i] = tpValue;
            
            std::cout << "AP:" << i << "\tNo data - using config RTT: " << default_rtt << "ms"
                    << "\tTP:" << tpDisplayMbps << "Mbps";
            if (m_has_tp[i])
            {
                std::cout << " (measured)";
            }
            std::cout << std::endl;
        }
    }

    // アプリ種別に応じた必要性能値を最新化
    cal_need_rt();

    // 割り当て前端末満足度の調和平均の計算
    cal_harmonic_mean();
    
    std::cout << "=== APselection::tmain() END ===" << std::endl;
}


//必要RTT, TPの算出
void APselection::cal_need_rt(){
    m_need.clear();
    m_need.reserve(terms);

    for(int i=0;i<terms;i++){
        if(m_use_appli.at(i) == static_cast<int>(APConstants::AppType::BROWSER)){
            m_need.push_back(APConstants::BROWSER_REQUIRED_TP);   //ブラウザ（TP）
        }
        else if(m_use_appli.at(i) == static_cast<int>(APConstants::AppType::VIDEO)){
            m_need.push_back(APConstants::VIDEO_REQUIRED_TP);  //動画ストリーミング（TP）
        }
        else if(m_use_appli.at(i) == static_cast<int>(APConstants::AppType::VOICE_CALL)){
            m_need.push_back(APConstants::VOICE_CALL_REQUIRED_RTT);  //通話アプリケーション（RTT）
        }
        else if(m_use_appli.at(i) == static_cast<int>(APConstants::AppType::LIVE_STREAM)){
            m_need.push_back(APConstants::LIVE_STREAM_REQUIRED_RTT);  //ライブ配信（RTT）
        }
    }
    
}

void APselection::cal_harmonic_mean(){
    double harmonic_mean = 0.0;
    double sum_satisfaction = 0.0;

    for(int i=0;i<terms;i++){
        int term_index = i;
        int ap_index = m_initialAp[term_index] - 1;
        double satisfaction = calculate_satisfaction(term_index, ap_index);
        sum_satisfaction += satisfaction;
    }
    harmonic_mean = terms / sum_satisfaction;
    std::cout << std::fixed << std::setprecision(6)
              << "割り当て前端末満足度の調和平均：" << harmonic_mean << std::endl;
}

// 端末の満足度計算（共通ロジック）
double APselection::calculate_satisfaction(int terminal_idx, int ap_idx) {
    int appNum = m_use_appli[terminal_idx];
    double satis = 0;

    if(appNum == static_cast<int>(APConstants::AppType::BROWSER) || 
        appNum == static_cast<int>(APConstants::AppType::VIDEO)) {
            // TP指標
            double needTp = m_need[terminal_idx];
            satis = m_monitor_tp[ap_idx] * APConstants::MIN_SATISFACTION_THRESHOLD / needTp;
        } else {
            // RTT指標
            double needRtt = m_need[terminal_idx];  
            satis = needRtt / m_monitor_rtt[ap_idx];
    }

    return satis;
}
}
