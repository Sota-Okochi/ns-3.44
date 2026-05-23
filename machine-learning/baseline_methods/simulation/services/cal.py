from statistics import stdev, fmean
import numpy as np
from typing import List
import copy
import math

from simulation.config import load_app_config, load_sim_config
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造
from simulation.entities.term import Term  # 端末1台が持つデータ構造
import simulation.services.model as model

# アプリケーション種類ごとの設定
confApp = load_app_config()
confSim = load_sim_config()


init_RTT = confSim["initRTT"]
init_TP = 65500 * 2 * 8 / init_RTT / 1024


class termApp:
    indicator: str
    needRTT: float
    needTP: float

# 必要RTT,TPの算出


def calAppNeed(appNum: int):
    app = termApp()
    app.indicator = ''
    app.needRTT = 0
    app.needTP = 0

    if confApp[appNum]["indicator"] == 'tp':
        app.indicator = 'tp'
        app.needTP = confApp[appNum]["needTP"]
    else:
        app.indicator = 'rtt'
        app.needRTT = confApp[appNum]["needRTT"]

    return app


# 各基地局の接続時RTT,TPの計算（APインスタンスに設定）
def calLink(terms: List[Term], aps: List[Ap], sec: float):
    init_rtt = [20.0, 28.0, 32.0]
    link_rtt = copy.copy(init_rtt)
    init_tp = [65500 * 2 * 8 / init_rtt[0] / 1024, 65500 * 2 *
               8 / init_rtt[1] / 1024, 65500 * 2 * 8 / init_rtt[2] / 1024]
    link_tp = copy.copy(init_tp)

    # TPの通信品質劣化用
    erlang_mu = [50, 50, 20]
    erlang_n = [10, 5, 3]

    def erlang_b(lambda_val, mu, n):
        rho = lambda_val / mu
        B = (rho ** n / math.factorial(n)) / \
            sum((rho ** k) / math.factorial(k) for k in range(n + 1))
        return B

    # RTTの通信品質劣化用
    respo_mu = [1/init_rtt[0]*1000, 1/init_rtt[1]*1000, 1/init_rtt[2]*1000]

    def response_time_mm1(lambda_val, mu):
        if lambda_val >= mu - 1.9:
            lambda_val = mu - 1 + (lambda_val-mu)*0.01
        return 1 / (mu - lambda_val)
    apTermNum = sumTermAp(terms, aps)
    for i in range(len(aps)):
        # print(i)
        # TP
        erlang = erlang_b(apTermNum[i]*100, erlang_mu[i], erlang_n[i])
        link_tp[i] = init_tp[i]*(1-erlang)
        # RTT
        link_rtt[i] = response_time_mm1(apTermNum[i], respo_mu[i])*1000
        # print(link_rtt)

    for (index, ap) in enumerate(aps):
        if link_tp[index] <= 0:
            link_tp[index] = 0.01  # TP限界値補正（0で割ることを防ぐため）
            # console.log('link_tp_AP' + index + ': ' + link_tp[index])
            # console.log('link_rtt_AP' + index + ': ' + link_rtt[index])
        ap.setRtt(link_rtt[index])
        ap.setTp(link_tp[index])
    # print(link_rtt[index])
    # print(link_tp[index])


# 端末満足度算出
def calSatis(terms: List[Term], aps: List[Ap]):
    satis_sum_r = 0  # 端末満足度の逆数の合計
    satis_sum_r1 = 0
    rate = 0

    for index, term in enumerate(terms):
        satis_r = calSatisTerm(term, aps)
        # 調和平均算出用（逆数を加算）
        satis_sum_r += satis_r
        # print(satis_sum_r)

    print("端末満足度計：" + str(satis_sum_r))

    # 調和平均算出
    SATIS_HARMEAN = len(terms) / round(satis_sum_r, 6)
    print("調和平均＝", len(terms), "/", round(satis_sum_r, 6), "=", SATIS_HARMEAN)
    # -------------------------------------------------------------------------------print

    return SATIS_HARMEAN


def calSatisTerm(term: Term, aps: List[Ap]):  # 端末満足度（端末1台算出）
    satis = 0  # 端末ごとの端末満足度
    satis_r = 0  # 端末満足度逆数（調和平均算出用）
    apRtt = aps[term.apBssid].rtt  # 基地局のRTT
    apTp = aps[term.apBssid].tp  # 基地局のTP
    # print("tp:"+ str(apTp))

    # 各端末の端末満足度計算
    TERM_APP = calAppNeed(term.appNum)  # 必要RTT & 必要TP 決定
    # print('termApp:' + str(TERM_APP.needTP))

    if TERM_APP.indicator == 'tp':
        # 指標: TP
        satis = apTp / TERM_APP.needTP
        satis_r = 1 / round(satis, 6)
        # satis_r = TERM_APP.needTP / apTp # 逆数（調和平均算出用）
        # print('AP_tp=' + str(apTp)+ ', needTP=' + str(TERM_APP.needTP))
    else:
        # 指標: RTT
        satis = TERM_APP.needRTT / apRtt
        satis_r = 1 / round(satis, 6)
        # satis_r = apRtt / TERM_APP.needRTT # 逆数（調和平均算出用）
        # print('AP_rtt=' + str(apRtt)+ ', needRTT=' + str(TERM_APP.needRTT))

    # 端末満足度 MAX=1
    # if satis > 1:
    #     satis = 1
    #     satis_r = 1
    # print(round(satis, 6))
    # print(satis_r1)

    return satis_r  # 端末満足度の逆数を返す


# 端末満足度（端末1台算出）
def calSatisTerm_a(term: Term, aps: List[Ap], rate_fspl, rate_nonfspl):
    satis = 0  # 端末ごとの端末満足度
    satis_p = 0  # 端末満足度逆数（調和平均算出用）
    apRtt = aps[term.apBssid].rtt  # 基地局のRTT
    apTp = aps[term.apBssid].tp  # 基地局のTP
    apTp1 = apTp * rate_fspl
    apTp2 = apTp * rate_nonfspl
    # print("tp:"+ str(apTp))
    # print("rtt:"+ str(apRtt))

    # 各端末の端末満足度計算
    TERM_APP = calAppNeed(term.appNum)  # 必要RTT & 必要TP 決定
    # print('termApp:' + str(TERM_APP.needTP))

    if TERM_APP.indicator == 'tp':
        # 指標: TP
        satis = apTp / TERM_APP.needTP
        satis_fspl = apTp1 / TERM_APP.needTP
        satis_nonfspl = apTp2 / TERM_APP.needTP
        satis_r = TERM_APP.needTP / apTp  # 逆数（調和平均算出用）
        # print('AP_tp=' + str(apTp)+ ', needTP=' + str(TERM_APP.needTP))
    else:
        # 指標: RTT
        satis = TERM_APP.needRTT / apRtt
        satis_fspl = TERM_APP.needRTT / apRtt
        satis_nonfspl = TERM_APP.needRTT / apRtt
        satis_r = apRtt / TERM_APP.needRTT  # 逆数（調和平均算出用）
        # print('AP_rtt=' + str(apRtt)+ ', needRTT=' + str(TERM_APP.needRTT))

    # 端末満足度 MAX=1
    # if satis > 1:
    #     satis = 1
    #     satis_r = 1

    return satis, satis_fspl, satis_nonfspl  # 端末満足度を返す


def calGap(terms: List[Term], aps: List[Ap]):
    gap_sum = 0  # 端末満足度の逆数の合計
    for term in terms:
        # 端末1台の端末満足度を計算（逆数）
        gap = calGapTerm(term, aps)
        # 調和平均算出用（逆数を加算）
        gap_sum += gap

    # print("端末満足度計："+ str(satis_sum_r))

    # 調和平均算出
    GAP_MEAN = gap / len(terms)
    # print(SATIS_HARMEAN)
    return GAP_MEAN


def calGapTerm(term: Term, aps: List[Ap]):  # ギャップ（端末1台算出）
    satis = 0  # 端末ごとの端末満足度
    satis_r = 0  # 端末満足度逆数（調和平均算出用）
    apRtt = aps[term.apBssid].rtt  # 基地局のRTT
    apTp = aps[term.apBssid].tp  # 基地局のTP

    # 各端末の端末満足度計算
    TERM_APP = calAppNeed(term.appNum)  # 必要RTT & 必要TP 決定
    # console.log('termApp:' + termApp.needTP)

    if TERM_APP.indicator == 'tp':
        # 指標: TP
        gap = abs(apTp - TERM_APP.needTP)
        # print('AP_tp=' + str(apTp)+ ', needTP=' + str(TERM_APP.needTP))
    else:
        # 指標: RTT
        tp_need = 0.5 / (TERM_APP.needRTT/1000)  # RTT→TP
        # print(tp_need)
        gap = abs(apTp - tp_need)
        # print('AP_rtt=' + str(apRtt)+ ', needTP=' + str(TERM_APP.needRTT))

    return gap  # 端末満足度の逆数を返す


# ----------------------------------------------------#
# エリア内の端末の残量標準化処理
# ----------------------------------------------------#
def termCapaSd(terms: List[Term]):
    term = terms[0]
    unitPriceArray: List[float] = []
    unitPriceArrayStdPri = np.empty((len(terms), len(term.lines)))
    for i in range(len(term.lines)):
        line = term.lines[i]

        # 残量計算
        transferLimit = line["dataLimit"] - line["transferRecieve"]
        if transferLimit < 0:
            transferLimit = 0
        unitPrice = transferLimit  # 残量

        # console.log('残量容量単価', unitPrice)
        unitPriceArray.append(unitPrice)

    sdUnit_pri: float = stdev(unitPriceArray)  # 標準偏差偏差
    average_pri: float = fmean(unitPriceArray)  # 平均値計算
    return {
        "sd": sdUnit_pri,
        "ave": average_pri
    }


# 各APに接続されている端末台数を計算
def sumTermAp(terms: List[Term], aps: List[Ap]):
    apTermNum = np.zeros(len(aps))

    for term in terms:
        apTermNum[term.apBssid] += 1  # 各基地局毎に端末台数をカウント
        # print(term.apBssid)

    # 基地局インスタンスに値をセット
    for (index, ap) in enumerate(aps):
        ap.setTermNum(apTermNum[index])
    # print("基地局接続数" + str(apTermNum))

    return apTermNum


def overTransferLimit(terms: List[Term], aps: List[Ap]):
    countLimitOver: float = 0

    for term in terms:
        for i in range(len(term.lines)):
            LINE = term.lines[i]
            # 残量計算
            transferLimit = LINE["dataLimit"] - LINE["transferRecieve"]
            # print(transferLimit)
            if (transferLimit < 0):
                transferLimit = 0
                countLimitOver += 1

            # console.log('term', term.id, 'line', line.id, '残量', transferLimit)
    return countLimitOver


def movingAverage(xArray: List[float]):
    # const windowsize: number = confSim.output.average.windowsize
    windowsize: int = len(xArray) / \
        confSim["output"]["average"]["windowsizePercent"]
    processed = np.empty(int(len(xArray) - windowsize + 1))
    total: float = 0
    # print('xArray:', xArray)
    # let total = xArray.reduce((p, x) => p + x, 0)
    for i in range(int(windowsize)):
        total += xArray[i]

    processed[0] = total / windowsize

    for i in range(len(processed)):
        total -= xArray[i - 1]
        total += xArray[i + int(windowsize) - 1]
        processed[i] = total / windowsize
    # print(processed)
    return processed


"""
exports.calAppNeed = calAppNeed
exports.calLink = calLink
exports.calSatis = calSatis
exports.sumTermAp = sumTermAp
exports.termCapaSd = termCapaSd
exports.overTransferLimit = overTransferLimit
exports.movingAverage = movingAverage
"""
