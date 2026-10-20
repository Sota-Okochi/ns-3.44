# 満足度下限 0.01（2026-10-21）

ユーザー指定により、新条件では `S_eff=max(S_raw, 0.01)` を採用する。
ゼロだけでなく正の0.01未満も下限の対象。過去の実験結果は変更しない。
PPO clipやDQNの探索epsilon、bps→Mbpsの1e-6は変更しない。

## 適用範囲

- MARL: `rl/marl/core.py` の既定epsilonと
  `configs/marl/ppo_rollout8.json` を0.01に変更。実測ゼロ・確認済みRTT
  timeoutは0.01。真の欠測は従来どおりNoneで、学習報酬に変換しない。
  raw満足度を保持し、effective満足度・観測・H・報酬に新下限を使用。
- random/logistic/centralized_dqn:
  共通APselectionの実測満足度・推定満足度・Hに新下限を使用。
  従来TPゼロ/未計測のfallbackは実際には0.1だったため、こちらも0.01へ変更。
  実測TP=0は既存のmissing-data graceで1.0にせず、0.01として扱う。
  この共通処理を使用するno_switch等の他手法も同じ下限になる。
- 旧 `APselection_memo.*` は現行ビルド対象外のため変更しない。

## 既存の差異（今回変更しないもの）

MARL以外の既存手法には未計測TPのfallback/猶予期間、
未計測RTTを要求RTTで代用する処理がある。
今回、これらをMARLの欠測・timeout規約へ全面移行したわけではない。
満足度下限の一致だけで、全手法の計測規約が完全に一致したとは言えない。
比較ではhandoverGraceCycles等の設定を保存し、差異を確認する。

## 記録と互換性

- MARL: manifest.jsonのconfig.epsilonに保存。
- C++: 初期化時に `[QoE] satisfaction_floor=0.01 ...` を標準出力へ記録。
  実行時はconsole.logを保存すること。
- master_log等の満足度列名は変更しない。新しい値は下限適用後。
- 旧CSV、checkpoint、学習用バイナリは上書きしない。
- 新MARL条件で旧checkpointを通常resumeすることは不可。
  旧条件の検証は旧コード/設定/バイナリのスナップショットで実行すること。
- centralized_dqnの旧重みも新条件で学習済みとは扱わない。

## ビルド

既存の検証済みWi-Fiライブラリを維持してmaster/kamedaのみ別ビルド:

```
python3 scripts/build_marl_isolated.py \
  --output build/qoe-floor001-v1 \
  --base-libraries build/wifi-band-cache-v1/lib --jobs 2
```

実行には `build/qoe-floor001-v1/master` を使用する。
旧 `build/wifi-band-cache-v1/run-master` は旧C++条件のままなので、
新条件のbaseline評価には使用しない。

## 検証

```
python3 -m unittest discover -s tests -p 'test_marl*.py'
python3 tests/run_qoe_floor_test.py --build build/qoe-floor001-v1
```

`tests/qoe_floor_test.cc` はネットワークシミュレーションを起動せず、
random/logistic/centralized_dqnの共通計算について、ゼロ・境界値・
RTT低満足度・調和平均・実測ゼロと未計測の区別を検証する。
長時間シミュレーション・新条件の学習性能評価は未実行。

今回の実行結果: 隔離ビルド成功、Python 15テスト成功、
C++共通計算テスト成功。kameda以外のコピーした共有ライブラリは
元wifi-band-cache-v1とSHA256一致を確認した。
次の検証は新バイナリでの短い通し実行と、同一seed・条件での各手法比較。
