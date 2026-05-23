import math as Math
import numpy as np
# from munkres import Munkres
import copy
from typing import List
from functools import reduce
import csv
import os
import sys

from simulation.config import load_app_config, load_sim_config
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造
from simulation.services import cal
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.services.model import TERM_AP, TERM_AP_NFSPL

# アプリケーション種類ごとの設定
confApp = load_app_config()
confSim = load_sim_config()


FIX_DIGIT = Math.pow(10, 6)  # コスト値 int変換桁数(float->int)


class hungarianResult():
    maxSum: float
    maxSum_r: float
    min: float
    Sum: float
    Harmean: float
    combiApTermArray: List[int]


class hungarianResult_fspl():
    maxSum: float
    maxSum_r: float
    min: float
    Sum: float
    Harmean: float
    combiApTermArray: List[int]


class hungarianResult_nonfspl():
    maxSum: float
    maxSum_r: float
    min: float
    Sum: float
    Harmean: float
    combiApTermArray: List[int]


def kumiawase(data, nukitoriNum: float):
    N = len(data)
    arrs: List[List[float]] = []

    if nukitoriNum == 1:
        i = 0
        while i < N:
            arrs.append([data[i]])
            i += 1

    else:
        j = 0
        i = 0
        count = N - nukitoriNum + 1
        while i < count:
            # print(data[(i + 1):])
            ts = kumiawase(data[(i + 1):], nukitoriNum - 1)
            for t in ts:
                t.insert(0, data[i])
                # print(t)
                arrs.append(t)
                j += 1

            i += 1
    return arrs


def makeCombiApTerm(terms: List[Term], aps: List[Ap]):
    n = len(terms) + len(aps) - 1
    r = len(aps) - 1
    flg_over_num_ap: bool = True
    data = np.empty(n, dtype=float)
    division: List[List[float]] = [[]]

    count_ap = np.zeros(len(aps))
    combi_ap_term: List = []
    combi_ap_term_tmp: List = []  # 全組み合わせ

    # 初期配列の準備
    for i in range(len(aps)):
        count_ap[i] = 0  # 各基地局（仮想）の接続台数を0
    for i in range(n):
        data[i] = i

    # 仕切り位置の組み合わせを出力
    # console.log(n, r)
    division = kumiawase(data, r)
    # print(len(division))

    # 基地局組み合わせ洗い出し
    for i in range(len(division)):
        no_divi: int = 0
        no_ap: int = 1
        j: int = 0
        wk_combi_ap_term_tmp: List[float] = []
        wk_combi_ap_term: List[float] = []

        while j < n:
            if j == division[i][no_divi]:
                # console.log('div[i][no_div]', division[i][no_divi])
                wk_combi_ap_term_tmp.append(0)
                no_ap += 1
                if no_divi == 0:
                    no_divi += 1
            else:
                wk_combi_ap_term_tmp.append(no_ap)

            j += 1

        k = 0
        j = 0
        while j < n:
            if wk_combi_ap_term_tmp[j] != 0:
                wk_combi_ap_term.append(wk_combi_ap_term_tmp[j])
                k += 1

            j += 1

        combi_ap_term_tmp.append(wk_combi_ap_term.copy())
        # print(len(wk_combi_ap_term))
        # print(len(combi_ap_term_tmp))

    # with open('kari.csv', mode='w', newline='', encoding='utf-8') as file:
    #     writer = csv.writer(file)
    #     for row in combi_ap_term_tmp:
    #         writer.writerow(row)

    # ------------------------------------------------------------------------------

    for i in range(len(combi_ap_term_tmp)):
        num_ap_accsess: List = []  # 【検討】基地局接続台数

        # AP毎の収容数をカウント
        for j in range(len(terms)):
            count_ap[combi_ap_term_tmp[i][j] - 1] += 1

        # 1つでも収容数を超えているAPは不採用
        for j in range(len(aps)):
            if count_ap[j] > aps[j].termCapa:
                flg_over_num_ap = False

        # 採用した組み合わせのみをリストに追加
        if flg_over_num_ap == True:
            combi_ap_term.append(combi_ap_term_tmp[i])
            # print(combi_ap_term_tmp[i])
            num_ap_accsess.append(count_ap)  # 【検討】接続台数カウント
            # print("接続台数："+ str(num_ap_accsess))

        # フラグを初期化
        flg_over_num_ap = True

        for j in range(len(aps)):
            count_ap[j] = 0

    # print(combi_ap_term)
    # print(len(combi_ap_term))
    return combi_ap_term_tmp


def call_hungarian(terms: List[Term], aps: List[Ap], term_ap_relations: List[List[TERM_AP]], term_ap_relations_nfspl: List[List[TERM_AP_NFSPL]]):
    hungarianResultAll: List[hungarianResult] = []
    hungarianResult_fsplAll: List[hungarianResult_fspl] = []
    hungarianResult_nonfsplAll: List[hungarianResult_nonfspl] = []
    index_of_max: int = 0
    index_of_max_fspl: int = 0
    index_of_max_nonfspl: int = 0

    # munkeres-------------------------------------------------------------------------------
    # hunres: List = []

    # 行: 組み合わせパターン, 列: 各端末端末
    COMBI_AP_TERM = makeCombiApTerm(terms, aps)
    # print(len(COMBI_AP_TERM))

    # ハンガリアン法計算用の仮想端末と基地局を生成(インスタンスをコピー) */
    APS_VIRTUAL: List[Ap] = aps
    TERMS_VIRTUAL: List[Term] = terms
    costMatrix = np.zeros((len(TERMS_VIRTUAL), len(TERMS_VIRTUAL)))
    costMatrix_fspl = np.zeros((len(TERMS_VIRTUAL), len(TERMS_VIRTUAL)))
    costMatrix_nonfspl = np.zeros((len(TERMS_VIRTUAL), len(TERMS_VIRTUAL)))

    for i in range(len(COMBI_AP_TERM)):
        # 対象の全組み合わせ（パターン）でハンガリアン法実行
        # 接続時RTT, 接続時TPを算出
        # cal.calLink(termsVirtual, apsVirtual)
        """
        ハンガリアン法に引き渡すコスト行列を生成
            行: 基地局リソース, 列: 端末, 値: 端末満足度
            正方行列
        """
        # print("-------------------------------------------------------------")
        # print("入力の組み合わせ", COMBI_AP_TERM[i])
        for j in range(len(terms)):  # 基地局リソース（=端末数）
            # print(TERMS_VIRTUAL[j].appNum)
            for l in range(len(terms)):
                if COMBI_AP_TERM[i][l] == 1:
                    TERMS_VIRTUAL[l].setSwitchAp(0)
                elif COMBI_AP_TERM[i][l] == 2:
                    TERMS_VIRTUAL[l].setSwitchAp(1)
                else:
                    TERMS_VIRTUAL[l].setSwitchAp(2)
            # 接続時RTT & 接続時TP計算
            # cal.sumTermAp(TERMS_VIRTUAL, APS_VIRTUAL)
            cal.calLink(TERMS_VIRTUAL, APS_VIRTUAL, confSim["appUseSec"])

            for k in range(len(terms)):  # 端末数
                # print("端末番号:", k+1)
                # 組み合わせパターンからデータ構造変換 (=接続k切り替え)
                distAp = COMBI_AP_TERM[i][j] - 1  # 基地局番号1を index 0 にする
                # print("dist:",distAp+1)
                TERMS_VIRTUAL[k].setSwitchAp(distAp)
                # 自由空間の重みを算出
                selected_term = term_ap_relations[k]
                selected_ap = selected_term[TERMS_VIRTUAL[k].apBssid]
                rate_fspl = selected_ap.rate
                # 非自由空間の重みを算出
                selected_term_nonfspl = term_ap_relations_nfspl[k]
                selected_ap_nonfspl = selected_term_nonfspl[TERMS_VIRTUAL[k].apBssid]
                rate_nonfspl = selected_ap_nonfspl.rate

                # 端末満足度計算
                # print("端末番号:", k, "接続先:", TERMS_VIRTUAL[k].apBssid, APS_VIRTUAL[TERMS_VIRTUAL[k].apBssid].tp, rate)
                satis, satis_fspl, statis_nonfspl = cal.calSatisTerm_a(
                    TERMS_VIRTUAL[k], APS_VIRTUAL, rate_fspl, rate_nonfspl)
                costMatrix[j][k] = round(satis, 6)
                costMatrix_fspl[j][k] = round(satis_fspl, 6)
                costMatrix_nonfspl[j][k] = round(statis_nonfspl, 6)
                # print("端末満足度", satis)
                # costMatrix[j][k] = satis

            # 基地局接続台数算出
            # cal.sumTermAp(TERMS_VIRTUAL, APS_VIRTUAL)
        # print("N：", costMatrix)
        # print("F:", costMatrix_fspl)
        # print("NF:", costMatrix_nonfspl)
        # -------------------------------------------------------------------------------print

        """
        組み合わせごとにハンガリアン法を試行
        端末満足度最大の組み合わせを選択・端末満足度の調和平均値を算出
        Object: hungarianResult
        """

        # munkeres-------------------------------------------------------------------------------
        # m = Munkres().compute(copy.copy(costMatrix))
        # asum = sum([costMatrix[idx] for idx in m])
        # hunres.append(asum)
        # print("モジュの満足度合計", hunres[i])
        # print("モジュの結果座標", m)

        # 伝搬損失を考慮する部分をここで制御
        HUNGARIAN_RESULT: hungarianResult = hungarian(
            costMatrix, COMBI_AP_TERM[i])
    #     HUNGARIAN_RESULT: hungarianResult = Parallel(n_jobs=-1)(
    #     delayed(run_hungarian)(costMatrix, combi) for combi in COMBI_AP_TERM
    # )
        # 自由空間のハンガリアン法
        HUNGARIAN_RESULT_FSPL: hungarianResult_fspl = hungarian(
            costMatrix_fspl, COMBI_AP_TERM[i])
    #     HUNGARIAN_RESULT_PROP: hungarianResult_fspl = Parallel(n_jobs=-1)(
    #     delayed(run_hungarian)(costMatrix_fspl, combi) for combi in COMBI_AP_TERM
    # )
        # 非自由空間のハンガリアン法
        HUNGARIAN_RESULT_NONFSPL: hungarianResult_nonfspl = hungarian(
            costMatrix_nonfspl, COMBI_AP_TERM[i])

        hungarianResultAll.append(HUNGARIAN_RESULT)
        hungarianResult_fsplAll.append(HUNGARIAN_RESULT_FSPL)
        hungarianResult_nonfsplAll.append(HUNGARIAN_RESULT_NONFSPL)
        # print(HUNGARIAN_RESULT.max, HUNGARIAN_RESULT.combiApTermArray, "\n")
    # print("-------------------------------------------------------------")
    # -------------------------------------------------------------------------------print

    # 端末満足度最大かつ最小値最大の組み合わせを選択

    # munkeres-------------------------------------------------------------------------------
    # hunres_max: float = 0
    # c = 0
    # for bk in hunres:
    #     c += 1
    #     if bk > hunres_max:
    #         hunres_max = bk
    #         d = c
    # print(hunres_max, d)

    # 最大満足度（maxSum）を持つ組み合わせを選択
    # HUNGARIAN_MAX_VALUE = max(result.maxSum for result in hungarianResultAll)

    # 最大値をとる巡目（インデックス）を取得 検証用
    index_of_max = max(enumerate(hungarianResultAll),
                       key=lambda x: x[1].Harmean)[0]
    index_of_max_fspl = max(
        enumerate(hungarianResult_fsplAll), key=lambda x: x[1].Harmean)[0]
    index_of_max_nonfspl = max(
        enumerate(hungarianResult_nonfsplAll), key=lambda x: x[1].Harmean)[0]
    # 最大値自体を取得（これは上記コードと同様）
    HUNGARIAN_MAX_VALUE = hungarianResultAll[index_of_max].Harmean
    HUNGARIAN_MAX_VALUE_FSPL = hungarianResult_fsplAll[index_of_max_fspl].Harmean
    HUNGARIAN_MAX_VALUE_NONFSPL = hungarianResult_nonfsplAll[index_of_max_nonfspl].Harmean

    # HUNGARIAN_MAX_VALUE :float = 0
    # a = 0
    # for hungarianResultOne in hungarianResultAll:
    #     a += 1
    #     if hungarianResultOne.maxSum > HUNGARIAN_MAX_VALUE:
    #         HUNGARIAN_MAX_VALUE = hungarianResultOne.maxSum
    #         b = a
    # maxSum 最大を返す
    # print("端末満足度の最大値", HUNGARIAN_MAX_VALUE, "\n選ばれた組み合わせ番号", b)
    print("調和平均の最大値", HUNGARIAN_MAX_VALUE, "最大値の場合の組み合わせ番", index_of_max)
    print("調和平均の最大値（FSPL）", HUNGARIAN_MAX_VALUE_FSPL,
          "最大値の場合の組み合わせ番", index_of_max_fspl)
    print("調和平均の最大値（NONFSPL）", HUNGARIAN_MAX_VALUE_NONFSPL,
          "最大値の場合の組み合わせ番", index_of_max_nonfspl)
    # -------------------------------------------------------------------------------print

    # 最大値であるレコードを抽出

    def filterMax(res, value):
        return res if res.Harmean >= value else None

    # def filterMax(res: hungarianResult, value: float):
    #     if(res.maxSum > value) :
    #         return False
    #     else:
    #         return True

    # resArray : List[hungarianResult] = []
    resArray = list(filter(lambda res: filterMax(
        res, HUNGARIAN_MAX_VALUE), hungarianResultAll))
    resArray_fspl = list(filter(lambda res_fspl: filterMax(
        res_fspl, HUNGARIAN_MAX_VALUE_FSPL), hungarianResult_fsplAll))
    resArray_nonfspl = list(filter(lambda res_nonfspl: filterMax(
        res_nonfspl, HUNGARIAN_MAX_VALUE_NONFSPL), hungarianResult_nonfsplAll))
    # resArray = list(filter(lambda x:filterMax(x, HUNGARIAN_MAX_VALUE), hungarianResultAll))
    # print(len(resArray))
    # print(resArray)

    # 最小値最大を選択
    res = max(resArray, key=lambda r: r.min)
    res_fspl = max(resArray_fspl, key=lambda r_fspl: r_fspl.min)
    res_nonfspl = max(resArray_nonfspl, key=lambda r_nonfspl: r_nonfspl.min)

    # cur = hungarianResultAll[0].min
    # for res in resArray:
    #     if res.min < cur:
    #         RES = res

    # 接続先切り替え
    for i in range(len(terms)):
        terms[i].setSwitchAp(res.combiApTermArray[i] - 1)
        # terms[i].setSwitchAp_fspl(res_fspl.combiApTermArray[i] - 1)
        # print(terms[i].apBssid + 1)
        # print(str(res.combiApTermArray[i]-1))
        # print(str(i)+ 'Dist AP'+ str(res.combiApTermArray[i]-1))

    print("各端末の接続先:", res.combiApTermArray)
    print("各端末の接続先（FSPL）:", res_fspl.combiApTermArray)
    print("各端末の接続先（NONFSPL）:", res_nonfspl.combiApTermArray)

    count_fspl = 0
    count_nonfspl = 0
    for i in range(len(terms)):
        if res.combiApTermArray[i] != res_fspl.combiApTermArray[i]:
            count_fspl += 1
        if (res.combiApTermArray[i] != res_nonfspl.combiApTermArray[i]):
            count_nonfspl += 1
    print("不一致数（FSPL）:", count_fspl)
    print("不一致数（NONFSPL）:", count_nonfspl)

    # console.log(res)
    return count_fspl, count_nonfspl, HUNGARIAN_MAX_VALUE_FSPL, HUNGARIAN_MAX_VALUE_NONFSPL


"""
    ハンガリアン法
    input: コスト行列
    output: 端末満足度の調和平均, 組み合わせパターン
"""
# def run_hungarian(costMatrix, combi):
#     return hungarian(costMatrix, combi)


def hungarian(costMatrix, combi_ap_term: List[List[float]]):
    # test
    # let a: number[] = [1, 2, 3]
    # let xa: number[] = []
    # xa[0] = a[2]
    # let xx: number = a[2]
    # a[2] = 5
    # console.log(xx, xa)
    # print(combi_ap_term)

    N: int = len(costMatrix[0])
    b = np.empty((N, N))
    wk_solution_value = np.empty(3)
    wk_solution_station = np.full(N, -1)
    nomi_solution_value: List = [[]]
    nomi_solution_station: List = []
    no_conbi_ap_term: int = 0

    for i in range(N):
        for j in range(N):
            b[i][j] = Math.floor(costMatrix[i][j] * FIX_DIGIT)

    fx = np.full(N, -sys.maxsize)
    fy = np.zeros(N)
    x = np.full(N, -1)
    y = np.full(N, -1)
    for i in range(N):
        for j in range(N):
            fx[i] = max(fx[i], b[i][j])

    i = 0
    while (i < N):
        t: List = []
        s: List = []
        j = 0

        while (j < N):
            t.append(-1)
            s.append(i)
            j += 1

        s.append(i)

        p = 0
        q = 0

        while (p <= q and x[i] < 0):
            k = s[p]
            j = 0
            while (j < N and x[i] < 0):
                if (fx[k] + fy[j] == b[k][j] and t[j] < 0):
                    q += 1
                    s[q] = y[j]
                    t[j] = k
                    if (s[q] < 0):
                        p = j
                        while (p >= 0):
                            y[j] = t[j]
                            k = t[j]
                            p = x[k]
                            x[k] = j
                            j = p
                j += 1
            p += 1

        if (x[i] < 0):
            d = sys.maxsize
            k = 0
            while (k <= q):
                j = 0
                while (j < N):
                    if (t[j] < 0):
                        d = min(d, fx[s[k]] + fy[j] - b[s[k]][j])
                    j += 1
                k += 1
            j = 0

            while (j < N):
                if t[j] < 0:
                    fy[j] += 0
                else:
                    fy[j] += d
                j += 1

            k = 0
            while (k <= q):
                fx[s[k]] -= d
                k += 1
        else:
            i += 1

    nomi_solution: List = []
    nomi_solution.append(x)
    # print("ハンガリアン結果座標", x)
    # print("ハンガリアン結果座標",nomi_solution)
    # print("ハンガリアン結果座標", combi_ap_term)

    # /* 調和平均の最大 かつ 最低値が最大の組み合わせを選択 */
    # 端末満足度の逆数の合計の算出(逆数の合計を返すため不要
    # 重み合計,最低値の算出
    min_max: float = 100
    wk_solution_value[0] = 0
    wk_solution_value[2] = 0
    for i in range(N):  # n = 対象組み合わせ数

        # satis: float = b[i][x[i]] / FIX_DIGIT # - ??
        satis: float = costMatrix[i][x[i]]
        # print(satis)
        wk_solution_value[0] += satis  # 合計
        wk_solution_value[2] += 1 / round(satis, 6)  # 逆数の合計
        # print(wk_solution_value[2])

        if (satis < min_max):  # 最低値の算出（=逆数の最大値を求める）
            min_max = satis

    # print(r_max)
    # mini = 1 / min_max # 逆数の最大値が元の最低値
    wk_solution_value[1] = min_max  # 最小値セット
    # print("満足度合計", wk_solution_value[0])
    # print("最大最小値", wk_solution_value[1])
    # print("逆数合計", round(wk_solution_value[2], 6))
    # print("調和平均", N / round(wk_solution_value[2], 6))

    # nomi_solution_value.push(wk_solution_value.slice())

    # comma_separated_array = x.tolist()

    # # 基地局Noの算出
    for i, new_index in enumerate(x):
        wk_solution_station[new_index] = combi_ap_term[i]
        # wk_solution_station.append(combi_ap_term[no_conbi_ap_term][x[i]])
        # print("b", wk_solution_station)

    nomi_solution_station.append(wk_solution_station)
    # print("組み合わせ", nomi_solution_station[0])

    # print("wk:"+str(wk_solution_value[0])+", N:" +str (N))

    RESULT = hungarianResult()
    RESULT.Harmean = N / round(wk_solution_value[2], 6)
    RESULT.maxSum_r = wk_solution_value[0] / N
    RESULT.Sum = wk_solution_value[0]
    RESULT.min = wk_solution_value[1] + 0
    RESULT.combiApTermArray = nomi_solution_station[0]

    wk_solution_station = []

    # console.log('hungarinan: ', result.maxSum_r)
    # print(RESULT.maxSum)
    return RESULT
