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

### `Latex/`

Fonte LaTeX do artigo/TCC (baixado do Overleaf), arquivo principal `VRBESS-JoaoBarbosa.tex`
(renomeado do `sbaconf.tex` original do template; classe `ifacconf`, formato IFAC). Compilação local
testada com sucesso (7 páginas, `VRBESS-JoaoBarbosa.pdf`), com `latexmk`
resolvendo sozinho as passadas de BibTeX (bibliografia usa `ifacconf.bst`, já incluído na pasta —
`\bibliographystyle` é definido dentro de `ifacconf.cls`, nada externo).

```
sudo pacman -S texlive-basic texlive-latex texlive-latexextra texlive-latexrecommended \
  texlive-pictures texlive-fontsrecommended texlive-langportuguese texlive-binextra
```

Instala o TeX Live necessário pra compilar o `VRBESS-JoaoBarbosa.tex`: núcleo do LaTeX e `graphicx`/`amsmath`/
`url` (`texlive-basic`/`texlive-latex`), `natbib`/`subcaption` (`texlive-latexrecommended`), `tikz`/
`circuitikz` (`texlive-pictures`), pacote `ae` (`texlive-fontsrecommended`), hifenização em
português pro `babel` (`texlive-langportuguese`), e `latexmk` (`texlive-binextra`) pra não precisar
rodar `pdflatex`/`bibtex` manualmente em sequência. Optou-se pelo TeX Live completo (em vez de algo
mais leve como `tectonic`) por compatibilidade — o documento usa `natbib`+BibTeX com `.bst` próprio,
mais seguro no TeX Live tradicional.

**Como compilar:**
```
cd Latex && latexmk -pdf VRBESS-JoaoBarbosa.tex
```

### `kicad/`

Reservado para o projeto de hardware (placa do conversor). Ainda vazio.

## Status

- MATLAB/Simscape: modelo validado em malha aberta.
- Typhoon HIL: modelo completo simulando em malha aberta, tensões/correntes próximas do projeto
  (Vo≈400V, Vbat≈252V), tanto na versão `.tlib` (`VR-BESS.tse`) quanto na alternativa com
  componentes nativos (`VR-BESS-LIBS-COMPARE.tse`).
- **Controlador FCS-MPC: implementado e funcionando em `VR-BESS.tse`**, substituindo
  definitivamente o PWM fixo. Regula `V_out≈400V` em regime permanente (testado com `SOC=50%` e
  `100%`), robusto a variação de irradiância (`G` de `0` a `1400W/m²`) e de temperatura
  (`25→0→45→65→25°C`). Proteção/limitador antigos da bateria permanecem desligados — o FCS-MPC
  assume os limites de corrente via saturação da referência. Ver `CLAUDE.md` (seção "Roteiro:
  controlador FCS-MPC") para o detalhamento completo das decisões de projeto e da depuração.
- KiCad: não iniciado.

## Pendências (checklist)

- [ ] **Degrau de carga (`Ro`)** — ainda não testado. `Po_ref` hoje é uma constante fixa (`1000W`),
  não lê `Ro` real; se `Ro` mudar de verdade, `Vo` provavelmente foge de `400V`. Decidir: testar
  assim mesmo (documentar limitação) ou calcular a potência-alvo a partir de `V_out×I_out` medidos
  antes de testar.
- [ ] **Valor comercial final do `Cvo`** — hoje testado em `400µF` (igual ao artigo), decidir entre
  `390µF` (mais próximo do valor validado) ou `470µF` (mais fácil de achar/mais barato).
- [ ] **Documentação do TCC**: reescrever a derivação do modelo discreto (hoje só em
  `Latex/secoes/old.tex`, fora do documento ativo, com valores de outro trabalho) com os valores
  reais deste projeto; adicionar ao `Latex/ifacconf.bib` as entradas que faltam:
  `braga2022controle`, `marcello22019modelagem`, `delay`, e **`paper_4440`** (usado pra validar o
  `Co` — Braga, Carvalho, Ferreira, Pacheco, Pereira, "Controle Preditivo Baseado em Modelo para
  Conversores de 3 Portas: o Sistema VR-BESS").
- [ ] **Escopo prático do TCC** (hardware real) — ainda não decidido: protótipo físico completo
  (KiCad + fabricação + bancada), projeto de hardware especificado sem montar, ou ficar só na
  simulação validada. Depende do prazo restante do trabalho.
- [ ] Reavaliar se a proteção da bateria (sinal `S`) e o limitador de corrente (`GainKi`) precisam
  ser religados antes de qualquer uso além de simulação/validação.
- [ ] KiCad: não iniciado — depende da decisão de escopo acima.

## Roteiro: controlador FCS-MPC

Resumo da implementação final — detalhamento completo (decisões, porquês, jornada de depuração)
na seção "Roteiro: controlador FCS-MPC" do `CLAUDE.md`:

- Plataforma base: `VR-BESS.tse` (`.tlib`), substituindo definitivamente o PWM fixo
  (`Constant9`/`Constant10` + `PWM_Mod_S1`/`PWM_Mod_S2`), sem fallback comutável.
- Escopo: 3 estados de chaveamento `(1,1)`/`(1,0)`/`(0,1)` (`(0,0)` é estruturalmente redundante,
  removido), `Ts=1µs`, horizonte de predição de 2 passos.
- Referências: `i_Ls*` por balanço de potência com teto de MPP escalado pela irradiância medida
  (`G`); `i_Lbat*` por balanço de potência usando `P_pv` medido em tempo real, saturado nos
  limites de projeto (0,5C carga / 2C descarga). Ambas com limitador de taxa de variação.
- Função custo com normalização linear entre os termos de corrente (fiel à dissertação da Isadora
  Braga, `braga2022controle`).
- `Cvo` (capacitor de saída) aumentado de `1,41µF` pra `~400µF` — necessário pro FCS-MPC
  (frequência de chaveamento variável), validado contra `paper_4440.pdf` (mesmo grupo de pesquisa,
  resultado equivalente).
- Proteção/limitador antigos da bateria permanecem desligados — o FCS-MPC assume os limites de
  corrente via saturação da referência.
