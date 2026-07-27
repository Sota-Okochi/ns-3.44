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
#include "ns3/system-path.h"

#include "ns3/APselection.h"

#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <climits>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <dirent.h>
#include <sys/stat.h>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("APselection");

namespace {

constexpr size_t kLogisticFeatureCount = 11;

std::string TrimCsvField(const std::string& str)
{
    const auto begin = str.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(begin, end - begin + 1);
}

int FindColumnIndex(const std::vector<std::string>& header, const std::string& name)
{
    for (size_t i = 0; i < header.size(); ++i)
    {
        if (TrimCsvField(header[i]) == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool IsDirectoryPath(const std::string& path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool HasCsvSuffix(const std::string& path)
{
    return path.size() >= 4 && path.substr(path.size() - 4) == ".csv";
}

bool ResolveLatestCsvInDirectory(const std::string& dirPath, std::string& resolvedPath)
{
    DIR* dir = opendir(dirPath.c_str());
    if (dir == nullptr)
    {
        return false;
    }

    bool found = false;
    time_t bestMtime = 0;
    std::string bestName;
    const std::string prefix = (dirPath.empty() || dirPath.back() == '/') ? dirPath : dirPath + "/";

    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || !HasCsvSuffix(name))
        {
            continue;
        }

        const std::string candidate = prefix + name;
        struct stat st;
        if (stat(candidate.c_str(), &st) != 0 || S_ISDIR(st.st_mode))
        {
            continue;
        }

        if (!found || st.st_mtime > bestMtime ||
            (st.st_mtime == bestMtime && name > bestName))
        {
            found = true;
            bestMtime = st.st_mtime;
            bestName = name;
            resolvedPath = candidate;
        }
    }

    closedir(dir);
    return found;
}

bool ExtractJsonArrayBody(const std::string& content,
                          const std::string& key,
                          std::string& body)
{
    const std::string quotedKey = "\"" + key + "\"";
    const size_t keyPos = content.find(quotedKey);
    if (keyPos == std::string::npos)
    {
        return false;
    }
    const size_t begin = content.find('[', keyPos + quotedKey.size());
    if (begin == std::string::npos)
    {
        return false;
    }

    int depth = 0;
    for (size_t pos = begin; pos < content.size(); ++pos)
    {
        if (content[pos] == '[')
        {
            ++depth;
        }
        else if (content[pos] == ']')
        {
            --depth;
            if (depth == 0)
            {
                body = content.substr(begin + 1, pos - begin - 1);
                return true;
            }
        }
    }
    return false;
}

std::vector<double> ParseJsonNumbers(const std::string& body)
{
    static const std::regex numberPattern(
        R"([-+]?(?:[0-9]*\.[0-9]+|[0-9]+\.?)(?:[eE][-+]?[0-9]+)?)");
    std::vector<double> values;
    for (std::sregex_iterator it(body.begin(), body.end(), numberPattern), end;
         it != end;
         ++it)
    {
        values.push_back(std::stod(it->str()));
    }
    return values;
}

bool ExtractJsonNumbers(const std::string& content,
                        const std::string& key,
                        std::vector<double>& values)
{
    std::string body;
    if (!ExtractJsonArrayBody(content, key, body))
    {
        return false;
    }
    values = ParseJsonNumbers(body);
    return true;
}

std::vector<std::string> ParseJsonStrings(const std::string& body)
{
    static const std::regex stringPattern(R"json("([^"]*)")json");
    std::vector<std::string> values;
    for (std::sregex_iterator it(body.begin(), body.end(), stringPattern), end;
         it != end;
         ++it)
    {
        values.push_back((*it)[1].str());
    }
    return values;
}

} // namespace

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
	    m_logisticModelPath =
        std::string(PROJECT_SOURCE_PATH) +
        "/baseline_methods/data/models/logistic_term80_runs50_seed001.json";
}

APselection::~APselection(){
}

void APselection::init(const ApSelectionInput& input){
    NS_LOG_FUNCTION(this);



    // --------APselectionのメンバ変数に受け渡し-------------
    aps = input.baseStations; //基地局数
    terms = input.terminals; // 端末数
    initial_app = input.useAppli; // 各端末の初期アプリ番号
    initial_AP = input.initialAp; // 各端末の初期接続先
    m_assignmentMethod = input.assignmentMethod;
    m_dqnActionCsvPath = input.dqnActionCsvPath;
    m_rngSeed = input.rngSeed;
    m_outputDir = input.outputDir;
    if (!m_outputDir.empty() && m_outputDir.back() != '/')
    {
        m_outputDir += "/";
    }
    SystemPath::MakeDirectories(m_outputDir);


    m_monitor_rtt.assign(aps, 0.0);
    m_rtt_sum.assign(aps, 0.0);
    m_rtt_count.assign(aps, 0);
    m_has_rtt.assign(aps, false);
    m_monitor_ip.assign(aps, "");
    m_terminal_tp.assign(terms, 0.0);
    m_has_terminal_tp.assign(terms, false);

    m_lastAssignment = initial_AP;
    m_assignmentBeforeAction = initial_AP;
    m_assignmentAfterAction = initial_AP;
    m_hBeforeAction = 0.0;
    m_hAfterEstimated = 0.0;
    m_lastReward = 0.0;
    m_switchCycle.assign(terms, 0);
    m_handoverGraceCycles = input.handoverGraceCycles;
    m_cycleIndex = 1;
    m_masterLogInitialized = false;
    m_decisionLogInitialized = false;
    if (m_assignmentMethod == "logistic")
    {
        m_logisticModelLoaded = LoadLogisticModel();
    }
    if ((m_assignmentMethod == "dqn" || m_assignmentMethod == "multi_dqn") && !LoadDqnActions())
    {
        NS_FATAL_ERROR("Failed to load DQN action CSV: " << m_dqnActionCsvPath);
    }

    {
        std::time_t t = std::time(nullptr);
        std::tm *tm_local = std::localtime(&t);
        char dateBuf[32];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d_%H%M%S", tm_local);
        m_masterLogPath = m_outputDir + "master_log_" + std::to_string(m_rngSeed) + "_" +
                          dateBuf + ".csv";
        m_decisionLogPath = m_outputDir + "decision_log_" + std::to_string(m_rngSeed) + "_" +
                            dateBuf + ".csv";
    }
    std::cout << "ログパス: " << m_masterLogPath << std::endl;
    if (m_assignmentMethod == "multi_dqn")
    {
        std::cout << "意思決定ログパス: " << m_decisionLogPath << std::endl;
    }
    std::cout << "割り当て手法(method): " << m_assignmentMethod << std::endl;
    if (m_assignmentMethod == "multi_greedy")
    {
        std::cout << "[MultiGreedy] 1サイクルあたりの最大切り替え端末数: "
                  << m_MaxSwitches << " 台" << std::endl;
    }
    if (m_assignmentMethod == "multi_offload")
    {
        std::cout << "[MultiOffload] 1サイクルあたりの最大offload端末数: "
                  << m_MaxSwitches << " 台" << std::endl;
    }
    if (m_assignmentMethod == "multi_dqn")
    {
        std::cout << "[MultiDQN] 1サイクルあたりの最大切り替え端末数: "
                  << m_MaxSwitches << " 台" << std::endl;
    }

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

    std::cout << "端末数: " << terms << std::endl;
}

void APselection::setData(std::string senderIpAddress, std::string recvMessage){
    NS_LOG_FUNCTION(this);

    //送られたRTTデータから基地局ごとにRTT平均値を求める ここでは基地局ごとにpush_back
    std::vector<std::string> ret2 = splitString(recvMessage, ",");
    if( ret2.size() < 2 || ret2.size() > 3 ) {
        std::cout << "[Monitor][WARN] Invalid message format: " << recvMessage << std::endl;
        return;
    }
    std::stringstream ss2(ret2[1]);
    double d; ss2 >> d;

    // AP番号をメッセージ本文から取得する。
    // "MONITOR_AP{id},{rtt}" 形式のメッセージに含まれるidを使用する。
    // IPの第3オクテットは LTE(6.0.0.x) が常に0になるなど、RATによって誤るため使わない。
    int apNo = -1;
    const std::string kPrefix = "MONITOR_AP";
    if (ret2[0].find(kPrefix) == 0)
    {
        std::string apIdStr = ret2[0].substr(kPrefix.length());
        std::stringstream apSs(apIdStr);
        apSs >> apNo;
    }
    else
    {
        // MONITOR_AP形式でない場合は従来どおりIPの第3オクテットを使う
        std::vector<std::string> ret = splitString(senderIpAddress, ".");
        if (ret.size() < 3)
        {
            std::cout << "[Monitor][WARN] Invalid IP address format: " << senderIpAddress << std::endl;
            return;
        }
        std::stringstream ss(ret[2]);
        ss >> apNo;
    }

    if(apNo < 0 || static_cast<size_t>(apNo) >= m_monitor_rtt.size()) {
        std::cout << "[Monitor][WARN] Invalid AP index: " << apNo << std::endl;
        return;
    }

    m_rtt_sum[apNo] += d;
    m_rtt_count[apNo] += 1;
    m_monitor_rtt[apNo] = m_rtt_sum[apNo] / static_cast<double>(m_rtt_count[apNo]);
    m_has_rtt[apNo] = true;
    if (static_cast<size_t>(apNo) < m_monitor_ip.size())
    {
        m_monitor_ip[apNo] = senderIpAddress;
    }
}

void APselection::tmain(){
    NS_LOG_FUNCTION(this);
    std::cout << "=== APselection::tmain() START (cycle " << m_cycleIndex << ") ===" << std::endl;

    PrintMonitorRttReport();

    // アプリ種別の必要TP, RTT
    cal_traffic_request();
    // 割り当て前端末満足度の調和平均の計算
    cal_initial_harmonic_mean();
    {
        const double hBefore =
            m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                         : m_cycleHarmonicMeans.back();
        PrepareDecisionLogState(initial_AP, initial_AP, hBefore, hBefore);
    }

    if (m_totalCycles == 0 || m_cycleIndex < m_totalCycles)
    {
        if (m_assignmentMethod == "random")
        {
            random_assignment();
        }
        else if (m_assignmentMethod == "all5g")
        {
            all5g_assignment();
        }
        else if (m_assignmentMethod == "rulebase")
        {
            rulebase_assignment();
        }
        else if (m_assignmentMethod == "greedy")
        {
            greedy_assignment();
        }
        else if (m_assignmentMethod == "multi_greedy")
        {
            multi_greedy_assignment();
        }
        else if (m_assignmentMethod == "multi_offload")
        {
            multi_offload_assignment();
        }
        else if (m_assignmentMethod == "logistic")
        {
            logistic_assignment();
        }
        else if (m_assignmentMethod == "dqn")
        {
            dqn_assignment();
        }
        else if (m_assignmentMethod == "multi_dqn")
        {
            multi_dqn_assignment();
        }
        else if (m_assignmentMethod == "no_switch")
        {
            m_lastAssignment = initial_AP;
        }
        else
        {
            NS_FATAL_ERROR("Unknown assignment method: " << m_assignmentMethod);
        }
    }
    else
    {
        m_lastAssignment = initial_AP;
    }

    if (!m_lastAssignment.empty())
    {
        std::vector<int> optimizedCounts(aps, 0);
        for (int apNo : m_lastAssignment)
        {
            if (apNo >= 1 && apNo <= aps)
            {
                optimizedCounts[apNo - 1]++;
            }
        }

        std::cout << "=== Optimized terminal counts per AP ===" << std::endl;
        for (int ap = 0; ap < aps; ++ap)
        {
            std::cout << "AP" << (ap + 1) << ": " << optimizedCounts[ap] << " terminals" << std::endl;
        }
    }

    // master_log.csv に現在状態と、このサイクルで選択された行動を記録
    WriteMasterLog();

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
        std::cout << "割り当て前端末満足度の調和平均計測値が不足しているため 0 として扱います" << std::endl;
        initial_harmonic_mean = 0.0;
    }
    else
    {
        initial_harmonic_mean = terms / sum_inverse_satisfaction;
    }
    RecordHarmonicMean(initial_harmonic_mean);
}

// 端末満足度計算（共通ロジック）
double APselection::calculate_satisfaction(int terminal_idx, int ap_idx) {
    if (ap_idx < 0 ||
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
            if (measuredTpMbps <= 0.0)
            {
                // ハンドオーバ直後の猶予期間中は満足扱いにして調和平均の破綻と再切り替えループを防ぐ
                if (!m_switchCycle.empty() &&
                    terminal_idx < static_cast<int>(m_switchCycle.size()) &&
                    m_switchCycle[terminal_idx] > 0 &&
                    m_cycleIndex >= m_switchCycle[terminal_idx] &&
                    (m_cycleIndex - m_switchCycle[terminal_idx]) < m_handoverGraceCycles)
                {
                    return APConstants::GRACE_SATISFACTION;
                }
                // TP未計測: 切り替え候補になるようペナルティ値を返す（0.00 ログと調和平均破綻を防ぐ）
                return APConstants::SATISFACTION_FLOOR;
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

std::vector<int>
APselection::count_users_per_ap(const std::vector<int>& assignment) const
{
    std::vector<int> usersPerAp(aps, 0);
    for (int apNo : assignment)
    {
        const int apIdx = apNo - 1;
        if (apIdx >= 0 && apIdx < aps)
        {
            usersPerAp[apIdx] += 1;
        }
    }
    return usersPerAp;
}

double
APselection::estimate_rtt_ms_for_assignment(int terminal_idx, int ap_idx) const
{
    if (ap_idx < 0 || ap_idx >= static_cast<int>(m_monitor_rtt.size()))
    {
        return traffic_request[terminal_idx];
    }
    double measuredRtt = m_has_rtt[ap_idx] ? m_monitor_rtt[ap_idx] : traffic_request[terminal_idx];
    if (measuredRtt <= 0.0)
    {
        measuredRtt = traffic_request[terminal_idx];
    }
    return measuredRtt;
}

double
APselection::estimate_tp_mbps_for_assignment(int terminal_idx,
                                             int ap_idx,
                                             const std::vector<int>& assignment)
{
    if (terminal_idx < 0 || terminal_idx >= terms || ap_idx < 0 || ap_idx >= aps)
    {
        return 0.0;
    }

    const int appNum = initial_app[terminal_idx];

    // 第1候補: 候補APに接続している同一アプリ端末の平均TP
    double sameAppSum = 0.0;
    uint32_t sameAppCount = 0;
    double tpAppSum = 0.0;
    uint32_t tpAppCount = 0;
    for (int i = 0; i < terms; ++i)
    {
        if (i == terminal_idx)
        {
            continue;
        }
        // TPサンプルは「現在そのAPに接続して実測できた端末」から取る。
        // 候補割当で移動予定の端末の旧AP TPを、移動先APのサンプルとして混ぜない。
        if (i >= static_cast<int>(initial_AP.size()) || initial_AP[i] - 1 != ap_idx)
        {
            continue;
        }
        if (i >= static_cast<int>(m_has_terminal_tp.size()) ||
            !m_has_terminal_tp[i] ||
            i >= static_cast<int>(m_terminal_tp.size()) ||
            m_terminal_tp[i] <= 0.0)
        {
            continue;
        }

        const int peerApp = initial_app[i];
        const bool peerTpApp =
            (peerApp == static_cast<int>(APConstants::AppType::BROWSER) ||
             peerApp == static_cast<int>(APConstants::AppType::VIDEO));
        if (!peerTpApp)
        {
            continue;
        }

        const double peerTpMbps = m_terminal_tp[i] * APConstants::BPS_TO_MBPS;
        tpAppSum += peerTpMbps;
        tpAppCount += 1;

        if (peerApp == appNum)
        {
            sameAppSum += peerTpMbps;
            sameAppCount += 1;
        }
    }

    double estimatedTpMbps = 0.0;
    if (sameAppCount > 0)
    {
        estimatedTpMbps = sameAppSum / static_cast<double>(sameAppCount);
    }
    else if (tpAppCount > 0)
    {
        // 第2候補: 候補AP上のTP系アプリ平均
        estimatedTpMbps = tpAppSum / static_cast<double>(tpAppCount);
    }
    else if (terminal_idx < static_cast<int>(m_has_terminal_tp.size()) &&
             m_has_terminal_tp[terminal_idx] &&
             terminal_idx < static_cast<int>(m_terminal_tp.size()) &&
             m_terminal_tp[terminal_idx] > 0.0)
    {
        // 第3候補: 端末自身の現在TP
        estimatedTpMbps = m_terminal_tp[terminal_idx] * APConstants::BPS_TO_MBPS;
    }
    else
    {
        // 第4候補: satisfaction floor 相当のTP
        estimatedTpMbps = traffic_request[terminal_idx] * APConstants::SATISFACTION_FLOOR;
    }

    // 候補割当前後の接続台数比で負荷影響を簡易補正する。
    // 移動先APでは usersBefore/usersAfter < 1、移動元APでは > 1、変更なしAPでは 1 になる。
    const std::vector<int> usersBefore = count_users_per_ap(initial_AP);
    const std::vector<int> usersAfter = count_users_per_ap(assignment);
    const int usersBeforeAp = (ap_idx < static_cast<int>(usersBefore.size())) ? usersBefore[ap_idx] : 0;
    const int usersAfterAp = (ap_idx < static_cast<int>(usersAfter.size())) ? usersAfter[ap_idx] : 0;
    if (usersBeforeAp > 0 && usersAfterAp > 0)
    {
        estimatedTpMbps *= static_cast<double>(usersBeforeAp) /
                           static_cast<double>(usersAfterAp);
    }

    return estimatedTpMbps;
}

double
APselection::estimate_satisfaction_for_assignment(int terminal_idx,
                                                  int ap_idx,
                                                  const std::vector<int>& assignment)
{
    if (terminal_idx < 0 || terminal_idx >= terms ||
        ap_idx < 0 || ap_idx >= aps ||
        terminal_idx >= static_cast<int>(traffic_request.size()))
    {
        return APConstants::MIN_SATISFACTION_THRESHOLD;
    }

    const int appNum = initial_app[terminal_idx];
    if (appNum == static_cast<int>(APConstants::AppType::BROWSER) ||
        appNum == static_cast<int>(APConstants::AppType::VIDEO))
    {
        const double needTp = traffic_request[terminal_idx];
        if (needTp <= 0.0)
        {
            return APConstants::MIN_SATISFACTION_THRESHOLD;
        }
        return estimate_tp_mbps_for_assignment(terminal_idx, ap_idx, assignment) / needTp;
    }

    const double needRtt = traffic_request[terminal_idx];
    const double estimatedRtt = estimate_rtt_ms_for_assignment(terminal_idx, ap_idx);
    if (estimatedRtt <= 0.0)
    {
        return APConstants::MIN_SATISFACTION_THRESHOLD;
    }
    return needRtt / estimatedRtt;
}

double
APselection::calculate_harmonic_mean_for_assignment(const std::vector<int>& assignment)
{
    if (assignment.empty())
    {
        return 0.0;
    }

    double sumInverseSatisfaction = 0.0;
    int validTerms = 0;
    const int evalTerms = std::min(terms, static_cast<int>(assignment.size()));
    for (int i = 0; i < evalTerms; ++i)
    {
        const int apIdx = assignment[i] - 1;
        const double satisfaction =
            estimate_satisfaction_for_assignment(i, apIdx, assignment);
        const double effectiveSatisfaction =
            std::max(satisfaction, APConstants::MIN_SATISFACTION_THRESHOLD);
        sumInverseSatisfaction += 1.0 / effectiveSatisfaction;
        validTerms += 1;
    }

    if (validTerms == 0 || sumInverseSatisfaction <= 0.0)
    {
        return 0.0;
    }
    return static_cast<double>(validTerms) / sumInverseSatisfaction;
}

void
APselection::PrepareDecisionLogState(const std::vector<int>& assignmentBefore,
                                     const std::vector<int>& assignmentAfter,
                                     double hBefore,
                                     double hAfterEstimated)
{
    m_assignmentBeforeAction = assignmentBefore;
    m_assignmentAfterAction = assignmentAfter;
    m_hBeforeAction = hBefore;
    m_hAfterEstimated = hAfterEstimated;
    m_lastReward = m_hAfterEstimated - m_hBeforeAction;
}

// ランダムにAPを割り当てるダミー処理
void APselection::random_assignment() {
    std::cout << "=== APselection::random_assignment() ===" << std::endl;

    // 割り当て結果用の一次元配列（initial_AP に合わせて 1 ベースで保持）
    std::vector<int> assignment;
    assignment.reserve(terms);

    // AP0〜2の範囲でランダム生成（AP数が3未満の場合は存在するAPの範囲に制限）
    const int maxApIndex = std::min(aps - 1, 2);
    std::mt19937 gen(m_rngSeed + m_cycleIndex * 1000003u);
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
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

// 1回目の切り替え処理で全端末を5G基地局（内部AP番号: 1, AP0/NR）へ接続し、
// 以降のサイクルでは同じ割り当てを維持する baseline。
void APselection::all5g_assignment() {
    std::cout << "=== APselection::all5g_assignment() ===" << std::endl;

    std::vector<int> assignment(terms, 1);

    std::cout << "全端末を5G基地局へ割り当て、cycle="
              << m_cycleIndex << std::endl;

    m_lastAssignment = assignment;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

// ルールベース割り当て（3分類し、不満足端末のみランダム再割り当て）
void APselection::rulebase_assignment() {
    std::cout << "=== APselection::rulebase_assignment() ===" << std::endl;

    // 端末ごとの満足度を現在のTP/RTTとアプリ情報から算出する。
    // master_log の列変更に影響されないよう、CSV読み込みには依存しない。
    std::vector<double> satisfactionPerTerm(terms, -1.0);
    for (int i = 0; i < terms; ++i)
    {
        int apIdx = initial_AP[i] - 1;
        satisfactionPerTerm[i] = calculate_satisfaction(i, apIdx);
    }

    // 割り当て結果（初期値は現在のAP）
    std::vector<int> assignment = initial_AP;

    std::mt19937 gen(m_rngSeed + m_cycleIndex * 1000003u + 17u);

    int superSatisfiedCount = 0;
    int satisfiedCount = 0;
    int unsatisfiedCount = 0;
    for (int i = 0; i < terms; ++i)
    {
        const double satisfaction = satisfactionPerTerm[i];

        if (satisfaction > 1.0)
        {
            superSatisfiedCount++;
            continue;
        }

        if (satisfaction >= 0.5)
        {
            satisfiedCount++;
            continue;
        }

        // 不満足端末（<0.5）のみ切り替え対象
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
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

// greedy割り当て（不満足端末を候補に、調和平均の推定改善量が最大の1切り替えを採用）
void APselection::greedy_assignment()
{
    std::cout << "=== APselection::greedy_assignment() ===" << std::endl;

    constexpr double kUnsatisfiedThreshold = 0.8;
    constexpr double kMinImprovement = 1e-6;

    std::vector<int> assignment = initial_AP;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(assignment)
                                     : m_cycleHarmonicMeans.back();

    int bestTerm = -1;
    int bestAp = -1; // 1-based
    double bestHAfter = hBefore;
    double bestDelta = 0.0;

    for (int termIdx = 0; termIdx < terms; ++termIdx)
    {
        if (termIdx >= static_cast<int>(assignment.size()))
        {
            continue;
        }
        const int currentAp = assignment[termIdx]; // 1-based
        const double currentSatisfaction =
            calculate_satisfaction(termIdx, currentAp - 1);

        // 初期greedyでは、切り替え過多を避けるため不満足端末のみ候補にする。
        if (currentSatisfaction >= kUnsatisfiedThreshold)
        {
            continue;
        }

        for (int ap = 1; ap <= aps; ++ap)
        {
            if (ap == currentAp)
            {
                continue;
            }

            std::vector<int> candidate = assignment;
            candidate[termIdx] = ap;
            const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
            const double delta = hAfter - hBefore;
            if (delta > bestDelta)
            {
                bestDelta = delta;
                bestHAfter = hAfter;
                bestTerm = termIdx;
                bestAp = ap;
            }
        }
    }

    if (bestTerm >= 0 && bestAp >= 1 && bestDelta > kMinImprovement)
    {
        std::cout << "[Greedy] switch term=" << (bestTerm + 1)
                  << " AP" << assignment[bestTerm]
                  << " -> AP" << bestAp
                  << " h_before=" << hBefore
                  << " h_after_estimated=" << bestHAfter
                  << " reward=" << bestDelta << std::endl;
        assignment[bestTerm] = bestAp;
    }
    else
    {
        std::cout << "[Greedy] no improving switch found"
                  << " h_before=" << hBefore << std::endl;
        bestHAfter = hBefore;
    }

    m_lastAssignment = assignment;
    PrepareDecisionLogState(initial_AP, assignment, hBefore, bestHAfter);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

void APselection::multi_greedy_assignment()
{
    std::cout << "=== APselection::multi_greedy_assignment() ===" << std::endl;

    constexpr double kUnsatisfiedThreshold = 0.8;
    constexpr double kMinImprovement = 1e-6;

    std::vector<int> assignment = initial_AP;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(assignment)
                                     : m_cycleHarmonicMeans.back();

    double hCurrent = hBefore;
    uint32_t switchCount = 0;
    std::vector<int> selectedTerms;

    while (switchCount < m_MaxSwitches)
    {
        int bestTerm = -1;
        int bestAp = -1; // 1-based
        double bestHAfter = hCurrent;
        double bestDelta = 0.0;

        for (int termIdx = 0; termIdx < terms; ++termIdx)
        {
            if (termIdx >= static_cast<int>(assignment.size()))
            {
                continue;
            }
            if (std::find(selectedTerms.begin(), selectedTerms.end(), termIdx) !=
                selectedTerms.end())
            {
                continue;
            }

            const int currentAp = assignment[termIdx]; // 1-based
            if (currentAp < 1 || currentAp > aps)
            {
                continue;
            }

            const double currentSatisfaction =
                estimate_satisfaction_for_assignment(termIdx, currentAp - 1, assignment);

            // multi_greedyでは、不満足端末のみを切り替え候補にする。
            if (currentSatisfaction >= kUnsatisfiedThreshold)
            {
                continue;
            }

            for (int ap = 1; ap <= aps; ++ap)
            {
                if (ap == currentAp)
                {
                    continue;
                }

                std::vector<int> candidate = assignment;
                candidate[termIdx] = ap;
                const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
                const double delta = hAfter - hCurrent;
                if (delta > bestDelta)
                {
                    bestDelta = delta;
                    bestHAfter = hAfter;
                    bestTerm = termIdx;
                    bestAp = ap;
                }
            }
        }

        if (bestTerm < 0 || bestAp < 1 || bestDelta <= kMinImprovement)
        {
            break;
        }

        std::cout << "[MultiGreedy] step=" << (switchCount + 1)
                  << "/" << m_MaxSwitches
                  << " switch term=" << (bestTerm + 1)
                  << " AP" << assignment[bestTerm]
                  << " -> AP" << bestAp
                  << " h_before_step=" << hCurrent
                  << " h_after_step=" << bestHAfter
                  << " delta=" << bestDelta << std::endl;

        assignment[bestTerm] = bestAp;
        hCurrent = bestHAfter;
        selectedTerms.push_back(bestTerm);
        switchCount++;
    }

    if (switchCount == 0)
    {
        std::cout << "[MultiGreedy] no improving switch found"
                  << " h_before=" << hBefore
                  << " MaxSwitches=" << m_MaxSwitches << std::endl;
    }
    else
    {
        std::cout << "[MultiGreedy] selected_switches=" << switchCount
                  << " MaxSwitches=" << m_MaxSwitches
                  << " h_before=" << hBefore
                  << " h_after_estimated=" << hCurrent
                  << " reward=" << (hCurrent - hBefore) << std::endl;
    }

    m_lastAssignment = assignment;
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hCurrent);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

void APselection::multi_offload_assignment()
{
    std::cout << "=== APselection::multi_offload_assignment() ===" << std::endl;

    constexpr double kMinImprovement = 1e-6;
    // multi_offload は「不満足UEを直接救う」のではなく、
    // 混雑APから移してもQoEが壊れにくいUEを逃がしてリソースを空ける方策。
    // TP/RTT推定には揺らぎがあるため、対象UE自身の満足度低下は0.1まで候補として許容する。
    constexpr double kSatisfactionDropTolerance = 0.1;
    // offload対象は、移動元で既にQoEに余裕があるUEに限定する。
    // 低満足UEを直接救う探索に寄せると multi_greedy と役割が重なるため。
    constexpr double kOffloadableSourceThreshold = 0.8;
    // 移動後もこの値以上なら「満足維持」とみなす。
    constexpr double kMaintainSatisfactionThreshold = 0.8;
    // 混雑AP選択は低満足UEの有無ではなく、AP負荷（接続数・監視RTT）を中心に行う。
    constexpr double kUserWeight = 0.8;
    constexpr double kRttWeight = 0.2;

    struct ApLoadInfo
    {
        int ap = -1; // 1-based
        int users = 0;
        double rttMs = 0.0;
        bool hasRtt = false;
        double avgSatisfaction = 0.0;
        double score = 0.0;
    };

    std::vector<int> assignment = initial_AP;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(assignment)
                                     : m_cycleHarmonicMeans.back();
    double hCurrent = hBefore;
    uint32_t switchCount = 0;
    std::vector<int> selectedTerms;

    if (aps <= 1 || assignment.empty())
    {
        std::cout << "[MultiOffload] no switch target: aps=" << aps
                  << " assignment_size=" << assignment.size() << std::endl;
        m_lastAssignment = assignment;
        PrepareDecisionLogState(initial_AP, assignment, hBefore, hCurrent);
        if (m_handoverCallback)
        {
            m_handoverCallback(assignment);
        }
        return;
    }

    auto buildApLoadInfo = [&]() {
        std::vector<ApLoadInfo> infos(aps);
        for (int apIdx = 0; apIdx < aps; ++apIdx)
        {
            infos[apIdx].ap = apIdx + 1;
            infos[apIdx].hasRtt =
                (apIdx < static_cast<int>(m_has_rtt.size())) && m_has_rtt[apIdx];
            infos[apIdx].rttMs =
                (infos[apIdx].hasRtt && apIdx < static_cast<int>(m_monitor_rtt.size()))
                    ? m_monitor_rtt[apIdx]
                    : 0.0;
        }

        std::vector<double> satisfactionSum(aps, 0.0);
        const int evalTerms = std::min(terms, static_cast<int>(assignment.size()));
        for (int termIdx = 0; termIdx < evalTerms; ++termIdx)
        {
            const int ap = assignment[termIdx]; // 1-based
            const int apIdx = ap - 1;
            if (apIdx < 0 || apIdx >= aps)
            {
                continue;
            }
            const double satisfaction =
                estimate_satisfaction_for_assignment(termIdx, apIdx, assignment);
            infos[apIdx].users += 1;
            satisfactionSum[apIdx] += satisfaction;
        }

        int maxUsers = 0;
        double maxMeasuredRtt = 0.0;
        for (int apIdx = 0; apIdx < aps; ++apIdx)
        {
            maxUsers = std::max(maxUsers, infos[apIdx].users);
            if (infos[apIdx].hasRtt)
            {
                maxMeasuredRtt = std::max(maxMeasuredRtt, infos[apIdx].rttMs);
            }
        }

        for (int apIdx = 0; apIdx < aps; ++apIdx)
        {
            if (infos[apIdx].users > 0)
            {
                infos[apIdx].avgSatisfaction =
                    satisfactionSum[apIdx] / static_cast<double>(infos[apIdx].users);
            }

            const double normalizedUsers =
                (maxUsers > 0) ? static_cast<double>(infos[apIdx].users) /
                                     static_cast<double>(maxUsers)
                               : 0.0;
            const double normalizedRtt =
                (infos[apIdx].hasRtt && maxMeasuredRtt > 0.0)
                    ? infos[apIdx].rttMs / maxMeasuredRtt
                    : 0.0;
            infos[apIdx].score = kUserWeight * normalizedUsers +
                                 kRttWeight * normalizedRtt;
        }
        return infos;
    };

    auto selectCongestedAp = [](const std::vector<ApLoadInfo>& infos) {
        int bestAp = -1;
        double bestScore = -std::numeric_limits<double>::infinity();
        int bestUsers = -1;
        double bestRtt = -1.0;
        double bestAvgSatisfaction = std::numeric_limits<double>::infinity();

        for (const auto& info : infos)
        {
            if (info.users <= 0)
            {
                continue;
            }

            const bool better =
                (info.score > bestScore + 1e-12) ||
                (std::abs(info.score - bestScore) <= 1e-12 &&
                 (info.users > bestUsers ||
                  (info.users == bestUsers &&
                   (info.rttMs > bestRtt ||
                    (std::abs(info.rttMs - bestRtt) <= 1e-12 &&
                     info.avgSatisfaction < bestAvgSatisfaction)))));

            if (better)
            {
                bestAp = info.ap;
                bestScore = info.score;
                bestUsers = info.users;
                bestRtt = info.rttMs;
                bestAvgSatisfaction = info.avgSatisfaction;
            }
        }
        return bestAp;
    };

    while (switchCount < m_MaxSwitches)
    {
        const std::vector<ApLoadInfo> loadInfos = buildApLoadInfo();
        const int congestedAp = selectCongestedAp(loadInfos); // 1-based
        if (congestedAp < 1)
        {
            std::cout << "[MultiOffload] no congested AP found" << std::endl;
            break;
        }

        const ApLoadInfo& congestedInfo = loadInfos[congestedAp - 1];
        std::cout << "[MultiOffload] step=" << (switchCount + 1)
                  << "/" << m_MaxSwitches
                  << " congested_ap=AP" << congestedAp
                  << " score=" << congestedInfo.score
                  << " users=" << congestedInfo.users
                  << " rtt_ms=" << congestedInfo.rttMs
                  << " has_rtt=" << (congestedInfo.hasRtt ? 1 : 0)
                  << " avg_satisfaction=" << congestedInfo.avgSatisfaction
                  << std::endl;

        int bestTerm = -1;
        int bestAp = -1; // 1-based
        double bestHAfter = hCurrent;
        double bestDeltaH = 0.0;
        double bestSBefore = 0.0;
        double bestSAfter = 0.0;

        const int evalTerms = std::min(terms, static_cast<int>(assignment.size()));
        for (int termIdx = 0; termIdx < evalTerms; ++termIdx)
        {
            if (assignment[termIdx] != congestedAp)
            {
                continue;
            }
            if (std::find(selectedTerms.begin(), selectedTerms.end(), termIdx) !=
                selectedTerms.end())
            {
                continue;
            }

            const double sBefore =
                estimate_satisfaction_for_assignment(termIdx, congestedAp - 1, assignment);
            if (sBefore < kOffloadableSourceThreshold)
            {
                continue;
            }

            for (int targetAp = 1; targetAp <= aps; ++targetAp)
            {
                if (targetAp == congestedAp)
                {
                    continue;
                }

                std::vector<int> candidate = assignment;
                candidate[termIdx] = targetAp;

                const double sAfter =
                    estimate_satisfaction_for_assignment(termIdx, targetAp - 1, candidate);
                const bool toleratedDrop =
                    (sAfter + kSatisfactionDropTolerance >= sBefore);
                const bool keepsSatisfaction =
                    (sAfter >= kMaintainSatisfactionThreshold);
                if (!toleratedDrop && !keepsSatisfaction)
                {
                    continue;
                }

                const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
                const double deltaH = hAfter - hCurrent;
                if (deltaH > bestDeltaH)
                {
                    bestDeltaH = deltaH;
                    bestHAfter = hAfter;
                    bestTerm = termIdx;
                    bestAp = targetAp;
                    bestSBefore = sBefore;
                    bestSAfter = sAfter;
                }
            }
        }

        if (bestTerm < 0 || bestAp < 1 || bestDeltaH <= kMinImprovement)
        {
            std::cout << "[MultiOffload] no improving offload found"
                      << " congested_ap=AP" << congestedAp
                      << " h_before_step=" << hCurrent
                      << " tolerance=" << kSatisfactionDropTolerance
                      << " source_threshold=" << kOffloadableSourceThreshold
                      << " maintain_threshold=" << kMaintainSatisfactionThreshold
                      << std::endl;
            break;
        }

        std::cout << "[MultiOffload] step=" << (switchCount + 1)
                  << "/" << m_MaxSwitches
                  << " switch term=" << (bestTerm + 1)
                  << " AP" << assignment[bestTerm]
                  << " -> AP" << bestAp
                  << " s_before=" << bestSBefore
                  << " s_after=" << bestSAfter
                  << " h_before_step=" << hCurrent
                  << " h_after_step=" << bestHAfter
                  << " delta=" << bestDeltaH << std::endl;

        assignment[bestTerm] = bestAp;
        hCurrent = bestHAfter;
        selectedTerms.push_back(bestTerm);
        switchCount++;
    }

    if (switchCount == 0)
    {
        std::cout << "[MultiOffload] no switch selected"
                  << " h_before=" << hBefore
                  << " MaxSwitches=" << m_MaxSwitches << std::endl;
    }
    else
    {
        std::cout << "[MultiOffload] selected_switches=" << switchCount
                  << " MaxSwitches=" << m_MaxSwitches
                  << " h_before=" << hBefore
                  << " h_after_estimated=" << hCurrent
                  << " reward=" << (hCurrent - hBefore) << std::endl;
    }

    m_lastAssignment = assignment;
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hCurrent);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

bool APselection::LoadDqnActions()
{
    m_dqnActions.clear();
    if (m_dqnActionCsvPath.empty())
    {
        std::cerr << "[DQN] action CSV path is empty" << std::endl;
        return false;
    }

    if (IsDirectoryPath(m_dqnActionCsvPath))
    {
        std::string resolvedPath;
        if (!ResolveLatestCsvInDirectory(m_dqnActionCsvPath, resolvedPath))
        {
            std::cerr << "[DQN] action CSV path がディレクトリですが、CSV が見つかりません: "
                      << m_dqnActionCsvPath << std::endl;
            return false;
        }
        std::cerr << "[DQN][WARN] --dqnActionCsv にディレクトリが指定されています。"
                  << " 最新の CSV を使用します: " << resolvedPath << std::endl;
        std::cerr << "[DQN][WARN] 意図したファイルを確実に使うには "
                  << "--dqnActionCsv=" << resolvedPath
                  << " のようにファイル名まで指定してください。" << std::endl;
        m_dqnActionCsvPath = resolvedPath;
    }

    std::ifstream ifs(m_dqnActionCsvPath);
    if (!ifs.is_open())
    {
        std::cerr << "[DQN] action CSV を開けません: " << m_dqnActionCsvPath << std::endl;
        return false;
    }

    std::string line;
    if (!std::getline(ifs, line))
    {
        std::cerr << "[DQN] action CSV が空です: " << m_dqnActionCsvPath << std::endl;
        return false;
    }

    const std::vector<std::string> header = splitString(line, ",");
    const int seedCol = FindColumnIndex(header, "seed");
    const int cycleCol = FindColumnIndex(header, "cycle_id");
    const int stepCol = FindColumnIndex(header, "step_id");
    const int targetCol = FindColumnIndex(header, "target_ue_id");
    const int currentCol = FindColumnIndex(header, "current_bs_id");
    const int selectedCol = FindColumnIndex(header, "selected_bs_id");
    const int advantageCol = FindColumnIndex(header, "advantage");
    const int q0Col = FindColumnIndex(header, "q_bs0");
    const int q1Col = FindColumnIndex(header, "q_bs1");
    const int q2Col = FindColumnIndex(header, "q_bs2");

    if (cycleCol < 0 || targetCol < 0 || selectedCol < 0)
    {
        std::cerr << "[DQN] action CSV に必要列 cycle_id,target_ue_id,selected_bs_id がありません: "
                  << m_dqnActionCsvPath << std::endl;
        return false;
    }
    if (m_assignmentMethod == "multi_dqn" &&
        (stepCol < 0 || currentCol < 0 || advantageCol < 0 || q0Col < 0 || q1Col < 0 || q2Col < 0))
    {
        std::cerr << "[MultiDQN] 新形式 action CSV が必要です: "
                  << "step_id,current_bs_id,advantage,q_bs0,q_bs1,q_bs2 が不足しています: "
                  << m_dqnActionCsvPath << std::endl;
        return false;
    }

    uint32_t loaded = 0;
    std::map<uint32_t, std::set<uint32_t>> seenSteps;
    std::map<uint32_t, std::set<int>> seenTargets;
    while (std::getline(ifs, line))
    {
        if (TrimCsvField(line).empty())
        {
            continue;
        }
        std::vector<std::string> cols = splitString(line, ",");
        const int requiredMaxCol = std::max({seedCol, cycleCol, stepCol, targetCol, currentCol,
                                             selectedCol, advantageCol, q0Col, q1Col, q2Col});
        if (static_cast<int>(cols.size()) <= requiredMaxCol)
        {
            std::cerr << "[DQN] 列数不足の行をスキップ: " << line << std::endl;
            continue;
        }

        DqnAction action;
        const uint32_t cycleId = static_cast<uint32_t>(std::stoul(TrimCsvField(cols[cycleCol])));
        action.stepId = (stepCol >= 0) ? static_cast<uint32_t>(std::stoul(TrimCsvField(cols[stepCol]))) : 0;
        action.targetUeId = std::stoi(TrimCsvField(cols[targetCol]));
        action.currentBsId = (currentCol >= 0) ? std::stoi(TrimCsvField(cols[currentCol])) : -1;
        action.selectedBsId = std::stoi(TrimCsvField(cols[selectedCol]));
        action.advantage = (advantageCol >= 0) ? std::stod(TrimCsvField(cols[advantageCol])) : 0.0;
        action.qBs0 = (q0Col >= 0 && !TrimCsvField(cols[q0Col]).empty()) ? std::stod(TrimCsvField(cols[q0Col])) : 0.0;
        action.qBs1 = (q1Col >= 0 && !TrimCsvField(cols[q1Col]).empty()) ? std::stod(TrimCsvField(cols[q1Col])) : 0.0;
        action.qBs2 = (q2Col >= 0 && !TrimCsvField(cols[q2Col]).empty()) ? std::stod(TrimCsvField(cols[q2Col])) : 0.0;

        if (seedCol >= 0)
        {
            const uint32_t csvSeed = static_cast<uint32_t>(std::stoul(TrimCsvField(cols[seedCol])));
            if (csvSeed != m_rngSeed)
            {
                std::cerr << "[DQN][WARN] action CSV seed=" << csvSeed
                          << " differs from setting rngSeed=" << m_rngSeed
                          << " at cycle " << cycleId << std::endl;
            }
        }

        if (action.targetUeId < 1 || action.targetUeId > terms)
        {
            std::cerr << "[DQN][WARN] target_ue_id 範囲外の行をスキップ: "
                      << action.targetUeId << std::endl;
            continue;
        }
        if (action.selectedBsId < 0 || action.selectedBsId >= aps)
        {
            std::cerr << "[DQN][WARN] selected_bs_id 範囲外の行をスキップ: "
                      << action.selectedBsId << std::endl;
            continue;
        }
        if (m_assignmentMethod == "multi_dqn")
        {
            if (action.currentBsId < 0 || action.currentBsId >= aps)
            {
                std::cerr << "[MultiDQN][WARN] current_bs_id 範囲外の行をスキップ: "
                          << action.currentBsId << std::endl;
                continue;
            }
            if (seenSteps[cycleId].count(action.stepId) > 0)
            {
                std::cerr << "[MultiDQN][WARN] cycle_id=" << cycleId
                          << " step_id=" << action.stepId
                          << " が重複しているためスキップ" << std::endl;
                continue;
            }
            if (seenTargets[cycleId].count(action.targetUeId) > 0)
            {
                std::cerr << "[MultiDQN][WARN] cycle_id=" << cycleId
                          << " target_ue_id=" << action.targetUeId
                          << " が重複しているためスキップ" << std::endl;
                continue;
            }
            seenSteps[cycleId].insert(action.stepId);
            seenTargets[cycleId].insert(action.targetUeId);
        }
        else if (!m_dqnActions[cycleId].empty())
        {
            std::cerr << "[DQN][WARN] cycle_id=" << cycleId
                      << " のactionが複数あります。先頭のみ使用します。" << std::endl;
            continue;
        }

        m_dqnActions[cycleId].push_back(action);
        ++loaded;
    }

    for (auto& kv : m_dqnActions)
    {
        std::sort(kv.second.begin(), kv.second.end(), [](const DqnAction& lhs, const DqnAction& rhs) {
            return lhs.stepId < rhs.stepId;
        });
    }

    std::cout << "[DQN] loaded actions: " << loaded
              << " from " << m_dqnActionCsvPath << std::endl;
    return !m_dqnActions.empty();
}

void APselection::dqn_assignment()
{
    std::cout << "=== APselection::dqn_assignment() ===" << std::endl;

    std::vector<int> assignment = initial_AP;
    auto it = m_dqnActions.find(m_cycleIndex);
    if (it == m_dqnActions.end() || it->second.empty())
    {
        std::cerr << "[DQN][WARN] cycle " << m_cycleIndex
                  << " のactionがありません。現在の割り当てを維持します。" << std::endl;
    }
    else
    {
        const DqnAction& action = it->second.front();
        const int targetIdx = action.targetUeId - 1;
        assignment[targetIdx] = action.selectedBsId + 1; // internal AP ID is 1-based

        std::cout << "[DQN] cycle=" << m_cycleIndex
                  << " target_ue_id=" << action.targetUeId
                  << " selected_bs_id=" << action.selectedBsId
                  << " (AP" << assignment[targetIdx] << ")" << std::endl;
    }

    m_lastAssignment = assignment;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

void APselection::multi_dqn_assignment()
{
    std::cout << "=== APselection::multi_dqn_assignment() ===" << std::endl;

    std::vector<int> assignment = initial_AP;
    auto it = m_dqnActions.find(m_cycleIndex);
    if (it == m_dqnActions.end() || it->second.empty())
    {
        std::cerr << "[MultiDQN][WARN] cycle " << m_cycleIndex
                  << " のactionがありません。現在の割り当てを維持します。" << std::endl;
        KeepCurrentAssignment("no multi_dqn action for cycle");
        return;
    }

    uint32_t applied = 0;
    std::set<int> seenTargets;
    for (const DqnAction& action : it->second)
    {
        const int targetIdx = action.targetUeId - 1;
        int previousBsId = -1;
        if (targetIdx >= 0 && targetIdx < static_cast<int>(assignment.size()))
        {
            previousBsId = assignment[targetIdx] - 1;
        }

        std::string skipReason;
        bool apply = true;
        if (applied >= m_MaxSwitches)
        {
            apply = false;
            skipReason = "max_switches";
        }
        else if (targetIdx < 0 || targetIdx >= terms || targetIdx >= static_cast<int>(assignment.size()))
        {
            apply = false;
            skipReason = "invalid_target_ue_id";
        }
        else if (seenTargets.count(targetIdx) > 0)
        {
            apply = false;
            skipReason = "duplicate_target_ue_id";
        }
        else if (action.selectedBsId < 0 || action.selectedBsId >= aps)
        {
            apply = false;
            skipReason = "invalid_selected_bs_id";
        }
        else if (assignment[targetIdx] == action.selectedBsId + 1)
        {
            apply = false;
            skipReason = "same_bs";
        }
        else if (action.currentBsId >= 0 && previousBsId >= 0 && action.currentBsId != previousBsId)
        {
            // sequential 推論で生成した current_bs_id と ns-3 側の現在割当が違う場合は、
            // action CSV と実行状態のずれを避けるためスキップする。
            apply = false;
            skipReason = "current_bs_mismatch";
        }

        if (apply)
        {
            assignment[targetIdx] = action.selectedBsId + 1;
            seenTargets.insert(targetIdx);
            ++applied;
            std::cout << "[MultiDQN] cycle=" << m_cycleIndex
                      << " step=" << action.stepId
                      << " switch term=" << action.targetUeId
                      << " AP" << (previousBsId + 1)
                      << " -> AP" << (action.selectedBsId + 1)
                      << " advantage=" << action.advantage << std::endl;
            WriteDecisionLogRow(action, previousBsId, true, "");
        }
        else
        {
            std::cout << "[MultiDQN] cycle=" << m_cycleIndex
                      << " step=" << action.stepId
                      << " skip target_ue_id=" << action.targetUeId
                      << " reason=" << skipReason << std::endl;
            WriteDecisionLogRow(action, previousBsId, false, skipReason);
        }
    }

    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    m_lastAssignment = assignment;

    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

// ロジスティック回帰モデルの読み込み
bool APselection::LoadLogisticModel()
{
    std::ifstream ifs(m_logisticModelPath);
    if (!ifs.is_open())
    {
        std::cerr << "[Logistic] 学習済みモデルを開けませんでした: "
                  << m_logisticModelPath << std::endl;
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
    if (content.find("\"model_type\": \"logistic_regression\"") == std::string::npos)
    {
        std::cerr << "[Logistic] model_type を確認できませんでした: "
                  << m_logisticModelPath << std::endl;
        return false;
    }

    std::string featureColumnsBody;
    if (!ExtractJsonArrayBody(content, "feature_columns", featureColumnsBody))
    {
        std::cerr << "[Logistic] feature_columns を読み込めませんでした: "
                  << m_logisticModelPath << std::endl;
        return false;
    }
    const std::vector<std::string> expectedFeatureColumns = {
        "app_type",
        "current_ap",
        "num_users_ap0",
        "num_users_ap1",
        "num_users_ap2",
        "rtt_ap0",
        "rtt_ap1",
        "rtt_ap2",
        "estimated_tp_ap0",
        "estimated_tp_ap1",
        "estimated_tp_ap2",
    };
    if (ParseJsonStrings(featureColumnsBody) != expectedFeatureColumns)
    {
        std::cerr << "[Logistic] feature_columns の順序が想定と一致しません: "
                  << m_logisticModelPath << std::endl;
        return false;
    }

    std::vector<double> classes;
    std::vector<double> flatCoef;
    if (!ExtractJsonNumbers(content, "classes", classes) ||
        !ExtractJsonNumbers(content, "scaler_mean", m_logisticScalerMean) ||
        !ExtractJsonNumbers(content, "scaler_scale", m_logisticScalerScale) ||
        !ExtractJsonNumbers(content, "coef", flatCoef) ||
        !ExtractJsonNumbers(content, "intercept", m_logisticIntercept))
    {
        std::cerr << "[Logistic] モデルパラメータを読み込めませんでした: "
                  << m_logisticModelPath << std::endl;
        return false;
    }

    if (classes.size() != 3 ||
        m_logisticScalerMean.size() != kLogisticFeatureCount ||
        m_logisticScalerScale.size() != kLogisticFeatureCount ||
        flatCoef.size() != classes.size() * kLogisticFeatureCount ||
        m_logisticIntercept.size() != classes.size())
    {
        std::cerr << "[Logistic] モデルパラメータの次元が不正です: "
                  << m_logisticModelPath << std::endl;
        return false;
    }

    m_logisticClasses.clear();
    for (double value : classes)
    {
        m_logisticClasses.push_back(static_cast<int>(value));
    }
    if (m_logisticClasses != std::vector<int>({0, 1, 2}))
    {
        std::cerr << "[Logistic] APクラスは [0, 1, 2] である必要があります" << std::endl;
        return false;
    }

    m_logisticCoef.assign(classes.size(), std::vector<double>(kLogisticFeatureCount, 0.0));
    for (size_t classIdx = 0; classIdx < classes.size(); ++classIdx)
    {
        for (size_t featureIdx = 0; featureIdx < kLogisticFeatureCount; ++featureIdx)
        {
            m_logisticCoef[classIdx][featureIdx] =
                flatCoef[classIdx * kLogisticFeatureCount + featureIdx];
        }
    }

    std::cout << "[Logistic] "
              << m_logisticModelPath << std::endl;
    return true;
}

// 現在の割り当てを維持
void APselection::KeepCurrentAssignment(const std::string& reason)
{
    std::cerr << "[Logistic] " << reason
              << " 現在の割り当てを維持" << std::endl;
    m_lastAssignment = initial_AP;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, m_lastAssignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(m_lastAssignment);
    }
}

// ロジスティック回帰による割り当て
void APselection::logistic_assignment()
{
    std::cout << "=== APselection::logistic_assignment() ===" << std::endl;
    if (!m_logisticModelLoaded)
    {
        KeepCurrentAssignment("学習済みモデルを利用不可");
        return;
    }

    std::vector<double> numUsers(aps, 0.0); 
    std::vector<double> tpSumMbps(aps, 0.0); // 各APの平均TP
    std::vector<uint32_t> tpCount(aps, 0); // 有効なTPを取得できた端末数

    for (int termIdx = 0; termIdx < terms; ++termIdx)
    {
        const int apIdx = initial_AP[termIdx] - 1;
        numUsers[apIdx] += 1.0;

        if (termIdx < static_cast<int>(m_has_terminal_tp.size()) &&
            m_has_terminal_tp[termIdx] &&
            m_terminal_tp[termIdx] > 0.0)
        {
            tpSumMbps[apIdx] += m_terminal_tp[termIdx] * APConstants::BPS_TO_MBPS; // 各APの平均TPを加算
            tpCount[apIdx] += 1;
        }
    }

    std::vector<double> estimatedTp(aps, 0.0); // 各APの推定TP
    for (int apIdx = 0; apIdx < aps; ++apIdx)
    {
        if (tpCount[apIdx] == 0)
        {
            KeepCurrentAssignment("AP" + std::to_string(apIdx) + " の平均TPを取得不可");
            return;
        }
        estimatedTp[apIdx] = tpSumMbps[apIdx] / static_cast<double>(tpCount[apIdx]); // 各APの平均TPを計算
    }

    std::vector<int> assignment;
    assignment.reserve(terms); 
    for (int termIdx = 0; termIdx < terms; ++termIdx)
    {
        // 各端末の特徴量を取得
        const std::vector<double> features = {
            static_cast<double>(initial_app[termIdx]),
            static_cast<double>(initial_AP[termIdx] - 1),
            numUsers[0],
            numUsers[1],
            numUsers[2],
            m_monitor_rtt[0],
            m_monitor_rtt[1],
            m_monitor_rtt[2],
            estimatedTp[0],
            estimatedTp[1],
            estimatedTp[2],
        };

        int bestClassIdx = -1; // 最適なAPクラス
        double bestLogit = -std::numeric_limits<double>::infinity(); // 最適なAPクラスのスコア（ロジット）
        for (size_t classIdx = 0; classIdx < m_logisticClasses.size(); ++classIdx)
        {
            double logit = m_logisticIntercept[classIdx];
            for (size_t featureIdx = 0; featureIdx < features.size(); ++featureIdx)
            {
                double scale = m_logisticScalerScale[featureIdx];
                if (scale == 0.0) // 0除算防止
                {
                    scale = 1.0;
                }
                const double standardized =
                    (features[featureIdx] - m_logisticScalerMean[featureIdx]) / scale; // 標準化：(現在の値 - 学習データの平均値) / 学習データの標準偏差
                logit += m_logisticCoef[classIdx][featureIdx] * standardized; // ロジットを計算：係数 * 標準化した値
            }
            if (logit > bestLogit) // 最適なAPクラスを更新
            {
                bestLogit = logit;
                bestClassIdx = static_cast<int>(classIdx);
            }
        }

        if (bestClassIdx < 0)
        {
            KeepCurrentAssignment("推論結果を算出不可");
            return;
        }
        assignment.push_back(m_logisticClasses[bestClassIdx] + 1);
    }

    m_lastAssignment = assignment;
    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}

void APselection::StartNewCycle(uint32_t cycleIndex)
{
    std::cout << "=== APselection::StartNewCycle(" << cycleIndex << ") ===" << std::endl;
    m_cycleIndex = cycleIndex;

    // ハンドオーバ検出: 前サイクルの割当と現在のAPを比較し、切り替わった端末の猶予期間を開始する
    if (!m_lastAssignment.empty() && !m_switchCycle.empty())
    {
        for (int i = 0; i < terms && i < static_cast<int>(m_lastAssignment.size()); ++i)
        {
            if (m_lastAssignment[i] != initial_AP[i])
            {
                m_switchCycle[i] = m_cycleIndex;
            }
        }
    }

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

void APselection::SetTotalCycles(uint32_t n)
{
    m_totalCycles = n;
}

void APselection::ResetMonitorStats()
{
    std::fill(m_monitor_rtt.begin(), m_monitor_rtt.end(), 0.0);
    std::fill(m_rtt_sum.begin(), m_rtt_sum.end(), 0.0);
    std::fill(m_rtt_count.begin(), m_rtt_count.end(), 0);
    std::fill(m_has_rtt.begin(), m_has_rtt.end(), false);
    std::fill(m_monitor_ip.begin(), m_monitor_ip.end(), "");
    std::fill(m_terminal_tp.begin(), m_terminal_tp.end(), 0.0);
    std::fill(m_has_terminal_tp.begin(), m_has_terminal_tp.end(), false);
}

void APselection::PrintMonitorRttReport() const
{
    std::ios oldState(nullptr);
    oldState.copyfmt(std::cout);

    for (int i = 0; i < aps; ++i)
    {
        if (static_cast<size_t>(i) < m_has_rtt.size() && m_has_rtt[i])
        {
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "AP:" << i << "\tRTT:" << m_monitor_rtt[i] << "ms" << std::endl;
        }
        else
        {
            std::cout << "AP:" << i << "\tRTT:N/A" << std::endl;
        }
    }

    std::cout.copyfmt(oldState);
}

void APselection::setTerminalTp(int termIdx, double tpBps)
{
    if (termIdx < 0 || termIdx >= static_cast<int>(m_terminal_tp.size()))
    {
        return;
    }
    m_terminal_tp[termIdx] = tpBps;
    m_has_terminal_tp[termIdx] = true;

    //デバッグ用（各端末のTPの表示）
    // std::cout << "Terminal TP stored: term=" << termIdx
    //           << ", TP=" << (tpBps * APConstants::BPS_TO_MBPS) << "Mbps" << std::endl;
}

void APselection::RecordHarmonicMean(double value)
{
    // サイクル順に積み上げる
    m_cycleHarmonicMeans.push_back(value);

    std::cout << std::fixed << std::setprecision(6)
              << "Cycle " << m_cycleIndex
              << " の調和平均：" << value << std::endl;
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

void
APselection::WriteDecisionLogRow(const DqnAction& action,
                                 int previousBsId,
                                 bool applied,
                                 const std::string& skipReason)
{
    if (m_assignmentMethod != "multi_dqn" || m_decisionLogPath.empty())
    {
        return;
    }

    if (!m_decisionLogInitialized)
    {
        std::ofstream headerOfs(m_decisionLogPath, std::ios::trunc);
        headerOfs << "seed,"
                  << "method,"
                  << "cycle_id,"
                  << "step_id,"
                  << "target_ue_id,"
                  << "previous_bs_id,"
                  << "current_bs_id,"
                  << "selected_bs_id,"
                  << "satisfaction_before,"
                  << "harmonic_mean_before,"
                  << "harmonic_mean_after_estimated,"
                  << "advantage,"
                  << "q_bs0,"
                  << "q_bs1,"
                  << "q_bs2,"
                  << "applied,"
                  << "skip_reason" << std::endl;
        m_decisionLogInitialized = true;
    }

    double satisfactionBefore = APConstants::MIN_SATISFACTION_THRESHOLD;
    const int targetIdx = action.targetUeId - 1;
    if (targetIdx >= 0 && targetIdx < terms && previousBsId >= 0 && previousBsId < aps)
    {
        satisfactionBefore = calculate_satisfaction(targetIdx, previousBsId);
    }

    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();
    const double hAfterEstimated =
        (m_hAfterEstimated > 0.0) ? m_hAfterEstimated : hBefore;

    std::ofstream ofs(m_decisionLogPath, std::ios::app);
    ofs << std::fixed << std::setprecision(6)
        << m_rngSeed << ","
        << m_assignmentMethod << ","
        << m_cycleIndex << ","
        << action.stepId << ","
        << action.targetUeId << ","
        << previousBsId << ","
        << action.currentBsId << ","
        << action.selectedBsId << ","
        << satisfactionBefore << ","
        << hBefore << ","
        << hAfterEstimated << ","
        << action.advantage << ","
        << action.qBs0 << ","
        << action.qBs1 << ","
        << action.qBs2 << ","
        << (applied ? 1 : 0) << ","
        << skipReason << std::endl;
}

void APselection::WriteMasterLog()
{
    const std::string filePath = m_masterLogPath;

    // 初回呼び出し時にヘッダーを書き込む
    if (!m_masterLogInitialized)
    {
        std::ofstream ofs(filePath, std::ios::trunc);
        ofs << "seed,"
            << "method,"
            << "cycle_id,"
            << "ue_id,"
            << "previous_bs_id,"
            << "current_bs_id,"
            << "app_type,"
            << "tp_mbps,"
            << "rtt_ms,"
            << "satisfaction,"
            << "num_users_on_current_bs,"
            << "harmonic_mean,"
            << "num_unsatisfied_users,"
            << "target_ue_flag,"
            << "action_selected_bs_id,"
            << "switch_flag,"
            << "h_after_estimated,"
            << "reward,"
            << "measurement_valid" << std::endl;
        ofs.close();
        m_masterLogInitialized = true;
    }

    std::ofstream ofs(filePath, std::ios::app);
    ofs << std::fixed << std::setprecision(6);

    const double harmonicMean =
        m_cycleHarmonicMeans.empty() ? 0.0 : m_cycleHarmonicMeans.back();

    std::vector<int> usersPerAp(aps, 0);
    for (int i = 0; i < terms; ++i)
    {
        const int apIdx = initial_AP[i] - 1;
        if (apIdx >= 0 && apIdx < aps)
        {
            usersPerAp[apIdx] += 1;
        }
    }

    std::vector<double> satisfactions(terms, APConstants::MIN_SATISFACTION_THRESHOLD);
    int numUnsatisfiedUsers = 0;
    int targetUeIdx = -1;
    double minUnsatisfied = std::numeric_limits<double>::infinity();
    int highSatisfactionIdx = -1;
    double maxHighSatisfaction = -std::numeric_limits<double>::infinity();
    int minAllIdx = -1;
    double minAllSatisfaction = std::numeric_limits<double>::infinity();

    constexpr double kUnsatisfiedThreshold = 0.7;
    constexpr double kHighSatisfactionThreshold = 1.4;

    for (int i = 0; i < terms; ++i)
    {
        const int apIdx = initial_AP[i] - 1;
        const double satisfaction = calculate_satisfaction(i, apIdx);
        satisfactions[i] = satisfaction;

        if (satisfaction < kUnsatisfiedThreshold)
        {
            ++numUnsatisfiedUsers;
            if (satisfaction < minUnsatisfied)
            {
                minUnsatisfied = satisfaction;
                targetUeIdx = i;
            }
        }
        if (satisfaction >= kHighSatisfactionThreshold &&
            satisfaction > maxHighSatisfaction)
        {
            maxHighSatisfaction = satisfaction;
            highSatisfactionIdx = i;
        }
        if (satisfaction < minAllSatisfaction)
        {
            minAllSatisfaction = satisfaction;
            minAllIdx = i;
        }
    }

    if (targetUeIdx < 0)
    {
        targetUeIdx = highSatisfactionIdx;
    }
    if (targetUeIdx < 0)
    {
        targetUeIdx = minAllIdx;
    }

    for (int i = 0; i < terms; ++i)
    {
        int ap_1based = initial_AP[i];
        int ap_idx = ap_1based - 1;
        const int previousBsId =
            (i < static_cast<int>(m_assignmentBeforeAction.size()))
                ? (m_assignmentBeforeAction[i] - 1)
                : ap_idx;
        int appNum = initial_app[i];

        // TP系(1,2) か RTT系(3,4) か判定
        bool isTpApp = (appNum == static_cast<int>(APConstants::AppType::BROWSER) ||
                        appNum == static_cast<int>(APConstants::AppType::VIDEO));

        double tpMbps = 0.0;
        if (i >= 0 &&
            i < static_cast<int>(m_has_terminal_tp.size()) &&
            m_has_terminal_tp[i] &&
            m_terminal_tp[i] > 0.0)
        {
            tpMbps = m_terminal_tp[i] * APConstants::BPS_TO_MBPS;
        }
        double rttMs = (ap_idx >= 0 &&
                        ap_idx < static_cast<int>(m_has_rtt.size()) &&
                        m_has_rtt[ap_idx])
                           ? m_monitor_rtt[ap_idx]
                           : 0.0;
        const double satisfaction = satisfactions[i];
        bool dataValid = isTpApp
            ? (i < static_cast<int>(m_has_terminal_tp.size()) && m_has_terminal_tp[i] && m_terminal_tp[i] > 0.0)
            : (ap_idx >= 0 && ap_idx < static_cast<int>(m_has_rtt.size()) && m_has_rtt[ap_idx]);
        const int numUsersOnCurrentBs =
            (ap_idx >= 0 && ap_idx < static_cast<int>(usersPerAp.size()))
                ? usersPerAp[ap_idx]
                : 0;
        const int actionSelectedBsId =
            (i < static_cast<int>(m_lastAssignment.size()))
                ? (m_lastAssignment[i] - 1)
                : ap_idx;
        const int switchFlag = (previousBsId != actionSelectedBsId) ? 1 : 0;
        const double hAfterEstimated =
            (m_hAfterEstimated > 0.0) ? m_hAfterEstimated : harmonicMean;
        const double reward =
            (m_hAfterEstimated > 0.0) ? m_lastReward : 0.0;

        ofs << m_rngSeed << ","
            << m_assignmentMethod << ","
            << m_cycleIndex << ","
            << (i + 1) << ","
            << previousBsId << ","
            << ap_idx << ","
            << appNum << ","
            << tpMbps << ","
            << rttMs << ","
            << satisfaction << ","
            << numUsersOnCurrentBs << ","
            << harmonicMean << ","
            << numUnsatisfiedUsers << ","
            << ((i == targetUeIdx) ? 1 : 0) << ","
            << actionSelectedBsId << ","
            << switchFlag << ","
            << hAfterEstimated << ","
            << reward << ","
            << (dataValid ? 1 : 0) << std::endl;
    }

    ofs.close();
}
}
