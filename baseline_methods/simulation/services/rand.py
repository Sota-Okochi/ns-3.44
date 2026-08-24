import math as Math
import random
from typing import List

from simulation.config import load_app_config
from simulation.entities.term import Term  # 端末1台が持つデータ構造
from simulation.entities.ap import Ap  # 1基地局が持つデータ構造

# アプリケーション種類ごとの設定
confApp = load_app_config()

APP_NUM_MAX = len(confApp) #アプリケーションの種類の数

# ns-3 側の APConstants::AppType と合わせ、appNum は 1..4 とする。
# 分布も ns-3 側 config.cc と同じ: browser 20%, video 40%, voice 15%, game 25%。
APP_DISTRIBUTION = [
    (1, 0.20),  # browser
    (2, 0.40),  # video
    (3, 0.15),  # voice
    (4, 0.25),  # online game
]


def _app_config_index(app_num: int) -> int:
    if 1 <= app_num <= APP_NUM_MAX:
        return app_num - 1
    return app_num


def _draw_app_num() -> int:
    draw = random.random()
    cumulative = 0.0
    for app_num, probability in APP_DISTRIBUTION:
        cumulative += probability
        if draw < cumulative:
            return app_num
    return APP_DISTRIBUTION[-1][0]


def switchAp(term: Term, distAp: int) :
    term.setSwitchAp(distAp)

def randAppOne (term: Term, aps: List[Ap]) :
    # 利用アプリランダム
    # random.seed(term.id)　シード値設定は繰り返しの実験で値をそろえるため？
    RAND_APP_NUM = _draw_app_num()
    app_index = _app_config_index(RAND_APP_NUM)

    # アプリ利用時間の決定
    MAX_TIME = confApp[app_index]["useApp"]["maxTime"]
    MIN_TIME = confApp[app_index]["useApp"]["minTime"]
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

