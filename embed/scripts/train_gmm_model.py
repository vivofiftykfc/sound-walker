#!/usr/bin/env python3
"""
train_gmm_model.py - Train UBM + SVM models and export to C-compatible binary

This script:
  1. Loads LibriSpeech dev-clean (or custom data)
  2. Extracts MFCC features (13-dim, 25ms/10ms)
  3. Trains Universal Background Model (64-component GMM)
  4. Trains SVM on GMM supervectors (optional)
  5. Exports as .bin files readable by gmm_svm.c

Usage:
  python embed/scripts/train_gmm_model.py
  python embed/scripts/train_gmm_model.py --train-svm
  python embed/scripts/train_gmm_model.py --max-speakers 20

Output:
  embed/models/ubm.bin   — UBM model
  embed/models/svm.bin   — SVM model (if --train-svm)
"""

import os, sys, struct, argparse, warnings
from pathlib import Path
import numpy as np
from sklearn.mixture import GaussianMixture
from sklearn.svm import SVC
from sklearn.preprocessing import StandardScaler, LabelEncoder
import librosa

warnings.filterwarnings('ignore')

# ─── Constants (must match voiceprint_verify.h exactly) ────────
N_MFCC = 13
N_GMM_COMPONENTS = 16
UBM_COMPONENTS = 64
N_GMM_FEATURES = N_MFCC * UBM_COMPONENTS  # 832
SR = 16000
FRAME_LEN = 0.025
FRAME_SHIFT = 0.01
N_ENROLL = 3
MIN_UTTERANCES = 8
GMM_MAGIC = 0x474D4D00
SVM_MAGIC = 0x53564D00

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DATA_DIR = PROJECT_ROOT / "data" / "LibriSpeech" / "dev-clean"
OUTPUT_DIR = PROJECT_ROOT / "embed" / "models"


def extract_mfcc(audio: np.ndarray) -> np.ndarray:
    """Extract 13-dim MFCC, return (n_frames, 13)"""
    mfcc = librosa.feature.mfcc(
        y=audio, sr=SR, n_mfcc=N_MFCC,
        n_fft=int(FRAME_LEN * SR),
        hop_length=int(FRAME_SHIFT * SR),
        window='hamming'
    )
    # CMVN (same as gmm_svm.c)
    mfcc = (mfcc - mfcc.mean(axis=1, keepdims=True)) / (mfcc.std(axis=1, keepdims=True) + 1e-8)
    return mfcc.T


def load_librispeech(data_dir, max_speakers=10, min_utterances=MIN_UTTERANCES):
    """Load LibriSpeech, return {spk_id: [audio_array, ...]}"""
    import collections
    speakers = collections.defaultdict(list)

    for root, dirs, files in os.walk(data_dir):
        flac_files = [f for f in files if f.endswith('.flac')]
        if not flac_files:
            continue
        spk_id = os.path.basename(root)
        for f in flac_files:
            speakers[spk_id].append(os.path.join(root, f))

    qualified = {s: p for s, p in speakers.items() if len(p) >= min_utterances}
    selected = dict(sorted(qualified.items())[:max_speakers])

    result = {}
    print(f"Loading {len(selected)} speakers...")
    for spk_id, paths in selected.items():
        audios = []
        for p in paths[:min_utterances]:
            audio, _ = librosa.load(p, sr=SR)
            audios.append(audio)
        result[spk_id] = audios
        print(f"  {spk_id}: {len(audios)} utterances")
    return result


def export_gmm_bin(path, means, covariances, weights):
    """Export GMM to binary format readable by gmm_svm.c"""
    n_comp, n_feat = means.shape
    assert n_feat == N_MFCC
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', GMM_MAGIC, n_comp, n_feat))
        f.write(means.astype(np.float32).tobytes())
        f.write(covariances.astype(np.float32).tobytes())
        f.write(weights.astype(np.float32).tobytes())
    print(f"  Exported: {path} ({n_comp}x{n_feat}, {os.path.getsize(path)/1024:.1f} KB)")


def export_svm_bin(path, weights, bias):
    """Export SVM to binary format readable by gmm_svm.c"""
    n_feat = len(weights)
    assert n_feat == N_GMM_FEATURES
    with open(path, 'wb') as f:
        f.write(struct.pack('<II', SVM_MAGIC, n_feat))
        f.write(weights.astype(np.float32).tobytes())
        f.write(struct.pack('<f', bias))
    print(f"  Exported: {path} ({n_feat} dim, {os.path.getsize(path)/1024:.1f} KB)")


def verify_bin(path, n_comp, n_feat):
    """Read back and verify binary format"""
    with open(path, 'rb') as f:
        magic, nc, nf = struct.unpack('<III', f.read(12))
        assert magic == GMM_MAGIC, f"Bad magic: {magic:#010x}"
        assert nc == n_comp
        assert nf == n_feat
        means = np.frombuffer(f.read(nc * nf * 4), dtype=np.float32).reshape(nc, nf)
        f.read(nc * nf * 4)  # variances
        f.read(nc * 4)       # weights
    print(f"  Verified: {path} — means[{means.min():.4f}, {means.max():.4f}]")
    return True


def train_ubm(dataset, n_components=UBM_COMPONENTS):
    """Train UBM on enrollment MFCC from all speakers"""
    print(f"\n{'='*50}")
    print(f"Training UBM ({n_components} components, {N_MFCC} dims)")
    print(f"{'='*50}")

    all_mfcc = []
    for spk_id, audios in dataset.items():
        for a in audios[:N_ENROLL]:
            all_mfcc.append(extract_mfcc(a))
        print(f"  {spk_id}: enrolled {N_ENROLL} utterances")

    X = np.vstack(all_mfcc)
    print(f"Total frames: {len(X)}")

    ubm = GaussianMixture(n_components=n_components, covariance_type='diag',
                          max_iter=200, n_init=3, random_state=42, tol=1e-4,
                          reg_covar=1e-3)
    ubm.fit(X)

    ll = ubm.score(X)
    bic = ubm.bic(X)
    print(f"  Log-likelihood: {ll:.2f}")
    print(f"  BIC: {bic:.2f}")
    print(f"  Converged: {ubm.converged_}")
    return ubm


def train_svm(dataset, ubm):
    """Train linear SVM on GMM supervectors"""
    print(f"\n{'='*50}")
    print(f"Training SVM ({N_GMM_FEATURES} dims)")
    print(f"{'='*50}")

    X_sv, y_sv = [], []
    for spk_id, audios in dataset.items():
        for audio in audios[:N_ENROLL]:
            mfcc = extract_mfcc(audio)
            posteriors = ubm.predict_proba(mfcc)
            sv_parts = []
            for k in range(UBM_COMPONENTS):
                gamma = posteriors[:, k]
                n_k = gamma.sum() + 1e-8
                f_k = (gamma[:, None] * (mfcc - ubm.means_[k])).sum(axis=0) / np.sqrt(n_k)
                sv_parts.append(f_k)
            sv = np.concatenate(sv_parts)
            X_sv.append(sv)
            y_sv.append(spk_id)

    X_sv = np.array(X_sv)
    le = LabelEncoder()
    y_enc = le.fit_transform(y_sv)
    scaler = StandardScaler()
    X_sv_scaled = scaler.fit_transform(X_sv)
    print(f"  Supervectors: {X_sv_scaled.shape}")

    svm = SVC(kernel='linear', probability=True, random_state=42)
    svm.fit(X_sv_scaled, y_enc)
    print(f"  Support vectors: {len(svm.support_vectors_)}")

    # Extract linear weights
    if len(le.classes_) == 2:
        sv_weights = svm.dual_coef_.ravel() @ svm.support_vectors_
        bias = svm.intercept_.ravel()[0]
    else:
        sv_weights = svm.dual_coef_[0] @ svm.support_vectors_
        bias = svm.intercept_[0]

    # Undo scaling
    sv_weights = sv_weights / scaler.scale_
    bias = bias - (sv_weights * scaler.mean_).sum()
    return sv_weights.astype(np.float64), float(bias)


def main():
    parser = argparse.ArgumentParser(description='Train and export GMM/SVM models')
    parser.add_argument('--data-dir', type=str, default=str(DATA_DIR))
    parser.add_argument('--output', type=str, default=str(OUTPUT_DIR))
    parser.add_argument('--max-speakers', type=int, default=10)
    parser.add_argument('--train-svm', action='store_true')
    args = parser.parse_args()

    data_dir = Path(args.data_dir)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    if not data_dir.exists():
        print(f"Data directory not found: {data_dir}")
        sys.exit(1)

    # 1. Load
    dataset = load_librispeech(data_dir, args.max_speakers)
    print(f"Loaded {len(dataset)} speakers")

    # 2. Train UBM
    ubm = train_ubm(dataset)

    # 3. Export UBM
    print(f"\nExporting models to {output_dir}/")
    ubm_path = output_dir / "ubm.bin"
    export_gmm_bin(ubm_path, ubm.means_, ubm.covariances_, ubm.weights_)
    verify_bin(ubm_path, UBM_COMPONENTS, N_MFCC)

    # 4. SVM (optional)
    if args.train_svm:
        sv_weights, sv_bias = train_svm(dataset, ubm)
        svm_path = output_dir / "svm.bin"
        export_svm_bin(svm_path, sv_weights, sv_bias)

    # 5. Quick accuracy test
    print(f"\n{'='*50}")
    print("Quick accuracy test (cosine similarity baseline)")
    print(f"{'='*50}")
    enrollments = {}
    for spk_id, audios in dataset.items():
        all_mfcc = [extract_mfcc(a) for a in audios[:N_ENROLL]]
        enrollments[spk_id] = np.vstack(all_mfcc).mean(axis=0)

    correct = total = 0
    for spk_id, audios in dataset.items():
        for audio in audios[N_ENROLL:]:
            test_mfcc = extract_mfcc(audio)
            test_mean = test_mfcc.mean(axis=0)
            total += 1
            best = max(enrollments.items(),
                       key=lambda kv: np.dot(kv[1], test_mean) /
                       (np.linalg.norm(kv[1]) * np.linalg.norm(test_mean) + 1e-8))
            if best[0] == spk_id:
                correct += 1
    print(f"  Accuracy: {correct}/{total} = {correct/max(total,1)*100:.1f}%")

    print(f"\nDone! Models in {output_dir}/")
    print("Next: scp ubm.bin pi@192.168.43.46:~/voiceprint_lock/models/")


if __name__ == '__main__':
    main()
