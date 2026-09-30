# Jagwire# Jagwire

## Looking to Use This Model?

If you want to run or experiment with this model, check out the following branches — each one corresponds to a different ablation of the Sub-Center ArcFace head:

| Branch | Description |
|---|---|
| `k1tests` | K=1 sub-centers (standard ArcFace) |
| `k3tests` | K=3 sub-centers |
| `k5tests` | K=5 sub-centers |

```bash
git checkout K1tests
# or
git checkout K3tests
# or
git checkout K5tests
```

---
 
A jaguar re-identification model that learns to recognise individual jaguars across photographs using metric learning. Built with a **MegaDescriptor-L (Swin-L) backbone**, **CBAM spatial attention**, and a **Sub-Center ArcFace** head. Trained and evaluated on the [Kaggle Jaguar Re-ID competition](https://www.kaggle.com/competitions/jaguar-re-id) dataset.
 
> **Note:** This branch contains only the README. To use the model, you must switch to one of the branches listed above.
 
---
 
## Table of Contents
 
- [Project Overview](#project-overview)
- [Setup Instructions](#setup-instructions)
- [Required Dependencies](#required-dependencies)
- [Dataset](#dataset)
- [Data Preprocessing](#data-preprocessing)
- [How to Train the Model](#how-to-train-the-model)
- [How to Evaluate the Model](#how-to-evaluate-the-model)
- [Expected Outputs](#expected-outputs)
- [Reproducing Results](#reproducing-results)
  
---
 
## Project Overview
 
Jagwire identifies individual jaguars by learning a compact 512-dimensional embedding space where images of the same jaguar cluster close together. Given a query image and a gallery image, the model outputs a similarity score between 0 and 1.
 
**Architecture:**
 
| Component | Detail |
|---|---|
| Backbone | `MegaDescriptor-L-384` (Swin-L, 384 × 384 input) via HuggingFace/timm |
| Attention | CBAM (Channel + Spatial) — focuses on spot and flank patterns |
| Embedding neck | Linear(1536 → 512) + BatchNorm1d |
| Loss head | Sub-Center ArcFace (K=3 sub-centers, s=64, m=0.5) |
| Similarity metric | Cosine similarity rescaled to [0, 1] |
 
**Why Sub-Center ArcFace?**
Jaguars photographed from their left flank and right flank have different spot patterns. Sub-Center ArcFace assigns K cluster centres per individual so both views are captured correctly.
 
**Why CBAM?**
Without attention, the backbone may focus on background vegetation rather than the jaguar's coat. CBAM forces the model to attend to the animal itself.
 
---
 
## Setup Instructions
 
### 1. Clone the repository
 
```bash
git clone https://github.com/just-maki/Jagwire.git
cd Jagwire
git checkout K1tests  # or K3tests / K5tests
```
 
### 2. Create and activate a virtual environment
 
```bash
python -m venv venv
 
# macOS / Linux
source venv/bin/activate
 
# Windows
venv\Scripts\activate
```
 
### 3. Install dependencies
 
```bash
pip install -r requirements.txt
```
 
The MegaDescriptor-L backbone weights (~400 MB) will be downloaded automatically from HuggingFace the first time the notebook is run.
 
---
 
## Required Dependencies
 
| Package | Purpose |
|---|---|
| `torch` / `torchvision` | Model training and transforms |
| `timm` | MegaDescriptor-L backbone |
| `pandas` / `numpy` | Data loading and manipulation |
| `Pillow` | Image loading |
| `scikit-learn` | Train/val splitting and AUC metric |
| `tqdm` | Progress bars |
| `matplotlib` / `seaborn` | EDA visualisations |
 
Install all at once with:
 
```bash
pip install -r requirements.txt
```
 
**Hardware:** An NVIDIA GPU with CUDA is strongly recommended. The notebook will fall back to CPU but training 20 epochs will take significantly longer.
 
---
 
## Dataset
 
The dataset comes from the Kaggle Jaguar Re-Identification competition and requires a free Kaggle account to download.
 
**Competition page:** [https://www.kaggle.com/competitions/jaguar-re-id](https://www.kaggle.com/competitions/jaguar-re-id)
 
 
1. Go to the competition page and accept the rules
2. Download the dataset zip from the **Data** tab
3. Unzip it into the `data/` folder in the repo root
 
 
## Data Preprocessing
 
Preprocessing is handled automatically inside the notebook via torchvision transforms. No manual preprocessing step is required.
 
**Training transforms** (applied on the fly during training):
 
| Transform | Detail |
|---|---|
| `RandomResizedCrop(384)` | Scale range 0.6–1.0 |
| `RandomHorizontalFlip` | p=0.5 |
| `ColorJitter` | brightness=0.3, contrast=0.3, saturation=0.2, hue=0.1 |
| `RandomGrayscale` | p=0.1 |
| `Normalize` | mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225] |
 
**Validation / inference transforms:**
 
| Transform | Detail |
|---|---|
| `Resize(384)` | Shorter edge to 384 |
| `CenterCrop(384)` | Centre crop to 384 × 384 |
| `Normalize` | Same as training |
 
**Class imbalance:** The dataset contains some jaguars with over 100 images and others with only a few. A `WeightedRandomSampler` is used during training to ensure each individual is seen equally often per epoch.
 
**Label encoding:** Jaguar names are sorted alphabetically and mapped to integer indices (e.g. `abril → 0`, `akaloi → 1`, etc.) inside the `jaguardataset` class.
 
---
 
## How to Train the Model
 
Open `jaguarid.ipynb` in Jupyter or VS Code and run all cells from top to bottom.
 
> **⚠️ Training is computationally intensive.** A full 20-epoch run takes several hours even on a modern NVIDIA GPU. On CPU expect significantly longer. It is strongly recommended to run this on a machine with a CUDA-capable GPU.
 
> **⚠️ Training time:** This model takes a significant amount of time to train. An NVIDIA GPU is strongly recommended — running on CPU will be considerably slower.
 
### Key hyperparameters (Cell 5)
 
| Parameter | Default | Description |
|---|---|---|
| `batchsize` | `8` | Images per batch |
| `workers` | `0` | Must stay 0 in Jupyter |
| `arcs` | `64` | ArcFace scale factor (s) |
| `arcm` | `0.5` | ArcFace final margin (m) |
 
### Training configuration (Cell 19)
 
| Setting | Value |
|---|---|
| Epochs | 20 |
| Optimizer | AdamW (lr=3e-4, weight_decay=1e-4) |
| Scheduler | CosineAnnealingLR |
| Loss | CrossEntropyLoss (label_smoothing=0.1) |
| Train / Val split | 80 / 20 stratified (random_state=99) |
| Input resolution | 384 × 384 |
| Dynamic margin | Ramps from 0.1 → 0.5 over 20 epochs |
 
### Per-epoch console output
 
```
Epoch 01 | margin=0.100 | loss=4.2301 | val_auc=0.7821
Epoch 02 | margin=0.121 | loss=3.8754 | val_auc=0.8103
...
```
 
---
 
## How to Evaluate the Model
 
Evaluation runs automatically at the end of each training epoch. It measures how well the model's embeddings separate same-jaguar pairs from different-jaguar pairs.
 
### Metric: ROC-AUC on cosine similarity
 
At the end of every epoch the notebook:
 
1. Embeds all validation images
2. Randomly samples up to 100,000 pairs from the validation set
3. Computes cosine similarity for each pair
4. Labels same-jaguar pairs as positive (1) and different-jaguar pairs as negative (0)
5. Computes ROC-AUC over those similarity scores
 
A score of **1.0** means perfect separation. A score of **0.5** is random chance.
 
### Running evaluation on a saved model without retraining
 
Add the following cell before the inference section (Cell 20) and run from there:
 
```python
model.load_state_dict(torch.load('jaguar_model.pth', map_location=device))
model.eval()
```
 
---
 
## Expected Outputs
 
| Output | Location | Description |
|---|---|---|
| `jaguar_model.pth` | repo root | Saved model weights after training |
| Per-epoch logs | notebook console | margin, loss, val_auc per epoch |
 

## Reproducing Results
 
To reproduce results exactly:
 
1. use the dataset from kaggle
2. Do **not** change `random_state=99` in Cell 13 — this controls the train/val split
3. Use the default hyperparameters in Cell 5 (`arcs=64`, `arcm=0.5`, `batchsize=8`)
4. Run all cells top to bottom without skipping any
 
> **Note:** Results may vary slightly across different GPU hardware and CUDA versions due to floating-point non-determinism. To reduce this, add the following before training:
> ```python
> torch.backends.cudnn.deterministic = True
> torch.backends.cudnn.benchmark = False
> ```
 