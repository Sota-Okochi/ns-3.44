from typing import List

from simulation.config import load_sim_config
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造

confSim = load_sim_config()

# エリア内の基地局生成
def createAp(apNum: int):
    aps: List[Ap] = []
    for apIndex in range(apNum):
        AP = Ap()
        AP.setBaseData('bssid' + str(apIndex), 0, 0)
        AP.setTermCapa(confSim["termCapa"]) # 基地局収容数
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

