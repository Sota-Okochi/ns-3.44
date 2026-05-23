# ここで距離などの数値を乱数によって生成（5~30m, WiFi基地局電波が届く範囲）
# その数値をモデルに代入（FSPL）して損失を計算（一つの端末につき3つの基地局までの距離をFSPLに代入し, そのFSPLの増を格納する[[3], [3]...]）
# それを通信品質に付加する

import math
import random
from typing import List
from dataclasses import dataclass

from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造

@dataclass
class TERM_AP:
    term_id: int      # 端末ID
    ap_id: int        # 基地局ID
    distance: float   # 距離
    fspl: float       # FSPL値
    rssi: float  # RSSI値
    snr: float  # SNR値（線形スケール）
    shanon: float # 通信容量
    rate: float # 重み

@dataclass
class TERM_AP_NFSPL:
    term_id: int      # 端末ID
    ap_id: int        # 基地局ID
    rssi: float  # RSSI値
    snr: float  # SNR値（線形スケール）
    shanon: float # 通信容量
    rate: float # 重み


# 端末につき3つの基地局までの距離を乱数で生成し、構造体で管理
def fspl(terms: List["Term"], aps: List["Ap"]) -> List[List[TERM_AP]]:
    term_ap_relations = []  # すべての端末と基地局の関係を格納するリスト

    #固定値
    ap_tssi = 20 # 送信信号強度(dBm)
    noise_snr = -100 # ノイズフロア(dBm)
    bandwith = 100 # 帯域幅（MHz

    # 検査用端末の値を設定
    ins_distance = 5
    ins_fspl = cal_fspl(ins_distance)  # FSPL値
    ins_rssi = cal_rssi(ins_fspl, ap_tssi)  # RSSI値
    ins_snr = cal_snr(ins_rssi, noise_snr) # SNRの線形スケール
    ins_shanon = cal_shanon(ins_snr, bandwith) # シャノン・ハートレーの定理, 通信容量(理論最大値)


    for term_index in range(len(terms)):
        term_aps = []  # 1端末とすべての基地局の関係を格納するリスト
        for ap_index in range(len(aps)):
            #各端末の値を設定
            distance = random.uniform(6, 50)
            fspl = cal_fspl(distance)
            rssi = cal_rssi(fspl, ap_tssi)
            snr = cal_snr(rssi, noise_snr)
            shanon = cal_shanon(snr, bandwith)
            shanon_rate = shanon / ins_shanon

            # TERM_APインスタンスを作成しリストに追加
            term_ap_relation = TERM_AP(
                term_id = term_index +1,
                ap_id = ap_index +1,
                distance = distance,
                fspl = fspl,
                rssi = rssi,
                snr = snr,
                shanon = shanon,
                rate = shanon_rate
            )
            term_aps.append(term_ap_relation)
        term_ap_relations.append(term_aps)

    # print("検査用端末の値", ins_rssi)
    # # 構造体として管理されているデータを出力
    # for term_aps in term_ap_relations:
    #     for relation in term_aps:
    #         print(relation)

    return term_ap_relations


# 自由空間伝搬損失のモデルに距離を代入し, 損失を計算（dB表記）
def cal_fspl(distance):
    return 20 * math.log10(4 * math.pi * distance * 5.0e9 / 299792458)

#RSSI値の計算
def cal_rssi(fspl, tssi):
    return tssi - fspl

#SNRの出力, dBスケールから線形スケールへの変換
def cal_snr(rssi, noise):
    snr = rssi - (noise)
    return math.pow(10, snr/10) 

#シャノン・ハートレーの定理, 通信容量の算出
def cal_shanon(snr,band):
    return band * math.log2(1 + snr)


def nfspl(terms: List["Term"], aps: List["Ap"]) -> List[List[TERM_AP_NFSPL]]:
    term_ap_relations_nfspl = []

    noise_snr = -100 # ノイズフロア(dBm)
    bandwith = 100 # 帯域幅（MHz）

    # 検査用端末の値を設定
    ins_rssi = -40 # [dBm]
    ins_snr = cal_snr(ins_rssi, noise_snr) # SNRの線形スケール
    # print(ins_snr)
    ins_shanon = cal_shanon(ins_snr, bandwith) # シャノン・ハートレーの定理, 通信容量(理論最大値)
    # print(ins_shanon)


    for term_index in range(len(terms)):
        term_aps_nfspl = []  # 1端末とすべての基地局の関係を格納するリスト
        for ap_index in range(len(aps)):
            #各端末の値を設定
            rssi = random.uniform(-57, -75) # FSPL_rssi - 15 
            snr = cal_snr(rssi, noise_snr)
            shanon = cal_shanon(snr, bandwith)
            shanon_rate = shanon / ins_shanon

            # TERM_APインスタンスを作成しリストに追加
            term_ap_relation_nfspl = TERM_AP_NFSPL(
                term_id = term_index +1,
                ap_id = ap_index +1,
                rssi = rssi,
                snr = snr,
                shanon = shanon,
                rate = shanon_rate
            )
            term_aps_nfspl.append(term_ap_relation_nfspl)
        term_ap_relations_nfspl.append(term_aps_nfspl)

    # print("検査用端末の値", ins_rssi)
    # # 構造体として管理されているデータを出力
    # for term_aps_nfspl in term_ap_relations_nfspl:
    #     for relation in term_aps_nfspl:
    #         print(relation)

    return term_ap_relations_nfspl




# -------------------------------------------------------------------------
    # メモ
    # ・重みが全組み合わせに対して付加された状態でハンガリアン法が行われるように修正
    #　　・伝搬損失の場合とそれ以外のデータを格納するクラスを指定する必要がある    
    # ・伝搬損失時の各端末の接続先情報の出力
# -------------------------------------------------------------------------
