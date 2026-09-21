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
Dois esquemáticos, mesma topologia, PV/bateria modelados de formas diferentes:

- `VR-BESS.tse` — esquemático principal, usa `Bat_array.tlib`/`pv_array.tlib` (bibliotecas
  próprias, blocos `core/C function`).
- `VR-BESS-LIBS-COMPARE.tse` — variante com componentes nativos da Typhoon (`core/Photovoltaic
  Panel` × 34, em arranjo 17 série × 2 paralelo, + `core/Battery`), sem depender de `core/C
  function` — alternativa útil na 2026.3, que tem um bug de compilação nesse tipo de bloco (ver
  `CLAUDE.md`).
- `Bat_array.tlib` — biblioteca do arranjo de baterias (`modelo_bateria` + `calc_it0` como blocos
  C function).
- `pv_array.tlib` — biblioteca do arranjo fotovoltaico (`modelo_pv`, modelo de diodo único).
- `modelo_painel_unicel.ipvx` — parâmetros de um módulo PV único (Ayaz et al. 2014), usado pelos
  34 blocos `core/Photovoltaic Panel` de `VR-BESS-LIBS-COMPARE.tse`.

**Como rodar:** abra o `.tse` desejado no Typhoon HIL Schematic Editor (testado na versão TySim
2026.2 — ver ressalva sobre a 2026.3 abaixo), mantendo os demais arquivos da pasta juntos, para o
esquemático encontrar bibliotecas/`.ipvx`. A pasta `*Target files/` (gerada ao compilar) não é
versionada — é recriada localmente a cada compilação.

#### Instalação do Typhoon HIL (Linux)

1. Baixar o instalador oficial do Typhoon HIL Control Center (`.sh`, self-extracting) no site da
   Typhoon HIL e instalar com `sudo`:
   ```
   sudo bash typhoon_hil_control_center_<versao>.sh -- --accept_license_agreement
   ```
   Instala em `/opt/typhoon/typhoonsim_<versao>/` (ou `typhoon_hil_control_center_<versao>/`,
   dependendo do produto) e cria o atalho no menu. Também aceita `-t <diretorio>` para instalar em
   outro lugar. **Em distros não listadas pelo instalador (ex. CachyOS/Arch — só reconhece
   `centos|rhel|fedora` e `debian|ubuntu`), esse passo aborta antes de criar o atalho/regras de
   udev** com `Not supported Linux distribution` — os arquivos já foram copiados, só falta esse
   trecho final; ver `CLAUDE.md` pro patch do script que resolve.
2. Ativar a licença:
   ```
   typhoon_hil_activation --activate --activation-key <caminho_para_o_arquivo>.lic
   ```

**Problemas que tive na instalação (Linux, sessão Wayland/GNOME, distro rolling-release):**

- **Cache com dono errado:** por rodar a primeira vez como root/sudo, `~/.cache/typhoon` ficou
  pertencendo ao root, e o programa não conseguia mais escrever nele rodando como usuário normal.
  Corrigido com:
  ```
  sudo chown -R $USER:$USER ~/.cache/typhoon
  ```
- **Tela em branco / não renderiza no Wayland:** abrir pelo atalho do menu (ou rodar
  `typhoon_hil.sh` direto) não funcionava corretamente em sessão Wayland. Corrigido forçando o
  backend X11 (xcb) do Qt:
  ```
  QT_QPA_PLATFORM=xcb typhoon_hil.sh
  ```
  (`typhoon_hil.sh` fica em `/opt/typhoon/typhoonsim_<versao>/bin/`.)
- **Crash (`signal 6/ABRT`) ao iniciar `typhoon_hil.exe`:** a `libreadline.so.8` empacotada pela
  Typhoon é incompatível com sistemas rolling-release (testado no CachyOS). Corrigido trocando por
  um symlink pra a do sistema, dentro do diretório de instalação:
  ```
  sudo mv libreadline.so.8 libreadline.so.8.orig
  sudo ln -s /usr/lib/libreadline.so.8 libreadline.so.8
  ```
  Precisa reaplicar a cada instalação/atualização de versão nova.
- **Distro não reconhecida pelo instalador** (ver passo 1 acima) — patch e reprocedimento
  documentados no `CLAUDE.md`.

### `kicad/`

Reservado para o projeto de hardware (placa do conversor). Ainda vazio.

## Status

- MATLAB/Simscape: modelo validado em malha aberta.
- Typhoon HIL: modelo completo simulando em malha aberta, tensões/correntes próximas do projeto
  (Vo≈400V, Vbat≈252V), tanto na versão `.tlib` (`VR-BESS.tse`) quanto na alternativa com
  componentes nativos (`VR-BESS-LIBS-COMPARE.tse`). Proteção/limitador da bateria ainda
  desligados pra fins de calibração, controlador FCS-MPC ainda não implementado — ver `CLAUDE.md`
  para o detalhamento completo do estado atual e das ressalvas.
- KiCad: não iniciado.
