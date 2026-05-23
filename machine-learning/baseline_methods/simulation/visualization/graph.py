from cProfile import label
from turtle import color
import matplotlib.pyplot as plt #type:ignore
from typing import List

from simulation.config import load_sim_config
from simulation.entities.term import Term # 端末1台が持つデータ構造

confSim = load_sim_config()

def exportGraph(satisArray: List, satisArray_fspl: List, satisArray_nonfspl: List, satisMovingArray: List, term:List[Term]):
    X_LEN = len(satisArray)
    xArray: List = []
    for i1 in range(len(satisArray)):
        xArray.append(i1)
    x2Array = list(range(len(satisMovingArray)))
      
    y1, y2, y3 = satisArray, satisArray_fspl, satisArray_nonfspl
    c1, c2, c3 = "black", "red", "blue"
    l1, l2, l3 = "既存方式",  "自由空間", "非自由空間"


    #Plot Data
    plt.rcParams["font.size"] = 11
    plt.rcParams["xtick.labelsize"] = 14  # x軸目盛りラベルのサイズ
    plt.rcParams["ytick.labelsize"] = 14  # y軸目盛りラベルのサイズ
    fig, ax1 = plt.subplots(figsize=(10.0, 6.0))
    ax2 = ax1.twinx()
    ax1.set_xlabel("割り当て回数", fontname = "IPAexGothic", fontsize=18, fontweight="heavy")
    ax1.set_ylabel("端末満足度（調和平均）", fontname = "IPAexGothic", fontsize=18, fontweight="heavy")
    ax1.grid()
    # ax1.set_xlim([0, X_LEN])

  # 背景色の設定
    # fig.patch.set_facecolor('#f5f5f5')

    ax1.relim()
    ax1.autoscale()
    ax1.set_ylim([0, 1])
    ax1.plot(xArray, y1, color=c1, linestyle="dashed", label=l1)
    ax1.plot(xArray, y2, color = c2, linestyle="dashed", label = l2)
    ax1.plot(xArray, y3, color = c3, linestyle="dashed", label = l3)
    # ax1.plot(x2Array, y3, color = c3, label = l3)

  # # y1の値を表示
  #   for i, v in enumerate(y1):
  #       ax1.text(i, v, f'{v:.3f}', color=c1, fontsize=8, ha='center', va='bottom')

  #   # y2の値を表示
  #   for i, v in enumerate(y2):
  #       ax1.text(i, v, f'{v:.5f}', color=c2, fontsize=8, ha='center', va='bottom')

  #   # y3の値を表示
  #   for i, v in enumerate(y3):
  #       ax1.text(i, v, f'{v:.5f}', color=c3, fontsize=8, ha='center', va='bottom')

    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1, l1)
    ax1.legend(h1 + h2, l1 + l2, prop={"family": "IPAexGothic", "size": 15, "weight": "heavy"})
    plt.show()



def exportGraph_count(count_fspl: List[int], count_nonfspl: List[int]):
    xArray: List = []
    # xArray = list(range(1, len(count_fspl) + 1))  # x軸を1からスタート
    for i1 in range(len(count_fspl)):
        xArray.append(i1+1)

    # データとラベルの設定
    data = [
        (count_fspl, "steelblue", "自由空間"),
        (count_nonfspl, "darkorange", "非自由空間")
    ]

    # プロットの設定
    plt.rcParams["font.size"] = 11
    plt.rcParams["axes.labelweight"] = "heavy"  # 軸ラベルの太さを設定
    plt.rcParams["xtick.labelsize"] = 14  # x軸目盛りラベルのサイズ
    plt.rcParams["ytick.labelsize"] = 14  # y軸目盛りラベルのサイズ
    fig, ax1 = plt.subplots(figsize=(10.0, 6.0))

    # 軸ラベルの設定
    ax1.set_xlabel("割り当て回数", fontname="IPAexGothic", fontsize=18, fontweight="heavy")
    ax1.set_ylabel("不一致数", fontname="IPAexGothic", fontsize=18, fontweight="heavy")

    # データのプロット
    for y_data, color, label in data:
        ax1.plot(
            xArray, y_data, color=color, linestyle="solid", label=label,
            linewidth=2
        )

    # x軸の刻みを調整（中央揃え）
    ax1.set_xticks(range(5, len(xArray), 5))

    # y軸の刻みを調整
    y_max = max(max(count_fspl), max(count_nonfspl)) + 2
    ax1.set_yticks(range(0, y_max + 1, 5))  # y軸の範囲を0から最大値まで5刻み

    # x軸とy軸の範囲を調整
    ax1.set_xlim([1, len(xArray)])  # x軸の範囲を1からデータ数まで
    ax1.set_ylim([0, y_max])        # y軸の範囲を0から最大値まで

    # メモリラベルのフォントサイズと色を設定
    ax1.tick_params(
        axis='x',  # x軸に適用
        direction='inout',  # メモリの向き
        length=10,  # メモリの長さ
        width=2,  # メモリの太さ
        color='black',  # メモリの色
        labelsize=14,  # ラベルフォントサイズ
        labelcolor='black',  # ラベルの色
    )

    ax1.tick_params(
        axis='y',  # y軸に適用
        direction='inout',
        length=10,
        width=2,
        color='black',
        labelsize=14,
        labelcolor='black',
    )

    # 凡例の設定
    ax1.legend(
        prop={"family": "IPAexGothic", "size": 15, "weight": "black"},  # フォントの太さを最大限に設定
        loc="upper right"
    )

    # グリッドの表示（縦のグリッドを消す）
    ax1.grid(axis='y', linestyle='--', linewidth=0.5, color='gray')  # 横方向のグリッドのみ表示

    # グラフの表示
    plt.show()











"""
  Layout
  const layout1: Layout = {
    title: '端末満足度の調和平均',
    xaxis: {
      title: '割当回数',
      showgrid: false,
      zeroline: false
    },
    yaxis: {
      title: '端末満足度の調和平均',
      showline: false,
      rangemode: 'nonnegative',
      range: [0,1]
    }
  }

  const layout2: Layout = {
    title: '通信制限数',
    xaxis: {
      title: '割当回数',
      showgrid: false,
      zeroline: false
    },
    yaxis: {
      title: '通信制限数',
      showline: false,
      rangemode: 'nonnegative'
    }
  }
"""

  # stack([graphData_satis, graphData_satis_moving], layout1)
  # stack([graphData_over], layout2)
  # グラフ1つver

  # const data = [graphData_satis, graphData_over]
  # const layout = [layout1, layout1]
  # plot(data, layout1)


# def exportGraphTypeC(satisArray: List, satisMovingArray: List, overTermArray: List, intervalSec: float, term:List[Term]) :
#     X_LEN = len(satisArray)
#     xArray: List = []
#     for i in range(len(satisArray)):
#         xArray.append(i)
#     x2Array = list(range(len(satisMovingArray)))
    
#     y1, y2, y3 = satisArray, satisMovingArray, overTermArray
#     c1, c2, c3 = "blue", "orange", "green"
#     l1, l2, l3 = "satis", "satis_moving", 'over'

#     #Plot Data 
#     fig, ax1 = plt.subplots(figsize=(10.0, 6.0))
#     ax2 = ax1.twinx()
#     ax1.set_xlabel("時間/s", fontname = "MS Gothic")
#     ax1.set_ylabel("端末満足度の調和平均", fontname = "MS Gothic")
#     ax2.set_ylabel("通信制限数", fontname = "MS Gothic")
#     ax1.grid()
#     ax1.set_ylim([0, 10])
#     ax2.set_ylim([0, len(term)+30])
#     ax1.fill_between(xArray, y1, color=c1, linestyle="dashed", label = l1, alpha=0.5)
#     ax1.plot(x2Array, y2, color = c2, label = l2)
#     ax2.plot(xArray, y3, color = c3, label= l3)
#     h1, l1 = ax1.get_legend_handles_labels()
#     h2, l2 = ax2.get_legend_handles_labels()
#     ax1.legend(h1 + h2, l1 + l2)
#     plt.show()

#     """
#     #Layout
#     const layout_combi: Layout = {
#       # title: '端末満足度の調和平均と通信制限数',
#       width: 750,
#       # showlegend: true, # 凡例の表示有無
#       # showlegend: false, # 凡例の表示有無
#       legend: { "x": 0.4, "y": 1.3 }, #凡例の位置
#       xaxis: {
#         title: '時間/s',
#         showgrid: true,
#         zeroline: false,
#         dtick: intervalSec,
#       },
#       yaxis: {
#         title: '端末満足度の調和平均',
#         showline: false,
#         rangemode: 'nonnegative',
#         range: [0,1]
#       },
#       yaxis2: {
#         title: '通信制限数',
#         showline: false,
#         rangemode: 'nonnegative',
#         overlaying: 'y',
#         side: 'right',
#         range: [0,100]
#       }
#     }
#     # stack([graphData_satis, graphData_satis_moving], layout1)
#     # stack([graphData_over], layout2)
#     # グラフ1つver
#     stack([graphData_satis, graphData_satis_moving, graphData_over], layout_combi)
#     plot()

#     # const data = [graphData_satis, graphData_over]
#     # const layout = [layout1, layout1]
#     # plot(data, layout1)
#   }


#   exports.exportGraph = exportGraph
#   exports.exportGraphTypeC = exportGraphTypeC
#   """
