import math as Math
import random
from typing import List

from simulation.config import load_app_config
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造

# アプリケーション種類ごとの設定
confApp = load_app_config()

APP_NUM_MAX = len(confApp) #アプリケーションの種類の数




def switchAp(term: Term, distAp: int) :
    term.setSwitchAp(distAp)

def randAppOne (term: Term, aps: List[Ap]) :
    # 利用アプリランダム
    # random.seed(term.id)　シード値設定は繰り返しの実験で値をそろえるため？
    RAND_APP_NUM = Math.floor(random.random() * (APP_NUM_MAX + 1 - 1))
    

    # アプリ利用時間の決定
    MAX_TIME = confApp[RAND_APP_NUM]["useApp"]["maxTime"]
    MIN_TIME = confApp[RAND_APP_NUM]["useApp"]["minTime"]
    USE_TIME = Math.floor(random.random() * (MAX_TIME - MIN_TIME + 1) + MIN_TIME)

    # アプリ切り替え
    term.setAppNum(RAND_APP_NUM, USE_TIME)
    

def randApp(terms: List[Term], aps: List[Ap]) :
    APPArray: List = []
    for term in terms:
        randAppOne(term, aps)
        APPArray.append(term.appNum)
        # print(term.appNum)
    print("使用アプリ番号", APPArray)

def randApOne (term: Term, aps: List[Ap]):
    # random.seed()
    RAND_AP_NUM = Math.floor(random.random() * (len(aps) + 1 - 1))
    switchAp(term, RAND_AP_NUM)


def randAp (terms: List[Term], aps: List[Ap]):
    APArray: List = []
    #接続基地局ランダム
    for term in terms:
        randApOne(term, aps)
        APArray.append(term.apBssid)
        # print(term.apBssid)
    print("接続先番号", APArray)

#アプリ利用時間秒数指定
def useApp (term: Term, appUseSec: float):
    term.useApp(appUseSec)

"""
exports.switchAp = switchAp
exports.randApp = randApp
exports.randAp = randAp
exports.useApp = useApp

export { switchAp, randAppOne, randApp, randApOne, randAp, useApp }
"""

