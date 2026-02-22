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
#include <ctime>
#include <algorithm>
#include <cmath>
#include <climits>
#include <fstream>
#include <iomanip>
#include <random>

namespace ns3{

extern int G_nth;

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
    m_terminal_tp.assign(terms, 0.0);
    m_has_terminal_tp.assign(terms, false);

    m_lastAssignment = initial_AP;
    m_cycleIndex = 1;
    m_masterLogInitialized = false;

    {
        std::time_t t = std::time(nullptr);
        std::tm *tm_local = std::localtime(&t);
        char dateBuf[32];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d_%H%M%S", tm_local);
        m_masterLogPath = "OUTPUT/master_log_" + std::to_string(terms) + "_" + dateBuf + ".csv";
    }
    std::cout << "master_log path: " << m_masterLogPath << std::endl;

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

    // デバッグ用（各端末のTPの表示）
    std::cout << "Monitor data stored: AP=" << apNo << ", RTT=" << d
              << "ms, AVG=" << m_monitor_rtt[apNo] << "ms" << std::endl;
}

void APselection::tmain(){
    NS_LOG_FUNCTION(this);
    std::cout << "=== APselection::tmain() START (cycle " << m_cycleIndex << ") ===" << std::endl;
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
    // master_log.csv に割り当て前の状態を記録
    WriteMasterLog();

    if (G_nth == 5)
    {
        policy_assignment1();
    }
    else
    {
        random_assignment();
    }

    // 現在までのサイクルの調和平均を即時表示
    PrintCycleHarmonicMeans();
    
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
        else if(initial_app.at(i) == static_cast<int>(APConstants::AppType::ONLINE_GAME)){
            traffic_request.push_back(APConstants::ONLINE_GAME_REQUIRED_RTT);  //オンラインゲーム（RTT）
        }
    }
    
}

void APselection::cal_initial_harmonic_mean(){
    double initial_harmonic_mean = 0.0;
    double sum_inverse_satisfaction = 0.0;

    for(int i=0;i<terms;i++){
        int term_index = i;
        int ap_index = initial_AP[term_index] - 1;
        double satisfaction = calculate_satisfaction(term_index, ap_index);
        double effectiveSatisfaction =
            std::max(satisfaction, APConstants::MIN_SATISFACTION_THRESHOLD);
        sum_inverse_satisfaction += 1.0 / effectiveSatisfaction;
    }
    if (sum_inverse_satisfaction <= 0.0)
    {
        std::cout << "割り当て前端末満足度の調和平均：計測値が不足しているため 0 として扱います" << std::endl;
        initial_harmonic_mean = 0.0;
    }
    else
    {
        initial_harmonic_mean = terms / sum_inverse_satisfaction;
    }
    std::cout << std::fixed << std::setprecision(6)
              << "端末満足度の調和平均：" << initial_harmonic_mean << std::endl;
    RecordHarmonicMean(initial_harmonic_mean);
}

// 端末満足度計算（共通ロジック）
double APselection::calculate_satisfaction(int terminal_idx, int ap_idx) {
    if (ap_idx < 0 ||
        ap_idx >= static_cast<int>(m_monitor_tp.size()) ||
        ap_idx >= static_cast<int>(m_monitor_rtt.size()))
    {
        return APConstants::MIN_SATISFACTION_THRESHOLD;
    }

    int appNum = initial_app[terminal_idx];
    double satis = 0;

    if(appNum == static_cast<int>(APConstants::AppType::BROWSER) ||
        appNum == static_cast<int>(APConstants::AppType::VIDEO)) {
            // TP指標: 端末自身のTPを優先、なければモニターTPにフォールバック
            double needTp = traffic_request[terminal_idx];
            double measuredTpMbps = 0.0;
            if (terminal_idx >= 0 &&
                terminal_idx < static_cast<int>(m_has_terminal_tp.size()) &&
                m_has_terminal_tp[terminal_idx] &&
                terminal_idx < static_cast<int>(m_terminal_tp.size()) &&
                m_terminal_tp[terminal_idx] > 0.0)
            {
                measuredTpMbps = m_terminal_tp[terminal_idx] * APConstants::BPS_TO_MBPS;
            }
            else if (m_has_tp[ap_idx] && m_monitor_tp[ap_idx] > 0.0)
            {
                uint32_t terminalsOnAp = 0;
                const int apNo = ap_idx + 1; // initial_AP は 1 ベース
                for (size_t i = 0; i < initial_AP.size(); ++i)
                {
                    if (initial_AP[i] == apNo)
                    {
                        ++terminalsOnAp;
                    }
                }
                if (terminalsOnAp == 0)
                {
                    terminalsOnAp = 1;
                }
                // AP全体TPを接続台数で按分した推定値を使用
                measuredTpMbps =
                    (m_monitor_tp[ap_idx] / static_cast<double>(terminalsOnAp)) *
                    APConstants::BPS_TO_MBPS;
            }
            if (measuredTpMbps <= 0.0)
            {
                measuredTpMbps = APConstants::MIN_SATISFACTION_THRESHOLD;
            }
            satis = measuredTpMbps / needTp;
        } else {
            // RTT指標
            double needRtt = traffic_request[terminal_idx];  
            double measuredRtt = m_has_rtt[ap_idx] ? m_monitor_rtt[ap_idx] : needRtt;
            if (measuredRtt <= 0.0)
            {
                measuredRtt = needRtt;
            }
            satis = needRtt / measuredRtt;
    }

    return satis;
}

// ランダムにAPを割り当てるダミー処理
void APselection::random_assignment() {
    std::cout << "=== APselection::random_assignment() ===" << std::endl;

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

    std::cout << "割り当て結果: [";
    for (size_t i = 0; i < assignment.size(); ++i) {
        std::cout << assignment[i];
        if (i + 1 != assignment.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;

    m_lastAssignment = assignment;
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

// 方策による割り当て（3分類し、不満足端末のみ再割り当て）
void APselection::policy_assignment1() {
    std::cout << "=== APselection::policy_assignment1() ===" << std::endl;

    // master_log を読み込み、現在サイクルの端末満足度を取得
    const std::string filePath = m_masterLogPath;
    std::ifstream ifs(filePath);
    if (!ifs.is_open())
    {
        std::cerr << "policy_assignment1: " << filePath << " を開けません" << std::endl;
        return;
    }

    // 端末ごとの満足度を格納（0ベースインデックス）
    std::vector<double> satisfactionPerTerm(terms, -1.0);
    std::vector<bool> hasSatisfaction(terms, false);

    std::string line;
    // ヘッダー行をスキップ
    std::getline(ifs, line);

    while (std::getline(ifs, line))
    {
        std::vector<std::string> cols = splitString(line, ",");
        if (cols.size() < 7)
        {
            continue;
        }
        // CSVカラム: サイクル, 端末, AP, アプリ, ネットワーク指標, 通信品質, 端末満足度
        int cycle = std::stoi(cols[0]);
        if (cycle != static_cast<int>(m_cycleIndex))
        {
            continue;
        }
        int termNo = std::stoi(cols[1]);    // 1ベース
        double satisfaction = std::stod(cols[6]);
        int termIdx = termNo - 1;           // 0ベースに変換
        if (termIdx >= 0 && termIdx < terms)
        {
            satisfactionPerTerm[termIdx] = satisfaction;
            hasSatisfaction[termIdx] = true;
        }
    }
    ifs.close();

    // 当該サイクル行が欠損していた端末は、現在のTP/RTTとアプリ情報から満足度を再計算
    for (int i = 0; i < terms; ++i)
    {
        if (hasSatisfaction[i])
        {
            continue;
        }
        int apIdx = initial_AP[i] - 1;
        satisfactionPerTerm[i] = calculate_satisfaction(i, apIdx);
    }

    // 割り当て結果（初期値は現在のAP）
    std::vector<int> assignment = initial_AP;

    std::random_device rd;
    std::mt19937 gen(rd());

    int superSatisfiedCount = 0;
    int satisfiedCount = 0;
    int unsatisfiedCount = 0;
    for (int i = 0; i < terms; ++i)
    {
        const double satisfaction = satisfactionPerTerm[i];

        if (satisfaction > 1.2)
        {
            superSatisfiedCount++;
            continue;
        }

        if (satisfaction >= 0.8)
        {
            satisfiedCount++;
            continue;
        }

        // 不満足端末（<0.8）のみ切り替え対象
        unsatisfiedCount++;
        int currentAp = initial_AP[i]; // 1ベース
        std::vector<int> candidates;
        for (int ap = 1; ap <= aps; ++ap)
        {
            if (ap != currentAp)
            {
                candidates.push_back(ap);
            }
        }
        if (!candidates.empty())
        {
            std::uniform_int_distribution<> dist(0, static_cast<int>(candidates.size()) - 1);
            assignment[i] = candidates[dist(gen)];
        }
    }

    std::cout << "超満足端末数: " << superSatisfiedCount << " / " << terms << std::endl;
    std::cout << "満足端末数: " << satisfiedCount << " / " << terms << std::endl;
    std::cout << "不満足端末数: " << unsatisfiedCount << " / " << terms << std::endl;
    std::cout << "割り当て結果: [";
    for (size_t i = 0; i < assignment.size(); ++i) {
        std::cout << assignment[i];
        if (i + 1 != assignment.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;

    m_lastAssignment = assignment;
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

void APselection::StartNewCycle(uint32_t cycleIndex)
{
    std::cout << "=== APselection::StartNewCycle(" << cycleIndex << ") ===" << std::endl;
    m_cycleIndex = cycleIndex;
    if (!m_lastAssignment.empty())
    {
        initial_AP = m_lastAssignment;
    }
    ResetMonitorStats();
}

void APselection::SetHandoverCallback(std::function<void(const std::vector<int>&)> cb)
{
    m_handoverCallback = std::move(cb);
}

void APselection::ResetMonitorStats()
{
    std::fill(m_monitor_rtt.begin(), m_monitor_rtt.end(), 0.0);
    std::fill(m_rtt_sum.begin(), m_rtt_sum.end(), 0.0);
    std::fill(m_rtt_count.begin(), m_rtt_count.end(), 0);
    std::fill(m_has_rtt.begin(), m_has_rtt.end(), false);
    std::fill(m_monitor_tp.begin(), m_monitor_tp.end(), 0.0);
    std::fill(m_has_tp.begin(), m_has_tp.end(), false);
    std::fill(m_terminal_tp.begin(), m_terminal_tp.end(), 0.0);
    std::fill(m_has_terminal_tp.begin(), m_has_terminal_tp.end(), false);
}

void APselection::setTerminalTp(int termIdx, double tpBps)
{
    if (termIdx < 0 || termIdx >= static_cast<int>(m_terminal_tp.size()))
    {
        return;
    }
    m_terminal_tp[termIdx] = tpBps;
    m_has_terminal_tp[termIdx] = true;

    bool isTpApp = false;
    if (termIdx < static_cast<int>(initial_app.size()))
    {
        const int app = initial_app[termIdx];
        isTpApp =
            (app == static_cast<int>(APConstants::AppType::BROWSER)) ||
            (app == static_cast<int>(APConstants::AppType::VIDEO));
    }

    //デバッグ用（各端末のTPの表示）
    // if (isTpApp)
    // {
    //     std::cout << "Terminal TP stored: term=" << termIdx
    //               << ", TP=" << (tpBps * APConstants::BPS_TO_MBPS) << "Mbps" << std::endl;
    // }
}

void APselection::RecordHarmonicMean(double value)
{
    // サイクル順に積み上げる
    m_cycleHarmonicMeans.push_back(value);
}

void APselection::PrintCycleHarmonicMeans()
{
    if (m_cycleHarmonicMeans.empty())
    {
        std::cout << "[HarmonicMean] 記録された値がありません" << std::endl;
        return;
    }

    std::cout << "=== サイクル別 調和平均一覧 ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < m_cycleHarmonicMeans.size(); ++i)
    {
        std::cout << "  Cycle " << (i + 1) << ": " << m_cycleHarmonicMeans[i] << std::endl;
    }
}

void APselection::WriteMasterLog()
{
    const std::string filePath = m_masterLogPath;

    // 初回呼び出し時にヘッダーを書き込む
    if (!m_masterLogInitialized)
    {
        std::ofstream ofs(filePath, std::ios::trunc);
        ofs << "サイクル, 端末, AP, アプリ, ネットワーク指標, 通信品質, 端末満足度" << std::endl;
        ofs.close();
        m_masterLogInitialized = true;
    }

    std::ofstream ofs(filePath, std::ios::app);
    ofs << std::fixed << std::setprecision(2);

    for (int i = 0; i < terms; ++i)
    {
        int ap_1based = initial_AP[i];
        int ap_idx = ap_1based - 1;
        int appNum = initial_app[i];

        // TP系(1,2) か RTT系(3,4) か判定
        bool isTpApp = (appNum == static_cast<int>(APConstants::AppType::BROWSER) ||
                        appNum == static_cast<int>(APConstants::AppType::VIDEO));

        std::string metricLabel = isTpApp ? "TP" : "RTT";

        // 通信品質の値を算出
        std::string qualityStr;
        if (isTpApp)
        {
            double measuredTpMbps = 0.0;
            if (i >= 0 &&
                i < static_cast<int>(m_has_terminal_tp.size()) &&
                m_has_terminal_tp[i] &&
                m_terminal_tp[i] > 0.0)
            {
                measuredTpMbps = m_terminal_tp[i] * APConstants::BPS_TO_MBPS;
            }
            else if (ap_idx >= 0 &&
                     ap_idx < static_cast<int>(m_has_tp.size()) &&
                     m_has_tp[ap_idx] &&
                     m_monitor_tp[ap_idx] > 0.0)
            {
                uint32_t terminalsOnAp = 0;
                for (size_t j = 0; j < initial_AP.size(); ++j)
                {
                    if (initial_AP[j] == ap_1based)
                    {
                        ++terminalsOnAp;
                    }
                }
                if (terminalsOnAp == 0) terminalsOnAp = 1;
                measuredTpMbps = (m_monitor_tp[ap_idx] / static_cast<double>(terminalsOnAp)) *
                                 APConstants::BPS_TO_MBPS;
            }
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << measuredTpMbps << "Mbps";
            qualityStr = ss.str();
        }
        else
        {
            double rttVal = (ap_idx >= 0 &&
                             ap_idx < static_cast<int>(m_has_rtt.size()) &&
                             m_has_rtt[ap_idx])
                                ? m_monitor_rtt[ap_idx]
                                : 0.0;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << rttVal << "ms";
            qualityStr = ss.str();
        }

        double satisfaction = calculate_satisfaction(i, ap_idx);

        ofs << m_cycleIndex << ", "
            << (i + 1) << ", "
            << ap_1based << ", "
            << appNum << ", "
            << metricLabel << ", "
            << qualityStr << ", "
            << satisfaction << std::endl;
    }

    ofs.close();
}
}
