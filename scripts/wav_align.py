# Compare la position des impulsions entre plusieurs exports WAV (test J2 :
# latence déclarée honorée par Live, bypass aligné).
# Usage : python scripts/wav_align.py ref.wav autre1.wav [autre2.wav ...]
import sys
import numpy as np
import wave

def load(path):
    with wave.open(path, "rb") as w:
        n, sw, ch, sr = w.getnframes(), w.getsampwidth(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = (a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8) | (a[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float64) / (1 << 23)
    elif sw == 2:
        x = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, dtype=np.int32).astype(np.float64) / (1 << 31)
    else:
        raise SystemExit(f"largeur {sw} non gérée")
    return x.reshape(-1, ch)[:, 0], sr

def peaks(x, thresh=0.25):
    idx = np.where(np.abs(x) > thresh)[0]
    out = []
    for i in idx:
        if not out or i - out[-1] > 100:
            out.append(int(i))
    return out

ref, sr = load(sys.argv[1])
pref = peaks(ref)
print(f"{sys.argv[1]} : sr={sr}, impulsions à {pref}")
ok = True
for p in sys.argv[2:]:
    x, sr2 = load(p)
    px = peaks(x)
    delta = [b - a for a, b in zip(pref, px)]
    same = (sr2 == sr and len(px) == len(pref) and all(d == 0 for d in delta))
    ok &= same
    print(f"{p} : sr={sr2}, impulsions à {px}, écart vs réf {delta} -> {'ALIGNÉ' if same else 'DÉCALÉ'}")
print("RÉSULTAT :", "TOUT ALIGNÉ" if ok else "DÉCALAGE DÉTECTÉ")
sys.exit(0 if ok else 1)
