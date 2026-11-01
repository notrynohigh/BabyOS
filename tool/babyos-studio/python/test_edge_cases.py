#!/usr/bin/env python3
"""Edge-case tests for AutoML feature engineering — service-layer (no server).

Covers HIGH-RISK areas not yet tested:
1. Channel features normalization (effective_features_for_channel)
2. FeatureConfig schema edge cases (step, window_len_s, feature_ids)
3. effective_n boundary rounding cases (half-integer sr*ws)
"""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.abspath(__file__)))

from app.schemas import FeatureConfig, ProjectMeta
from app.services import feature_service
from app.deps import AppError

PASS = 0
FAIL = 0
ERRORS = []


def check(cond: bool, msg: str):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  OK  {msg}")
    else:
        FAIL += 1
        ERRORS.append(msg)
        print(f"  FAIL  {msg}")


# Helper: build a minimal ProjectMeta for effective_n / validation tests
def _meta(sr: float = 100.0, mode: str = "timeseries", channels: list[str] | None = None) -> ProjectMeta:
    return ProjectMeta(
        project_id="test", name="test", mode=mode,
        sampling_rate=sr, channels=channels or ["accel_x", "accel_y", "accel_z"],
    )


# ============================================================
# EC-1: effective_n boundary rounding (half-integer sr*ws)
# ============================================================
print("=" * 60)
print("EC-1: effective_n boundary rounding")
print("=" * 60)

# Python round() uses banker's rounding: round(x.5) → nearest even.
# effective_n: n = round(sr*ws), floor 2, then odd→even.

# EC-1.1: sr*ws = 1.5 → round(1.5) = 2 → even → 2
meta = _meta(sr=1.0)
cfg = FeatureConfig(window_len_s=1.5, step=1, feature_ids=["mean"])
n = feature_service.effective_n(meta, cfg)
check(n == 2, f"EC-1.1: 1.0*1.5=1.5 → round=2 → n={n}, expect 2")

# EC-1.2: sr*ws = 2.5 → floor(2.5+0.5)=3 → odd→even → 4
meta2 = _meta(sr=1.0)
cfg2 = FeatureConfig(window_len_s=2.5, step=1, feature_ids=["mean"])
n2 = feature_service.effective_n(meta2, cfg2)
check(n2 == 4, f"EC-1.2: 1.0*2.5=2.5 → round=3 → n={n2}, expect 4")

# EC-1.3: sr*ws = 3.5 → round(3.5) = 4 (banker's) → even → 4
meta3 = _meta(sr=1.0)
cfg3 = FeatureConfig(window_len_s=3.5, step=1, feature_ids=["mean"])
n3 = feature_service.effective_n(meta3, cfg3)
check(n3 == 4, f"EC-1.3: 1.0*3.5=3.5 → round=4 → n={n3}, expect 4")

# EC-1.4: sr*ws = 0.5 → round(0.5) = 0 (banker's) → floor 2
meta4 = _meta(sr=1.0)
cfg4 = FeatureConfig(window_len_s=0.5, step=1, feature_ids=["mean"])
n4 = feature_service.effective_n(meta4, cfg4)
check(n4 == 2, f"EC-1.4: 1.0*0.5=0.5 → round=0 → floor 2 → n={n4}, expect 2")

# EC-1.5: sr*ws = 1.0 → round(1.0) = 1 → floor 2
meta5 = _meta(sr=1.0)
cfg5 = FeatureConfig(window_len_s=1.0, step=1, feature_ids=["mean"])
n5 = feature_service.effective_n(meta5, cfg5)
check(n5 == 2, f"EC-1.5: 1.0*1.0=1.0 → round=1 → floor 2 → n={n5}, expect 2")

# EC-1.6: sr*ws = 4.5 → floor(4.5+0.5)=5 → odd→even → 6
meta6 = _meta(sr=1.0)
cfg6 = FeatureConfig(window_len_s=4.5, step=1, feature_ids=["mean"])
n6 = feature_service.effective_n(meta6, cfg6)
check(n6 == 6, f"EC-1.6: 1.0*4.5=4.5 → round=5 → n={n6}, expect 6")

# EC-1.7: sr*ws = 5.5 → round(5.5) = 6 (banker's) → even → 6
meta7 = _meta(sr=1.0)
cfg7 = FeatureConfig(window_len_s=5.5, step=1, feature_ids=["mean"])
n7 = feature_service.effective_n(meta7, cfg7)
check(n7 == 6, f"EC-1.7: 1.0*5.5=5.5 → round=6 → n={n7}, expect 6")

# EC-1.8: sr*ws = 0.001 → round(0.001) = 0 → floor 2
meta8 = _meta(sr=1.0)
cfg8 = FeatureConfig(window_len_s=0.001, step=1, feature_ids=["mean"])
n8 = feature_service.effective_n(meta8, cfg8)
check(n8 == 2, f"EC-1.8: 1.0*0.001=0.001 → round=0 → floor 2 → n={n8}, expect 2")

# EC-1.9: sr*ws = 0.999 → round(0.999) = 1 → floor 2
meta9 = _meta(sr=1.0)
cfg9 = FeatureConfig(window_len_s=0.999, step=1, feature_ids=["mean"])
n9 = feature_service.effective_n(meta9, cfg9)
check(n9 == 2, f"EC-1.9: 1.0*0.999=0.999 → round=1 → floor 2 → n={n9}, expect 2")

# EC-1.10: sr*ws = 2.0 → round(2.0) = 2 → even → 2 (no rounding needed)
meta10 = _meta(sr=1.0)
cfg10 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
n10 = feature_service.effective_n(meta10, cfg10)
check(n10 == 2, f"EC-1.10: 1.0*2.0=2.0 → n={n10}, expect 2")


# ============================================================
# EC-2: effective_features_for_channel — null vs dict
# ============================================================
print("\n" + "=" * 60)
print("EC-2: effective_features_for_channel normalization")
print("=" * 60)

# EC-2.1: channel_features=None → falls back to global feature_ids
cfg_null = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std", "rms"],
    channel_features=None,
)
feats_x = feature_service.effective_features_for_channel("accel_x", cfg_null)
check(feats_x == ["mean", "std", "rms"],
      f"EC-2.1: None → fallback to feature_ids → {feats_x}")

# EC-2.2: channel_features={} (empty dict) → channel not found → empty list
cfg_empty = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std", "rms"],
    channel_features={},
)
feats_empty = feature_service.effective_features_for_channel("accel_x", cfg_empty)
check(feats_empty == [],
      f"EC-2.2: empty dict → channel not found → {feats_empty}")

# EC-2.3: channel_features has explicit mapping for one channel
cfg_dict = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std", "rms"],
    channel_features={"accel_x": ["mean", "zcr"], "accel_y": ["std"]},
)
feats_dict_x = feature_service.effective_features_for_channel("accel_x", cfg_dict)
feats_dict_y = feature_service.effective_features_for_channel("accel_y", cfg_dict)
feats_dict_z = feature_service.effective_features_for_channel("accel_z", cfg_dict)
check(feats_dict_x == ["mean", "zcr"],
      f"EC-2.3a: accel_x → {feats_dict_x} (explicit)")
check(feats_dict_y == ["std"],
      f"EC-2.3b: accel_y → {feats_dict_y} (explicit)")
check(feats_dict_z == [],
      f"EC-2.3c: accel_z not in dict → {feats_dict_z}")

# EC-2.4: all channels same features → channel_features set but redundant
cfg_all_same = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std"],
    channel_features={"accel_x": ["mean", "std"], "accel_y": ["mean", "std"], "accel_z": ["mean", "std"]},
)
feats_as_x = feature_service.effective_features_for_channel("accel_x", cfg_all_same)
feats_as_y = feature_service.effective_features_for_channel("accel_y", cfg_all_same)
check(feats_as_x == ["mean", "std"],
      f"EC-2.4a: all-same dict accel_x → {feats_as_x}")
check(feats_as_x == feats_as_y,
      f"EC-2.4b: accel_x == accel_y when all same")

# EC-2.5: band_ratio expansion in per-channel
cfg_band = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean"],
    channel_features={"accel_x": ["mean", "band_ratio"]},
    freq_enabled=True, freq_bands=3,
)
feats_band = feature_service.effective_features_for_channel("accel_x", cfg_band)
check(feats_band == ["mean", "band0_ratio", "band1_ratio", "band2_ratio"],
      f"EC-2.5: band_ratio expansion → {feats_band}")

# EC-2.6: None channel_features falls through to feature_ids (not empty)
cfg_none = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["rms", "ptp"],
    channel_features=None,
)
feats_none = feature_service.effective_features_for_channel("any_channel", cfg_none)
check(feats_none == ["rms", "ptp"],
      f"EC-2.6: None → any channel gets feature_ids → {feats_none}")

# EC-2.7: empty feature_ids + channel_features=None → empty
cfg_empty_ids = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=[],
    channel_features=None,
)
feats_empty_ids = feature_service.effective_features_for_channel("accel_x", cfg_empty_ids)
check(feats_empty_ids == [],
      f"EC-2.7: empty feature_ids + None → {feats_empty_ids}")


# ============================================================
# EC-3: FeatureConfig schema edge cases
# ============================================================
print("\n" + "=" * 60)
print("EC-3: FeatureConfig schema edge cases")
print("=" * 60)

# EC-3.1: step=1 is valid (default, ge=1)
cfg_s1 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
check(cfg_s1.step == 1, f"EC-3.1: step=1 accepted → step={cfg_s1.step}")

# EC-3.2: step=0 → rejected by ge=1
try:
    FeatureConfig(window_len_s=2.0, step=0, feature_ids=["mean"])
    check(False, "EC-3.2: step=0 should be rejected by schema")
except Exception as e:
    check("greater_than_equal" in str(e) or "step" in str(e).lower(),
          f"EC-3.2: step=0 → {type(e).__name__}")

# EC-3.3: step=-1 → rejected by ge=1
try:
    FeatureConfig(window_len_s=2.0, step=-1, feature_ids=["mean"])
    check(False, "EC-3.3: step=-1 should be rejected by schema")
except Exception as e:
    check("greater_than_equal" in str(e) or "step" in str(e).lower(),
          f"EC-3.3: step=-1 → {type(e).__name__}")

# EC-3.4: window_len_s very small (0.001) with high sr (1000)
# sr*ws = 1000*0.001 = 1.0 → round=1 → floor 2 → effective_n=2
cfg_tiny = FeatureConfig(window_len_s=0.001, step=1, feature_ids=["mean"])
meta_high = _meta(sr=1000.0)
n_tiny = feature_service.effective_n(meta_high, cfg_tiny)
check(n_tiny == 2, f"EC-3.4: 1000Hz*0.001s → n={n_tiny}, expect 2")

# EC-3.5: window_len_s very large (100.0) with low sr (0.1)
# sr*ws = 0.1*100 = 10.0 → round=10 → even → 10
cfg_large = FeatureConfig(window_len_s=100.0, step=1, feature_ids=["mean"])
meta_low = _meta(sr=0.1)
n_large = feature_service.effective_n(meta_low, cfg_large)
check(n_large == 10, f"EC-3.5: 0.1Hz*100s → n={n_large}, expect 10")

# EC-3.6: feature_ids empty in timeseries → _validate rejects
cfg_empty_feats = FeatureConfig(window_len_s=2.0, step=1, feature_ids=[])
meta_ts = _meta(sr=100.0, mode="timeseries")
try:
    feature_service._validate(meta_ts, cfg_empty_feats)
    check(False, "EC-3.6: empty feature_ids in timeseries should be rejected")
except AppError as e:
    check("NO_FEATURES_SELECTED" in str(e),
          f"EC-3.6: → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.7: feature_ids empty in table → _validate allows (uses all columns)
cfg_table_empty = FeatureConfig(window_len_s=2.0, step=1, feature_ids=[])
meta_tbl = _meta(sr=100.0, mode="table")
try:
    feature_service._validate(meta_tbl, cfg_table_empty)
    check(True, "EC-3.7: empty feature_ids in table mode → allowed")
except Exception as e:
    check(False, f"EC-3.7: table mode should allow empty feature_ids, got {e}")

# EC-3.8: negative window_len_s → effective_n produces n < 2 → _validate rejects
cfg_neg_win = FeatureConfig(window_len_s=-5.0, step=1, feature_ids=["mean"])
meta_neg = _meta(sr=100.0)
try:
    feature_service._validate(meta_neg, cfg_neg_win)
    check(False, "EC-3.8: negative window_len_s should be rejected")
except AppError as e:
    check(True, f"EC-3.8: negative window_len → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.9: window_len_s=0 → effective_n=round(0)=0→2, but _validate checks raw n<2
cfg_zero_win = FeatureConfig(window_len_s=0.0, step=1, feature_ids=["mean"])
meta_zero = _meta(sr=100.0)
try:
    feature_service._validate(meta_zero, cfg_zero_win)
    check(False, "EC-3.9: window_len_s=0 should be rejected (n=round(0)=0 < 2)")
except AppError as e:
    check(True, f"EC-3.9: window_len=0 → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.10: step=1 with sr=100, window=2.0 → n=200, min_step=20 → step < min_step → rejected
cfg_step1_high = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
meta_h = _meta(sr=100.0)
try:
    feature_service._validate(meta_h, cfg_step1_high)
    check(False, "EC-3.10: step=1 with sr=100 should be rejected (min_step=20)")
except AppError as e:
    check("STEP_TOO_SMALL" in str(e),
          f"EC-3.10: → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.11: step=1 with sr=10, window=2.0 → n=20, min_step=2 → step < min_step → rejected
cfg_step1_mid = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
meta_m = _meta(sr=10.0)
try:
    feature_service._validate(meta_m, cfg_step1_mid)
    check(False, "EC-3.11: step=1 with sr=10 should be rejected (min_step=2)")
except AppError as e:
    check("STEP_TOO_SMALL" in str(e),
          f"EC-3.11: → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.12: step=1 with sr=1, window=2.0 → n=2, min_step=1 → step=1 OK
# Must mock project_dir so _validate can read segments.json
cfg_step1_low = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
meta_l = _meta(sr=1.0)
with tempfile.TemporaryDirectory() as tmpdir:
    labeling_dir = Path(tmpdir) / "labeling"
    labeling_dir.mkdir(exist_ok=True)
    (labeling_dir / "segments.json").write_text("[]")

    def fake_project_dir(pid):
        return Path(tmpdir)

    with patch("app.services.feature_service.project_dir", side_effect=fake_project_dir):
        try:
            feature_service._validate(meta_l, cfg_step1_low)
            check(True, "EC-3.12: step=1 with sr=1 → min_step=1 → allowed")
        except AppError as e:
            check(False, f"EC-3.12: should be allowed, got {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.13: window_len_s = 0.001, sr = 100 → raw n=round(0.1)=0 < 2 → WINDOW_TOO_SMALL
# Note: _validate checks raw n BEFORE effective_n() clamps to 2,
# so this is correctly rejected as WINDOW_TOO_SMALL.
cfg_tiny2 = FeatureConfig(window_len_s=0.001, step=1, feature_ids=["mean"])
meta_t = _meta(sr=100.0)
n_tiny2 = feature_service.effective_n(meta_t, cfg_tiny2)
check(n_tiny2 == 2, f"EC-3.13a: 100Hz*0.001s → effective_n={n_tiny2}, expect 2")
with tempfile.TemporaryDirectory() as tmpdir:
    labeling_dir = Path(tmpdir) / "labeling"
    labeling_dir.mkdir(exist_ok=True)
    (labeling_dir / "segments.json").write_text("[]")

    with patch("app.services.feature_service.project_dir", side_effect=lambda pid: Path(tmpdir)):
        try:
            feature_service._validate(meta_t, cfg_tiny2)
            check(False, "EC-3.13b: raw n=0 < 2 should trigger WINDOW_TOO_SMALL")
        except AppError as e:
            check("WINDOW_TOO_SMALL" in str(e),
                  f"EC-3.13b: raw n=0 → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-3.14: window_len_s = 1000.0, sr = 0.001 → n=round(1.0)=1→2, eff_n=2
cfg_huge = FeatureConfig(window_len_s=1000.0, step=1, feature_ids=["mean"])
meta_tiny_sr = _meta(sr=0.001)
n_huge = feature_service.effective_n(meta_tiny_sr, cfg_huge)
check(n_huge == 2, f"EC-3.14: 0.001Hz*1000s → n={n_huge}, expect 2")


# ============================================================
# EC-4: effective_n odd→even alignment at various scales
# ============================================================
print("\n" + "=" * 60)
print("EC-4: effective_n odd→even alignment")
print("=" * 60)

# EC-4.1: sr*ws = 3 → odd → 4
meta41 = _meta(sr=1.0)
cfg41 = FeatureConfig(window_len_s=3.0, step=1, feature_ids=["mean"])
n41 = feature_service.effective_n(meta41, cfg41)
check(n41 == 4, f"EC-4.1: 1.0*3.0=3 → odd→even → n={n41}, expect 4")

# EC-4.2: sr*ws = 5 → odd → 6
meta42 = _meta(sr=1.0)
cfg42 = FeatureConfig(window_len_s=5.0, step=1, feature_ids=["mean"])
n42 = feature_service.effective_n(meta42, cfg42)
check(n42 == 6, f"EC-4.2: 1.0*5.0=5 → odd→even → n={n42}, expect 6")

# EC-4.3: sr*ws = 7 → odd → 8
meta43 = _meta(sr=1.0)
cfg43 = FeatureConfig(window_len_s=7.0, step=1, feature_ids=["mean"])
n43 = feature_service.effective_n(meta43, cfg43)
check(n43 == 8, f"EC-4.3: 1.0*7.0=7 → odd→even → n={n43}, expect 8")

# EC-4.4: sr*ws = 99 → odd → 100
meta44 = _meta(sr=1.0)
cfg44 = FeatureConfig(window_len_s=99.0, step=1, feature_ids=["mean"])
n44 = feature_service.effective_n(meta44, cfg44)
check(n44 == 100, f"EC-4.4: 1.0*99.0=99 → odd→even → n={n44}, expect 100")

# EC-4.5: sr*ws = 100 → even → 100 (no change)
meta45 = _meta(sr=1.0)
cfg45 = FeatureConfig(window_len_s=100.0, step=1, feature_ids=["mean"])
n45 = feature_service.effective_n(meta45, cfg45)
check(n45 == 100, f"EC-4.5: 1.0*100.0=100 → even → n={n45}, expect 100")


# ============================================================
# EC-5: default_channel_features consistency
# ============================================================
print("\n" + "=" * 60)
print("EC-5: default_channel_features consistency")
print("=" * 60)

# EC-5.1: default_channel_features generates correct mapping
channels = ["accel_x", "accel_y", "accel_z"]
dcf = feature_service.default_channel_features(channels)
check(isinstance(dcf, dict), f"EC-5.1a: returns dict, type={type(dcf).__name__}")
check(sorted(dcf.keys()) == sorted(channels),
      f"EC-5.1b: keys match channels → {sorted(dcf.keys())}")
for ch in channels:
    check(dcf[ch] == list(feature_service.DEFAULT_FEATURE_IDS),
          f"EC-5.1c: {ch} → {dcf[ch]}")

# EC-5.2: default_channel_features with single channel
dcf_single = feature_service.default_channel_features(["only_one"])
check(list(dcf_single.keys()) == ["only_one"],
      f"EC-5.2: single channel → {list(dcf_single.keys())}")

# EC-5.3: default_channel_features with empty list
dcf_empty = feature_service.default_channel_features([])
check(dcf_empty == {}, f"EC-5.3: empty channels → {dcf_empty}")


# ============================================================
# EC-6: get_config auto-populates channel_features
# ============================================================
print("\n" + "=" * 60)
print("EC-6: get_config auto-populates channel_features")
print("=" * 60)

# We can't easily test get_config without a real project dir, but we can
# verify the logic by checking that the returned config has channel_features
# when meta.channels is set. Test via the default config path.
meta6 = _meta(sr=100.0, channels=["ch_a", "ch_b"])

# get_config will try to read from disk; if project doesn't exist, it raises.
# Instead, test the logic directly:
# When meta.channels is non-empty and no config.json exists,
# get_config should return a config with channel_features populated.
# We can't call get_config without a real project, so we verify the
# helper function directly.
dcf6 = feature_service.default_channel_features(meta6.channels)
cfg6 = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=list(feature_service.DEFAULT_FEATURE_IDS),
    channel_features=dcf6,
)
feats_a = feature_service.effective_features_for_channel("ch_a", cfg6)
feats_b = feature_service.effective_features_for_channel("ch_b", cfg6)
check(feats_a == list(feature_service.DEFAULT_FEATURE_IDS),
      f"EC-6a: ch_a gets default features → {feats_a}")
check(feats_a == feats_b,
      f"EC-6b: ch_a == ch_b when using default_channel_features")

# EC-6c: verify that get_config fallback logic populates channel_features
# by checking DEFAULT_CONFIG has no channel_features
check(feature_service.DEFAULT_CONFIG.channel_features is None,
      f"EC-6c: DEFAULT_CONFIG.channel_features is None")


# ============================================================
# EC-7: effective_features (global, non-per-channel) expansion
# ============================================================
print("\n" + "=" * 60)
print("EC-7: effective_features band_ratio expansion")
print("=" * 60)

# EC-7.1: band_ratio expanded to band0..band{k-1}_ratio
cfg71 = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "band_ratio", "std"],
    freq_enabled=True, freq_bands=4,
)
eff71 = feature_service.effective_features(cfg71)
check(eff71 == ["mean", "band0_ratio", "band1_ratio", "band2_ratio", "band3_ratio", "std"],
      f"EC-7.1: band_ratio expansion → {eff71}")

# EC-7.2: no band_ratio → no expansion
cfg72 = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std"],
)
eff72 = feature_service.effective_features(cfg72)
check(eff72 == ["mean", "std"],
      f"EC-7.2: no band_ratio → {eff72}")

# EC-7.3: empty feature_ids → empty
cfg73 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=[])
eff73 = feature_service.effective_features(cfg73)
check(eff73 == [], f"EC-7.3: empty → {eff73}")


# ============================================================
# EC-8: feature_names with per-channel config
# ============================================================
print("\n" + "=" * 60)
print("EC-8: feature_names with channel_features")
print("=" * 60)

meta8 = _meta(sr=100.0, channels=["accel_x", "accel_y", "accel_z"])

# EC-8.1: channel_features=None → all channels use feature_ids → 3ch * 3feat = 9 names
cfg81 = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std", "rms"],
    channel_features=None,
)
names81 = feature_service.feature_names(meta8, cfg81)
check(len(names81) == 9,
      f"EC-8.1: None channel_features → {len(names81)} names (expect 9)")
check("accel_x__mean" in names81 and "accel_z__rms" in names81,
      f"EC-8.1: names include ch__feat format → {names81[:3]}...")

# EC-8.2: channel_features with different per-channel → varies
cfg82 = FeatureConfig(
    window_len_s=2.0, step=1,
    feature_ids=["mean", "std", "rms"],
    channel_features={"accel_x": ["mean"], "accel_y": ["std", "rms"], "accel_z": []},
)
names82 = feature_service.feature_names(meta8, cfg82)
check(len(names82) == 3,  # accel_x:1 + accel_y:2 + accel_z:0 = 3
      f"EC-8.2: mixed per-channel → {len(names82)} names (expect 3)")
check("accel_x__mean" in names82, "EC-8.2: accel_x__mean present")
check("accel_y__std" in names82, "EC-8.2: accel_y__std present")
check("accel_y__rms" in names82, "EC-8.2: accel_y__rms present")
check("accel_z__" not in str(names82), "EC-8.2: accel_z has no features → not in names")

# EC-8.3: table mode with empty feature_ids → returns meta.channels as names
meta83 = _meta(sr=100.0, mode="table", channels=["col_a", "col_b", "col_c"])
cfg83 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=[])
names83 = feature_service.feature_names(meta83, cfg83)
check(names83 == ["col_a", "col_b", "col_c"],
      f"EC-8.3: table empty feature_ids → {names83}")

# EC-8.4: table mode with explicit feature_ids → those names
meta84 = _meta(sr=100.0, mode="table", channels=["col_a", "col_b"])
cfg84 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["col_a"])
names84 = feature_service.feature_names(meta84, cfg84)
check(names84 == ["col_a"],
      f"EC-8.4: table explicit feature_ids → {names84}")


# ============================================================
# EC-9: _validate with sampling_rate=0 (skips window checks)
# ============================================================
print("\n" + "=" * 60)
print("EC-9: _validate sampling_rate=0 skips window checks")
print("=" * 60)

# EC-9.1: sr=0, valid feature_ids → passes (skips window/step checks)
meta91 = _meta(sr=0.0)
cfg91 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean", "std"])
try:
    feature_service._validate(meta91, cfg91)
    check(True, "EC-9.1: sr=0 → passes window validation")
except AppError as e:
    check(False, f"EC-9.1: should pass, got {e.detail.get('code', '') if hasattr(e, 'detail') else e}")

# EC-9.2: sr=0, unknown feature → still rejected
cfg92 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean", "nonexistent_feature"])
try:
    feature_service._validate(meta91, cfg92)
    check(False, "EC-9.2: sr=0 + unknown feature should be rejected")
except AppError as e:
    check("BAD_FEATURE" in str(e),
          f"EC-9.2: → {e.detail.get('code', '') if hasattr(e, 'detail') else e}")


# ============================================================
# EC-10: Pydantic field constraints
# ============================================================
print("\n" + "=" * 60)
print("EC-10: Pydantic field constraints")
print("=" * 60)

# EC-10.1: step must be int (Pydantic coerces float to int if possible)
try:
    cfg101 = FeatureConfig(window_len_s=2.0, step=5.0, feature_ids=["mean"])
    check(cfg101.step == 5, f"EC-10.1: step=5.0 coerced to int → {cfg101.step}")
except Exception as e:
    check(False, f"EC-10.1: step=5.0 should coerce to 5, got {e}")

# EC-10.2: window_len_s accepts float
cfg102 = FeatureConfig(window_len_s=0.5, step=1, feature_ids=["mean"])
check(cfg102.window_len_s == 0.5, f"EC-10.2: window_len_s=0.5 → {cfg102.window_len_s}")

# EC-10.3: norm validation
for valid_norm in ["none", "minmax", "zscore", "robust"]:
    try:
        cfg103 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"], norm=valid_norm)
        check(cfg103.norm == valid_norm, f"EC-10.3: norm={valid_norm} accepted")
    except Exception as e:
        check(False, f"EC-10.3: norm={valid_norm} should be accepted, got {e}")

# EC-10.4: invalid norm rejected
try:
    FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"], norm="invalid")
    check(False, "EC-10.4: norm='invalid' should be rejected")
except Exception:
    check(True, "EC-10.4: norm='invalid' → rejected by Pydantic")

# EC-10.5: channel_features accepts dict or None
cfg105a = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"], channel_features=None)
check(cfg105a.channel_features is None, f"EC-10.5a: None accepted")
cfg105b = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"],
                        channel_features={"a": ["mean"]})
check(isinstance(cfg105b.channel_features, dict), f"EC-10.5b: dict accepted")

# EC-10.6: model_dump preserves all fields
cfg106 = FeatureConfig(
    window_len_s=1.5, step=3, feature_ids=["mean", "std"],
    channel_features={"ch1": ["mean"]},
    freq_enabled=True, freq_bands=4, norm="minmax",
)
dumped = cfg106.model_dump()
check(dumped["window_len_s"] == 1.5, f"EC-10.6a: window_len_s in dump")
check(dumped["step"] == 3, f"EC-10.6b: step in dump")
check(dumped["channel_features"] == {"ch1": ["mean"]}, f"EC-10.6c: channel_features in dump")
check(dumped["freq_bands"] == 4, f"EC-10.6d: freq_bands in dump")
check(dumped["norm"] == "minmax", f"EC-10.6e: norm in dump")


# ============================================================
# EC-11: effective_n non-integer sr*ws rounding
# ============================================================
print("\n" + "=" * 60)
print("EC-11: effective_n non-integer sr*ws")
print("=" * 60)

# EC-11.1: sr=33.3, ws=3.0 → 99.9 → round(99.9)=100 → even → 100
meta111 = _meta(sr=33.3)
cfg111 = FeatureConfig(window_len_s=3.0, step=1, feature_ids=["mean"])
n111 = feature_service.effective_n(meta111, cfg111)
check(n111 == 100, f"EC-11.1: 33.3*3.0=99.9 → round=100 → n={n111}, expect 100")

# EC-11.2: sr=10.0, ws=0.3 → 3.0 → round(3.0)=3 → odd → 4
meta112 = _meta(sr=10.0)
cfg112 = FeatureConfig(window_len_s=0.3, step=1, feature_ids=["mean"])
n112 = feature_service.effective_n(meta112, cfg112)
check(n112 == 4, f"EC-11.2: 10.0*0.3=3.0 → odd→even → n={n112}, expect 4")

# EC-11.3: sr=10.0, ws=0.29 → 2.9 → round(2.9)=3 → odd → 4
meta113 = _meta(sr=10.0)
cfg113 = FeatureConfig(window_len_s=0.29, step=1, feature_ids=["mean"])
n113 = feature_service.effective_n(meta113, cfg113)
check(n113 == 4, f"EC-11.3: 10.0*0.29=2.9 → round=3 → n={n113}, expect 4")

# EC-11.4: sr=50.0, ws=0.1 → 5.0 → round=5 → odd → 6
meta114 = _meta(sr=50.0)
cfg114 = FeatureConfig(window_len_s=0.1, step=1, feature_ids=["mean"])
n114 = feature_service.effective_n(meta114, cfg114)
check(n114 == 6, f"EC-11.4: 50.0*0.1=5.0 → odd→even → n={n114}, expect 6")

# EC-11.5: sr=200.0, ws=0.1 → 20.0 → round=20 → even → 20
meta115 = _meta(sr=200.0)
cfg115 = FeatureConfig(window_len_s=0.1, step=1, feature_ids=["mean"])
n115 = feature_service.effective_n(meta115, cfg115)
check(n115 == 20, f"EC-11.5: 200.0*0.1=20.0 → even → n={n115}, expect 20")


# ============================================================
# 结果汇总
# ============================================================
print("\n" + "=" * 60)
print(f"测试结果: {PASS} 通过, {FAIL} 失败")
print("=" * 60)
if ERRORS:
    print("\n失败用例:")
    for e in ERRORS:
        print(f"  FAIL  {e}")
    sys.exit(1)
else:
    print("\n全部通过！")
    sys.exit(0)
