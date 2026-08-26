#1基地局が持つデータ構造

class Ap:
    #型注釈
    bssid: str 
    rtt: float
    tp: float 
    termNum: int
    termCapa: int
    tpScale: float
    rttOffset: float

    #インスタンス（コンストラクタ）
    def __init__(self):
        self.bssid = ''
        self.rtt = 0 
        self.tp = 0
        self.termNum = 0
        self.termCapa = 0
        # baseline_methods の簡易リンクモデル用の AP 固定補正。
        # 教師データ生成時に createAp() で run ごとに一度だけ設定し、
        # Hungarian 探索中の calLink() では値を変えない。
        self.tpScale = 1.0
        self.rttOffset = 0.0
        
    def setBaseData(self, bssid: str, rtt: float, tp: float) :
        self.bssid = bssid
        self.rtt = rtt
        self.tp = tp

    def setRtt(self, rtt: float):
        self.rtt = rtt

    def setTp(self, tp: float):
        self.tp = tp

    def setTermNum(self, termNum: int):
        self.termNum = termNum
        
    def getTermNum(self):
        return self.termNum

    def isTermCapa(self):
        return True if self.termCapa >= self.termNum else False

    def setTermCapa(self, termCapa: int):
        self.termCapa = termCapa

    def setQualityVariation(self, tpScale: float, rttOffset: float):
        self.tpScale = tpScale
        self.rttOffset = rttOffset

