# 時刻検索カーソルの撤回と次候補の検討

この文書は復旧時点の記録です。その後の関数内hint再利用実装は [wifi_local_hint.md](wifi_local_hint.md) を参照。

## A/B結果

`results/perf/wifi_time_cursor_ab_20261009_013654` の両seedでログ一致は成功したが、
実時間はseed1001で6.42%、seed1002で9.11%増加。カーソル方式を採用しない。

## 通常実行を復旧

SHA256 manifestを検査した `results/perf/wifi_time_cursor_before/` の
interference-helper.h/.ccを復元。帯域共有化と干渉管理統合は維持し、
カーソルの追加フィールド/分岐/挿入削除時の管理処理を取り除いた。
旧カーソル版ソースと専用テストは `results/perf/wifi_time_cursor_retired/` に保存。
旧カーソル専用テストは通常testsから取り除き、次候補の独立試験へ置換した。

復元時copy2で古い更新時刻も戻るため、最初のビルドはno workだった。
その後両ソースをtouchして再ビルドし、保存beforeとのバイト一致を確認した。
**現在のmasterと全共有ライブラリのSHA256は、検索位置再利用導入前の保存版と一致。**

実行コマンド：

```bash
touch src/wifi/model/interference-helper.h src/wifi/model/interference-helper.cc
cmake --build cmake-cache -j 4 --target master
NS3_INTERFERENCE_AB_BASELINE=results/perf/wifi_time_cursor_before python3 tests/test_interference_band_state.py
NS3_WIFI_AB_BASELINE=results/perf/wifi_time_cursor_before python3 tests/test_wifi_shared_bands_ab.py
git diff --check
```

ビルド、履歴回帰試験成功。小規模A/Bは両seedとも200 bytes / 378 events、
devices.csvと境界の実時間以外も一致。本条件の長時間シミュレーションは再実行していない。
設定/seed/実験ログは変更していない。

## 次候補（研究コードには未適用）

AppendEventの同じ呼び出し・同じ帯域内で、開始/終了時刻のupper_bound結果を
挿入hintにも使う。永続キャッシュは持たない。
現在は基準電力取得で2回、開始/終了履歴の挿入で2回の木検索があるため、
挿入時の2回の再検索を避ける候補。

正しさの条件：
- 終了時刻 >= 開始時刻。
- 古い履歴の削除範囲は開始時刻以下で、開始upper_boundは削除範囲の外。
- 終了upper_boundもその範囲の外。
- 開始ノードの挿入は終了upper_boundを変えない（ゼロ長信号も同じ）。
- 同時刻の全既存ノードの後ろへ挿入し、加算順序を維持。

独立した候補アルゴリズム試験（シミュレーションへの統合試験ではない）：

```bash
g++ -std=c++20 -O2 -Ibuild/include tests/interference_local_hint_test.cc \
  -Lbuild/lib -Wl,-rpath,"$PWD/build/lib" -lns3.44-core-optimized \
  -o /tmp/interference-local-hint-test
/tmp/interference-local-hint-test
```

seed1001/1002、各10,000回で元アルゴリズムと履歴全体の順序/電力/イベント識別子、
基準電力が一致。同時刻・ゼロ長信号・prefix削除・全リセットを含む。
**次候補はライブラリへ未実装。高速化は未測定。**
次に実装する場合は現在の復旧版をbeforeとし、本条件2seed A/Bで判断する。
