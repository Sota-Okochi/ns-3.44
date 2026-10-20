# MARL診断ログ追加（2026-10-21）

目的: 次の新規学習から、収集済みデータでPPO更新を診断する。通信失敗率は研究評価対象とせず集計しない。
clip=0.1、学習率、rollout/epoch等は変更しない。
満足度下限0.01への変更とは別の、受動的な診断追加。

## updates.csv（従来列を保持）

- explained_variance_before/after:
  `1 - Var(G-V)/Var(G)`。母分散、float64で集計。
  beforeは収集時old value、afterは更新後Criticの同じ観測上の予測。
  `Var(G)<=1e-12` はNone（CSV空欄）。0に置換しない。
  afterは学習データ内の説明力であり汎化評価ではない。
- advantage_raw_mean/std: `G-V_old` の正規化前平均・母標準偏差。
- return_mean/std: 割引率1、4step完結軌跡のreturn平均・母標準偏差。
- joint_kl: `mean_step(sum_UE(KL(old || new)))`。
  観測を条件とする各端末の独立カテゴリ分布の積に対するKL。
  従来klは端末・step平均。固定80端末なら概ね80倍
  （集計精度の違いあり）。独立の情報ではなく全体変化の尺度。

optimizer更新終了後にno_gradで集計。入力テンソル変更、追加の乱数、
損失/報酬への追加、環境通信はない。

## episodes.csv（学習・検証・smoke共通）

平均H、報酬、seed、policy_version、実行時間など従来の軽量な集計を保持する。
今回追加した失敗率・その分子分母・下限到達/欠測件数の集計は削除した。
qoe.csvと欠測episodeの拒否処理は保持し、欠測を報酬へ置換しない。
既存の結果ファイルと過去の詳細デバッグログは書き換え・削除しない。

## 描画

```
python3 scripts/plot_marl.py NEW_RUN_DIRECTORY --output NEW_FIGURE_DIRECTORY
```

既存のtraining/validation図に加え、新列があれば
update_diagnosticsのPNG/PDFを出す。失敗率のグラフは生成しない。
欠測列を0埋めせず、旧CSVでは追加図を省略する。
この既存描画ツールは独立model seedごとに1ディレクトリを取る仕様で、
同一seedの分割再開runは事前結合が必要。

## テストと次の手順

```
python3 -m unittest discover -s tests -p 'test_marl*.py'
```

- EV、定数return、advantage、KLの数式テスト。
- 同一条件の模擬Sessionの観測・行動・qoeログがバイト一致。
- 診断有効/無効でActor/Criticのパラメータが完全一致。
  次回更新も比較し、optimizer状態への間接影響を確認。
- torch/Python RNG状態が更新診断で変わらないことを確認。
- 新旧CSVの描画試験。テスト用合成データは一時ディレクトリのみ。

新しいPythonソースhashがexperiment_idへ入るため、旧runのresume条件は
変わる。新規学習用の変更であり、旧runのcheckpoint/cacheを混在させない。
C++変更はないので、今回の診断追加だけを理由に再ビルドは不要。

次は満足度下限0.01の新バイナリで1episodeのsmoke（更新なし）を実行し、
実シミュレーション経由のepisodes.csvを確認する。
その後、別出力先で新規学習を開始。64ごとの検証は途中確認であり、
64での性能向上や収束を必須条件としない。
本変更時点で実ns-3の通し実行・新規学習は未実行。

失敗率削除後の実行結果: 上記unittestは18件すべて成功。`git diff --check`も成功。
テスト出力中の `Resume environment differs from checkpoint` は、
条件不一致を拒否する既存テストで意図的に発生させたもので、失敗ではない。
