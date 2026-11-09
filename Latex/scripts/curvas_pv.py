#!/usr/bin/env python3
"""Gabarito do MPP (ponto de máxima potência) do arranjo PV do VR-BESS.

Reproduz as equações do bloco `C function1` de `typhoonsim/pv_array.tlib` (modelo de diodo único
ideal, sem Rs/Rsh, parâmetros do módulo de Ayaz et al. 2014) para o arranjo 17 série x 2 paralelo,
e acha o MPP de duas formas:

1. Varredura numérica de tensão (0 a 1,2*Voc, 40 000 passos) — força bruta.
2. Forma fechada pela função W de Lambert: Vmp = Vt*[W(e*(Iph+I0)/I0) - 1].

Também calcula os números usados no projeto do P&O (Femia et al. 2005):
- Rd = Vmp/Imp (resistência dinâmica no MPP) e tau = Rd*Cpv;
- |P''| (curvatura da curva PxV no MPP) e o passo mínimo dV pela Eq. 15 do Femia.

Só usa a biblioteca padrão do Python. Uso: python3 Latex/scripts/curvas_pv.py
"""
import math

# ---- Módulo (Ayaz 2014), igual ao pv_array.tlib ----
ISC_1 = 3.14      # A
VOC_1 = 21.4      # V
KI_1 = 0.0024     # A/K
NCEL_1 = 36       # células em série por módulo
K_B = 1.3806e-23  # J/K (Boltzmann)
Q_E = 1.6022e-19  # C (carga do elétron)
A_ID = 1.2        # fator de idealidade
EG = 1.12         # eV (silício)

# ---- Arranjo e circuito ----
NS_MOD = 17       # módulos em série
NP_MOD = 2        # strings em paralelo
CPV = 220e-6      # F (capacitor do PV em VR-BESS.tse)

# ---- Projeto do P&O ----
TA = 5e-3         # s, período do P&O
DG_DT = 100.0     # W/m²/s, borda de nuvem rápida (Femia, pior caso)

T_REF = 298.15
G_REF = 1000.0


def parametros(G, T_c):
    """Retorna (Iph, I0, Vt, Voc_ref) do arranjo para irradiância G (W/m²) e temperatura T_c (°C)."""
    isc = ISC_1 * NP_MOD
    voc = VOC_1 * NS_MOD
    ki = KI_1 * NP_MOD
    ncel = NCEL_1 * NS_MOD
    T = T_c + 273.15
    iph = (isc + ki * (T - T_REF)) * (G / G_REF)
    irs = isc / (math.exp(Q_E * voc / (ncel * K_B * A_ID * T_REF)) - 1.0)
    i0 = irs * (T / T_REF) ** 3 * math.exp((Q_E * EG / (A_ID * K_B)) * (1.0 / T_REF - 1.0 / T))
    vt = ncel * K_B * A_ID * T / Q_E
    return iph, i0, vt, voc


def corrente(V, iph, i0, vt):
    """I(V) = Iph - I0*(e^(V/Vt) - 1), com corte em zero (igual ao Typhoon)."""
    x = min(V / vt, 80.0)
    return max(iph - i0 * (math.exp(x) - 1.0), 0.0)


def lambert_w(z):
    """Ramo principal de W(z) para z > 0, por Newton (dispensa scipy)."""
    w = math.log(z) - math.log(math.log(z)) if z > 3 else 1.0
    for _ in range(50):
        ew = math.exp(w)
        w_novo = w - (w * ew - z) / (ew * (w + 1.0))
        if abs(w_novo - w) < 1e-12:
            return w_novo
        w = w_novo
    return w


def mpp(G, T_c, n=40000):
    iph, i0, vt, voc_ref = parametros(G, T_c)
    vmax = 1.2 * voc_ref
    melhor = (0.0, 0.0, 0.0)
    voc = 0.0
    for k in range(n + 1):
        V = vmax * k / n
        I = corrente(V, iph, i0, vt)
        if I > 0:
            voc = V
        if V * I > melhor[2]:
            melhor = (V, I, V * I)
    vmp, imp, pmp = melhor
    # Forma fechada (Lambert W)
    vmp_w = vt * (lambert_w(math.e * (iph + i0) / i0) - 1.0)
    pmp_w = vmp_w * corrente(vmp_w, iph, i0, vt)
    # Curvatura no MPP por diferença finita central
    h = 0.01
    P = lambda v: v * corrente(v, iph, i0, vt)
    p2 = abs((P(vmp_w + h) - 2 * P(vmp_w) + P(vmp_w - h)) / h ** 2)
    return dict(vmp=vmp, imp=imp, pmp=pmp, voc=voc, isc=corrente(0, iph, i0, vt),
                vmp_w=vmp_w, pmp_w=pmp_w, p2=p2)


def main():
    casos = [(1000, 25), (800, 25), (600, 25), (400, 25), (200, 25), (50, 25),
             (1000, 0), (1000, 45), (1000, 65)]
    print(f"Arranjo {NS_MOD}s x {NP_MOD}p, Cpv = {CPV*1e6:.0f} uF, Ta = {TA*1e3:.0f} ms, "
          f"dG/dt = {DG_DT:.0f} W/m2/s\n")
    cab = ("G(W/m2) T(C)  Vmp(V)  Imp(A)  Pmp(W)  Voc(V)  Isc(A)  Rd(Ohm) tau(ms) "
           "|P''|(W/V2) dVmin(V) | Vmp_W(V) Pmp_W(W)")
    print(cab)
    print("-" * len(cab))
    for G, T in casos:
        r = mpp(G, T)
        rd = r["vmp"] / r["imp"]
        dp_g = (r["pmp"] / G) * DG_DT * TA            # variação de P causada pela irradiância
        dv_min = math.sqrt(2 * dp_g / r["p2"])        # Femia Eq. 15: 1/2|P''|dV^2 > dP_G
        print(f"{G:7.0f} {T:4.0f} {r['vmp']:7.1f} {r['imp']:7.2f} {r['pmp']:7.0f} "
              f"{r['voc']:7.1f} {r['isc']:7.2f} {rd:8.0f} {rd*CPV*1e3:7.1f} "
              f"{r['p2']:11.3f} {dv_min:8.1f} | {r['vmp_w']:8.2f} {r['pmp_w']:8.1f}")


if __name__ == "__main__":
    main()
