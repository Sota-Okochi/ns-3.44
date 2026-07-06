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
        "/home/sota/ns-3.44/machine-learning/baseline_methods/data/models/"
        "logistic_term80_runs50_seed001.json";
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
    m_switchCycle.assign(terms, 0);
    m_handoverGraceCycles = input.handoverGraceCycles;
    m_cycleIndex = 1;
    m_masterLogInitialized = false;
    if (m_assignmentMethod == "logistic")
    {
        m_logisticModelLoaded = LoadLogisticModel();
    }
    if (m_assignmentMethod == "dqn" && !LoadDqnActions())
    {
        NS_FATAL_ERROR("Failed to load DQN action CSV: " << m_dqnActionCsvPath);
    }

    {
        std::time_t t = std::time(nullptr);
        std::tm *tm_local = std::localtime(&t);
        char dateBuf[32];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d_%H%M%S", tm_local);
        m_masterLogPath = m_outputDir + "master_log_" + std::to_string(terms) + "_" +
                          m_assignmentMethod + "_" + dateBuf + ".csv";
    }
    std::cout << "ログパス: " << m_masterLogPath << std::endl;

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
        else if (m_assignmentMethod == "logistic")
        {
            logistic_assignment();
        }
        else if (m_assignmentMethod == "dqn")
        {
            dqn_assignment();
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

bool APselection::LoadDqnActions()
{
    m_dqnActions.clear();
    if (m_dqnActionCsvPath.empty())
    {
        std::cerr << "[DQN] action CSV path is empty" << std::endl;
        return false;
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
    const int targetCol = FindColumnIndex(header, "target_ue_id");
    const int selectedCol = FindColumnIndex(header, "selected_bs_id");
    if (cycleCol < 0 || targetCol < 0 || selectedCol < 0)
    {
        std::cerr << "[DQN] action CSV に必要列 cycle_id,target_ue_id,selected_bs_id がありません: "
                  << m_dqnActionCsvPath << std::endl;
        return false;
    }

    uint32_t loaded = 0;
    while (std::getline(ifs, line))
    {
        if (TrimCsvField(line).empty())
        {
            continue;
        }
        std::vector<std::string> cols = splitString(line, ",");
        const int requiredMaxCol = std::max({seedCol, cycleCol, targetCol, selectedCol});
        if (static_cast<int>(cols.size()) <= requiredMaxCol)
        {
            std::cerr << "[DQN] 列数不足の行をスキップ: " << line << std::endl;
            continue;
        }

        const uint32_t cycleId = static_cast<uint32_t>(std::stoul(TrimCsvField(cols[cycleCol])));
        const int targetUeId = std::stoi(TrimCsvField(cols[targetCol]));
        const int selectedBsId = std::stoi(TrimCsvField(cols[selectedCol]));

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

        if (targetUeId < 1 || targetUeId > terms)
        {
            std::cerr << "[DQN][WARN] target_ue_id 範囲外の行をスキップ: "
                      << targetUeId << std::endl;
            continue;
        }
        if (selectedBsId < 0 || selectedBsId >= aps)
        {
            std::cerr << "[DQN][WARN] selected_bs_id 範囲外の行をスキップ: "
                      << selectedBsId << std::endl;
            continue;
        }

        if (m_dqnActions.find(cycleId) != m_dqnActions.end())
        {
            std::cerr << "[DQN][WARN] cycle_id=" << cycleId
                      << " のactionが複数あります。後勝ちで上書きします。" << std::endl;
        }
        m_dqnActions[cycleId] = std::make_pair(targetUeId, selectedBsId);
        ++loaded;
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
    if (it == m_dqnActions.end())
    {
        std::cerr << "[DQN][WARN] cycle " << m_cycleIndex
                  << " のactionがありません。現在の割り当てを維持します。" << std::endl;
    }
    else
    {
        const int targetUeId = it->second.first;     // 1-based
        const int selectedBsId = it->second.second; // 0-based
        const int targetIdx = targetUeId - 1;
        assignment[targetIdx] = selectedBsId + 1; // internal AP ID is 1-based

        std::cout << "[DQN] cycle=" << m_cycleIndex
                  << " target_ue_id=" << targetUeId
                  << " selected_bs_id=" << selectedBsId
                  << " (AP" << assignment[targetIdx] << ")" << std::endl;
    }

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

void APselection::WriteMasterLog()
{
    const std::string filePath = m_masterLogPath;

    // 初回呼び出し時にヘッダーを書き込む
    if (!m_masterLogInitialized)
    {
        std::ofstream ofs(filePath, std::ios::trunc);
        ofs << "seed,"
            << "cycle_id,"
            << "ue_id,"
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

        ofs << m_rngSeed << ","
            << m_cycleIndex << ","
            << (i + 1) << ","
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
            << (dataValid ? 1 : 0) << std::endl;
    }

    ofs.close();
}
}
