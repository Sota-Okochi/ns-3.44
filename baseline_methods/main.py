import time
from typing import List

from simulation.config import (
    load_ap_config,
    load_app_config,
    load_sim_config,
)
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.results.result import simResult
from simulation.services import cal, rand, create
from simulation.visualization import output as output
from simulation.visualization import graph as gp
from simulation.algorithms import hungarian_kai as hung

confSim = load_sim_config()
confAp = load_ap_config()
confAPP = load_app_config()


if __name__ == '__main__':

    # 端末および基地局の生成
    # 引数: 回線数->term.tsのConstructorと合わせる
    APS = create.createAp(confSim["apNumMax"])
    TERMS: List[Term] = create.createTerm(confSim["termNum"])

# 接続先基地局をランダムに
    # rand.randAp(TERMS, APS)

# 実行

    # 結果格納 変数定義
    PresatisHarmeanArray: List = []
    satisHarmeanArray: List = []
    termNumOverLimitArray: List = []
    linkTerm: List = []  # 基地局に接続されている端末台数
    jtime: List = []

    # 繰り返し割り当て
    for i in range(confSim["simNumTime"]):
        start_time = time.time()
        print("-------------------------------------------------------------")
        print("割り当て回数="+str(i+1)+"回目")
        satisHarmean: float

        # 端末の接続基地局先をランダムに設定
        rand.randAp(TERMS, APS)
        # 端末のアプリをランダムに設定
        rand.randApp(TERMS, APS)

        # 基地局接続台数算出
        cal.sumTermAp(TERMS, APS)

        # 接続時RTT & 接続時TP計算
        cal.calLink(TERMS, APS, confSim["appUseSec"])

        # 割り当て前端末満足度の調和平均算出
        satisHarmean = cal.calSatis(TERMS, APS)  # 端末満足度の調和平均算出
        PresatisHarmeanArray.append(satisHarmean)

        print("↓")

        # ハンガリアン法による接続先最適化
        result = hung.call_hungarian(TERMS, APS)

        # 基地局接続台数
        cal.sumTermAp(TERMS, APS)

        # 接続時RTT & 接続時TP計算
        cal.calLink(TERMS, APS, confSim["appUseSec"])

        # 端末満足度算出
        satisHarmean = cal.calSatis(TERMS, APS)  # 端末満足度の調和平均算出
        satisHarmeanArray.append(satisHarmean)

        # 容量超過端末数算出
        # TERM_NUM_OVER_LIMIT = cal.overTransferLimit(TERMS, APS)
        # termNumOverLimitArray.append(TERM_NUM_OVER_LIMIT)

        # linkTerm.append(copy.deepcopy(cal.sumTermAp(TERMS, APS)))

        end_time = time.time()  # 終了時刻
        execution_time = end_time - start_time  # 実行時間の計算
        print(f"実行時間: {execution_time}秒")
        jtime.append(execution_time)

    RES = {
        "satisHarmeanArray": satisHarmeanArray,
        "PresatisHarmeanArray": PresatisHarmeanArray,
        "linkTerm": linkTerm
    }

    print("調和平均", satisHarmeanArray)
    print("時間", jtime)


# グラフ出力

    # 移動平均
    # SATIS_MOVING_AVE: List = cal.movingAverage(RES["satisHarmeanArray"])

    # gp.exportGraph_propa(
    #             RES["satisHarmeanArray"],
    #             RES["PresatisHarmeanArray"],
    #             RES["satisHarmeanArray_propa"],
    #             RES["PresatisHarmeanArray_propa"],
    #             SATIS_MOVING_AVE,
    #             TERMS
    #     )

    # RES_TEXT: str = output.satisLimit(
    #     SATIS_MOVING_AVE
    # )

    # gp.exportGraph(
    #         RES["satisHarmeanArray"],
    #         SATIS_MOVING_AVE,
    #         RES["termNumOverLimitArray"],
    #         TERMS
    # )

    # RES_TEXT: str = output.satisLimit(
    #     SATIS_MOVING_AVE,
    #     RES["termNumOverLimitArray"]
    # )
