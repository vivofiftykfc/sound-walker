#!/usr/bin/env python3
"""
声纹锁三路线完整对比实验
=============================
路线1: MFCC + DTW (传统方法)
路线2: GMM + SVM (统计学习)
路线3: 深度学习 Embedding (Resemblyzer)

评估指标: 准确率, EER, FAR, FRR, 推理时间, 模型大小
"""

import os, sys, time, pickle, json, warnings, itertools
from pathlib import Path
from collections import defaultdict
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional

import numpy as np
import librosa
import soundfile as sf
from scipy.spatial.distance import cdist
from sklearn.mixture import GaussianMixture
from sklearn.svm import SVC
from sklearn.preprocessing import StandardScaler, LabelEncoder
from sklearn.metrics import roc_curve, accuracy_score, classification_report
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import warnings
warnings.filterwarnings('ignore')

# ─── 配置 ───────────────────────────────────────────
PROJECT_ROOT = Path(__file__).parent.parent
DATA_DIR = PROJECT_ROOT / "data" / "LibriSpeech" / "dev-clean"
RESULTS_DIR = PROJECT_ROOT / "results"
RESULTS_DIR.mkdir(exist_ok=True)

SR = 16000          # 采样率
N_MFCC = 13         # MFCC 系数数量
N_MELS = 40         # Mel 滤波器数量
FRAME_LEN = 0.025   # 帧长 25ms
FRAME_SHIFT = 0.01  # 帧移 10ms
N_ENROLL = 3        # 注册语音条数
N_TEST_MIN = 3      # 测试语音最少条数
MAX_SPEAKERS = 10   # 最多使用的说话人数 (快速测试)
MIN_UTTERANCES = 8  # 每个说话人最少语音条数

@dataclass
class EvalResult:
    """评估结果"""
    name: str
    accuracy: float = 0
    eer: float = 0
    far_at_frr01: float = 0   # FRR=1% 时的 FAR
    avg_enroll_time: float = 0
    avg_verify_time: float = 0
    model_size_kb: float = 0
    scores: List[float] = field(default_factory=list)
    labels: List[int] = field(default_factory=list)
    thresholds: List[float] = field(default_factory=list)


# ═══════════════════════════════════════════════════════
#  数据集加载
# ═══════════════════════════════════════════════════════

def load_librispeech(data_dir: Path, max_speakers: int = MAX_SPEAKERS,
                     min_utterances: int = MIN_UTTERANCES) -> Dict[str, List[np.ndarray]]:
    """
    加载 LibriSpeech 数据集
    返回: {speaker_id: [audio1, audio2, ...], ...}
    """
    speakers = defaultdict(list)
    for root, dirs, files in os.walk(data_dir):
        flac_files = [f for f in files if f.endswith('.flac')]
        if not flac_files:
            continue
        spk_id = os.path.basename(root)
        for f in flac_files:
            speakers[spk_id].append(os.path.join(root, f))

    # 筛选: 足够语音条数的说话人
    qualified = {s: paths for s, paths in speakers.items()
                 if len(paths) >= min_utterances}
    # 取前 N 个
    selected = dict(sorted(qualified.items())[:max_speakers])

    # 加载音频
    result = {}
    print(f"\n加载数据集: {len(selected)} 个说话人...")
    for spk_id, paths in selected.items():
        audios = []
        for p in paths[:min_utterances + N_ENROLL]:
            audio, _ = librosa.load(p, sr=SR)
            audios.append(audio)
        result[spk_id] = audios
        print(f"  {spk_id}: {len(audios)} 条语音")

    return result


# ═══════════════════════════════════════════════════════
#  特征提取 (共享)
# ═══════════════════════════════════════════════════════

def extract_mfcc(audio: np.ndarray) -> np.ndarray:
    """提取 MFCC 特征, 返回 (n_frames, n_mfcc)"""
    mfcc = librosa.feature.mfcc(
        y=audio, sr=SR, n_mfcc=N_MFCC, n_mels=N_MELS,
        n_fft=int(FRAME_LEN * SR), hop_length=int(FRAME_SHIFT * SR),
        window='hamming'
    )
    # CMVN (倒谱均值方差归一化)
    mfcc = (mfcc - mfcc.mean(axis=1, keepdims=True)) / (mfcc.std(axis=1, keepdims=True) + 1e-8)
    return mfcc.T  # (n_frames, n_mfcc)


def extract_mfccs_for_speaker(audios: List[np.ndarray]) -> List[np.ndarray]:
    return [extract_mfcc(a) for a in audios]


# ═══════════════════════════════════════════════════════
#  路线1: MFCC + DTW
# ═══════════════════════════════════════════════════════

def dtw_distance(x: np.ndarray, y: np.ndarray, window: int = None) -> float:
    """
    动态时间规整距离
    x: (N, D), y: (M, D)
    使用 Sakoe-Chiba 窗加速
    """
    N, M = len(x), len(y)
    if window is None:
        window = max(N, M)

    # 初始化代价矩阵
    D = np.full((N + 1, M + 1), np.inf)
    D[0, 0] = 0

    # 预计算距离
    dist_mat = cdist(x, y, metric='euclidean')

    for i in range(1, N + 1):
        j_start = max(1, i - window)
        j_end = min(M, i + window) + 1
        for j in range(j_start, j_end):
            cost = dist_mat[i-1, j-1]
            D[i, j] = cost + min(D[i-1, j], D[i, j-1], D[i-1, j-1])

    return D[N, M] / (N + M)  # 归一化


class MFCC_DTW:
    """路线1: MFCC + DTW 声纹验证"""
    def __init__(self):
        self.templates: Dict[str, List[np.ndarray]] = {}  # {spk_id: [mfcc1, mfcc2, ...]}
        self.threshold: float = 0

    def enroll(self, spk_id: str, enroll_audios: List[np.ndarray]):
        self.templates[spk_id] = extract_mfccs_for_speaker(enroll_audios)

    def verify(self, test_audio: np.ndarray) -> Dict[str, float]:
        """返回与每个注册说话人的 DTW 距离"""
        test_mfcc = extract_mfcc(test_audio)
        results = {}
        for spk_id, templates in self.templates.items():
            # 取所有注册模板的最小 DTW 距离
            distances = [dtw_distance(t, test_mfcc) for t in templates]
            results[spk_id] = np.min(distances)
        return results

    def predict(self, test_audio: np.ndarray, threshold: float) -> Tuple[str, float]:
        """预测: 返回最匹配的说话人和距离"""
        results = self.verify(test_audio)
        best_spk = min(results, key=results.get)
        best_dist = results[best_spk]
        if best_dist < threshold:
            return best_spk, best_dist
        return "unknown", best_dist


def evaluate_mfcc_dtw(dataset: Dict[str, List[np.ndarray]], n_enroll: int = N_ENROLL) -> EvalResult:
    """评估路线1"""
    result = EvalResult(name="MFCC + DTW")
    model = MFCC_DTW()

    # 划分注册/测试
    enroll_data, test_data = {}, {}
    for spk_id, audios in dataset.items():
        enroll_data[spk_id] = audios[:n_enroll]
        test_data[spk_id] = audios[n_enroll:]

    # 注册
    print("\n[MFCC+DTW] 注册中...")
    t0 = time.time()
    for spk_id, audios in enroll_data.items():
        model.enroll(spk_id, audios)
    enroll_time = (time.time() - t0) / len(enroll_data)
    result.avg_enroll_time = enroll_time

    # 生成正负样本对
    scores, labels = [], []
    print("[MFCC+DTW] 验证中...")
    t0 = time.time()
    n_tests = 0
    for spk_id, test_audios in test_data.items():
        for audio in test_audios:
            n_tests += 1
            results = model.verify(audio)
            for enrolled_spk, dist in results.items():
                scores.append(-dist)  # 负距离→分数 (越大越好)
                labels.append(1 if enrolled_spk == spk_id else 0)
    verify_time = (time.time() - t0) / max(n_tests, 1)
    result.avg_verify_time = verify_time

    result.scores = np.array(scores)
    result.labels = np.array(labels)
    result.model_size_kb = sum(
        sum(t.nbytes for t in templates) for templates in model.templates.values()
    ) / 1024

    # 计算指标
    compute_metrics(result)
    print(f"[MFCC+DTW] Acc={result.accuracy:.3f}  EER={result.eer:.3f}  "
          f"Enroll={result.avg_enroll_time*1000:.1f}ms  Verify={result.avg_verify_time*1000:.1f}ms")
    return result


# ═══════════════════════════════════════════════════════
#  路线2: GMM + SVM
# ═══════════════════════════════════════════════════════

class GMM_SVM:
    """路线2: GMM-UBM + SVM 声纹验证"""
    def __init__(self, n_components: int = 16, ubm_components: int = 64):
        self.n_components = n_components
        self.ubm_components = ubm_components
        self.ubm: Optional[GaussianMixture] = None
        self.gmms: Dict[str, GaussianMixture] = {}
        self.svm: Optional[SVC] = None
        self.scaler = StandardScaler()
        self.le = LabelEncoder()
        self.threshold: float = 0
        self.n_enroll = N_ENROLL

    def _supervector(self, mfccs: List[np.ndarray]) -> np.ndarray:
        """用 UBM 对 MFCC 序列做后验统计 -> supervector"""
        if self.ubm is None:
            return np.mean(np.vstack(mfccs), axis=0)
        all_mfcc = np.vstack(mfccs)
        posteriors = self.ubm.predict_proba(all_mfcc)  # (T, K)
        sv = []
        for k in range(self.ubm_components):
            gamma = posteriors[:, k]
            n_k = gamma.sum() + 1e-8
            # Baum-Welch 统计量
            f_k = (gamma[:, None] * (all_mfcc - self.ubm.means_[k])).sum(axis=0) / np.sqrt(n_k)
            sv.append(f_k)
        return np.concatenate(sv)

    def fit(self, dataset: Dict[str, List[np.ndarray]], n_enroll: int = N_ENROLL):
        self.n_enroll = n_enroll
        # 划分
        enroll_audios, test_audios = {}, {}
        for spk_id, audios in dataset.items():
            enroll_audios[spk_id] = audios[:n_enroll]
            test_audios[spk_id] = audios[n_enroll:]

        # 提取所有 MFCC
        print("[GMM+SVM] 提取 MFCC...")
        all_mfccs = []
        for audios in enroll_audios.values():
            for a in audios:
                all_mfccs.append(extract_mfcc(a))

        # 训练 UBM
        print("[GMM+SVM] 训练 UBM...")
        X_ubm = np.vstack(all_mfccs)
        self.ubm = GaussianMixture(n_components=self.ubm_components, covariance_type='diag',
                                    max_iter=100, random_state=42)
        self.ubm.fit(X_ubm)

        # 训练每个说话人的 GMM (MAP 自适应)
        print("[GMM+SVM] 训练说话人 GMM...")
        for spk_id, audios in enroll_audios.items():
            X = np.vstack([extract_mfcc(a) for a in audios])
            gmm = GaussianMixture(n_components=self.n_components, covariance_type='diag',
                                   max_iter=100, random_state=42)
            gmm.fit(X)
            self.gmms[spk_id] = gmm

        # 构建 supervector -> SVM
        print("[GMM+SVM] 训练 SVM...")
        X_sv, y_sv = [], []
        for spk_id, audios in enroll_audios.items():
            for i, audio in enumerate(audios):
                sv = self._supervector([extract_mfcc(audio)])
                X_sv.append(sv)
                y_sv.append(spk_id)

        X_sv = np.array(X_sv)
        self.scaler.fit(X_sv)
        X_sv = self.scaler.transform(X_sv)
        self.le.fit(y_sv)
        y_enc = self.le.transform(y_sv)

        self.svm = SVC(kernel='linear', probability=True, random_state=42)
        self.svm.fit(X_sv, y_enc)

        return enroll_audios, test_audios

    def predict(self, test_audio: np.ndarray) -> Tuple[str, float]:
        sv = self._supervector([extract_mfcc(test_audio)])
        sv = self.scaler.transform(sv.reshape(1, -1))
        probs = self.svm.predict_proba(sv)[0]
        best_idx = np.argmax(probs)
        return self.le.inverse_transform([best_idx])[0], probs[best_idx]

    def verify_gmm(self, test_audio: np.ndarray) -> Dict[str, float]:
        """GMM 对数似然比打分"""
        mfcc = extract_mfcc(test_audio)
        results = {}
        for spk_id, gmm in self.gmms.items():
            ll_target = gmm.score(mfcc)
            ll_ubm = self.ubm.score(mfcc) if self.ubm else 0
            results[spk_id] = ll_target - ll_ubm
        return results


def evaluate_gmm_svm(dataset: Dict[str, List[np.ndarray]]) -> EvalResult:
    result = EvalResult(name="GMM + SVM")
    model = GMM_SVM()

    print("\n[GMM+SVM] 训练中...")
    t0 = time.time()
    enroll_audios, test_audios = model.fit(dataset)
    enroll_time = (time.time() - t0) / len(enroll_audios)
    result.avg_enroll_time = enroll_time

    # 评估
    scores, labels = [], []
    print("[GMM+SVM] 验证中...")
    t0 = time.time()
    n_tests = 0
    # 用 GMM likelihood ratio
    for spk_id, audios in test_audios.items():
        for audio in audios:
            n_tests += 1
            gmm_results = model.verify_gmm(audio)
            for enrolled_spk, score in gmm_results.items():
                scores.append(score)
                labels.append(1 if enrolled_spk == spk_id else 0)

    verify_time = (time.time() - t0) / max(n_tests, 1)
    result.avg_verify_time = verify_time
    result.scores = np.array(scores)
    result.labels = np.array(labels)

    # 模型大小
    result.model_size_kb = (sum(g.means_.nbytes + g.covariances_.nbytes
                                 for g in model.gmms.values()) +
                            (model.ubm.means_.nbytes + model.ubm.covariances_.nbytes
                             if model.ubm else 0)) / 1024

    compute_metrics(result)
    print(f"[GMM+SVM] Acc={result.accuracy:.3f}  EER={result.eer:.3f}  "
          f"Enroll={result.avg_enroll_time*1000:.1f}ms  Verify={result.avg_verify_time*1000:.1f}ms")
    return result


# ═══════════════════════════════════════════════════════
#  路线3: 深度学习 Embedding (Resemblyzer)
# ═══════════════════════════════════════════════════════

class DeepEmbedding:
    """路线3: 预训练声纹嵌入 + 余弦相似度"""
    def __init__(self):
        from resemblyzer import VoiceEncoder
        self.encoder = VoiceEncoder()
        self.embeddings: Dict[str, np.ndarray] = {}  # {spk_id: mean_embedding}

    def enroll(self, spk_id: str, audios: List[np.ndarray]):
        embeds = []
        for audio in audios:
            # Resemblyzer 需要 float32 16kHz
            embed = self.encoder.embed_utterance(audio.astype(np.float32))
            embeds.append(embed)
        self.embeddings[spk_id] = np.mean(embeds, axis=0)

    def verify(self, test_audio: np.ndarray) -> Dict[str, float]:
        test_embed = self.encoder.embed_utterance(test_audio.astype(np.float32))
        results = {}
        for spk_id, enroll_embed in self.embeddings.items():
            sim = np.dot(test_embed, enroll_embed) / (
                np.linalg.norm(test_embed) * np.linalg.norm(enroll_embed) + 1e-8)
            results[spk_id] = sim
        return results

    def predict(self, test_audio: np.ndarray, threshold: float) -> Tuple[str, float]:
        results = self.verify(test_audio)
        best_spk = max(results, key=results.get)
        best_sim = results[best_spk]
        if best_sim > threshold:
            return best_spk, best_sim
        return "unknown", best_sim


def evaluate_deep_embedding(dataset: Dict[str, List[np.ndarray]]) -> EvalResult:
    result = EvalResult(name="Deep Embedding (Resemblyzer)")

    try:
        from resemblyzer import VoiceEncoder
    except ImportError:
        print("[DL] Resemblyzer 未安装, 跳过")
        result.accuracy = -1
        return result

    model = DeepEmbedding()

    # 划分
    enroll_data, test_data = {}, {}
    for spk_id, audios in dataset.items():
        enroll_data[spk_id] = audios[:N_ENROLL]
        test_data[spk_id] = audios[N_ENROLL:]

    # 注册
    print("\n[DL Embedding] 注册中...")
    t0 = time.time()
    for spk_id, audios in enroll_data.items():
        model.enroll(spk_id, audios)
    enroll_time = (time.time() - t0) / len(enroll_data)
    result.avg_enroll_time = enroll_time

    # 验证
    scores, labels = [], []
    print("[DL Embedding] 验证中...")
    t0 = time.time()
    n_tests = 0
    for spk_id, test_audios in test_data.items():
        for audio in test_audios:
            n_tests += 1
            sims = model.verify(audio)
            for enrolled_spk, sim in sims.items():
                scores.append(sim)
                labels.append(1 if enrolled_spk == spk_id else 0)

    verify_time = (time.time() - t0) / max(n_tests, 1)
    result.avg_verify_time = verify_time
    result.scores = np.array(scores)
    result.labels = np.array(labels)
    result.model_size_kb = sum(
        e.nbytes for e in model.embeddings.values()) / 1024

    compute_metrics(result)
    print(f"[DL Embedding] Acc={result.accuracy:.3f}  EER={result.eer:.3f}  "
          f"Enroll={result.avg_enroll_time*1000:.1f}ms  Verify={result.avg_verify_time*1000:.1f}ms")
    return result


# ═══════════════════════════════════════════════════════
#  评估指标
# ═══════════════════════════════════════════════════════

def compute_metrics(result: EvalResult):
    """从 scores 和 labels 计算所有指标"""
    scores = np.array(result.scores)
    labels = np.array(result.labels)

    if len(scores) == 0 or len(np.unique(labels)) < 2:
        result.accuracy = -1
        result.eer = -1
        return

    # EER
    fpr, tpr, thresholds = roc_curve(labels, scores)
    fnr = 1 - tpr
    eer_idx = np.argmin(np.abs(fpr - fnr))
    result.eer = float((fpr[eer_idx] + fnr[eer_idx]) / 2)
    result.thresholds = thresholds

    # 最佳阈值处的准确率
    best_thresh = thresholds[np.argmax(tpr - fpr)]
    preds = (scores >= best_thresh).astype(int)
    result.accuracy = float(accuracy_score(labels, preds))

    # FAR @ FRR=1%
    frr_target = 0.01
    far_at_frr = 0
    for i in range(len(fpr)):
        if fnr[i] <= frr_target:
            far_at_frr = fpr[i]
            break
    result.far_at_frr01 = float(far_at_frr)


# ═══════════════════════════════════════════════════════
#  可视化
# ═══════════════════════════════════════════════════════

def plot_comparison(results: List[EvalResult], output_dir: Path):
    """生成对比图表"""
    # 1. ROC 曲线对比
    fig, axes = plt.subplots(1, 3, figsize=(18, 5))

    colors = ['#2196F3', '#FF9800', '#4CAF50']
    markers = ['o', 's', '^']

    for i, (res, c, m) in enumerate(zip(results, colors, markers)):
        ax = axes[i]
        if res.accuracy < 0:
            ax.text(0.5, 0.5, f'{res.name}\n(未运行)', transform=ax.transAxes,
                    ha='center', va='center', fontsize=14)
            continue

        scores = np.array(res.scores)
        labels = np.array(res.labels)
        fpr, tpr, _ = roc_curve(labels, scores)

        ax.plot(fpr, tpr, color=c, lw=2, label=f'EER={res.eer:.3f}')
        ax.plot([0, 1], [0, 1], 'k--', alpha=0.3)
        ax.fill_between(fpr, tpr, alpha=0.1, color=c)

        # EER 点
        fnr = 1 - tpr
        eer_idx = np.argmin(np.abs(fpr - fnr))
        ax.plot(fpr[eer_idx], tpr[eer_idx], 'ro', markersize=8)

        ax.set_xlabel('False Positive Rate')
        ax.set_ylabel('True Positive Rate')
        ax.set_title(res.name, fontweight='bold')
        ax.legend(loc='lower right')
        ax.set_xlim([0, 1])
        ax.set_ylim([0, 1])
        ax.grid(True, alpha=0.3)

    fig.suptitle('ROC Curve Comparison — Voiceprint Lock', fontsize=16, fontweight='bold')
    plt.tight_layout()
    fig.savefig(output_dir / 'roc_comparison.png', dpi=150)

    # 2. 指标对比柱状图
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    valid = [r for r in results if r.accuracy >= 0]
    names = [r.name for r in valid]

    # Accuracy + EER
    ax = axes[0, 0]
    x = np.arange(len(names))
    accs = [r.accuracy for r in valid]
    eers = [r.eer for r in valid]
    w = 0.35
    bars1 = ax.bar(x - w/2, [a*100 for a in accs], w, label='Accuracy (%)', color='#2196F3')
    bars2 = ax.bar(x + w/2, [e*100 for e in eers], w, label='EER (%)', color='#FF5722')
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=15, ha='right', fontsize=9)
    ax.set_ylabel('%')
    ax.set_title('Accuracy vs EER')
    ax.legend()
    ax.grid(axis='y', alpha=0.3)

    # 推理速度
    ax = axes[0, 1]
    enroll_t = [r.avg_enroll_time * 1000 for r in valid]
    verify_t = [r.avg_verify_time * 1000 for r in valid]
    bars3 = ax.bar(x - w/2, enroll_t, w, label='Enroll (ms)', color='#4CAF50')
    bars4 = ax.bar(x + w/2, verify_t, w, label='Verify (ms)', color='#FFC107')
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=15, ha='right', fontsize=9)
    ax.set_ylabel('Time (ms)')
    ax.set_title('Enroll & Verify Time')
    ax.legend()
    ax.grid(axis='y', alpha=0.3)

    # 模型大小
    ax = axes[1, 0]
    sizes = [r.model_size_kb for r in valid]
    bars5 = ax.bar(x, sizes, color=['#2196F3', '#FF9800', '#4CAF50'])
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=15, ha='right', fontsize=9)
    ax.set_ylabel('KB')
    ax.set_title('Model / Template Size')
    for bar, sz in zip(bars5, sizes):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                f'{sz:.1f} KB', ha='center', va='bottom', fontsize=9)
    ax.grid(axis='y', alpha=0.3)

    # 综合对比表格
    ax = axes[1, 1]
    ax.axis('off')
    table_data = [['Metric', 'MFCC+DTW', 'GMM+SVM', 'DL Embedding']]
    metrics_labels = ['Accuracy (%)', 'EER (%)', 'Enroll (ms)', 'Verify (ms)', 'Size (KB)']
    for ml, func in zip(metrics_labels, [
        lambda r: f"{r.accuracy*100:.1f}",
        lambda r: f"{r.eer*100:.1f}",
        lambda r: f"{r.avg_enroll_time*1000:.1f}",
        lambda r: f"{r.avg_verify_time*1000:.1f}",
        lambda r: f"{r.model_size_kb:.1f}",
    ]):
        row = [ml]
        for r in results:
            if r.accuracy < 0:
                row.append('N/A')
            else:
                row.append(func(r))
        table_data.append(row)

    table = ax.table(cellText=table_data, cellLoc='center', loc='center',
                     colWidths=[0.3, 0.2, 0.2, 0.25])
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1.2, 1.6)
    for j in range(4):
        table[0, j].set_facecolor('#2196F3')
        table[0, j].set_text_props(color='white', fontweight='bold')

    ax.set_title('Summary Comparison', fontweight='bold', fontsize=13, pad=20)

    plt.tight_layout()
    fig.savefig(output_dir / 'metrics_comparison.png', dpi=150)
    print(f"\nCharts saved to {output_dir}/")


# ═══════════════════════════════════════════════════════
#  主流程
# ═══════════════════════════════════════════════════════

def main():
    print("=" * 60)
    print("   Voiceprint Lock: 3-Method Comparison")
    print("=" * 60)

    # 1. 加载数据集
    if not DATA_DIR.exists():
        print(f"\nError: Data directory not found: {DATA_DIR}")
        print("Run: cd data && wget https://openslr.org/resources/12/dev-clean.tar.gz")
        print("Then: tar xzf dev-clean.tar.gz")
        sys.exit(1)

    dataset = load_librispeech(DATA_DIR, MAX_SPEAKERS, MIN_UTTERANCES)
    n_spk = len(dataset)
    n_utt = [len(v) for v in dataset.values()]
    print(f"\nTotal: {n_spk} speakers, {min(n_utt)}-{max(n_utt)} utterances each")

    results: List[EvalResult] = []

    # 2. 路线1: MFCC + DTW
    print("\n" + "-" * 40)
    print("  Method 1: MFCC + DTW")
    print("-" * 40)
    r1 = evaluate_mfcc_dtw(dataset)
    results.append(r1)

    # 3. 路线2: GMM + SVM
    print("\n" + "-" * 40)
    print("  Method 2: GMM + SVM")
    print("-" * 40)
    r2 = evaluate_gmm_svm(dataset)
    results.append(r2)

    # 4. 路线3: 深度学习 Embedding
    print("\n" + "-" * 40)
    print("  Method 3: Deep Embedding (Resemblyzer)")
    print("-" * 40)
    r3 = evaluate_deep_embedding(dataset)
    results.append(r3)

    # 5. 生成报告
    print("\n" + "=" * 60)
    print("  Results Summary")
    print("=" * 60)
    print(f"\n{'Method':<30} {'Accuracy':>8} {'EER':>8} {'Enroll':>10} {'Verify':>10} {'Size':>10}")
    print("-" * 80)
    for r in results:
        print(f"{r.name:<30} {r.accuracy*100:>7.1f}% {r.eer*100:>7.1f}% "
              f"{r.avg_enroll_time*1000:>8.1f}ms {r.avg_verify_time*1000:>8.1f}ms "
              f"{r.model_size_kb:>8.1f}KB")

    # 保存详细结果
    output = {
        "config": {
            "sr": SR, "n_mfcc": N_MFCC, "n_mels": N_MELS,
            "frame_len": FRAME_LEN, "frame_shift": FRAME_SHIFT,
            "n_enroll": N_ENROLL, "n_speakers": len(dataset)
        },
        "results": {}
    }
    for r in results:
        output["results"][r.name] = {
            "accuracy": round(r.accuracy, 4),
            "eer": round(r.eer, 4),
            "far_at_frr0.01": round(r.far_at_frr01, 4),
            "avg_enroll_time_ms": round(r.avg_enroll_time * 1000, 2),
            "avg_verify_time_ms": round(r.avg_verify_time * 1000, 2),
            "model_size_kb": round(r.model_size_kb, 2),
        }

    with open(RESULTS_DIR / "comparison_results.json", 'w') as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

    # 生成图表
    print("\nGenerating comparison charts...")
    plot_comparison(results, RESULTS_DIR)

    print(f"\nResults saved to: {RESULTS_DIR}/")
    print("  - comparison_results.json")
    print("  - roc_comparison.png")
    print("  - metrics_comparison.png")
    print("\n" + "=" * 60)
    print("  Experiment Complete!")
    print("=" * 60)


if __name__ == '__main__':
    main()
