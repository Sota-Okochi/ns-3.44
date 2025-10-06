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

void APselection::tmain(){
    NS_LOG_FUNCTION(this);
    std::cout << "=== APselection::tmain() START ===" << std::endl;
    std::cout << "m_APNum: " << m_APNum << std::endl;
    std::cout << "m_real_rtt size: " << m_real_rtt.size() << std::endl;
    
    // 実測RTTデータから各APの平均RTTとTP値を計算
    m_link_rtt.resize(m_APNum);
    init_rtt.clear();
    init_rtt.resize(m_APNum);
    init_tp.clear();
    init_tp.resize(m_APNum);
    
    for(int i=0; i<m_APNum; i++){
        double sum = 0.0;
        for(auto data : m_real_rtt[i]){
            sum += data;
        }
        
        // ゼロ除算エラーを防ぐ
        if(m_real_rtt[i].size() > 0){
            double ave_rtt = sum / static_cast<double>(m_real_rtt[i].size());
            m_link_rtt[i] = ave_rtt;
            init_rtt[i] = ave_rtt;
            
            // 実測RTTからTP値を直接計算 (RTTが小さいほどTP値が大きくなる)
            init_tp[i] = APConstants::INITIAL_TP_MULTIPLIER[0] / ave_rtt;
            
            std::cout << "AP:" << i << "\tSUM:" << sum
                    << "\tSIZE:" << m_real_rtt[i].size() 
                    << "\tAVE_RTT:" << ave_rtt << "ms"
                    << "\tTP:" << init_tp[i] << "KB/s" << std::endl;
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
            init_tp[i] = APConstants::INITIAL_TP_MULTIPLIER[0] / default_rtt;
            
            std::cout << "AP:" << i << "\tNo data - using config RTT: " << default_rtt << "ms"
                    << "\tTP:" << init_tp[i] << "KB/s" << std::endl;
        }
    }
    
    cal_need_rt();
    make_combi_ap_term();
    
    // ハンガリアン法の実行時間測定
    std::cout << "↓\n";
    auto start_time = std::chrono::high_resolution_clock::now();
    call_hungarian();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "\n■ハンガリアン法計算時間: " << duration.count() << " ms\n";
    
    // ランダム法との比較
    combi_random();

    // Greedy法との比較
    combi_greedy();

    
    std::cout << "=== APselection::tmain() END ===" << std::endl;
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
    if( ret2.size() != 2 ) {
        std::cout << "Invalid message format" << std::endl;
        return;
    }
    std::stringstream ss2(ret2[1]);
    double d; ss2 >> d;
    m_real_rtt[apNo].push_back(d);
    std::cout << "Data stored: AP=" << apNo << ", RTT=" << d << "ms" << std::endl;
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

    incRTT.assign(aps, APConstants::RTT_INCREASE_PER_TERMINAL);
    incTP.assign(aps, APConstants::TP_DECREASE_PER_TERMINAL);

    init_rtt.assign(aps, 0.0);
    init_tp.assign(aps, 0.0);
    m_real_rtt.clear();
    m_real_rtt.resize(aps);

    std::cout << "=== 初期設定値 ===" << std::endl;
    for(int i = 0; i < aps; i++){
        std::cout << "AP:" << i << "\tRTT:" << m_initialRtt[i] << "ms\tTP:" << (APConstants::INITIAL_TP_MULTIPLIER[0] / m_initialRtt[i]) << "KB/s" << std::endl;
    }

    std::cout << "=== APselection::init() completed ===" << std::endl;
    std::cout << "APs: " << m_APNum << ", Terms: " << m_termNum << std::endl;
}

//必要RTT, TPの算出
void APselection::cal_need_rt(){
    
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


//基地局と端末の組み合わせの作成
void APselection::make_combi_ap_term(){
    
    int n = terms + aps -1;
    int r = aps -1;
    std::vector<int> data;
    std::vector<int> wk_division(r);
    std::vector<std::vector<int>> division;
    std::vector<int> wk_combi_ap_term(terms);
    std::vector<int> wk_combi_ap_term_temp(n); //全組み合わせ
    std::vector<int> count_ap(aps,0);
    int flg_over_num_ap=0;
    
    // 初期配列の準備
    for ( int i=0; i<n; i++ )
        data.push_back(i);
    
    // 仕切りの位置の全組み合わせを出力（重複無し組み合わせ）
    do {
        for ( int i=0; i<r; i++ ) wk_division[i] = data[i];

        division.push_back(wk_division);
        for ( int i=0; i<r; i++ ) wk_division[i] = 0;
        
    } while ( next_combination(data.begin(), data.begin()+r, data.end()) );
    
    // 各端末がどのAPに割り当てられるかを計算
    for(size_t i=0;i<division.size();i++){
        int no_divi = 0;
        int no_ap = 1;
        for(int j=0;j<n;j++){
            if(j==division[i][no_divi]){
                wk_combi_ap_term_temp[j] = 0;
                no_ap++;
                if(no_divi==0) no_divi++;
            }
            else{
                wk_combi_ap_term_temp[j] = no_ap;
            }
        }
        int k=0;
        for(int j=0;j<n;j++){
            if(wk_combi_ap_term_temp[j] != 0){
                wk_combi_ap_term[k] = wk_combi_ap_term_temp[j];
                k++;
            }
        }
        m_combi_ap_term_temp.push_back(wk_combi_ap_term);
    }

    
    std::cout << "■全組み合わせ数: " << m_combi_ap_term_temp.size() << "\n";
    
    
    for(size_t i=0;i<m_combi_ap_term_temp.size();i++){
        
        //AP毎の収容数をカウント
        for(int j=0;j<terms;j++){
            count_ap[m_combi_ap_term_temp[i][j]-1]++;
        }
        //一つでも収容数を超えているAPがあれば、不採用
        for(int j=0;j<aps;j++){
            if(count_ap[j]>m_capa[j]){
                flg_over_num_ap = 1;
            }
        }
        
        //採用した組み合わせのみをリストに追加
        if(flg_over_num_ap == 0){
            m_combi_ap_term.push_back(m_combi_ap_term_temp[i]);
            m_num_ap_access.push_back(count_ap);
        }
        
        //フラグを初期化
        flg_over_num_ap =0;
        for(int j=0;j<aps;j++){
            count_ap[j] = 0;
        }
        
    }
    
   std::cout << "■対象組み合わせ数: " << m_combi_ap_term.size() << "\n";
    
    // メモリの削減（最適化）
    data.shrink_to_fit();
    wk_division.shrink_to_fit();
    division.shrink_to_fit();
    wk_combi_ap_term.shrink_to_fit();
    wk_combi_ap_term_temp.shrink_to_fit();
    count_ap.shrink_to_fit();
}


//すべての基地局の組み合わせに対して，ハンガリアン法を呼び出し
void APselection::call_hungarian(){
    
    std::vector<HungarianResult> hungarianResultAll;
    hungarianResultAll.reserve(m_combi_ap_term.size());  // 事前メモリ確保で高速化
    
    m_no_combi_ap_term = 0;
    
    
    //収容総数からの組み合わせ
    for(size_t i=0;i<m_combi_ap_term.size();i++){
        
        std::vector<std::vector<double> > mat_ent(terms,std::vector<double> (terms));
        std::vector<double> ap_rtt_current(aps,0);
        std::vector<double> ap_tp_current(aps, 0);
        
        // AP性能計算（接続台数を考慮）
        calculate_ap_performance(m_num_ap_access[i], ap_rtt_current, ap_tp_current);

        
        // 端末満足度行列作成
        create_satisfaction_matrix(m_combi_ap_term[i], ap_rtt_current, ap_tp_current, mat_ent);

        
        // ハンガリアン法を呼び出し
        HungarianResult result = hungarian(mat_ent, m_combi_ap_term[i]);
        
        hungarianResultAll.push_back(result);
        
        m_no_combi_ap_term++;
    }
    
    // 最適解選択（調和平均最大）
    int best_index = find_best_solution(hungarianResultAll);
    
    HungarianResult final_result = hungarianResultAll[best_index];
    
    // 結果表示
    display_final_results(final_result, best_index);
    
    // 結果を既存の変数に設定（互換性のため）
    m_best_solution_station = final_result.combiApTermArray;
    m_best_solution_value = {final_result.Harmean, final_result.min};  // 調和平均を保存
    
    // 最適解のAP性能値を再計算
    std::vector<double> best_ap_rtt_current(aps, 0);
    std::vector<double> best_ap_tp_current(aps, 0);
    calculate_ap_performance(m_num_ap_access[best_index], best_ap_rtt_current, best_ap_tp_current);
    
    m_best_solution_ap_rtt = best_ap_rtt_current;
    m_best_solution_ap_tp = best_ap_tp_current;

}

//ハンガリアン法の計算（Pythonのhungarian_kai.pyのhungarian相当）
HungarianResult APselection::hungarian(std::vector<std::vector<double> > &a, std::vector<int> &combi_ap_term) {
    
    std::vector<std::vector<double> > b(terms,std::vector<double> (terms));
    std::vector<double> wk_solution_value(3);  // [合計, 最小値, 逆数合計]
    std::vector<int> wk_solution_station(terms);
    double fix_digit = APConstants::PRECISION_MULTIPLIER;        //桁数調整用
    
    int n = a.size(), p, q;
    
    // コスト行列を整数に変換（最大化のため符号反転）
    for(int i=0;i<n;i++)
        for(int j=0;j<n;j++)
            b[i][j] = (int) (a[i][j] * fix_digit);  // 符号反転を削除（満足度を最大化）
    
    std::vector<double> fx(n, -LONG_MAX), fy(n, 0);  // fx初期値を最小値に変更
    std::vector<int> x(n, -1), y(n, -1);
    
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            fx[i] = std::max(fx[i], b[i][j]);
    for (int i = 0; i < n; ) {
        std::vector<int> t(n, -1), s(n+1, i);
        for (p = q = 0; p <= q && x[i] < 0; ++p)
            for (int k = s[p], j = 0; j < n && x[i] < 0; ++j)
                if (fx[k] + fy[j] == b[k][j] && t[j] < 0) {
                    s[++q] = y[j], t[j] = k;
                    if (s[q] < 0)
                        for (p = j; p >= 0; j = p)
                            y[j] = k = t[j], p = x[k], x[k] = j;
                }
        if (x[i] < 0) {
            double d = LONG_MAX;
            for (int k = 0; k <= q; ++k)
                for (int j = 0; j < n; ++j)
                    if (t[j] < 0) d = std::min(d, fx[s[k]] + fy[j] - b[s[k]][j]);
            for (int j = 0; j < n; ++j) fy[j] += (t[j] < 0 ? 0 : d);
            for (int k = 0; k <= q; ++k) fx[s[k]] -= d;
        } else ++i;
    }
    
    // 結果計算
    double min_max = APConstants::MAX_SATISFACTION;
    wk_solution_value[0] = 0;  // 合計
    wk_solution_value[2] = 0;  // 逆数の合計
    
    for(int i=0; i<n; i++){
        double satis = a[i][x[i]];  // 元のコスト行列から満足度を取得
        
        wk_solution_value[0] += satis;           // 合計
        wk_solution_value[2] += 1.0 / satis;    // 逆数の合計
        
        if(satis < min_max){
            min_max = satis;  // 最小値
        }
    }
    
    wk_solution_value[1] = min_max;  // 最小値セット
    
    //基地局Noの算出
    for(int i=0; i<n; i++){
        wk_solution_station[i] = combi_ap_term[x[i]];
    }
    

    
    // HungarianResult構造体を作成
    HungarianResult result;
    result.maxSum = wk_solution_value[0];
    result.maxSum_r = wk_solution_value[0] / n;
    result.min = wk_solution_value[1];
    result.Sum = wk_solution_value[0];
    result.Harmean = n / wk_solution_value[2];
    result.combiApTermArray = wk_solution_station;
    
    return result;
}

// AP性能計算（接続台数を考慮したRTT/TP計算）
void APselection::calculate_ap_performance(const std::vector<int>& num_ap_access, 
                                         std::vector<double>& ap_rtt_current, 
                                         std::vector<double>& ap_tp_current) {
    for(int j=0; j<aps; j++){
        ap_rtt_current[j] = init_rtt[j] + incRTT[j] * num_ap_access[j];
        ap_tp_current[j] = init_tp[j] - incTP[j] * num_ap_access[j];
        if(ap_tp_current[j] <= 0) ap_tp_current[j] = APConstants::MIN_TP_THRESHOLD;
    }
}

// 端末満足度行列の作成
void APselection::create_satisfaction_matrix(const std::vector<int>& combi_ap_term,
                                           const std::vector<double>& ap_rtt_current,
                                           const std::vector<double>& ap_tp_current,
                                           std::vector<std::vector<double>>& mat_ent) {
    for(int j=0; j<terms; j++) {
        for(int k=0; k<terms; k++) {
            int distAp = combi_ap_term[j] - 1;  // 基地局番号（0ベース）
            mat_ent[j][k] = calculate_satisfaction(k, distAp, ap_rtt_current, ap_tp_current);
        }
    }
}

// 最適解の検索（調和平均最大）
int APselection::find_best_solution(const std::vector<HungarianResult>& results) {
    double best_harmean = 0;
    int best_index = 0;
    
    for(size_t i = 0; i < results.size(); i++){
        if(results[i].Harmean > best_harmean){
            best_harmean = results[i].Harmean;
            best_index = i;
        }
    }
    
    std::cout << "\n■調和平均の最大値: " << best_harmean << " (組み合わせ番号: " << best_index << ")\n";
    return best_index;
}

// 最終結果の表示（監視端末用に簡略化）
void APselection::display_final_results(const HungarianResult& final_result, int best_index) {
    std::cout << "\n===== ハンガリアン法による基地局割り当て =====" << "\n";

    // 監視端末方式では詳細な配列表示は不要（50台分の配列は長すぎる）
    std::cout << "監視端末RTT値による最適化完了 (端末数: " << terms << "台)" << "\n";

    // 各APの接続台数のみ表示
    std::vector<int> count_ap(aps, 0);
    for(int i = 0; i < terms; i++){
        int ap_id = final_result.combiApTermArray[i]; // 1ベース
        if(ap_id >= 1 && ap_id <= aps) {
            count_ap[ap_id - 1]++;
        }
    }
    std::cout << "各基地局接続台数: ";
    for(int j = 0; j < aps; j++){
        std::cout << "AP" << (j+1) << ":" << count_ap[j] << "台 ";
    }
    std::cout << "\n";

    // 調和平均（小数6桁で統一）
    std::cout << "■ハンガリアン法の調和平均: " << std::fixed << std::setprecision(6) << final_result.Harmean << "\n";
}

// 端末の満足度計算（共通ロジック）
double APselection::calculate_satisfaction(int terminal_idx, int ap_idx, 
                                         const std::vector<double>& ap_rtt, 
                                         const std::vector<double>& ap_tp) {
    int appNum = m_use_appli[terminal_idx];
    double satis = 0;
    
    if(appNum == static_cast<int>(APConstants::AppType::BROWSER) || 
       appNum == static_cast<int>(APConstants::AppType::VIDEO)) {
        // TP指標
        double needTp = m_need[terminal_idx];
        satis = ap_tp[ap_idx] / needTp;
    } else {
        // RTT指標
        double needRtt = m_need[terminal_idx];  
        satis = needRtt / ap_rtt[ap_idx];
    }
    
    return satis;
}

// 調和平均計算（共通ロジック）
double APselection::calculate_harmonic_mean(const std::vector<int>& solution,
                                          const std::vector<double>& ap_rtt,
                                          const std::vector<double>& ap_tp) {
    double satis_sum_r = 0;  // 満足度の逆数の合計
    
    for(int i=0; i<terms; i++){
        int ap_index = solution[i] - 1;  // AP番号（0ベース）
        double satis = calculate_satisfaction(i, ap_index, ap_rtt, ap_tp);
        satis_sum_r += 1.0 / satis;
    }
    
    return terms / satis_sum_r;
}

//ランダム法
void APselection::combi_random(){
    
    std::vector<int> count_ap(aps, 0);
    int temp_ap;
    std::vector<int> random_solution(terms, 0);
    std::random_device rand;
    std::mt19937 mt(rand());
    std::uniform_int_distribution<> rand3(0, aps-1);  // 0~2 (AP1~AP3)
    
    std::cout << "\n===== ランダム法による基地局割り当て =====" << "\n";
    
    // ランダムに基地局を割り当て（収容数制限考慮）
    for(int i=0; i<terms; ){
        temp_ap = rand3(mt);
        if(count_ap[temp_ap] < m_capa[temp_ap]){
            random_solution[i] = temp_ap + 1;  // AP番号は1ベース
            count_ap[temp_ap]++;
            i++;
        }
    }
    
    // 各APのRTT, TP値を計算（接続数を考慮）
    std::vector<double> ap_rtt_random(aps);
    std::vector<double> ap_tp_random(aps);
    
    for(int j=0; j<aps; j++){
        ap_rtt_random[j] = init_rtt[j] + incRTT[j] * count_ap[j];
        ap_tp_random[j] = init_tp[j] - incTP[j] * count_ap[j];
        if(ap_tp_random[j] <= 0) ap_tp_random[j] = APConstants::MIN_TP_THRESHOLD;
    }
    
    // 端末満足度の調和平均を計算（共通ロジック使用）
    double random_harmean = calculate_harmonic_mean(random_solution, ap_rtt_random, ap_tp_random);
    
    // 結果表示
    std::cout << "ランダム法結果: [";
    for(int i=0; i<terms; i++){
        std::cout << random_solution[i] << " ";
    }
    std::cout << "]" << "\n";
    
    std::cout << "各基地局接続台数: ";
    for(int j=0; j<aps; j++){
        std::cout << "AP" << (j+1) << ":" << count_ap[j] << "台 ";
    }
    std::cout << "\n";
    
    std::cout << "■ランダム法の調和平均: " << std::fixed << std::setprecision(6) << random_harmean << "\n";
    std::cout << "■ハンガリアン法の調和平均: " << std::fixed << std::setprecision(6) << m_best_solution_value[0] << "\n";
    
    double improvement = m_best_solution_value[0] / random_harmean;
    std::cout << "■性能向上率: " << std::fixed << std::setprecision(2) << improvement << "倍\n";
}

// Greedy法
void APselection::combi_greedy(){

    // Greedy: 各端末を順に、現時点のAP性能（仮に自身を追加した場合の性能）で
    // 満足度が最大となるAPに割り当てる（容量制約を順守）

    std::vector<int> count_ap(aps, 0);
    std::vector<int> greedy_solution(terms, 0);

    // 端末ごとに最良APを貪欲に選択
    for(int i = 0; i < terms; i++){
        int best_ap = -1;
        double best_satis = -1.0;

        for(int j = 0; j < aps; j++){
            if(count_ap[j] >= m_capa[j]){
                continue; // 容量オーバーのAPはスキップ
            }

            // 仮の接続数（この端末をAP jに追加する）
            std::vector<int> tmp_counts = count_ap; // 前回の値をそのままコッピ― 
            tmp_counts[j]++;

            // 仮のAP性能（全APを再計算：他APは変化しないが簡潔さのため全再計算）
            std::vector<double> ap_rtt_tmp(aps, 0.0);
            std::vector<double> ap_tp_tmp(aps, 0.0);
            for(int h = 0; h < aps; h++){
                ap_rtt_tmp[h] = init_rtt[h] + incRTT[h] * tmp_counts[h];
                ap_tp_tmp[h]  = init_tp[h]  - incTP[h]  * tmp_counts[h];
                if(ap_tp_tmp[h] <= 0) ap_tp_tmp[h] = APConstants::MIN_TP_THRESHOLD;
            }

            // 端末iがAP jを選んだ場合の自身の満足度
            double satis = calculate_satisfaction(i, j, ap_rtt_tmp, ap_tp_tmp);

            if(satis > best_satis){
                best_satis = satis;
                best_ap = j;
            }
        }

        // 割り当て確定
        if(best_ap < 0){
            // 全AP満杯のはずだが、ガードとして最小接続APに入れる
            best_ap = std::min_element(count_ap.begin(), count_ap.end()) - count_ap.begin(); //保険として各APの中で現在最小の接続台数のところに入れる
        }
        greedy_solution[i] = best_ap + 1; // 1ベース
        count_ap[best_ap]++;
    }

    // 最終的なAP性能（最終接続数に基づく）
    std::vector<double> ap_rtt_greedy(aps, 0.0);
    std::vector<double> ap_tp_greedy(aps, 0.0);
    for(int j = 0; j < aps; j++){
        ap_rtt_greedy[j] = init_rtt[j] + incRTT[j] * count_ap[j];
        ap_tp_greedy[j]  = init_tp[j]  - incTP[j]  * count_ap[j];
        if(ap_tp_greedy[j] <= 0) ap_tp_greedy[j] = APConstants::MIN_TP_THRESHOLD;

        // std::cout << "AP" << (j+1) << "のRTT: " << ap_rtt_greedy[j] << " TP: " << ap_tp_greedy[j] << "\n";
    }

    // 調和平均を算出
    double greedy_harmean = calculate_harmonic_mean(greedy_solution, ap_rtt_greedy, ap_tp_greedy);



    // 結果表示（監視端末用に簡略化）
    std::cout << "\n===== Greedy法による基地局割り当て =====" << "\n";
    std::cout << "監視端末RTT値によるGreedy法最適化完了 (端末数: " << terms << "台)" << "\n";

    std::cout << "各基地局接続台数: ";
    for(int j = 0; j < aps; j++){
        std::cout << "AP" << (j+1) << ":" << count_ap[j] << "台 ";
    }
    std::cout << "\n";

    std::cout << "■Greedy法の調和平均: " << std::fixed << std::setprecision(6) << greedy_harmean << "\n";
    std::cout << "■ハンガリアン法の調和平均: " << std::fixed << std::setprecision(6) << m_best_solution_value[0] << "\n";
    double improvement = m_best_solution_value[0] / greedy_harmean;
    std::cout << "■性能向上率: " << std::fixed << std::setprecision(2) << improvement << "倍\n";
}

}
