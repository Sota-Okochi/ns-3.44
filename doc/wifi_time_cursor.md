# 干渉履歴の検索位置再利用

**この方式はA/Bで性能悪化したため撤回済みです。以下は過去の実装・試験記録です。
現在の通常実行と次候補は [wifi_cursor_rollback.md](wifi_cursor_rollback.md) を参照してください。**

## 不変条件と変更範囲

BandInterferenceStateにupper_boundの位置と問い合わせ時刻を保持する。
履歴の型、帯域キー、信号・イベント・電力計算は変更しない。

- `moment == Simulator::Now()` の問い合わせのみ再利用候補とする。
- 初回は元のupper_bound。時刻が単調増加する場合、前の位置から `key <= moment`
  の間だけ進める。等時刻キーはすべて通過するため、元のupper_boundと同じ位置。
- 8ノード進めても見つからなければ元の木検索に戻す。この上限は計算結果に影響しない
  実装上の性能ガードであり、履歴を省略するパラメータではない。
- 過去/未来時刻の問い合わせ、逆行する問い合わせは元のupper_boundを使う。
- 挿入位置は依然upper_boundで、等時刻の順序を維持する。
  キャッシュ時刻より後、かつキャッシュ位置より前に新規ノードを挿入した場合だけ
  キャッシュを新ノードへ更新。同じキーの追加は既存ノードの後ろなので位置を維持。
- AppendEventの削除はcutoff以下のprefixのみ。キャッシュが厳密にcutoffより後なら
  有効なまま、それ以外は削除前に無効化。帯域削除/全履歴削除も無効化。
- レコードのコピーはキャッシュを継承せず、コピー先の木で再検索する。
- GetPreviousPositionの「upper_boundから1つ戻る」処理、電力更新、条件分岐を維持。

帯域検索キャッシュ、メモリプール、履歴コンテナ変更は含めない。
元の干渉管理統合変更は引き続き有効。比較beforeも統合済み。
通常実行にも反映されるため、新旧ライブラリを混在させないこと。

## before保存と2seed A/B

変更前master/.so/公開ヘッダ/設定/該当ソース/ハッシュを
`results/perf/wifi_time_cursor_before/` に保存済み。

```bash
cd /home/sota/ns-3.44
python3 scripts/verify_wifi_insert.py \
  --baseline results/perf/wifi_time_cursor_before \
  --seeds 1001 1002 \
  --output "results/perf/wifi_time_cursor_ab_$(date +%Y%m%d_%H%M%S)"
```

順序は1001 before→after→比較→1002 before→after→比較。
実行中に設定変更/再ビルド/別master実行をしない。
各seedのPASS.jsonが、全境界の時刻/イベント数、通信/QoE/割当/報酬の
CSV精度での一致を示す。実時間列のみ比較除外。
処理時間は各before/after/elapsed_seconds.txtとperf配下のboundaries.csvに保存。

## テスト実行記録

```bash
cmake --build cmake-cache -j 4 --target master
g++ -std=c++20 -O2 -Ibuild/include tests/interference_time_cursor_test.cc \
  -Lbuild/lib -Wl,-rpath,"$PWD/build/lib" -lns3.44-wifi-optimized \
  -lns3.44-core-optimized -lns3.44-network-optimized -o /tmp/interference-time-cursor-test
/tmp/interference-time-cursor-test
NS3_INTERFERENCE_AB_BASELINE=results/perf/wifi_time_cursor_before \
  python3 tests/test_interference_band_state.py
NS3_WIFI_AB_BASELINE=results/perf/wifi_time_cursor_before \
  python3 tests/test_wifi_shared_bands_ab.py
python3 tests/test_wifi_perf_integration.py
python3 tests/test_verify_wifi_insert.py
git diff --check
```

- optimizedビルド成功（初回ccache sandbox制約で失敗、許可後成功）。
- 固定乱数seed1001の30,000操作をstd::multimapそのものと差分比較し成功。
  等時刻/過去/未来の挿入、時刻検索、履歴削除、全削除、コピーを混在させ、
  毎操作で履歴全体の順序・値を照合。長い前進/逆行のfallbackも検証。
- 同時刻信号を含む干渉履歴・基準電力・6イベントのbefore/after一致を確認。
  最初はテストがbeforeを統合前型と仮定してコンパイル失敗。保存ヘッダに応じた
  テスト分岐へ修正して成功（研究コードの変更ではない）。
- 小規模Wi-Fiのseed1001/1002とも200 bytes / 378 eventsで一致。
  devices.csvと境界の実時間以外も一致。
- 通常Wi-Fi結合試験、比較器6テスト成功。

**本条件の80端末2seed A/Bは未実行。結果不変と速度向上は本条件では未確認。**
実験結果の完全な一般保証ではなく、設計上の等価性と上記試験の結果である。

追加検査：同じ単体試験を `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`
でビルドし、`/tmp/interference-time-cursor-sanitized` を実行して成功。
検査対象はテスト/ヘッダ内の検索実装であり、既存ns-3共有ライブラリ全体の
sanitizerビルドではない。LeakSanitizerのptrace制約を避け、許可の上でsandbox外で実行。
