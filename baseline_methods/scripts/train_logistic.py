#!/usr/bin/env python3
"""Train logistic regression for AP assignment and export C++-readable params."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import csv

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

FEATURE_COLUMNS = [
    "app_type",
    "current_ap",
    "num_users_ap0",
    "num_users_ap1",
    "num_users_ap2",
    "rtt_ap0",
    "rtt_ap1",
    "rtt_ap2",
    "estimated_tp_ap0",
    "estimated_tp_ap1",
    "estimated_tp_ap2",
]
LABEL_COLUMN = "assigned_ap"


def train(input_csv: Path, output_json: Path, max_iter: int) -> None:
    try:
        from sklearn.linear_model import LogisticRegression
        from sklearn.metrics import accuracy_score, classification_report
        from sklearn.model_selection import train_test_split
        from sklearn.pipeline import Pipeline
        from sklearn.preprocessing import StandardScaler
    except ImportError as exc:
        raise SystemExit(
            "scikit-learn is required. Install it in your environment before training."
        ) from exc

    with input_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise ValueError(f"empty CSV: {input_csv}")
        missing = [c for c in FEATURE_COLUMNS + [LABEL_COLUMN] if c not in reader.fieldnames]
        if missing:
            raise ValueError(f"missing columns in {input_csv}: {missing}")

        x_rows = []
        y_rows = []
        for row in reader:
            x_rows.append([float(row[col]) for col in FEATURE_COLUMNS])
            y_rows.append(int(row[LABEL_COLUMN]))

    X = np.asarray(x_rows, dtype=float)
    y = np.asarray(y_rows, dtype=int)
    if len(y) == 0:
        raise ValueError(f"no data rows in {input_csv}")

    unique_classes = sorted(set(y.tolist()))
    if len(unique_classes) < 2:
        raise ValueError(
            f"logistic regression needs at least 2 assigned_ap classes, "
            f"but the dataset contains only {unique_classes}. "
            "Generate more diverse teacher data or adjust the simulation scenario."
        )

    stratify = y
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=stratify
    )

    pipeline = Pipeline(
        [
            ("scaler", StandardScaler()),
            (
                "model",
                LogisticRegression(
                    max_iter=max_iter,
                    solver="lbfgs",
                ),
            ),
        ]
    )
    pipeline.fit(X_train, y_train)

    y_pred = pipeline.predict(X_test)
    print("accuracy:", accuracy_score(y_test, y_pred))
    print(classification_report(y_test, y_pred, digits=4))

    scaler = pipeline.named_steps["scaler"]
    model = pipeline.named_steps["model"]

    output = {
        "model_type": "logistic_regression",
        "feature_columns": FEATURE_COLUMNS,
        "label_column": LABEL_COLUMN,
        "classes": model.classes_.astype(int).tolist(),
        "scaler_mean": scaler.mean_.tolist(),
        "scaler_scale": scaler.scale_.tolist(),
        "coef": model.coef_.tolist(),
        "intercept": model.intercept_.tolist(),
        "notes": {
            "prediction": "standardize features, compute logits = coef*x + intercept, choose class with max logit",
            "method_name_for_ns3": "logistic",
        },
    }

    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"saved: {output_json}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("data/raw/logistic_teacher.csv"))
    parser.add_argument("--output", type=Path, default=Path("data/models/logistic_model.json"))
    parser.add_argument("--max-iter", type=int, default=1000)
    args = parser.parse_args()

    train(args.input, args.output, args.max_iter)


if __name__ == "__main__":
    main()
