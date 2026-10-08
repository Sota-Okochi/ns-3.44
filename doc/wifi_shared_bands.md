# Wi-Fi受信帯域情報の共有化（2026-10-07）

## 変更範囲

前回の `try_emplace` 版をbeforeとし、帯域別電力コンテナを共有キー＋連続領域へ変更。
`RxPowerBandLayout` は帯域キー（indices/frequencies）を不変のテーブルとして所有する。
`WifiSpectrumPhyInterface` が通常/HEを含む2種類のレイアウトを必要時に生成・保持する。
各受信イベントは `shared_ptr<const Layout>` と、帯域への参照＋電力値のvectorを保持。
帯域数ごとのmapノード・キーvectorの確保/破棄を、受信ごとの連続領域確保へ置換する。
`StartRx` は既存と同じ順序で**全帯域を計算**し、事前計算したスロットへ電力を格納。
干渉履歴（InterferenceHelperのNiChanges）や送信PSDの構造変更は今回含めない。

### 保つ条件

- 元の `operator<` と同じ帯域順序、比較上同等のキーは最初のキー/電力を保持。
- 通常帯域→HE RUという電力計算・浮動小数点演算の順序を維持。
- 各イベントの電力値は独立（コピー後の更新も他イベントへ伝播しない）。
- SetBands/SetHeRuBands/Disposeでキャッシュを解放・無効化。
  生存中のイベントはshared_ptrにより旧レイアウトを保持し、参照切れしない。
- 端末数・通信量・干渉・乱数・イベント登録・QoE/ログ列は変更しない。
- `Event::UpdateRxPowerW` のキー取得も値コピーからconst参照にする。

### API/ABI上の注意

`RxPowerWattPerChannelBand` はstd::mapの別名ではなく、リポジトリで使われる
map風APIを持つクラスになる。汎用insertは新レイアウトへコピーして追加するため、
std::map全APIや挿入時のiterator安定性を提供しない。受信ホットパスでは使わない。
外部コードのstd::map固有操作には移行が必要。依存モジュール/masterも再ビルドした。
新レイアウト付きコンストラクタは全帯域0初期値を持ち、StartRx内で入力順に
SetInputPowerを呼んで全値を設定してから、受信・干渉処理へ渡す。
新旧共有ライブラリを混在させないこと。

## 変更前スナップショット

`results/perf/wifi_shared_bands_before/` に変更前のmaster、全.so、設定、SHA256 manifestを保存。
前回の `wifi_insert_before`（insert版）とは**異なる**。今回はtry_emplace版対共有化版。
小規模比較用ヘッダも保存（公開ヘッダの転送includeを実体化し、変更した2ヘッダは
変更前のGit HEADから復元。生成wifi-module.hから新ヘッダのincludeを除外）。
本実験のbeforeは保存済み実行物を直接使用し、ヘッダは使わない。

## 2 seed・4実行の本条件A/B

```bash
cd /home/sota/ns-3.44
python3 scripts/verify_wifi_insert.py \
  --baseline results/perf/wifi_shared_bands_before \
  --seeds 1001 1002
```

順序：1001 before→after→1002 before→after。別masterの終了後に開始し、
実行中は設定編集・再ビルド・別シミュレーション起動をしない。
従来速度のままなら4回で約13時間。短縮幅は未確認。
各ペア開始時にafterの全実行物を凍結し、両版のライブラリ解決先/ハッシュを記録。
スクリプトは設定・実行物検査と完全比較を行い、不一致/未完了では終了コード1で停止。

出力：`results/perf/wifi_shared_ab_日時/seed1001/` と `seed1002/`。
各seed直下の `PASS.json` が成功マーカー。各before/afterのconsole.logで進行確認可能。
再比較には `--compare-only .../seed1001` を使用する。
比較対象は全境界の時刻/イベント数、master全行全列（assignment_compute_msだけ除く）、
実測報酬全行全列。実時間と研究出力を混同しない。CSV出力精度での一致を確認する。

## 実施した検証

```bash
cmake --build cmake-cache -j 4 --target master
python3 tests/test_wifi_perf_integration.py
python3 tests/test_wifi_shared_bands_ab.py
python3 tests/test_verify_wifi_insert.py
g++ -std=c++20 -O2 -Ibuild/include tests/wifi_shared_bands_test.cc \
  -Lbuild/lib -Wl,-rpath,"$PWD/build/lib" -lns3.44-wifi-optimized \
  -lns3.44-spectrum-optimized -lns3.44-core-optimized -lns3.44-network-optimized \
  -o /tmp/wifi-shared-bands-test
/tmp/wifi-shared-bands-test
git diff --check
```

- optimizedビルド成功（初回sandboxのccache制限、次にPtr三項演算型エラー。許可/修正後成功）。
- 通常のWi-Fi結合試験成功。
- before/after小規模試験はseed1001/1002ともbytes=200、events=378で一致。
  各seedでdevices.csv全行および境界の時刻/イベント数も一致。
  初回は旧ヘッダ束に新生成ヘッダincludeが混入しコンパイル失敗、修正後成功。
- 帯域順序/重複/比較上同等キー/複数セグメント/コピー/ムーブ/電力値独立性/
  キャッシュ無効化/旧イベントの寿命を検証。単体試験成功。
- 比較器・2seed逐次呼び出しの6テスト成功。
- 同じ単体試験を `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`
  でもコンパイルして実行成功（テストとヘッダが対象、ns-3全ライブラリは非instrumented）。
  初回はsandboxのptrace制約でLeakSanitizerが停止し、許可後のsandbox外実行で成功。

**80端末の2seed A/Bは未実行。高速化率、通信/QoEの本条件での一致は未確認。**
小規模試験の端末数は診断用であり、本実験の設定を減らしたものではない。
次は上記本条件A/Bを実行し、両seedで一致と所要時間を確認する。
