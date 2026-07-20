# # 基地局と端末の関係
from typing import List
from dataclasses import dataclass


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
