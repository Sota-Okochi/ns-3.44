from typing import List
import random

from simulation.config import load_sim_config
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造

confSim = load_sim_config()


# ns-3 OUTPUT/80 の観測範囲から大きく外れないように、run ごとの AP 固定ばらつきを入れる。
# ここで一度だけ乱数を引き、calLink() 中では固定値として使う。
TP_SCALE_RANGE = [
    (0.70, 1.30),  # AP0
    (0.70, 1.30),  # AP1
    (0.70, 1.30),  # AP2
]
RTT_OFFSET_RANGE_MS = [
    (-10.0, 10.0),  # AP0
    (-5.0, 5.0),    # AP1
    (-8.0, 8.0),    # AP2
]

# エリア内の基地局生成
def createAp(apNum: int):
    aps: List[Ap] = []
    for apIndex in range(apNum):
        AP = Ap()
        AP.setBaseData('bssid' + str(apIndex), 0, 0)
        AP.setTermCapa(confSim["termCapa"]) # 基地局収容数
        if apIndex < len(TP_SCALE_RANGE):
            tp_min, tp_max = TP_SCALE_RANGE[apIndex]
            rtt_min, rtt_max = RTT_OFFSET_RANGE_MS[apIndex]
            AP.setQualityVariation(
                random.uniform(tp_min, tp_max),
                random.uniform(rtt_min, rtt_max),
            )
        aps.append(AP)
    return aps

# エリア内の端末生成
def createTerm(termNum: int):
    terms: List[Term] = []
    # const ap = new Ap()
    for i in range(termNum):
        TERM = Term()
        TERM.setBaseData(i, 0)
        terms.append(TERM)
    return terms

