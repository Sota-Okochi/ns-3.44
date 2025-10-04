/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef AP_SELECTION_H
#define AP_SELECTION_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"

#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <chrono>
#include <iostream>

namespace ns3 {

// === 定数定義 ===
namespace APConstants {
    // アプリケーション種別
    enum class AppType {
        BROWSER = 1,     // ブラウザ
        VIDEO = 2,       // 動画ストリーミング
        VOICE_CALL = 3,  // 通話アプリケーション
        LIVE_STREAM = 4  // ライブ配信
    };
    
    // システム設定
    constexpr int DEFAULT_TERMINALS = 100; //端末数
    constexpr int DEFAULT_ACCESS_POINTS = 3; //基地局数
    constexpr int DEFAULT_AP_CAPACITY = 1000; //基地局の収容数
    
    // // 初期基地局性能値
    const std::vector<double> INITIAL_RTT = {20.0, 30.0, 32.0};
    const std::vector<double> INITIAL_TP_MULTIPLIER = {65500 * 2 * 8 / 1024}; // KB/s計算用
    
    // 負荷による性能劣化
    constexpr double RTT_INCREASE_PER_TERMINAL = 5.0; //端末1台あたりのRTT増加量
    constexpr double TP_DECREASE_PER_TERMINAL = 1.0; //端末1台あたりのTP減少量
    constexpr double MIN_TP_THRESHOLD = 0.01; //TPの最小値
    
    // アプリケーション要求性能
    constexpr double BROWSER_REQUIRED_TP = 5.0;      // KB/s（ブラウザ）
    constexpr double VIDEO_REQUIRED_TP = 10.0;       // KB/s（動画ストリーミング）
    constexpr double VOICE_CALL_REQUIRED_RTT = 200.0; // ms（通話アプリケーション）
    constexpr double LIVE_STREAM_REQUIRED_RTT = 50.0; // ms（ライブ配信）
    
    // ハンガリアン法計算用
    constexpr double PRECISION_MULTIPLIER = 1000000.0; // 10^6
    constexpr double MAX_SATISFACTION = 100.0;
}

// PythonのhungarianResult相当の構造体
struct HungarianResult {
    double maxSum;
    double maxSum_r;
    double min;
    double Sum;
    double Harmean;
    std::vector<int> combiApTermArray;
};

class APselection : public Object{

public:
    APselection();
    virtual ~APselection();
    void init();
    void tmain();
    void setData(std::string senderIpAddress, std::string recvMessage);
    
private:
    void cal_need_rt();
    void make_combi_ap_term();
    void call_hungarian();
    HungarianResult hungarian(std::vector<std::vector<double> > &a, std::vector<int> &combi_ap_term);
    void combi_random();
    void combi_greedy();

    void calculate_ap_performance(const std::vector<int>& num_ap_access,
                                  std::vector<double>& ap_rtt_current,
                                  std::vector<double>& ap_tp_current);
    void create_satisfaction_matrix(const std::vector<int>& combi_ap_term,
                                    const std::vector<double>& ap_rtt_current,
                                    const std::vector<double>& ap_tp_current,
                                    std::vector<std::vector<double>>& mat_ent);
    int find_best_solution(const std::vector<HungarianResult>& results);
    void display_final_results(const HungarianResult& final_result, int best_index);

    // 共通計算ロジック
    double calculate_satisfaction(int terminal_idx, int ap_idx, 
                                const std::vector<double>& ap_rtt, 
                                const std::vector<double>& ap_tp);
    double calculate_harmonic_mean(const std::vector<int>& solution,
                                 const std::vector<double>& ap_rtt,
                                 const std::vector<double>& ap_tp);
    
    std::vector<std::vector<double> > m_real_rtt;   //各基地局ごとに計測したRTTデータ
    std::vector<double> m_link_rtt;                   //接続時のRTTデータ
    
    std::vector<double> init_rtt;                   //初期のRTTデータ
    std::vector<double> init_tp;                    //初期のTPデータ
    
    int m_termNum;                      //端末数
    int m_APNum;                        //基地局数
    int terms = APConstants::DEFAULT_TERMINALS;                     //端末数
    int aps = APConstants::DEFAULT_ACCESS_POINTS;                        //基地局数
    std::vector<double> incRTT;   //各基地局の端末1台あたりのRTT増加量
    std::vector<double> incTP;    //各基地局の端末1台あたりのTP減少量
    std::vector<double> m_appli_rtt;    //各アプリに必要なRTT
    std::vector<double> m_appli_tp;     //各アプリに必要なTP
    std::vector<int> m_use_appli;       //使用アプリ
    std::vector<int> m_capa;            //各基地局の収容数
    
    std::vector<double> m_need;      //必要RTT
    std::vector<std::vector<double> > m_rt_gap; //RTTギャップ
    std::vector<std::vector<int> > m_combi_ap_term; //基地局から端末数分を取る全組み合わせ
    std::vector<std::vector<int> > m_combi_ap_term_temp; //基地局から端末数分を取る全組み合わせ（制限考慮）
    int m_no_combi_ap_term;             //組み合わせNo
    std::vector<std::vector<int> > m_num_ap_access; //APに接続する端末数（RTT, TP計算用）

    std::vector<double> m_initialRtt;   // JSONから読み込んだ初期RTT
    
    std::vector<int> m_best_solution;           //重み最小解
    std::vector<double> m_best_solution_value;  //重み最小解の重み・分散
    std::vector<int> m_best_solution_station;   //重み最小解の基地局解
    std::vector<double> m_best_solution_ap_rtt; //重み最小解の各APのRTT
    std::vector<double> m_best_solution_ap_tp;  //重み最大解の各APのTP
    
    std::vector<std::vector<int> > m_nomi_solution;             //重み最小候補
    std::vector<std::vector<double> > m_nomi_solution_value;    //重み最小候補の重み・分散
    std::vector<std::vector<int> > m_nomi_solution_station;     //重み最小候補の基地局解
    std::vector<std::vector<double> > m_nomi_solution_ap_rtt;   //各組み合わせの各APのRTT
    std::vector<std::vector<double> > m_nomi_solution_ap_tp;    //各組み合わせの各APのTP

    // next_combination()の補完実装
    // http://www.programming-magic.com/20090123132035/
    template < class BidirectionalIterator >
    bool next_combination ( BidirectionalIterator first1 ,
      BidirectionalIterator last1 ,
      BidirectionalIterator first2 ,
      BidirectionalIterator last2 )
    {
      if (( first1 == last1 ) || ( first2 == last2 )) {
        return false ;
      }
      BidirectionalIterator m1 = last1 ;
      BidirectionalIterator m2 = last2 ; --m2;
      while (--m1 != first1 && !(* m1 < *m2 )){
      }
      bool result = (m1 == first1 ) && !(* first1 < *m2 );
      if (! result ) {
        while ( first2 != m2 && !(* m1 < * first2 )) {
          ++ first2 ;
        }
        first1 = m1;
        std :: iter_swap (first1 , first2 );
        ++ first1 ;
        ++ first2 ;
      }
      if (( first1 != last1 ) && ( first2 != last2 )) {
        m1 = last1 ; m2 = first2 ;
        while (( m1 != first1 ) && (m2 != last2 )) {
          std :: iter_swap (--m1 , m2 );
          ++ m2;
        }
        std :: reverse (first1 , m1 );
        std :: reverse (first1 , last1 );
        std :: reverse (m2 , last2 );
        std :: reverse (first2 , last2 );
      }
      return ! result ;
    }

    template < class BidirectionalIterator >
    bool next_combination ( BidirectionalIterator first ,
      BidirectionalIterator middle ,
      BidirectionalIterator last )
    {
      return next_combination (first , middle , middle , last );
    }

    // stringの指定文字区切り用
    std::vector<std::string> split(const std::string& input, char delimiter)
    {
        std::istringstream stream(input);
        
        std::string field;
        std::vector<std::string> result;
        while (std::getline(stream, field, delimiter)) {
            result.push_back(field);
        }
        return result;
    }   //split
    
};

}

#endif /* AP_SELECTION_H */
