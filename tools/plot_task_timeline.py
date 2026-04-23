import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.cm as cm
import numpy as np

# Per ogni task, tieni traccia dell'ordine di apparizione
from collections import defaultdict
task_block_order = defaultdict(list)

TASK_NAMES = {0: "sampler", 1: "filter", 2: "fft", 3: "comm"}
nplot = 100
cmap = cm.get_cmap("tab20", 20)

def load_rows(path):
    rows = []
    counter = 0
    with open(path, "r", newline="") as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) != 4:
                continue
            try:
                task_id  = int(parts[0])
                block    = int(parts[1])
                start_us = int(parts[2])
                end_us   = int(parts[3])
                if task_id not in TASK_NAMES:
                    continue
                rows.append({"task": task_id, "block": block,
                             "start_us": start_us, "end_us": end_us})
            except ValueError:
                continue
            counter += 1
            if counter >= nplot:
                break
    return rows

# Colore = indice posizione nel task, non numero blocco assoluto
def get_color(task, block):
    order = task_block_order[task]
    idx = order.index(block) if block in order else 0
    return cmap(idx % 20)

def main():
    readings = "tools/data.csv" 

    rows = load_rows(readings)
    if not rows:
        print("Nessuna riga valida.")
        return


    min_start = min(r["start_us"] for r in rows)
    block_ids = sorted(set(r["block"] for r in rows))

    # Colore unico per numero di blocco
    
    block_color = {b: cmap(i % 20) for i, b in enumerate(block_ids)}

    fig, ax = plt.subplots(figsize=(16, 5))

    for r in rows:
        tid   = r["task"]
        b     = r["block"]
        start = (r["start_us"] - min_start) / 1000.0
        dur   = (r["end_us"]   - r["start_us"]) / 1000.0
        if r["block"] not in task_block_order[r["task"]]:
            task_block_order[r["task"]].append(r["block"])
        color = get_color(r["task"], r["block"])

        ax.broken_barh([(start, dur)], (tid - 0.4, 0.8),
                       facecolors=color, edgecolors="white", linewidth=0.5)

        # Label con numero blocco centrata sulla barra
        if dur > 0.5:  # evita label su barre troppo sottili
            ax.text(start + dur / 2, tid, str(b),
                    ha="center", va="center", fontsize=7,
                    color="black", fontweight="bold")

    ax.set_yticks(list(TASK_NAMES.keys()))
    ax.set_yticklabels(list(TASK_NAMES.values()))
    ax.set_xlabel("Tempo [ms]")
    ax.set_title("Pipeline timeline — colore = numero blocco")
    ax.grid(True, axis="x", alpha=0.3)
    ax.set_ylim(-0.6, len(TASK_NAMES) - 0.4)

    # Legenda blocchi (solo i primi 20 per non ingolfare)
    shown = block_ids[:20]
    patches = [mpatches.Patch(color=block_color[b], label=f"blocco {b}") for b in shown]
    if len(block_ids) > 20:
        patches.append(mpatches.Patch(color="white", label=f"... +{len(block_ids)-20} altri"))
    ax.legend(handles=patches, loc="upper right", fontsize=7,
              ncol=2, framealpha=0.7)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()




