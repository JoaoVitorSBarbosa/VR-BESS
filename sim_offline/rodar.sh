#!/usr/bin/env bash
# Simulação offline (C puro) do VR-BESS com o controlador do C function1, para testar
# mudanças em segundos antes de ir pro Typhoon. Uso:
#   sim_offline/rodar.sh [controlador.c] [G1,G2,...] [t_trecho_s] [Op_mode]
#   (padrão: sim_offline/controlador_mppt.c  650,1000,650  0.1  1)
# O arquivo do controlador tem as duas partes do bloco (PARTE 1 = arb_defs, PARTE 2 = output_fnc),
# no mesmo formato do que é colado no editor do Typhoon.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CTRL="${1:-$DIR/controlador_mppt.c}"
shift || true
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
python3 -I - "$CTRL" "$TMP/ctrl.c" <<'PY'
import sys, re
s = open(sys.argv[1]).read()
p1 = s.index("PARTE 1 - aba arb_defs"); p2 = s.index("PARTE 2 - aba output_fnc")
def corpo(t):
    a = t.index("/*Begin code section*/") + len("/*Begin code section*/")
    return t[a:t.index("/*End code section*/", a)]
arb, out = corpo(s[p1:p2]), corpo(s[p2:])
hdr = ("#include <math.h>\n"
       "double V_pv,V_bat,V_o,I_pv,I_bat,G_in,Op_mode;\n"
       "double S1,S2,dbg_ILs_ref,dbg_ILbat_ref,dbg_cost11,dbg_cost10,dbg_cost01,dbg_cost00,"
       "dbg_Vpv_m,dbg_Vbat_m,dbg_Vo_m,dbg_ILs_m,dbg_ILbat_m;\n")
open(sys.argv[2], "w").write(hdr + arb + "\nvoid step(void){\n" + out + "\n}\n")
PY
gcc -O2 -w "$TMP/ctrl.c" "$DIR/planta.c" -lm -o "$TMP/sim"
"$TMP/sim" "$@"
