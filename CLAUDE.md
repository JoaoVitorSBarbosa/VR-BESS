# CLAUDE.md — VR-BESS

Contexto para trabalhar neste repositório. Ver também `README.md` (visão geral e como rodar).

## O que é o projeto

Conversor CC-CC de três portas (PV + bateria + carga, 2 chaves) para sistemas fotovoltaicos com
armazenamento — TCC do João Vitor Barbosa (orientação Silvia Costa Ferreira, UFLA). Modelado em
MATLAB/Simscape, validado em malha aberta, e sendo portado para Typhoon HIL (TySim) para rodar no
simulador HIL. Próxima etapa (ainda não implementada no código deste repo): controlador FCS-MPC em
malha fechada. O artigo/dissertação em si (LaTeX) fica em `/home/joaovitor/Documents/Artigos/`
(fora deste repo, não versionado aqui).

## Estrutura

- `matlab/` — modelos `.slx` (Simulink/Simscape): `bat.slx`/`bat_lib.slx` (bateria),
  `PV.slx`/`PV_lib.slx` (arranjo fotovoltaico), `Modo_MA_simscape.html` (relatório publicado do
  MATLAB, malha aberta).
- `typhoonsim/` — porte para Typhoon HIL: `VR-BESS.tse` (esquemático principal),
  `Bat_array.tlib`, `pv_array.tlib` (bibliotecas). `VR-BESS Target files/` é saída de compilação
  (gitignored, recriada a cada build).
- `kicad/` — vazio, reservado para o hardware (placa).

## Valores de projeto (Modo 1 — regula tensão + carrega bateria)

- Vo = 400 V (Ro = 160 Ω, Po = 1000 W); Vbat = 252 V (Ns_bat=15, Np_bat=1)
- PV: Ns_mod=17, Np_mod=2 (painel com Vmp=18,228 V, Imp=2,96 A — parâmetros de Ayaz et al. 2014)
- f_sw = 100 kHz; duty ciclos finais (autoconsistentes, não vêm do Vmp nominal): Di=13,77% (S2),
  Dii=76,77% (S1) — ver `secoes/03-Metodologia.tex` no projeto do artigo para a derivação em
  ponto fixo (a tensão real do painel depende da corrente que o conversor demanda).
- Ls≈3,89 mH, Lbat≈4,23 mH, Co≈1,41 µF, Cbat≈0,109 µF
- Limites de corrente da bateria: 0,5C=1,15 A (carga), 2C=4,6 A (descarga), via resistor série
  proporcional (não é clamp duro) dentro de `modelo_bateria`
- MATLAB: solver **Local Solver** (Backward Euler, passo fixo 200 ns) — necessário pelo duty
  estreito (13,77%) em 100 kHz; passo variável não resolve o pulso.

## Convenções e armadilhas do Typhoon HIL (blocos "core/C function")

Descobertas portando `Bat_array.tlib`/`pv_array.tlib` do MATLAB. Valem para qualquer biblioteca
nova (ex.: se um dia portar mais alguma coisa do MATLAB para cá):

- **Nomeie terminais pelo nome puro, sem sufixo `_in`/`_out`.** O compilador atribui cada entrada
  a uma global mangled usando o nome puro do terminal logo antes do corpo `{ }` do bloco (ex.:
  `_bat_array1_calc_it0__Np_bat = _constant2__out;`), e o código deve referenciar `Np_bat`, não
  `Np_bat_in`. Saídas funcionam igual ao contrário: atribua ao nome puro da saída (`it0`, não
  `it0_out`).
- **Não use o mesmo nome de uma saída para uma variável local de cálculo intermediário.** O
  compilador substitui todo mundo com aquele nome pela global mangled, inclusive a declaração
  local — isso cria sombreamento e o valor nunca chega no destino real. Dê nomes diferentes às
  variáveis de rascunho (ex.: sufixo `_calc`).
- **Terminais de entrada são somente leitura** — `V = clamp(V);` dá erro de compilação
  (`Cannot write to the terminal 'V'. Input terminals are read-only.`). Sempre copie para uma
  variável local antes de modificar: `real_t V_clamped = V;`.
- **Debug de erro de compilação confuso:** leia o C gerado direto em
  `~/.local/share/typhoon/TySim 2026.2/apps/schematic_editor/code_generation/offline_simulator/c_code/root.c`
  (sobrescrito a cada tentativa de compilação) — mostra os nomes reais das variáveis em vez de
  adivinhar pelo texto do erro. Log de erro de compilação:
  `~/.local/share/typhoon/TySim 2026.2/logs/errlog.txt`.
- **Erro "pop from an empty set" ao compilar:** normalmente é um `Goto`/`From` com tag que não
  bate (ex.: `Iout_mes` vs `I_out_mes`, faltando underscore) — o correlacionador de tags do
  compilador (`c_code_exporter/interfaces.py:_add_tags_to_corr_interface`) estoura ao não achar
  par. Corrigir o valor da tag no bloco `From`/`Goto` que estiver divergente.
- **Sinal no I/O digital sem mapear canal manualmente:** colocar um bloco **Probe** nomeado igual
  ao sinal-alvo funciona como sink implícito no I/O digital — mais simples que mexer em
  `channels`/`di_ctrl_addrs` de componentes como o PWM Modulator.

## Bug conhecido, não corrigido (`modelo_bateria`)

Com os parâmetros estimados de forma da curva LiFePO4 (E0_1=13,32, A_1=1,4), na carga cheia
(`it_c=0`) o modelo calcula `E = E0 + A = 14,72 V`, que excede `V_max=14,6 V`. Isso dispara o ramo
`elseif V > V_max: S=0`, que bloqueia carga **e** descarga — o ramo `SOC>=100 && I<0` (que deveria
bloquear só a carga) nunca é alcançado porque o `elseif` anterior já disparou. Correção pendente:
baixar `A_1` (ex.: ~1,0) ou garantir `E0_1 + A_1 < V_max_1` em corrente zero. Verificar os valores
atuais de `E0_1`/`A_1` antes de assumir que já foi corrigido.

## Instalação/ambiente do Typhoon HIL (Linux) — ver detalhes e comandos completos no README

Instalado em `/opt/typhoon/typhoonsim_2026.2/` via instalador oficial (`sudo bash
typhoon_hil_control_center_<versão>.sh -- --accept_license_agreement`), licença ativada com
`typhoon_hil_activation --activate --activation-key <arquivo>.lic`. Dois problemas resolvidos:
permissão de `~/.cache/typhoon` (ficou do root por ter rodado como sudo na primeira vez) e tela em
branco no Wayland (corrigido com `QT_QPA_PLATFORM=xcb typhoon_hil.sh`).

## Status

- MATLAB/Simscape: modelo validado em malha aberta.
- Typhoon HIL: porte em andamento — `Bat_array.tlib` e `pv_array.tlib` compilando (bug do
  SOC=100% acima ainda não corrigido). Falta portar/validar o modelo completo VR-BESS com as 2
  chaves e comparar contra os resultados do MATLAB.
- Controlador FCS-MPC: ainda não implementado em nenhuma das duas plataformas.
- KiCad: não iniciado.
