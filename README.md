# VR-BESS

Conversor CC-CC de três portas (Voltage Regulator + Battery Energy Storage System) para sistemas
fotovoltaicos com armazenamento, com controle FCS-MPC (Finite Control Set - Model Predictive
Control). Trabalho de TCC de João Vitor Barbosa, orientação de Silvia Costa Ferreira (UFLA).

Topologia: 3 portas (PV, bateria, carga/barramento CC) com 2 chaves semicondutoras. Modelo
validado em malha aberta com Vo = 400 V, Vbat = 252 V, PV com Ns_mod=17/Np_mod=2.

## Estrutura do repositório

```
VR-BESS/
├── matlab/       Modelos MATLAB/Simscape (bibliotecas de bateria e PV, testes)
├── typhoonsim/   Projeto portado para Typhoon HIL (TySim) — mesmo modelo rodando no simulador HIL
├── kicad/        Projeto de hardware (placa), ainda não iniciado
└── README.md
```

### `matlab/`

- `bat.slx`, `bat_lib.slx` — modelo e biblioteca do bloco de bateria (`modelo_bateria`, curva
  Tremblay-Dessaint, limitador de corrente de carga/descarga).
- `PV.slx`, `PV_lib.slx` — modelo e biblioteca do arranjo fotovoltaico (modelo de diodo único).
- `Modo_MA_simscape.html` — relatório publicado pelo MATLAB (Conversion Assistant Report) do modelo
  em malha aberta.

**Como rodar:** abra os `.slx` no MATLAB/Simulink (usados com Simscape). O modelo completo de
malha aberta usa o **Local Solver** (Backward Euler, passo fixo de 200 ns) — necessário porque um
solver de passo variável não resolve o pulso estreito de PWM (duty ~13,77%) em 100 kHz.

### `typhoonsim/`

Porte do mesmo conversor para o Typhoon HIL Schematic Editor (TySim), para rodar no simulador HIL.

- `VR-BESS.tse` — esquemático principal do projeto.
- `Bat_array.tlib` — biblioteca do arranjo de baterias (`modelo_bateria` + `calc_it0` como blocos
  C function).
- `pv_array.tlib` — biblioteca do arranjo fotovoltaico (`modelo_pv`, modelo de diodo único).

**Como rodar:** abra `VR-BESS.tse` no Typhoon HIL Schematic Editor (testado na versão TySim
2026.2), com `Bat_array.tlib` e `pv_array.tlib` na mesma pasta para o esquemático encontrar as
bibliotecas. A pasta `VR-BESS Target files/` (gerada ao compilar) não é versionada — é
recriada localmente a cada compilação.

### `kicad/`

Reservado para o projeto de hardware (placa do conversor). Ainda vazio.

## Status

- MATLAB/Simscape: modelo validado em malha aberta.
- Typhoon HIL: porte em andamento — bibliotecas de bateria e PV compilando.
- KiCad: não iniciado.
