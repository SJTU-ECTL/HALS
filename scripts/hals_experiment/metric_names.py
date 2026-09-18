"""Canonical error-metric names and legacy aliases.

The project historically used ``BIAS`` for the signed mean error term.  The
paper now calls the same quantity ``ME`` (mean error).  New generated configs
and reports should use ``ME``; readers keep accepting old ``BIAS`` artifacts so
that historical experiments remain replayable.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any


LEGACY_METRIC_ALIASES: dict[str, str] = {
    "BIAS": "ME",
    # Historical generators used MED for an absolute distance.  MAE is the
    # paper-facing spelling; old columns remain readable.
    "MED": "MAE",
}

SIGNED_MEAN_ERROR_METRICS = {"ME", "BIAS"}

CANONICAL_LOCAL_METRICS = (
    "MAE",
    "MAPE",
    "NMED",
    "NMSE",
    "ER",
    "ME",
)
LOCAL_METRIC_ALIASES = {
    "ME": ("ME", "BIAS"),
    "MAE": ("MAE", "MED"),
}


def canonical_metric(metric: str) -> str:
    """Return the project-facing spelling for an error metric name."""

    text = str(metric).strip().upper()
    return LEGACY_METRIC_ALIASES.get(text, text)


def metric_aliases(metric: str) -> tuple[str, ...]:
    """Return acceptable spellings for ``metric``, canonical first."""

    canonical = canonical_metric(metric)
    aliases = LOCAL_METRIC_ALIASES.get(canonical, (canonical,))
    # Preserve a caller's legacy spelling as an accepted value even if it is not
    # listed above.
    legacy = str(metric).strip().upper()
    if legacy and legacy not in aliases:
        return (*aliases, legacy)
    return aliases


def canonicalize_metrics(metrics: Sequence[str]) -> tuple[str, ...]:
    """Canonicalize and de-duplicate a metric sequence while preserving order."""

    out: list[str] = []
    for metric in metrics:
        canonical = canonical_metric(metric)
        if canonical not in out:
            out.append(canonical)
    return tuple(out)


def split_metric_column(column: str, metrics: Sequence[str] = CANONICAL_LOCAL_METRICS) -> tuple[str, str]:
    """Split ``<prefix>_<metric>`` and canonicalize legacy metric suffixes."""

    text = str(column)
    candidates: list[str] = []
    for metric in metrics:
        candidates.extend(metric_aliases(metric))
    # Prefer longer suffixes first so NMED/NMSE cannot be confused with ME.
    for metric in sorted(dict.fromkeys(candidates), key=len, reverse=True):
        suffix = f"_{metric}"
        if text.upper().endswith(suffix) and len(text) > len(suffix):
            return text[: -len(suffix)], canonical_metric(metric)
    raise ValueError(f"unsupported metric column {column!r}")


def column_aliases(prefix: str, metric: str) -> tuple[str, ...]:
    """Return acceptable ``<prefix>_<metric>`` column names."""

    return tuple(f"{prefix}_{alias}" for alias in metric_aliases(metric))


def first_existing_column(fields: Sequence[str], prefix: str, metric: str) -> str | None:
    """Return the first canonical/legacy column present in ``fields``."""

    field_set = set(fields)
    for column in column_aliases(prefix, metric):
        if column in field_set:
            return column
    return None


def metric_value(row: Mapping[str, Any], prefix: str, metric: str, default: float = 0.0) -> float:
    """Read a metric value from a row, accepting canonical and legacy names."""

    for column in column_aliases(prefix, metric):
        if column in row and row[column] not in ("", None):
            return float(row[column])
    return float(default)
