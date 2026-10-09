# 同一AppendEvent内での検索位置再利用

## 今回の差分

撤回済みのイベント間カーソルは再導入しない。変更はinterference-helper.ccの
AppendEvent内のみ（およびstd::prev/next用のiterator include）。
開始/終了時刻について取得したupper_boundを、基準電力の取得と履歴挿入の両方に使う。
従来の検索4回（基準電力用2回＋挿入用2回）を2回にする。
他の呼び出し箇所のGetPreviousPosition/GetNextPosition/AddNiChangeEventは変更しない。

安全性：
- 正常な信号のdurationは非負。end >= startをassertで明示。
- prefix削除はstartNextを含まない。endNextも削除範囲外。
- multimapの挿入は既存iteratorを無効化しない。
- start <= endなので開始履歴の挿入後もendNextは終了時刻のupper_bound。
  start == endでも既存の同時刻履歴の後へ「開始→終了」の順で挿入する。
- 元の時間別履歴・同時刻順序・加算順序・基準電力更新条件を維持。
- 永続キャッシュ、Simulator::Nowとの比較、追加の検索位置管理はない。

## before

`results/perf/wifi_local_hint_before/` に、復旧版のmaster/.so/実体化した公開ヘッダ/
設定/変更対象ソース/Git HEAD・SHA256 manifestを保存した。
保存前ビルドはno workで、復旧版は帯域共有化・干渉管理統合を含みカーソルは含まない。
以前の遅いカーソル版をbeforeにしない。

## 本条件の2seed A/B（未実行）

```bash
cd /home/sota/ns-3.44
python3 scripts/verify_wifi_insert.py \
  --baseline results/perf/wifi_local_hint_before \
  --seeds 1001 1002 \
  --output "results/perf/wifi_local_hint_ab_$(date +%Y%m%d_%H%M%S)"
```

1001 before→after→比較→1002 before→after→比較の4回を順次実行。
他のmasterを終了してから開始し、設定編集・再ビルド・並行シミュレーションを避ける。
各seedのPASS.jsonは全境界時刻/イベント数・通信/QoE/割当/報酬ログの一致を示す。
実時間列だけ除外し、CSV出力精度で比較する。失敗/未完了は停止する。
実時間は各before/after/elapsed_seconds.txt、区間実時間はperf内boundaries.csv。

## 実施済み検証

```bash
cmake --build cmake-cache -j 4 --target master
NS3_INTERFERENCE_AB_BASELINE=results/perf/wifi_local_hint_before python3 tests/test_interference_band_state.py
NS3_WIFI_AB_BASELINE=results/perf/wifi_local_hint_before python3 tests/test_wifi_shared_bands_ab.py
python3 tests/test_wifi_perf_integration.py
python3 tests/test_verify_wifi_insert.py
g++ -std=c++20 -O2 -Ibuild/include tests/interference_local_hint_test.cc \
  -Lbuild/lib -Wl,-rpath,"$PWD/build/lib" -lns3.44-core-optimized \
  -o /tmp/interference-local-hint-test
/tmp/interference-local-hint-test
git diff --check
```

ビルド初回はccacheのsandbox書き込み制限で失敗、許可後成功。
本条件の結果一致・速度改善はまだ確認していない。候補は通常ビルドへ適用済みであり、
高速化の採否は上記A/Bで判断する。

検証結果：
- 実ライブラリのbefore/afterで干渉履歴・基準電力・6イベントが一致。
  同時刻、非受信時のprefix削除、受信中、帯域削除再追加、ゼロ長信号を含む。
- ゼロ長信号後のNotifyRxEndについて、追加テストの期待値0がbeforeでも失敗した。
  従来の「終了マーカーから1つ戻る」処理に合わせ、直前の開始マーカーの電力16を
  期待値に訂正した。研究コードの従来動作は変更していない。
- 小規模Wi-Fiはseed1001/1002とも200 bytes / 378 eventsで一致。
  devices.csv全行・境界の実時間以外も一致。
- 独立アルゴリズム試験は両seed各10,000回で全履歴・基準電力が一致。
- 通常Wi-Fi結合試験、比較器6テスト成功。git diff --check成功。
- beforeハッシュ検証成功。beforeと現行のinterference-helper.hは完全一致し、
  永続状態を追加していない。ソース差分はAppendEventとiterator includeだけ。
