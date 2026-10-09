# 帯域ごとの干渉履歴・基準電力の統合

## 今回の範囲

`InterferenceHelper` の `m_niChanges` と `m_firstPowers` を、帯域をキーとする
単一mapへ統合した。値は `BandInterferenceState { NiChanges changes; Watt_u firstPower; }`。
基準電力は0初期化。元の帯域キー・比較・走査順序を維持する。

- AppendEvent、CalculateNoiseInterferenceW、NotifyRxEndでは、取得済み帯域レコードから
  基準電力へアクセスし、2つ目のmap検索をしない。
- Payload/PHY header PER計算でも統合レコードから基準電力を読む。
- AddBand/RemoveBand/DoDisposeは統合レコードの生成・削除へ対応。
- 時刻別履歴は従来の `std::multimap<Time, NiChange>` のまま。
  upper_boundによる挿入位置、同一時刻の順序、電力加算・基準電力更新の条件を維持。
- PER計算用一時履歴 `NiChangesPerBand` は変更せず、永続管理用 `BandStates` と分離。
- 帯域検索キャッシュ、履歴ノード再利用、PSD変更は実装していない。
- シミュレーション条件・乱数・イベント・QoE・ログ列は変更していない。

通常の `./ns3 run master -- --method=random` にも適用される（計測フラグ不要）。
内部protectedメンバーの型が変わるため、依存モジュールも再ビルド。

## beforeの保存

変更直前に `cmake --build cmake-cache -j 4 --target master` がno workであることを確認。
`results/perf/wifi_interference_record_before/` にmaster、全共有ライブラリ、設定、
実体化した公開ヘッダ、Git HEAD/SHA256 manifestを保存した。
**beforeも帯域情報共有化済み。今回の統合だけの差を比較する。**
以前の `wifi_shared_bands_before` / `wifi_insert_before` は使わない。

## 本条件・2seedの比較コマンド

別masterが終了した状態で、リポジトリルートから実行する。

```bash
python3 scripts/verify_wifi_insert.py \
  --baseline results/perf/wifi_interference_record_before \
  --seeds 1001 1002 \
  --output "results/perf/wifi_interference_ab_$(date +%Y%m%d_%H%M%S)"
```

順序は1001 before→after→比較→1002 before→after→比較、合計4回の本条件実行。
並行実行せず、実行中の設定変更・再ビルド・別master起動を避ける。
各seedのPASS.jsonは全境界の時刻・イベント数と全通信/QoE・報酬ログ一致の印。
assignment_compute_msなど実時間列は比較から除外。未完了/不一致は失敗として停止。
各before/afterのelapsed_seconds.txtにプロセス実時間、perf配下のboundaries.csvに
区間実時間を保存する。結果一致はCSV出力精度での比較であり、全内部状態の証明ではない。

再比較例：

```bash
python3 scripts/verify_wifi_insert.py --compare-only results/perf/wifi_interference_ab_実際の日時/seed1001
```

## 実施した検証

```bash
cmake --build cmake-cache -j 4 --target master
NS3_WIFI_AB_BASELINE=results/perf/wifi_interference_record_before \
  python3 tests/test_wifi_shared_bands_ab.py
python3 tests/test_interference_band_state.py
python3 tests/test_wifi_perf_integration.py
python3 tests/test_verify_wifi_insert.py
git diff --check
```

- optimizedビルド成功。初回はccacheのsandbox書き込み制限で失敗し、許可後に成功。
- 小規模before/after試験：seed1001/1002の両方でbytes=200、events=378。
  devices.csv全行、boundaries.csvの実時間以外の全項目一致。
- 干渉履歴専用試験：変更前/後の全履歴（時刻・電力・イベントの開始時刻/電力）と
  エネルギー継続時間、6イベントの一致を確認。同一開始/終了時刻を持つ重複信号を含む。
  統合版の基準電力について、非受信時/受信中/HE開始/受信終了/帯域削除再追加をassert。
- 通常Wi-Fi結合試験、比較スクリプト6テスト成功。

**80端末の本条件2seed A/Bは未実行。速度改善と全通信/QoE一致は未確認。**
次は上記コマンドで両seedを完走し、結果一致と実時間を確認する。
