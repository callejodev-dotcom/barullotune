#!/usr/bin/env python3
"""Axusta unha regresión loxística (voz / sen voz) ás características do detector.
Valida por casos (as voces de validación non se usan para axustar).

  BTDetectorTest --features datos.csv 1000 1199   (os casos difíciles, coas regras de antes)
  axustar_voz.py datos.csv pesos.npy
"""
import sys, numpy as np
D = np.loadtxt(sys.argv[1], delimiter=',')
X, y, case = D[:, :7], D[:, 7], D[:, 8].astype(int)
def feats(X):
    # termos: lineais + algúns cadrados/cruces que fan falta (fondo é moi non lineal)
    # sen a enerxía: co ruído de fondo, un silencio non é máis baixo ca a voz feble
    f = [X[:, 0], X[:, 1], X[:, 2], X[:, 4], X[:, 5], X[:, 6]]
    f += [X[:, 0] ** 2, X[:, 1] ** 2, X[:, 0] * X[:, 1], X[:, 2] * X[:, 1]]
    return np.column_stack(f)
names = ["fondo", "prominencia", "continua", "agudos/graves", "media_d", "aceptado", "fondo²", "prom²", "fondo·prom", "continua·prom"]
F = feats(X)
mu, sd = F.mean(0), F.std(0) + 1e-9
Z = (F - mu) / sd
val = (case % 5) == 0
def fit(Z, y, lam=1e-3, iters=300):
    w = np.zeros(Z.shape[1]); b = 0.0
    for _ in range(iters):   # Newton
        p = 1 / (1 + np.exp(-(Z @ w + b)))
        g = Z.T @ (p - y) / len(y) + lam * w; gb = np.mean(p - y)
        W = p * (1 - p)
        H = (Z * W[:, None]).T @ Z / len(y) + lam * np.eye(Z.shape[1])
        Hb = np.mean(W); Hwb = Z.T @ W / len(y)
        Hfull = np.block([[H, Hwb[:, None]], [Hwb[None, :], np.array([[Hb]])]])
        step = np.linalg.solve(Hfull, np.concatenate([g, [gb]]))
        w -= step[:-1]; b -= step[-1]
    return w, b
w, b = fit(Z[~val], y[~val])
def report(mask, tag):
    p = 1 / (1 + np.exp(-(Z[mask] @ w + b)))
    yy = y[mask]
    for th in (0.5,):
        miss = np.mean(p[yy == 1] < th) * 100; fa = np.mean(p[yy == 0] >= th) * 100
        print(f"{tag}: {mask.sum()} tramas, voz {yy.mean()*100:.1f}%, perde {miss:.2f}%, falsa {fa:.2f}%")
    acc = X[mask, 6]
    print(f"   (o detector de agora: perde {np.mean(acc[yy == 1] < 0.5)*100:.2f}%, falsa {np.mean(acc[yy == 0] > 0.5)*100:.2f}%)")
report(~val, "adestramento"); report(val, "validación")
# pesos no espazo orixinal das características: z = (f - mu)/sd -> w·z + b = (w/sd)·f + (b - Σ w mu/sd)
wo = w / sd; bo = b - np.sum(w * mu / sd)
prior = y[~val].mean()
print("prior", prior)
for n, v in zip(names, wo): print(f"  {n:16s} {v:+.4f}")
print("  sesgo           ", f"{bo:+.4f}")
np.save(sys.argv[2], np.concatenate([wo, [bo, prior]]))
# limiares dunha soa trama: perde / falsa
p = 1 / (1 + np.exp(-(Z[val] @ w + b))); yy = y[val]
for th in (0.5, 0.7, 0.8, 0.9, 0.95):
    print(f"  limiar {th}: perde {np.mean(p[yy==1] < th)*100:.2f}%  falsa {np.mean(p[yy==0] >= th)*100:.2f}%")
