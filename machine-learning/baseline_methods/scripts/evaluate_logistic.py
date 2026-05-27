#!/usr/bin/env python3
"""Evaluate exported logistic-regression AP assignment model.

This script reads the C++/ns-3-readable JSON exported by train_logistic.py,
applies it to a teacher-data CSV, and reports imitation-learning metrics:
accuracy, precision, recall, F1, support, and confusion matrix.

The evaluation here measures how well the logistic model reproduces the
Hungarian teacher labels. It does not directly prove QoE improvement; QoE must
be evaluated separately by running the assignment policy in simulation.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np


def _load_model(model_path: Path) -> dict[str, Any]:
    with model_path.open(encoding="utf-8") as f:
        model = json.load(f)

    required = [
        "model_type",
        "feature_columns",
        "label_column",
        "classes",
        "scaler_mean",
        "scaler_scale",
        "coef",
        "intercept",
    ]
    missing = [key for key in required if key not in model]
    if missing:
        raise ValueError(f"missing model keys in {model_path}: {missing}")
    if model["model_type"] != "logistic_regression":
        raise ValueError(f"unsupported model_type: {model['model_type']}")

    return model


def _load_csv(input_csv: Path, feature_columns: list[str], label_column: str) -> tuple[np.ndarray, np.ndarray]:
    with input_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise ValueError(f"empty CSV: {input_csv}")

        missing = [c for c in feature_columns + [label_column] if c not in reader.fieldnames]
        if missing:
            raise ValueError(f"missing columns in {input_csv}: {missing}")

        x_rows: list[list[float]] = []
        y_rows: list[int] = []
        for row in reader:
            x_rows.append([float(row[col]) for col in feature_columns])
            y_rows.append(int(row[label_column]))

    if not y_rows:
        raise ValueError(f"no data rows in {input_csv}")

    return np.asarray(x_rows, dtype=float), np.asarray(y_rows, dtype=int)


def _predict(model: dict[str, Any], X: np.ndarray) -> np.ndarray:
    classes = np.asarray(model["classes"], dtype=int)
    mean = np.asarray(model["scaler_mean"], dtype=float)
    scale = np.asarray(model["scaler_scale"], dtype=float)
    coef = np.asarray(model["coef"], dtype=float)
    intercept = np.asarray(model["intercept"], dtype=float)

    if X.shape[1] != len(mean) or X.shape[1] != len(scale):
        raise ValueError(
            f"feature dimension mismatch: CSV has {X.shape[1]} columns, "
            f"model scaler has {len(mean)}"
        )
    if coef.shape != (len(classes), X.shape[1]):
        raise ValueError(
            f"coef shape mismatch: expected {(len(classes), X.shape[1])}, got {coef.shape}"
        )
    if intercept.shape[0] != len(classes):
        raise ValueError(
            f"intercept length mismatch: expected {len(classes)}, got {intercept.shape[0]}"
        )

    safe_scale = np.where(scale == 0.0, 1.0, scale)
    X_std = (X - mean) / safe_scale
    logits = X_std @ coef.T + intercept
    return classes[np.argmax(logits, axis=1)]


def _classification_metrics(y_true: np.ndarray, y_pred: np.ndarray, labels: list[int]) -> dict[str, Any]:
    total = int(len(y_true))
    accuracy = float(np.mean(y_true == y_pred))

    confusion: list[list[int]] = []
    per_class: dict[str, dict[str, float | int]] = {}

    for true_label in labels:
        row = []
        for pred_label in labels:
            row.append(int(np.sum((y_true == true_label) & (y_pred == pred_label))))
        confusion.append(row)

    for label in labels:
        tp = int(np.sum((y_true == label) & (y_pred == label)))
        fp = int(np.sum((y_true != label) & (y_pred == label)))
        fn = int(np.sum((y_true == label) & (y_pred != label)))
        support = int(np.sum(y_true == label))

        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0

        per_class[str(label)] = {
            "precision": precision,
            "recall": recall,
            "f1": f1,
            "support": support,
            "tp": tp,
            "fp": fp,
            "fn": fn,
        }

    macro_precision = float(np.mean([per_class[str(label)]["precision"] for label in labels]))
    macro_recall = float(np.mean([per_class[str(label)]["recall"] for label in labels]))
    macro_f1 = float(np.mean([per_class[str(label)]["f1"] for label in labels]))

    supports = np.asarray([per_class[str(label)]["support"] for label in labels], dtype=float)
    if supports.sum() > 0:
        weighted_precision = float(
            np.average([per_class[str(label)]["precision"] for label in labels], weights=supports)
        )
        weighted_recall = float(
            np.average([per_class[str(label)]["recall"] for label in labels], weights=supports)
        )
        weighted_f1 = float(np.average([per_class[str(label)]["f1"] for label in labels], weights=supports))
    else:
        weighted_precision = weighted_recall = weighted_f1 = 0.0

    return {
        "num_samples": total,
        "accuracy": accuracy,
        "labels": labels,
        "label_distribution": {str(k): int(v) for k, v in sorted(Counter(y_true.tolist()).items())},
        "prediction_distribution": {str(k): int(v) for k, v in sorted(Counter(y_pred.tolist()).items())},
        "per_class": per_class,
        "macro_avg": {
            "precision": macro_precision,
            "recall": macro_recall,
            "f1": macro_f1,
            "support": total,
        },
        "weighted_avg": {
            "precision": weighted_precision,
            "recall": weighted_recall,
            "f1": weighted_f1,
            "support": total,
        },
        "confusion_matrix": confusion,
    }


def _print_report(metrics: dict[str, Any]) -> None:
    print(f"num_samples: {metrics['num_samples']}")
    print(f"label_distribution: {metrics['label_distribution']}")
    print(f"prediction_distribution: {metrics['prediction_distribution']}")
    print(f"accuracy: {metrics['accuracy']:.6f}")
    print()
    print("classification report:")
    print(f"{'class':>8} {'precision':>10} {'recall':>10} {'f1':>10} {'support':>10}")
    for label in metrics["labels"]:
        row = metrics["per_class"][str(label)]
        print(
            f"{str(label):>8} "
            f"{row['precision']:>10.4f} "
            f"{row['recall']:>10.4f} "
            f"{row['f1']:>10.4f} "
            f"{row['support']:>10}"
        )
    for name in ["macro_avg", "weighted_avg"]:
        row = metrics[name]
        print(
            f"{name:>8} "
            f"{row['precision']:>10.4f} "
            f"{row['recall']:>10.4f} "
            f"{row['f1']:>10.4f} "
            f"{row['support']:>10}"
        )
    print()
    print("confusion matrix: rows=true, cols=pred")
    print("labels:", metrics["labels"])
    for label, row in zip(metrics["labels"], metrics["confusion_matrix"]):
        print(f"{label}: {row}")


def evaluate(model_path: Path, input_csv: Path, output_json: Path | None) -> dict[str, Any]:
    model = _load_model(model_path)
    feature_columns = list(model["feature_columns"])
    label_column = str(model["label_column"])

    X, y_true = _load_csv(input_csv, feature_columns, label_column)
    y_pred = _predict(model, X)

    labels = [int(label) for label in model["classes"]]
    metrics = _classification_metrics(y_true, y_pred, labels)
    result = {
        "evaluation_type": "logistic_imitation",
        "evaluated_at": datetime.now(timezone.utc).isoformat(),
        "model_path": str(model_path),
        "input_csv": str(input_csv),
        "model_type": model["model_type"],
        "feature_columns": feature_columns,
        "label_column": label_column,
        "notes": {
            "meaning": "Metrics show how well logistic regression imitates Hungarian assigned_ap labels.",
            "limitation": "QoE improvement must be evaluated separately in simulation.",
        },
        **metrics,
    }

    _print_report(result)

    if output_json is not None:
        output_json.parent.mkdir(parents=True, exist_ok=True)
        output_json.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nsaved: {output_json}")

    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True, help="Path to exported logistic model JSON.")
    parser.add_argument("--input", type=Path, required=True, help="Teacher-data CSV to evaluate on.")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help=(
            "Optional output JSON path. If omitted, saves under "
            "data/results/logistic_eval_<model>_<input>.json. Use --no-save to disable."
        ),
    )
    parser.add_argument("--no-save", action="store_true", help="Print metrics only; do not write JSON.")
    args = parser.parse_args()

    output = args.output
    if args.no_save:
        output = None
    elif output is None:
        output = Path("data/results") / f"logistic_eval_{args.model.stem}_{args.input.stem}.json"

    evaluate(args.model, args.input, output)


if __name__ == "__main__":
    main()
