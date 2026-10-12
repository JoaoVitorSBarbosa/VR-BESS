# CLAUDE.md — VR-BESS

Contexto para trabalhar neste repositório. Ver também `README.md` (visão geral e como rodar).

## O que é o projeto

Conversor CC-CC de três portas (PV + bateria + carga, 2 chaves) para sistemas fotovoltaicos com
armazenamento — TCC do João Vitor Barbosa (orientação Silvia Costa Ferreira, UFLA). Modelado em
MATLAB/Simscape, validado em malha aberta, e sendo portado para Typhoon HIL (TySim) para rodar no
simulador HIL. **Motivo do porte para Typhoon**: sair de vez do MATLAB — a licença é cara e difícil
de manter/renovar fora do ambiente institucional, enquanto o Typhoon HIL é gratuito com conta de
estudante. O plano é migrar a matemática/simulação inteira (incluindo o futuro controlador FCS-MPC)
pro Typhoon, não usar os dois em paralelo indefinidamente. Próxima etapa (ainda não implementada no
código deste repo): controlador FCS-MPC em malha fechada. O artigo/dissertação em si (LaTeX) fica em
`/home/joaovitor/Documents/Artigos/` (fora deste repo, não versionado aqui) — nesta máquina, os
artigos de referência estão em `~/Documentos/Artigos` (ver memória do Claude Code deste projeto).

## Estrutura

- `matlab/` — modelos `.slx` (Simulink/Simscape): `bat.slx`/`bat_lib.slx` (bateria),
  `PV.slx`/`PV_lib.slx` (arranjo fotovoltaico), `Modo_MA_simscape.html` (relatório publicado do
  MATLAB, malha aberta).
- `typhoonsim/` — porte para Typhoon HIL: `VR-BESS.tse` (esquemático principal),
  `Bat_array.tlib`, `pv_array.tlib` (bibliotecas). `VR-BESS Target files/` é saída de compilação
  (gitignored, recriada a cada build).
- `kicad/` — vazio, reservado para o hardware (placa).

## Convenção: documentar dependências instaladas no README

Sempre que uma dependência/pacote for instalada no sistema pra viabilizar alguma parte do projeto
(ex.: `sudo pacman -S texlive-...` pra compilar o LaTeX do TCC, um pacote Python, etc.), registrar
no `README.md`: o comando exato usado, o que ele instala, e por que foi necessário (qual parte do
projeto depende disso) — mesmo padrão já usado na seção de instalação do Typhoon HIL do README
(bloco de comando + explicação em prosa ao redor). Objetivo: manter o ambiente reproduzível pra quem
clonar o repo depois, sem precisar redescobrir essas dependências na unha.

## Valores de projeto (Modo 1 — regula tensão + carrega bateria)

- Vo = 400 V (Ro = 160 Ω, Po = 1000 W); Vbat = 252 V (Ns_bat=15, Np_bat=1)
- PV: Ns_mod=17, Np_mod=2 (painel com Vmp=18,228 V, Imp=2,96 A — parâmetros de Ayaz et al. 2014)
- f_sw = 100 kHz.
- **Duty cycles — MATLAB vs Typhoon, valores diferentes e isso é esperado:**
  - **MATLAB** (autoconsistentes, não vêm do Vmp nominal): Di=13,77% (S2), Dii=76,77% (S1) — ver
    `secoes/03-Metodologia.tex` no projeto do artigo para a derivação em ponto fixo por balanço
    volt-segundo (Eq. 5/6), que assume conversor **ideal/sem perdas** (a tensão real do painel
    depende da corrente que o conversor demanda, mas diodos/chaves são tratados como ideais).
  - **Typhoon** (`VR-BESS.tse`, `Constant9`/`Constant10`): Di=21%, Dii=84,5% — recalibrados
    **empiricamente**, não recalculados pela mesma equação ideal. Motivo: o circuito Typhoon tem
    perdas reais que a Eq. 5/6 não modela — os diodos `D1`/`D2`/`D3` usam o `Vd=1,2V` padrão do
    `core/Diode` (nunca sobrescrito) e o dead-time (`d_time=1e-6`) é 10% do período de comutação
    (1 µs em 10 µs), bem mais alto que o típico (1–2%). Com os valores ideais do MATLAB, o Typhoon
    converge para um ponto de operação autoconsistente diferente do projetado (confirmado mesmo
    depois de corrigir o bug do painel PV abaixo — ver `git log`, commits de recalibração). Ajuste
    prático: `Di` define o ganho de tensão `Vo=Vpv/(1-Di)`; a janela `Dii-Di` (que por construção
    corresponde a `Vbat/Vo`, o trecho do período em que S1 está fechada e S2 aberta) é o que
    direciona corrente especificamente para carregar a bateria — mexer nela não afeta `Vo`
    significativamente. Se os componentes/perdas do Typhoon forem ajustados no futuro, os duty
    cycles do MATLAB voltam a ser o ponto de partida correto e essa recalibração precisa repetir.
- Ls≈3,89 mH, Lbat≈4,23 mH, Co≈1,41 µF (malha aberta/PWM fixo), Cbat≈0,109 µF. **Pro FCS-MPC
  (malha fechada), `Co` foi aumentado pra ~400µF** — o valor original foi dimensionado pro PWM de
  frequência fixa, mas o FCS-MPC tem frequência de chaveamento variável e precisa de capacitor de
  saída maior pra ripple aceitável (ver seção "Roteiro: controlador FCS-MPC" abaixo pro porquê).
- Limites de corrente da bateria: 0,5C=1,15 A (carga), 2C=4,6 A (descarga), via resistor série
  proporcional (não é clamp duro) dentro de `modelo_bateria`
- MATLAB: solver **Local Solver** (Backward Euler, passo fixo 200 ns) — necessário pelo duty
  estreito (13,77%) em 100 kHz; passo variável não resolve o pulso. No Typhoon o equivalente foi
  `simulation_time_step` fixo em 2e-7 (200 ns) em vez de `auto` — ver armadilhas abaixo.

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
  `~/.local/share/typhoon/TySim 2026.3/apps/schematic_editor/code_generation/offline_simulator/c_code/root.c`
  (sobrescrito a cada tentativa de compilação) — mostra os nomes reais das variáveis em vez de
  adivinhar pelo texto do erro. Log de erro de compilação:
  `~/.local/share/typhoon/TySim 2026.3/logs/errlog.txt` (esse log só registra crashes da aplicação,
  não erros de validação/compilação do modelo — para esses últimos, checar o console de saída do
  Schematic Editor).
- **Erro "pop from an empty set" ao compilar:** normalmente é um `Goto`/`From` com tag que não
  bate (ex.: `Iout_mes` vs `I_out_mes`, faltando underscore) — o correlacionador de tags do
  compilador (`c_code_exporter/interfaces.py:_add_tags_to_corr_interface`) estoura ao não achar
  par. Corrigir o valor da tag no bloco `From`/`Goto` que estiver divergente.
- **Sinal no I/O digital sem mapear canal manualmente:** colocar um bloco **Probe** nomeado igual
  ao sinal-alvo funciona como sink implícito no I/O digital — mais simples que mexer em
  `channels`/`di_ctrl_addrs` de componentes como o PWM Modulator.
- **`core/PWM Modulator` → `core/Digital Input` só funciona com dois requisitos, os DOIS, não só
  um:** `vhil_adio_loopback = True` na `configuration` do modelo (habilita o loopback virtual de
  I/O digital em simulação VHIL+ sem hardware real) **e** um bloco `core/Initial Settings`
  (categoria "System" na paleta) mapeando explicitamente cada sinal do PWM Modulator para um canal
  de saída digital, via a propriedade `digital_outputs` — ex.:
  `digital_outputs = "['DO1;..PWM_Mod_S1.TOP_1;False;False;0', 'DO2;..PWM_Mod_S2.TOP_1;False;False;0']"`.
  Sem esse mapeamento explícito, `vhil_adio_loopback=True` sozinho não basta: o sinal do PWM
  Modulator nunca é colocado em nenhum canal DIO virtual, então `core/Digital Input` não tem nada
  para ler de volta (fica preso em `ctrl_in` do MOSFET recebendo silêncio/zero, mesmo com
  `ctrl_src = "Model"` correto e o PWM Modulator gerando pulso normalmente). Confirmado por resposta
  oficial da Typhoon (fórum, usuário "Milan") depois de uma sessão inteira de debug em que
  trocar `ctrl_src` para `"Internal modulator"` (cada chave gera seu próprio PWM interno, sem
  I/O digital nenhum) foi o único jeito de fazer o chaveamento funcionar sem este bloco — abordagem
  também usada nos exemplos oficiais da Typhoon (ex.: `core/Three Phase Inverter` no exemplo "back
  to back converter"), então é uma alternativa válida quando não se quer mexer em `Initial
  Settings`/canais DIO.
- **`execution_rate` precisa ser IGUAL em todos os componentes de um laço algébrico/dinâmico
  (sensor → `core/C function` → atuador).** Descoberto duas vezes de forma independente nesta
  sessão: (1) `Bat_array1.Integrator1`/`Sum2` não conseguiam herdar (`inherit`) a taxa porque as
  entradas vinham de componentes em taxas diferentes; (2) pior ainda, no `pv_array.tlib`, `Va1`
  (sensor de tensão) e `"C function1"` estavam explicitamente em `2e-7` mas `Isp1` (fonte de
  corrente controlada, quem injeta a corrente calculada de volta no circuito) tinha ficado em
  `inherit` (resolvendo para uma taxa ~500x mais lenta). Isso NÃO deu erro de compilação — o
  circuito simulou normalmente, só que `Isp1` injetava um valor de corrente praticamente
  congelado (perto do calculado em t≈0, quando V≈0), como se o painel nunca respondesse à própria
  tensão de terminal. Sintoma: valores de tensão/corrente que não batem com a curva do
  componente (ver bug do painel PV abaixo) mesmo com a fórmula e a fiação corretas — sempre
  conferir `execution_rate` de TODO componente do laço antes de desconfiar da fórmula ou da
  fiação.
- **Erro de fiação já cometido: trocar dois `Goto`/`From` (ou duas portas) de mesmo tipo `real`
  na hora de conectar — não dá erro de compilação, só produz resultado fisicamente errado.**
  Em `pv_array.tlib`, os sinais `Ns_mod` e `T` (ambos `real`, mesma direção) ficaram trocados na
  entrada de `"C function1"` (`Ns_mod` recebendo o valor de `Temp_pv`, `T` recebendo o valor de
  `Num_cel_serie_pv`). Como os tipos batem, o compilador aceita numa boa — o bug só aparece como
  comportamento fisicamente impossível em runtime (nesse caso, o Voc calculado saiu ~535V em vez
  de ~364V, permitindo o painel sustentar corrente muito além do seu Voc real e entregar potência
  acima do Pmax físico). Ao ligar várias entradas `real` de um `core/C function`, conferir cada
  par `Goto`/`From` pelo **valor da tag**, não só pela ordem/posição das linhas `connect`.
- **Sinal de dentro de uma library (`.tlib`) não aparece no Scope/Probe do simulador offline
  (TySim software, sem HIL real) — mensagem `"<sinal> is not supported by TyphoonSim yet. Signal
  will be zeroed."`.** Isso vale mesmo quando o sinal já sai por uma porta da subsystem exportada
  como pino da library (ex.: `Bat_array1.V`, `Bat_array1.SOC`) e mesmo quando internamente ele já
  passa por um bloco nativo com `sig_output = "True"` (ex.: `Va1`/`Iout` dentro de `Bat_array.tlib`
  já tinham isso e ainda assim zeraram) — `signal_access = "Public"` no `core/Probe` do lado de
  fora também não resolve. O limite parece ser cruzar a fronteira da library em si, não o tipo de
  bloco que produz o sinal.
  - **O que resolveu para tensão/corrente da bateria** (`V_bat_mes`, `I_bat`): colocar um bloco
    nativo `core/Voltage Measurement`/`core/Current Measurement` **direto no schematic principal**,
    medindo o nó real fora da library, e ligar o Probe nele — sem passar pela porta da subsystem.
  - **Por que isso não existe para SOC:** SOC não é uma grandeza do circuito, é estado interno
    (contagem de Coulomb via `Integrator1` + fórmula em `modelo_bateria`, um `core/C function`)
    dentro de `Bat_array.tlib`. Não tem bloco de medição nativo equivalente para colocar fora da
    library.
  - Confirmado rastreando a fiação: mesmo o probe do PWM (`Digital Probe1`, rotulado
    `"PWM Modulator1.TOP_1"` via `override_signal_name`) não lê o componente PWM Modulator por
    dentro — ele está fiado em `Digital Input1.out` (leitura real de um canal DIO físico) que
    também aciona `S1.ctrl_in`. Ou seja, todo sinal visível no Scope/Probe do TySim offline vem de
    um produtor nativo no nível do schematic principal (medição física ou readback de I/O), nunca
    de dentro de uma library.
  - **Resolvido:** o SOC foi replicado direto no schematic principal (`SOC_it0_calc` →
    `SOC_I_to_Ah` + `SOC_it_integrator` → `SOC_calc_top`, em `VR-BESS.tse`), alimentado pelos
    sinais `I_bat`/`V_bat` que já funcionam — mesmo padrão do fix de tensão/corrente, replicando a
    fórmula de `modelo_bateria`/`calc_it0`. Visível no Scope como `SOC_mes`. Isso duplica lógica
    (risco de divergir se a fórmula dentro de `Bat_array.tlib` mudar sem replicar a mudança aqui),
    mas é o único jeito encontrado até agora de ver o SOC no simulador offline. Alternativa não
    testada: rodar no HIL real (não o TySim software) — a mensagem de erro diz "not supported by
    TyphoonSim **yet**", sugerindo que é uma limitação específica do simulador local, não do
    hardware.

## Modelo nativo `core/Photovoltaic Panel`/`core/Battery` como alternativa aos `.tlib`

Motivação: contornar o bug do `core/C function` na 2026.3 (ver acima) sem depender só do downgrade
pra 2026.2 — usar componentes nativos da Typhoon (`core/Photovoltaic Panel`, `core/Battery`) no lugar
de `pv_array.tlib`/`Bat_array.tlib`. Em andamento em `VR-BESS-LIBS-COMPARE.tse`, ainda não validado
formalmente contra o modelo `.tlib` original (nem religadas proteção/limitador, mesma ressalva do
`VR-BESS.tse` — ver "Status").

- **`core/Photovoltaic Panel` não tem campo Ns/Np pra representar um arranjo** — confirmado nos docs
  oficiais instalados localmente (`.../Documentation/html/pv_generator_api.html`): nenhum dos 3
  modelos (`Detailed`, `EN50530 Compatible`, `Normalized IV`) tem parâmetro de composição
  série/paralelo. `Nc` ("número de células") precisa ser fisicamente coerente com `Voc_ref` (tensão
  por célula ~0,59V pra c-Si — bate com o painel Ayaz real: 21,4V/36 células). Escalar
  `Voc_ref`/`Isc_ref` pro arranjo inteiro (17×21,4V=363,8V) mantendo `Nc=36` (escala de módulo) dá
  ~10,1V/célula — fisicamente absurdo — e degenera o ajuste do diodo único (força um `I0`
  praticamente zero pra "esticar" a curva), gerando um painel com potência ~0W em qualquer ponto de
  operação real. Usar o `Nc` fisicamente correto pro arranjo inteiro (612=36×17) evita esse
  degeneramento mas esbarra no limite numérico já documentado na memória do projeto (overflow no fit
  interno, mesmo sintoma de antes). Ou seja: **não existe par (Nc, Voc_ref) que resolva o arranjo
  inteiro num único bloco `core/Photovoltaic Panel`.**
  - **Fix que funcionou:** usar o `.ipvx` de **um módulo só** (fisicamente são) e montar o arranjo
    **fisicamente no esquemático** — 17 instâncias de `core/Photovoltaic Panel` em série (string) ×
    2 strings em paralelo, 34 blocos ao todo, todos carregando o mesmo `.ipvx` de módulo único.
    Implementado e funcionando em `VR-BESS-LIBS-COMPARE.tse` (`Photovoltaic Panel1`...`Panel34`;
    fiação conferida: `Panel1→...→Panel17` e `Panel18→...→Panel34` são as duas strings, unidas em
    paralelo nos dois extremos via `Junction56`/`Junction57`).
  - **Achado confuso, não é bug real:** o arquivo `ayaz_pv_array_17s2p.ipvx` (nome sugere arranjo
    inteiro) foi reescrito ao vivo — provavelmente pela própria ferramenta de fit de painel do
    Schematic Editor — com valores de **módulo único** (Voc_ref=21,4V, Nc=36), mas com
    `dIsc_dT`/`dV_dI_ref`/`Vg` diferentes (mais refinados, presumivelmente de um fit real feito na
    UI) dos que estão em `ayaz_pv_module.ipvx`/`gerar_pv_array.py` (que geram a versão
    ARRAY-escalada, abordagem abandonada). **`ayaz_pv_array_17s2p.ipvx` é o que está efetivamente em
    uso pelos 34 blocos hoje e deve ser tratado como fonte de verdade atual** — `Vg` nele aparece
    como `1.12` (eV, o valor numérico de `"cSi"` já resolvido) em vez da string `"cSi"`. Renomeado
    pra `typhoonsim/modelo_painel_unicel.ipvx` (os 34 `filename_init` foram atualizados junto).
    `ayaz_pv_module.ipvx` e `gerar_pv_array.py` (raiz do repo, versão antiga array-escalada) estão
    desatualizados/não usados — podem ser removidos numa limpeza futura.

- **A janela `Dii - Di` do PWM precisa ser recalibrada em conjunto com `Di`, não isoladamente,
  senão a bateria para de carregar mesmo com `Vo` correto.** Achado nesta sessão com números reais
  medidos em `VR-BESS-LIBS-COMPARE.tse`. Por construção `Dii - Di ≈ Vbat/Vo` (ver "Valores de
  projeto" acima) é a heurística de partida, mas **não é um preditor exato** pra essa topologia —
  uma primeira tentativa (`Di=0,28`/`Dii` parado em 0,88, janela 0,60, tensão média estimada
  ≈240V < 252V) foi descartada por essa conta dar errado, mas o valor final que funcionou
  (`Dii=0,9`) ficou com janela 0,558 — **menor** que a tentativa descartada, e mesmo assim carrega.
  Ou seja, a aproximação volt-segundo simples (`(Dii-Di)×Vo`) serve só de direção inicial; o ponto de
  operação real depende de `Lbat`/`Cbat`/dinâmica de carga, então **validar sempre por simulação
  real**, não só pela fórmula.
  - **Valores confirmados funcionando (Scope real, `t≈0,067s`, regime permanente):** `Di=0,342`,
    `Dii=0,9`. Medido: `V_bat=252,71V`, `V_out=396,77V`, `V_pv=292,42V`, `I_bat=-2,40A` (negativo =
    carregando, pela convenção de sinal de `I_bat` — ver "Convenções e armadilhas"), `I_out=2,48A`,
    `I_pv=5,40A`, `SOC_mes=50,01%` (partiu de 50%, subindo). Bateria carregando confirmado.
  - Esses valores (`Di=0,342`/`Dii=0,9`) são específicos da topologia de painéis nativos (34 blocos)
    de `VR-BESS-LIBS-COMPARE.tse` — **não confundir com os duty cycles do `VR-BESS.tse` original
    (`.tlib`), que continuam Di=21%/Dii=84,5%** (ver "Valores de projeto"); são calibrações
    empíricas independentes porque a física de perdas dos dois modelos de painel/bateria é
    diferente.

- Parâmetros do `Battery1` nativo (`core/Battery`, `battery_type="User defined"`) em
  `VR-BESS-LIBS-COMPARE.tse`: `nominal_voltage=252`, `capacity=2.3` (Ah — consistente com os limites
  de corrente do projeto, 0,5C=1,15A/2C=4,6A), `initial_soc=50`, `R_series=0.15`, `Kdisc_I=100`,
  `Ke_exp=103`, `Ke_full=113.1`, `Kq_exp=4.91`, `Kq_nom=50`, `execution_rate=0.0001`.

## Bug do SOC=100% (`modelo_bateria`) — verificado resolvido em 12/10/2026

Nota antiga descrevia: com parâmetros estimados de forma da curva LiFePO4 (E0_1=13,32, A_1=1,4),
na carga cheia (`it_c=0`) o modelo calculava `E = E0 + A = 14,72 V`, excedendo `V_max=14,6 V` e
disparando o ramo `elseif V > V_max: S=0`, que bloqueava carga **e** descarga (o ramo
`SOC>=100 && I<0`, que deveria bloquear só a carga, nunca era alcançado). **Conferido contra o
`Bat_array.tlib` atual (linhas 93-99 de `modelo_bateria`): os parâmetros em uso hoje são os do
Metodologia (`E0_1=16,8`, `A_1=0,26422`, `V_max_1=19,0`), não os `13,32`/`1,4` que a nota antiga
descrevia** — com esses valores, `E0+A=17,064 < V_max=19,0` em qualquer `Ns_bat`, então a condição
nunca dispara. O bug parece ter sido resolvido de carona em algum commit que atualizou os
parâmetros da bateria para os valores do artigo, sem atualizar esta nota. Nenhuma mudança de código
foi necessária nesta verificação.

## Instalação/ambiente do Typhoon HIL (Linux) — ver detalhes e comandos completos no README

**Dois produtos Typhoon distintos instalados, não confundir:**

- **TyphoonSim** (o simulador em si) — voltado de propósito pra **2026.2** em 20/09/2026, pra
  contornar o bug do `core/C function` da 2026.3 (ver abaixo). Instalado em
  `/opt/typhoon/typhoonsim_2026.2/`, **sem atalho de desktop** (reinstalação manual via `.run`, sem
  `.desktop` gerado) — abrir com `QT_QPA_PLATFORM=xcb /opt/typhoon/typhoonsim_2026.2/bin/typhoon_hil.sh`.
- **Typhoon HIL Control Center** (app de gestão/licença, produto separado) — ficou na **2026.3**,
  em `/opt/typhoon/typhoon_hil_control_center_2026.3/`, com atalho de desktop (mas o `Name=` do
  `.desktop` saiu vazio — bug do instalador da Typhoon, variável sem aspas na linha `app_name=Typhoon
  HIL Control Center`, cosmético, não corrigido). Abrir com
  `QT_QPA_PLATFORM=xcb /opt/typhoon/typhoon_hil_control_center_2026.3/typhoon_hil.exe`.

Se essas versões mudarem de novo, atualizar também os caminhos de log/cache citados neste arquivo,
que têm o número da versão no path, ex. `~/.local/share/typhoon/TySim 2026.3/...`.

Instalado via instalador oficial (`sudo bash typhoon_hil_control_center_<versão>.sh --
--accept_license_agreement`), licença ativada com `typhoon_hil_activation --activate
--activation-key <arquivo>.lic`. Problemas conhecidos — **reaparecem a cada instalação/reinstalação
nova**, já se repetiram de forma independente nas instalações da 2026.2 e da 2026.3 do Control
Center nesta mesma sessão, então checar de novo sempre que rodar o instalador:

- Permissão de `~/.cache/typhoon` (ficou do root por ter rodado como sudo na primeira vez).
- Tela em branco no Wayland — `QT_QPA_PLATFORM=xcb typhoon_hil.sh`.
- `libreadline.so.8` empacotada pela Typhoon incompatível com esse sistema rolling-release — sem
  isso o `typhoon_hil.exe` crasha com `signal 6/ABRT` ao iniciar. Fix: `sudo mv
  libreadline.so.8 libreadline.so.8.orig && sudo ln -s /usr/lib/libreadline.so.8 libreadline.so.8`
  dentro do diretório de instalação.
- **`linux_install_script.sh` (embutido em todo instalador `.run` da Typhoon) não reconhece
  CachyOS** (`ID=cachyos` em `/etc/os-release`) — o `case $distro` interno só cobre
  `centos|rhel|fedora` e `debian|ubuntu`; qualquer outra distro cai no `*) echo Not supported Linux
  distribution. Stopping installation. exit -1`, abortando ANTES de criar regras udev/PATH/atalho de
  desktop — mas DEPOIS de copiar os arquivos pro diretório alvo (a cópia não se perde, só falta o
  resto). Fix: extrair sem rodar (`bash <instalador>.run --noprogress --nox11 --target <dir>
  --noexec`), editar `linux_install_script.sh` acrescentando um ramo `arch|cachyos|manjaro|
  endeavouros)` antes do `*)`, usando só `TAG+="uaccess"` nas regras udev (sem `GROUP=`, dispensa
  grupo — ACL moderna via udev/logind) e `pacman -S --needed` pros pacotes de dependência; depois
  rodar `sudo bash linux_install_script.sh --accept_license_agreement` de dentro do diretório
  extraído. Confirmado funcionando em 20/09/2026 (criou `/etc/udev/rules.d/50-arch-typhoon-hil.rules`,
  PATH em `/etc/bash.bashrc`, `.desktop` em `/usr/share/applications/`). Patch completo (diff do
  script) salvo na memória do Claude Code deste projeto — reaplicar em qualquer instalador `.run`
  futuro da Typhoon nesta máquina.

**Bug conhecido, não resolvido, na 2026.3 (TyphoonSim especificamente — é por isso que o TyphoonSim
está voltado pra 2026.2 acima):** qualquer modelo `.tse` com um componente `core/C function` falha ao
compilar no TyphoonSim offline com `Unable to prepare signal processing code! Please, contact Typhoon
HIL.` — confirmado que não é cache, não é migração de arquivo antigo, não é o compilador C do sistema
(nem chega a ser invocado). Repro mínimo em `sp_codegen_test.tse` na raiz do repo. Sem workaround
encontrado além do downgrade pra 2026.2 (já aplicado). Isso bloqueia qualquer simulação em malha
fechada na 2026.3 (o controlador FCS-MPC vai precisar de `core/C function` ou equivalente) até
resolver — na 2026.2 esse bug não existe, então o downgrade também desbloqueia isso, não só o
`core/C function` dos `.tlib` atuais. Detalhes da investigação (incluindo uma tentativa anterior
inconclusiva de mexer no `cc1`/`gcc` do sistema, já desfeita por update do pacote) na memória do
Claude Code deste projeto, não repetidos aqui pra não inflar este arquivo.

## Status

- MATLAB/Simscape: modelo validado em malha aberta.
- Typhoon HIL: modelo completo VR-BESS (2 chaves, PV, bateria) simulando em malha aberta com
  tensões/correntes próximas do projeto (Vbat≈252V, Vout≈400V, Vpv com ~2-9% de erro dependendo
  da rodada — ver `git log` para o histórico de correções). Chaveamento PWM sincronizado
  corretamente (`core/Initial Settings` + `vhil_adio_loopback`), modelo do painel PV corrigido
  (bug de `Ns_mod`/`T` trocados), SOC visível no schematic principal, duty cycles recalibrados
  empiricamente para as perdas reais do Typhoon (ver "Valores de projeto" acima).
- **Atenção, estado atual não é o final:**
  - **Proteção da bateria (sinal `S` de `modelo_bateria`) e limitador de corrente (`GainKi` em
    `Bat_array.tlib`) estão DESLIGADOS** (`MOSFET1.ctrl_in` fixo numa constante, `GainKi=0`) —
    feito para isolar o comportamento do conversor/painel durante a recalibração dos duty cycles.
    **Decisão (12/10/2026, planejamento do FCS-MPC): não religar por enquanto.** O controlador vai
    assumir os limites de corrente da bateria via saturação da referência `i_Lbat*` nos mesmos
    limiares de projeto (0,5C carga / 2C descarga — ver roteiro do FCS-MPC abaixo), não via o
    resistor série antigo. Reavaliar se isso se mostrar insuficiente durante a validação.
  - `Cbat`/`Cvo`/`Cpv` têm ficado sem `initial_voltage` definido (partindo de 0V) mais de uma vez
    nesta sessão, aparentemente revertido sozinho por algum salvamento da UI do TySim — causa
    inrush de energização grande. Vale conferir se ainda está setado (`initial_voltage = "252"`
    em `Cbat`, `"400"` em `Cvo`, `"346"` em `Cpv`) antes de rodar simulações longas/sensíveis.
  - Bug do SOC=100% em `modelo_bateria`: verificado resolvido (ver seção acima).
- **Ambiente Typhoon (20/09/2026):** TyphoonSim rodando na **2026.2** (downgrade intencional pra
  contornar o bug do `core/C function` da 2026.3), Typhoon HIL Control Center instalado à parte na
  2026.3 (produto diferente, ver "Instalação/ambiente" acima).
- **`VR-BESS-LIBS-COMPARE.tse` (alternativa com componentes nativos, sem `core/C function`):** PV
  reconstruído como 34 blocos `core/Photovoltaic Panel` (17 série × 2 paralelo,
  `typhoonsim/modelo_painel_unicel.ipvx`) + `Battery1` nativo (`core/Battery`), no lugar dos
  `.tlib`. **Duty cycles calibrados e confirmados funcionando por medição real**: `Di=0,342`,
  `Dii=0,9` → `V_out≈397V`, `V_bat≈252,7V`, bateria carregando (`I_bat≈-2,4A`) — ver seção "Modelo
  nativo" acima pro resto dos sinais medidos. Ainda não comparado formalmente contra o `VR-BESS.tse`
  original (curvas/dinâmica lado a lado) nem religadas proteção/limitador.
- **Controlador FCS-MPC: implementado e FUNCIONANDO (12/10/2026)** — substitui definitivamente o
  PWM fixo em `VR-BESS.tse`. Regula `V_out≈400V` em regime permanente (testado com `SOC=50%` e
  `SOC=100%`) e é robusto a variação de irradiância (`G` de `0` a `1400W/m²`, incluindo valores
  fixos intermediários). Teste de variação de temperatura em andamento. Jornada de depuração
  completa (bugs encontrados/corrigidos, decisões de projeto e seus porquês) documentada na seção
  "Roteiro: controlador FCS-MPC" abaixo — resumo rápido logo no início dela.
- KiCad: não iniciado.

## Roteiro: controlador FCS-MPC (planejado em 12/10/2026)

### Resumo da implementação final (FUNCIONANDO, 12/10/2026)

- **Conjunto finito de controle: 3 estados** `(S1,S2)∈{(1,1),(1,0),(0,1)}` — `(0,0)` foi removido
  por ser estruturalmente redundante (suas equações sempre coincidem exatamente com `(1,1)` ou
  `(1,0)`, dependendo do sinal de `i_Lbat` no momento — nunca é uma opção genuinamente distinta).
- **Horizonte de predição: 2 passos** (`k+2`), com `static real_t S1_prev/S2_prev` guardando o
  estado aplicado no ciclo anterior pra compensar o atraso de cálculo.
- **Referências:** `i_Ls* = sat((Po_ref+Pbat_target)/V_pv_medido, 0, I_MP(G))` — teto de MPP escala
  linearmente com `G` medido (`I_MP(G)=I_MP,STC×G/1000`); `i_Lbat* = sat((Po_ref−P_pv_medido)/
  V_bat_medido, −0,5C, +2C)`. As duas com limitador de taxa (slew-rate, `0,02A`/ciclo) aplicado
  **antes** do teto físico (teto sempre vence por último, nunca atrasa uma queda de potência).
- **Função custo:** `g = (erro_i_Ls)²/I_MP,STC + (erro_i_Lbat)²/I_limite` — normalização **linear**
  (não quadrática), fiel à Eq. 3.12/3.13 de `braga2022controle` (dissertação da Isadora).
- **Convenção de sinal:** as equações diferenciais usam `i_Lbat` positivo = carregando (convenção
  dos artigos/`old.tex`), mas o sensor real do Typhoon usa positivo = descarregando (convenção do
  projeto) — **as duas são opostas**, e as equações precisam da conversão de sinal entre elas
  (achado tardio, custou bastante tempo de depuração).
- **`Cvo.capacitance` aumentado de `1,41µF` pra `~400µF`** (valor comercial final entre `390-470µF`
  ainda a confirmar) — causa raiz do ripple excessivo de `V_out` em irradiância intermediária. O
  `Co` original foi dimensionado pro PWM de frequência fixa (malha aberta); o FCS-MPC tem
  frequência de chaveamento variável e precisa de capacitor de saída maior. Validado comparando com
  `paper_4440` (ver abaixo), mesmo valor usado lá (`400µF`) resolveu aqui também.
- Fontes usadas: `old.tex` (derivação base, valores de outro trabalho), `braga2022controle`
  (Isadora — função custo normalizada), **`paper_4440.pdf`** (`~/Documentos/Artigos` —
  "Controle Preditivo Baseado em Modelo para Conversores de 3 Portas: o Sistema VR-BESS", Braga,
  Carvalho, **Ferreira [Silvia, orientadora deste projeto]**, Pacheco, Pereira — aplicação de
  FCS-MPC no VR-BESS pelo mesmo grupo de pesquisa, usada pra validar o dimensionamento de `Co`).

Detalhamento completo da jornada de depuração (tentativas que não funcionaram, hipóteses
descartadas, achados do Typhoon) abaixo — útil pra entender o "porquê" de cada decisão, mas não
precisa ler tudo pra saber o que está rodando hoje.

Decisões tomadas em sessão de planejamento com o usuário, antes de qualquer implementação. Cada
etapa trava a próxima — manter esta lista atualizada (marcar `[x]`) conforme avança. Pesquisa de
apoio: `Latex/secoes/old.tex` (rascunho fora do documento ativo, `\input` não o referencia) contém
uma derivação prévia de modelo unificado discreto e algoritmo FCS-MPC para o VR-BESS, mas com
valores de componentes de outro trabalho (`Ls=3,5mH`, `Co=400µF`, `Vin=300V` — não os deste
projeto) e citando `\cite{braga2022controle}`/`marcello22019modelagem`/`delay`, nenhuma dessas
chaves existe ainda em `Latex/ifacconf.bib`. `braga2022controle` é a dissertação de **Isadora**
Della-Sávia Braga (UFLA, 2022, "Controle preditivo baseado em modelo com conjunto de controle
finito aplicado a conversores CC-CC" — em `~/Documentos/Artigos`), usada no rascunho para justificar
controle indireto de corrente em vez de controle direto de tensão (o VR-BESS tem fase não mínima
pra controle direto de tensão, igual ao boost que ela estudou). A estrutura/método do `old.tex`
(tabela lógica S1/S2/λ unificando os modos em uma única matriz de estados, função custo quadrática
de correntes, horizonte de 2 passos) é reaproveitável; os valores numéricos não são — precisam ser
recalculados com os parâmetros reais deste projeto (ver "Valores de projeto" acima).

- [x] **Plataforma base: `VR-BESS.tse` (`.tlib`)** — não `VR-BESS-LIBS-COMPARE.tse` (nativo, menos
  maduro/validado).
- [x] **Proteção/limitador da bateria:** decidido não religar por enquanto (ver "Status" acima) —
  o FCS-MPC assume os limites via saturação de `i_Lbat*`.
- [x] **Bug do SOC=100%:** verificado resolvido, nenhuma correção necessária (ver seção acima).
- [x] **Modelo discreto do conversor** (derivado e verificado em 12/10/2026 — ver cálculo abaixo):
  - Escopo: **4 estados completos de chaveamento** (S1,S2)∈{0,1}² desde já — inclui o caminho de
    descarga da bateria ((S1,S2)=(0,1), Etapa 5 do `old.tex`, e (0,0) com λ=1, Etapa 4), nunca
    testado em malha fechada neste projeto.
  - `Ts = 1µs` (10× `f_sw=100kHz` de projeto). Horizonte de predição: **reduzido pra 1 passo (k+1)
    em 12/10/2026**, abandonando a compensação de atraso de 2 passos (k+2) — depois de `V_out`
    estabilizar em ~450-456V em vez de 400V mesmo com `i_Lbat*` calculado corretamente pedindo
    carga (`-1,15A`) e a função custo já normalizada/priorizando a bateria, suspeitei de duas
    causas ligadas à memória `S1(k-1)`/`S2(k-1)`: (a) **não confirmado que `static` realmente
    persiste entre chamadas** no `core/C function` deste Typhoon (nenhum bloco deste projeto usava
    isso antes), e (b) o candidato `(0,0)` muda de comportamento (`λ`) quando `i_Lbat` cruza zero,
    possivelmente criando um "teto" perto de zero. O horizonte de 1 passo prediz direto de `x(k)`
    medido pra `x(k+1)` pros 4 candidatos, **sem nenhuma variável `static`/memória entre
    chamadas** — elimina essa fonte de incerteza por completo pra isolar se o problema era mesmo
    aí. Mais simples de depurar; perde só a compensação fina do atraso de cálculo (razoável dado
    `Ts=1µs` já ser bem mais rápido que a dinâmica dominante do circuito, `f0≈2,15kHz`).
    **Resultado: `V_out` continuou travado em ~450-456V mesmo sem nenhum `static`/memória** — isso
    **descarta a hipótese (a)** (persistência de `static`) como causa raiz, já que removê-la por
    completo não mudou o sintoma. Confirmado separadamente com um bloco de teste isolado (somatório
    incremental, `static real_t sum_test=0.0; sum_test=sum_test+1.0; accum=sum_test;`): **`static`
    funciona normalmente** neste `core/C function` do Typhoon — `accum` subiu em rampa como
    esperado. **Achado novo de armadilha do editor:** a primeira tentativa desse teste deu
    `SyntaxError`/`undeclared identifier 'accum'` mesmo com o terminal de saída `accum` escrito
    certo no código — porque **declarar `output_terminals`/`input_terminals` só dentro do texto do
    `output_fnc` não basta; existe uma aba/seção separada no editor do bloco pra cadastrar cada
    terminal (nome+tipo) como pino de fato**, e só depois disso o compilador declara a global
    correspondente. Sem isso, o nome fica "undeclared" mesmo usado corretamente no corpo do código.
    Sobra a hipótese (b) (`λ` no candidato `(0,0)`) e outras ainda não identificadas — próxima
    suspeita: o modelo assume condução contínua (CCM) em `Lbat`, mas `kai2024projeto` (seção 2.2)
    já registra que o Modo 2 do VR-BESS opera com `Lbat` em condução **descontínua** — se `i_Lbat`
    oscila perto de zero (como observado, entre -0,5A e +0,56A), pode estar cruzando pro regime DCM
    onde as equações de Etapa 1-5 (todas derivadas assumindo CCM) deixam de valer, explicando por
    que o controlador não consegue sustentar corrente negativa (carga) mesmo pedindo. Investigação
    continua. **Achado novo (confirmado no Scope):** `S1_opt` e `S2_opt` são **sempre idênticos**
    (conferido pulso a pulso) — o controlador só alterna entre `(1,1)` e `(0,0)`, nunca escolhe
    `(1,0)` nem `(0,1)`. Pelas contas feitas à mão (ver histórico da conversa), `(0,1)` deveria
    vencer `(0,0)` com margem clara sempre que `i_Lbat<0` (não é empate apertado) — se isso nunca
    acontece na prática, há uma divergência entre a lógica pretendida e o que está rodando de fato.
    Não dá mais pra diagnosticar só por fora (`S1_opt`/`S2_opt`/Scope) — próximo passo:
    instrumentar o bloco com saídas de depuração (`dbg_ILs_ref`, `dbg_ILbat_ref`, `dbg_cost11`,
    `dbg_cost10`, `dbg_cost01`, `dbg_cost00`) pra ver os valores reais de cada candidato e achar a
    divergência. Resultado ainda não registrado.
  - Estado `x=[i_Lbat, i_Ls, v_Co]`, entrada `u=[v_pv, v_bat]` (`v_pv` é o `v_in` do `old.tex`,
    já que `Ls` é alimentado direto pelo painel):
    `A(γ,m) = [[0,0,γ/Lbat],[0,0,-m/Ls],[-γ/Co,m/Co,-1/(Ro·Co)]]`,
    `B = [[0,-1/Lbat],[1/Ls,0],[0,0]]` (B não depende do estado de chaveamento),
    `γ=S1(1-S2)+λ(1-S1)(1-S2)`, `m=1-S1·S2`.
  - **Achado estrutural (vale pra essa topologia independente dos valores de L/C — conferir se
    quiser, mas é genérico):** só existem 3 combinações distintas de `(γ,m)`, não 4: `(1,1)→(0,0)`;
    `(1,0)` e `(0,0,λ=1)→(1,1)`; `(0,1)` e `(0,0,λ=0)→(0,1)`. Ou seja, o comando `(S1,S2)=(0,0)`
    **nunca produz uma dinâmica predita diferente de uma das outras duas chaves fechadas** — empata
    com `(1,0)` quando `i_Lbat<0` e com `(0,1)` quando `i_Lbat>0`. Útil como critério de desempate:
    preferir `(0,0)` quando empatar (nenhuma chave conduz corrente → menor perda de condução; o
    modelo ideal não vê essa diferença, mas fisicamente ela existe).
  - Com `Ls=3,89mH`, `Lbat=4,23mH`, `Co=1,41µF`, `Ro=160Ω`, `Ts=1µs` (`Ad=I+A·Ts`):
    - `(γ,m)=(0,0)`: `Ad=diag(1,1,0,99557)` (i_Lbat e i_Ls desacoplados de v_Co).
    - `(γ,m)=(1,1)`: `Ad=[[1,0,2,364e-4],[0,1,-2,571e-4],[-0,7092,0,7092,0,99557]]`.
    - `(γ,m)=(0,1)`: `Ad=[[1,0,0],[0,1,-2,571e-4],[0,0,7092,0,99557]]`.
    - `Bd=B·Ts=[[0,-2,364e-4],[2,571e-4,0],[0,0]]`.
  - Validação: autovalores de `A` no máx. `|·|≈18700` rad/s → `|eig·Ts|≈0,019≪1` (Euler-forward
    seguro); `f0(Ls-Co)≈2,15kHz≪1/Ts=1MHz` (465× de sobreamostragem); ripple de `i_Lbat` por
    período fecha em ~0 quando avaliado com os duty cycles ideais do MATLAB (Di=13,77%/Dii=76,77%)
    — consistente com regime periódico esperado.
- [x] **Função custo e referências** (finalizado em 12/10/2026, revisado no mesmo dia após teste):
  - **Revisão (12/10/2026):** a primeira versão fixava `i_Ls*` no MPP cheio do arranjo
    (`Np_mod×I_MP,1=5,92A`), sem checar se a demanda (carga+bateria) comportava isso. Testado no
    Typhoon: `V_out` divergiu pra ~568V médio (picos de 709V) em vez de 400V — `P_pv≈1833W` no MPP
    excede `Po_ref(1000W)+P_bat_max(~290W a 0,5C)` em ~550W sem nenhum lugar pra ir (função custo só
    tem corrente, não tensão). **Não é fiel às referências do projeto:** nem `braga2022controle`
    (Eq. 3.8/3.9 — corrente de referência amarrada à tensão de referência via balanço de
    potência/Ohm, nunca um valor fixo desconectado da demanda) nem `kai2024projeto` (seção 2.2,
    Modo 1 definido por `Ps=Pbat+Po`, onde `Ps` é o que o duty cycle demanda — não é forçado no
    MPP; o ponto autoconsistente do Metodologia mesmo dá `I_s≈3,97A`, abaixo do MPP de 5,92A) fazem
    isso. Corrigido pra amarrar `i_Ls*` à demanda, com o MPP só como teto:
    `P_bat_target=Ibat_chg_max×V_bat_mes`; `i_Ls*=sat((Po_ref+P_bat_target)/V_pv_mes, 0, I_MP_array)`.
  - `i_Lbat* = sat((Po_ref − V_pv_mes×I_pv_mes)/V_bat_mes, −1,15A, +4,6A)`, `Po_ref=Vo_ref²/Ro=
    400²/160=1000W` — usa `V-pv`/`I_pv` (sensores já existentes em `VR-BESS.tse`) medidos em tempo
    real, não `P_max` de datasheet assumido. O sinal de `i_Lbat*` decide carga vs. descarga
    naturalmente. **Atenção à convenção de sinal (positivo=descarga, negativo=carga, já
    documentada no projeto): o limite de carga (0,5C=1,15A, menor) limita o lado NEGATIVO, o
    limite de descarga (2C=4,6A, maior) limita o lado POSITIVO** — na primeira versão do código
    (12/10/2026) saiu invertido por engano (`sat(...,−4,6,+1,15)`), causando a bateria descarregar
    em vez de carregar quando `i_Lbat*` calculado excedia o limite; corrigido após observar `SOC`
    caindo no Scope em vez de subir. Corrigido no código abaixo.
  - `g = [i_Ls*−i_Ls(k+2)]² + [i_Lbat*−i_Lbat(k+2)]²`, pesos iguais (`w1=w2=1`) — **revisado
    12/10/2026** depois de testar: com `Vo` em ~450V, os únicos estados com `γ=1` (carregam a
    bateria) também forçam `m=1` (acoplam `i_Ls` a `Vo`), fazendo `i_Ls` variar muito a cada escolha
    de chave nessa faixa de tensão. Com pesos iguais em Ampère absoluto, o erro de `i_Ls` dominava a
    função custo e o controlador evitava carregar a bateria (medido `I_bat≈+0,04A` com
    `i_Lbat*≈-1,15A` — quase sem efeito), sobrando energia na carga resistiva e mantendo `Vo` alto
    (ciclo que se realimenta). Corrigido normalizando cada termo pela própria escala de referência —
    mesma ideia dos ganhos `ki=1/i*`, `kv=1/Vc*` de `braga2022controle` (Eq. 3.12/3.13), usados lá
    exatamente pra esse desbalanço de escala.
  - **Revisão 2 (12/10/2026):** a primeira normalização usou `wLs=1/I_Ls,mpp²`,
    `wLbat=1/I_bat,chg_max²` (quadrática) — peso relativo da bateria ficava **~26,5×** maior que o
    do painel. Isso resolveu o caso de `Vo` muito alto, mas testado de novo partindo dos
    `initial_voltage` corretos (`Cbat=252`,`Cvo=400`,`Cpv=346`), `cost01`/`cost11`/`cost00` ainda
    empatavam entre si em regime permanente (termo de `i_Ls` praticamente sem efeito, dominado pelo
    peso grande demais do termo de bateria) e `SOC_mes` não progredia. **Corrigido pra normalização
    linear, fiel à fórmula original de `braga2022controle`** (`ki=1/i*`, não `1/i*²`, multiplicando
    o termo já elevado ao quadrado): `wLs=1/I_Ls,mpp`, `wLbat=1/I_bat,chg_max` — peso relativo cai
    pra **~5,15×**, bem menos agressivo. Conferido por conta à mão no ponto de operação validado
    (`Vo=400V`): `cost10`/`cost00` continuam vencendo `cost01` por margem boa (~3×), preservando o
    uso da Etapa2 que o PWM original precisa. Testado — mesmo sintoma (`SOC` ainda caindo).
  - **Causa raiz encontrada (12/10/2026), com números reais de um instante capturado:**
    `S1_opt` tinha média `0,248` numa janela de regime "permanente" — ou seja, o candidato `(0,0)`
    (não `(1,1)`) dominava `~75%` do tempo. Esse é exatamente o candidato cujo `γ` depende do sinal
    de `i_Lbat` (`λ`) — assim que a bateria cruza pra carga (`i_Lbat<0`), `(0,0)` vira
    `γ=1` (descarga-favorável) e empurra a corrente de volta, cancelando o progresso. Como o
    `argmin` sempre parte de `best=cost00` e só troca se outro candidato for *estritamente* menor,
    `(0,0)` vence a maioria dos empates por construção, reforçando a armadilha. Confirmado com
    números reais: `cost00≈cost01` empatados quase exatos, `cost11` só `0,0000017` abaixo — um
    empate de ponta de faca que o `argmin` resolve a favor de quem foi checado primeiro, não do que
    é fisicamente melhor.
  - **Tentativa descartada: remover `(0,0)` da busca.** Testado (v10) e também com `(0,0)` mantido
    mas como última prioridade no desempate (v11) — **as duas versões divergiram do mesmo jeito**
    (`i_Lbat` medido subindo a ~50.000A/s até milhares de Ampère, custos na casa de `10⁷`). Ou seja,
    a presença/prioridade de `(0,0)` não era a causa raiz — descartar essa hipótese.
  - **Causa real encontrada (12/10/2026, apontada numa segunda lista de revisão externa, só esse
    ponto dela se confirmou): a predição de 2 passos (reintroduzida a pedido numa rodada anterior)
    usava `Vo_m` (valor bruto medido) tanto no passo `k→k+1` quanto no `k+1→k+2`, sem recalcular
    `Vo_k1` entre eles** — regressão em relação à primeira versão do roteiro (que calculava
    `Vo_k1` corretamente). Com `Co=1,41µF` (constante de tempo `Ro·Co≈225µs`, da mesma ordem que
    os `2µs` do horizonte de predição), usar `Vo` desatualizado no segundo passo descola a predição
    da física real. **Os outros 3 pontos dessa mesma lista de revisão estavam errados** — um
    propunha inverter o sinal de `di_Lbat/dt` (contradiz as equações do `old.tex` lidas direto, já
    verificado 3x) e outro propunha voltar a normalização quadrática (contradiz a fórmula literal
    `ki=1/i*` de `braga2022controle`, Eq. 3.12/3.13) — nenhum dos dois foi aplicado.
  - Corrigido recalculando `Vo_k1` explicitamente entre os dois passos (igual à primeira versão do
    roteiro) e usando `Vo_k1` (não `Vo_m`) nas 4 predições de `k+1→k+2`. `(0,0)` mantido na busca
    com prioridade mais baixa (v11). Testado — mesma divergência (`i_Lbat` subindo a ~50.000A/s até
    milhares de Ampère, custos em `10⁷`), então também não era a causa raiz.
  - **Causa raiz real encontrada (12/10/2026): convenção de sinal de `i_Lbat` invertida entre a
    fonte das equações e o sinal medido real.** O `old.tex` declara explicitamente (linha 85):
    "o sentido da corrente i_Lbat que é considerado positivo é aquele que carrega a bateria" —
    **positivo=carregando** no `old.tex`. Mas o projeto real (Metodologia, CLAUDE.md, confirmado no
    sensor `I_bat` do Typhoon) usa a convenção oposta, **positivo=descarregando**. As equações
    diferenciais foram copiadas certas do `old.tex`, mas aplicadas direto ao sinal medido do
    Typhoon **sem converter a convenção** — equações e sensor estavam em sistemas de sinal opostos
    o tempo todo. Isso bateu com a "contradição física" observada: `S1_opt=S2_opt=1` (Etapa1)
    deveria fazer `i_Lbat` cair (`γ=0`, só `-Vbat/Lbat`), mas estava subindo — porque o sinal real
    da equação, na convenção certa, é outro. Também explica por que o teste de PWM fixo tinha
    funcionado (ele nunca lê `I_bat` pra decidir nada — é sequência aberta, nunca exercita a lógica
    de `γ`/`λ` dependente de sinal, só essa lógica estava com o bug).
    Corrigido: invertido o sinal do termo `γ` nas equações de `i_Lbat` (`Vbat/Lbat - γ·Vo/Lbat` em
    vez de `γ·Vo/Lbat - Vbat/Lbat`) e de `v_Co` (`+γ/Co·i_Lbat` em vez de `-γ/Co·i_Lbat`), e
    invertida a condição de `λ` no candidato `(0,0)` (`ILbat_m≥0 → γ=1`, não `γ=0`). A referência
    `i_Lbat*` (balanço de potência) não precisou de correção — já usava a convenção certa do
    projeto (Metodologia), só as equações físicas copiadas do `old.tex` estavam na convenção
    errada. Resultado desse teste ainda não registrado.
- [x] **Implementação no Typhoon — FUNCIONANDO (confirmado 12/10/2026).** Testado partindo de
  `SOC=50%` e de `SOC=100%`: nos dois casos `V_out` se mantém em `400V`. Jornada de depuração longa
  (ver achados abaixo) — causa raiz final foi convenção de sinal de `i_Lbat` invertida entre as
  equações (copiadas do `old.tex`, que usa positivo=carregando) e o sensor real do Typhoon
  (positivo=descarregando, convenção do projeto). Código final funcionando guardado no histórico da
  conversa de 12/10/2026 (mensagem com `ERROR: (SyntaxError(137:28...` seguida da correção —
  busque "Vbat_m/Lbat - gamma_c/Lbat*Vo_k1" pra achar a versão certa se precisar recuperar depois).
  Detalhes do processo (código C abaixo escrito e verificado em 12/10/2026):
  - Um novo bloco `core/C function` (nome sugerido: `FCSMPC1`) **substitui definitivamente**
    `Constant9`/`Constant10` + `PWM_Mod_S1`/`PWM_Mod_S2` — sem fallback comutável pro PWM fixo
    (decisão do usuário: substituir, não manter os dois caminhos).
  - Entradas (`input_terminals`): `real V_pv;real V_bat;real V_o;real I_pv;real I_bat;` —
    reaproveita os sensores já existentes no esquemático principal (`V-pv`, `V_bat`, `V_out`,
    `I_pv`, `I_bat`) — sem sensores novos. (Terminal batizado `V_o`, não `V_out`, só por cautela
    com a armadilha de nomes/sufixos de terminal já documentada acima — não é estritamente
    necessário, mas evita qualquer ambiguidade.)
  - Saídas (`output_terminals`): `real S1_opt;real S2_opt;` (0,0/1,0) — repontar
    `"Initial Settings1".digital_outputs` de `"..PWM_Mod_S1.TOP_1"`/`"..PWM_Mod_S2.TOP_1"` pros
    pinos do novo bloco, mantendo `DO1`/`DO2` e o caminho `Digital Input1`/`Digital Input2` →
    `MOSFET.ctrl_in` como está. **Não confirmado** se o mapeamento de `digital_outputs` aceita um
    sinal `real` direto do C function do mesmo jeito que aceita `PWM_Mod_S1.TOP_1` — se não aceitar,
    testar trocar `output_terminals` pra `uint`/`boolean`, ou inserir um bloco de comparação
    (`>0.5`) entre o C function e o `Initial Settings`.
  - Memória de `S1(k-1)`/`S2(k-1)` pro horizonte de 2 passos: **variáveis `static` declaradas
    dentro do próprio `output_fnc`** (não em `arb_defs`/`update_fnc`) — `static` local em C
    persiste entre chamadas da função sem depender do mecanismo `init_fnc`/`update_fnc` do Typhoon
    (que nenhum C function deste projeto usa até agora). Mais simples e não depende de confirmar
    nada sobre esse mecanismo — mas também não testado ainda neste projeto especificamente, validar
    ao compilar.
  - `execution_rate` do bloco e de todas as entradas/saídas precisa ser igual (`1e-6` explícito,
    não `"inherit"`) — ver armadilha de `execution_rate` já documentada acima.
  - **Achado (12/10/2026, primeira tentativa de colar no Typhoon):** o editor do `core/C function`
    **não aceita comentários de bloco `/* */` dentro do corpo** — causou `SyntaxError` (token
    inesperado após sumir pedaço de código em volta dos comentários no dump de erro). Consistente
    com o próprio `modelo_bateria` (`Bat_array.tlib`) não usar nenhum comentário `/* */` dentro do
    `output_fnc`. **Não usar comentários de bloco dentro de `output_fnc`/`init_fnc`/`update_fnc`
    neste editor** — só os marcadores fixos "Begin/End code section" do template, que já vêm
    prontos. Essa trava de "crash ao clicar em editar" era outra coisa (biblioteca `libgbm.so.1`
    empacotada incompatível, já resolvida com symlink pro `libgbm` do sistema — mesmo padrão do
    `libreadline.so.8` já documentado acima).
  - **Achado 2 (12/10/2026, segunda tentativa, já sem comentários):** o editor **corta/quebra
    linhas muito compridas do corpo**, derrubando o `;` final e causando `SyntaxError` — reproduzido
    numa linha de 90 caracteres (`Vo_k1 = Vo_m + Ts * ( -gamma_prev/Co * ILbat_m + m_prev/Co *
    ILs_m - Vo_m/(Ro*Co) );`), que ao submeter chegava sem o `;`. As demais linhas do arquivo,
    todas ≤67 caracteres, passaram ilesas. **Regra prática pra este editor: manter toda linha do
    corpo (`output_fnc`/`init_fnc`/`update_fnc`) com no máximo ~70 caracteres**, quebrando
    expressões longas em variáveis intermediárias (ex.: `dVo_dt1`) em vez de uma linha só. Limite
    exato não determinado (só sabemos que 90 quebra e 67 não) — se uma linha por volta de 70-85
    falhar de novo, encurtar mais e registrar aqui o novo limite observado.
  - Código C (sem comentários, linhas curtas) foi passado direto no chat, não duplicado aqui — ver
    histórico da conversa de 12/10/2026 pra recuperar se precisar.
  - **Hipótese testada e DESCARTADA (12/10/2026): `Play` compila sim.** Levantei a suspeita de que
    `Play` não recompilava sozinho (pra explicar resultados idênticos entre versões de código
    diferentes), mas afirmei isso sem nunca ter visto um botão "Compile" em nenhuma captura real —
    o usuário corretamente contestou. Confirmado pelo log de saída do próprio `Play`: "Your
    schematic model is being prepared for the compilation... Validation passed! ... Simulation
    begins." — ou seja, `Play` já inclui a compilação, não existe um botão separado de "Compile".
    **A causa real dos resultados idênticos entre versões continua em aberto** — cogitar: o código
    colado no editor pode não ter substituído a versão anterior por completo (sobrou texto antigo
    misturado), ou o Scope não estava mostrando a rodada certa (já visto antes nesta sessão — ver
    achados anteriores sobre a tabela de sinais ficar com leitura desatualizada até clicar no traço
    ou mover um cursor). Prática recomendada: sempre conferir um sinal de depuração que mudou de
    fórmula entre versões (ex.: `dbg_cost00`) antes de interpretar qualquer resultado como "não
    mudou nada" — e, se continuar idêntico mesmo com `Play` confirmadamente compilando, reabrir o
    editor do bloco e colar o código de novo do zero (selecionar tudo, apagar, colar) em vez de
    editar por cima do texto existente.
- [ ] **Validação** (ordem sugerida pelo usuário):
  1. [x] Regime permanente (STC fixo, `G=1000W/m²`, `T=25°C`, `Ro=160Ω`) — **confirmado 12/10/2026**,
     `V_out≈400V` tanto partindo de `SOC=50%` quanto de `SOC=100%`.
  2. [ ] Degrau de irradiância/temperatura — em andamento (12/10/2026). **Achado: sistema fica
     instável com `G≠1000W/m²`** — `ILs_mpp=5,92A` era fixo pra STC, não escalava com `G` medido,
     fazendo o controlador tentar puxar mais corrente do que o painel aguenta em irradiância menor
     (exatamente o risco já avisado antes do teste). Corrigido escalando `ILs_mpp` linearmente com
     `G` medido (`I_MP(G)≈I_MP,STC×G/1000`, aproximação padrão pra célula solar) — precisa de uma
     entrada nova no bloco, `G_m`, ligada no mesmo sinal `G_pv` que já alimenta `pv_array1.G`.
     Testado — melhorou mas **ainda instável especificamente durante o degrau** `1000→700W/m²`
     (não durante o regime permanente em `700`/`400` em si). `dbg_Vpv_m` chegou a oscilar entre
     `135V` e `372V` nesse exato instante. Causa provável: `ILs_ref_l=(Po_ref+Pbat_target)/Vpv_m`
     divide pelo `Vpv` medido na hora — se `Vpv` cai bruscamente durante o transiente do degrau, a
     referência dispara pra cima antes do teto `ILs_mpp` (escalado por `G`) conseguir segurar,
     possivelmente realimentando o próprio tombo de `Vpv` por um instante. Corrigido com limitador
     de taxa de variação (slew-rate) em `ILs_ref_l` e `ILbat_ref_l` — no máximo `0,02A` por ciclo
     (`Ts=1µs`, ~`20.000A/s`), suavizando qualquer degrau brusco em rampa em vez de salto
     instantâneo. Precisa de duas variáveis `static` novas (`ILs_ref_prev`, `ILbat_ref_prev`) pra
     guardar a referência do ciclo anterior. Testado — **piorou em `G=0`**: o teto `ILs_mpp` cai pra
     `0` instantaneamente (sem potência disponível), mas o limitador de taxa também segurava a
     **queda** da referência (só `0,02A`/ciclo), levando ~185µs pra chegar a zero — nesse meio tempo
     o controlador continuava pedindo corrente de um painel sem nenhuma potência disponível.
     Corrigido invertendo a ordem: **primeiro** o slew-rate (suaviza picos na subida), **depois** o
     teto físico `ILs_mpp` por cima como última etapa — assim o teto sempre vence imediatamente
     quando a potência despenca, e o slew-rate só amortece o resto. Testado — **ainda instável
     especificamente na queda de `G`** (`1000→700`), regime permanente em cada nível ficou bom.
     Comparado com `braga2022controle` Eq. 4.14 (`i*_Ls=(V*_C0²/Ro+V*_Cbat²/Rbat)/Vin`, testada por
     ela com degrau de `Vin` 200→250→300V, sem instabilidade catastrófica) — a fórmula é
     estruturalmente igual à minha (potência-alvo fixa / tensão de entrada medida), então o formato
     da equação não é o problema. Diferença provável: o `Vin` dela é fonte ideal (modelo Modo 1
     simplificado, sem limite de corrente); nosso `V_pv` vem de um painel PV real com curva I-V
     própria, que pode afundar de verdade se a demanda momentânea exceder o disponível — complicação
     que o modelo dela não tem. `Stair Generator` não tem parâmetro de rampa/transição (confirmado
     com o usuário) — degrau é sempre instantâneo. Corrigido suavizando a própria leitura de `G`
     dentro do bloco (mesmo limitador de taxa já usado nas referências, aplicado a `G_m` antes de
     calcular `ILs_mpp`) — simula transição de ~1ms em vez de instantânea (`1µs`), sem precisar
     mexer no esquemático. Resultado ainda não registrado.
     **Nota pra trabalhos futuros:** achado um artigo em `~/Documentos/Artigos`
     (`Adaptive_Active_Disturbance_Rejection_Control_for_Three-Port_DC-DC_Converters.pdf`, 2025,
     conversor de três portas) usando **ADRC com observador de estado estendido** — estima o
     distúrbio (degrau de carga/barramento) em tempo real e compensa antes de se propagar, em vez
     de reagir à medição instantânea como o FCS-MPC faz. Tecnicamente relacionado ao problema do
     degrau de `G`, mas é uma arquitetura de controle inteira diferente — fora de escopo pra
     corrigir agora, mas vale citar como direção de trabalho futuro na dissertação.
     **Testado — piorou em `G=0`, mesmo motivo do limitador de referência (atrasar a QUEDA do teto
     mantém o controlador pedindo mais corrente do que existe por mais tempo — em `G=0` não tem
     nenhuma potência disponível, pior caso possível).** Removido o `G_filt` (volta a usar `G_m`
     direto, teto sempre instantâneo na queda); mantido o limitador de taxa da própria referência
     (`ILs_ref_l`/`ILbat_ref_l`), que já tem a ordem certa (teto físico sempre vence por último).
     **Próximo passo sugerido:** testar com degrau menor (`1000→900W/m²`, queda de só 10%) antes de
     continuar — separa "ainda é bug" de "é limitação física de testar com degrau literalmente
     instantâneo" (artificialmente mais severo que qualquer nuvem real).
  - **Achado decisivo (12/10/2026):** usuário testou valores FIXOS de `G` (sem nenhuma transição) e
    achou que a faixa estável é `G≤2` ou `G≥900` — **instável em toda a faixa intermediária**
    (`i_Lbat*` cruzando perto de zero, nem carga forte nem descarga forte). Isso não é sobre
    transição/degrau — é um bug de **regime permanente** nessa faixa, nunca testada antes (o teste
    de `STC` só validou carregamento forte, `i_Lbat*≈-1,15A`, longe de zero). Aponta de volta pra
    hipótese mais antiga do projeto: o candidato `(0,0)` muda de `γ` (via `λ`) exatamente quando
    `i_Lbat` cruza zero, criando oscilação auto-cancelante — já suspeitado antes da correção de
    sinal, nunca retestado depois dela (a primeira tentativa de remover `(0,0)` da busca, v10, foi
    **antes** da correção de sinal, resultado pode ter sido mascarado pelo bug de sinal ainda
    presente). **Decisão: remover `(0,0)` da busca de novo**, agora com o sinal já corrigido — só
    os 3 estados genuinamente distintos (`(1,1)`,`(1,0)`,`(0,1)`), nenhum deles depende de `λ`/sinal
    de `i_Lbat` pra decidir sua própria dinâmica. Testado — **ainda instável em `G=800` fixo**, nem
    isso resolveu. Dados reais capturados: `V_pv` estável (`330-333V`), `I_pv` estável (`~3,89A`),
    `I_bat` perto da meta (`média -1,11A`) — só `V_out` instável de verdade, oscilando `249-572V`
    (`média 436,9V`, perto do esperado). Balanço de potência bate em média
    (`P_in≈1287W≈P_carga+P_bat_carga≈1284W`). **Não é mais erro de referência/sinal — é ripple
    excessivo especificamente em `V_out`**, possível ressonância com `f0(Ls-Co)≈2,15kHz`.
  - **Causa provável encontrada (12/10/2026): `Co` fisicamente pequeno demais pro FCS-MPC.**
    Achado `paper_4440.pdf` em `~/Documentos/Artigos` — "Controle Preditivo Baseado em Modelo para
    Conversores de 3 Portas: o Sistema VR-BESS", coautorado pela **Isadora** (Braga) **e pela
    Silvia** (Ferreira, orientadora deste projeto) — aplicação de FCS-MPC no VR-BESS com função
    custo só de corrente (igual a nossa), resultado validado: erro máx. `~1%` em `Vco`, ripple só
    `0,23-0,43V`. **Mas o `Co` deles é `400µF` — o nosso é `1,41µF`, 284× menor** (`Ro·Co`:
    `64ms` deles vs `225µs` nosso). O próprio artigo admite ter ajustado os componentes
    especificamente pro FCS-MPC (frequência de chaveamento variável), não usando os valores
    originais do Pacheco(2002)/projeto de malha aberta — exatamente a situação do nosso `Co`, que
    foi dimensionado pro PWM fixo original (balanço volt-segundo), nunca pro FCS-MPC. **Decisão:
    testar `Cvo.capacitance` em `400e-6` (valor exato do artigo, não uma versão escalada — mais
    direto e cientificamente mais correto testar o parâmetro já validado primeiro)**, em vez de
    `1.41e-6`. **CONFIRMADO (12/10/2026): `G=800` fixo estabilizou com `Co=400µF`.** Causa raiz do
    ripple excessivo de `V_out` era mesmo o `Co` subdimensionado pro FCS-MPC — não um bug de código.
    `Cvo.capacitance` no `VR-BESS.tse` mudado de `1.41e-6` pra `400e-6`. Testou aumentar ainda mais
    e o ripple caiu mais — mas decidiu não seguir só aumentando. **Valor comercial sugerido pra
    ficar perto de `400µF` com opção de catálogo real: `390µF` (E12, mais próximo do artigo) ou
    `470µF` (E6/E12, mais fácil de achar/mais barato, com alguma margem a mais)** — valor final
    exato ainda não escolhido pelo usuário, registrar aqui quando decidir. Teste de degrau de `G`
    considerado satisfatório pelo usuário.
  - **Degrau de temperatura testado (12/10/2026) — OK.** Degraus `25→0→45→65→25°C` com `G=1000`
    fixo, resultado satisfatório pelo usuário. Nenhum ajuste de código foi necessário (diferente de
    `G`, o teto `ILs_mpp` não escala com `T` — o coeficiente de corrente `Ki` é pequeno o bastante
    pra não precisar, e o `Co` maior já absorve a variação de tensão por `Kv`).
  3. [ ] Degrau de carga (`Ro` muda) — **ainda não testado**. Achado antes de testar: `Po_ref` no
    `output_fnc` é uma **constante fixa** (`1000W = Vo_ref²/Ro_nominal`), não lê `Ro` real da
    simulação — se `Ro` mudar de verdade (não só a carga momentânea), o controlador continua
    mirando `1000W`, fazendo `Vo` se estabilizar longe de `400V` (`P=V²/R` com `P` fixo e `R` maior
    → `V` sobe). Duas opções discutidas, nenhuma decidida ainda: (a) testar assim mesmo e
    documentar como limitação conhecida, ou (b) calcular a potência-alvo a partir de `V_out×I_out`
    medidos (sensores já existem no esquemático) em vez da constante, pra perseguir `Vo=400V`
    independente de `Ro`.
  - Comparação lado a lado com malha aberta não foi priorizada (o PWM fixo será removido, não
    mantido como fallback) — se precisar comparar depois, salvar uma cópia do `.tse` antes de
    remover `Constant9`/`Constant10`/`PWM_Mod_S1`/`PWM_Mod_S2`.
- [ ] **Documentação:**
  - Reescrever a derivação do modelo discreto (hoje só em `old.tex`, fora do documento, com
    valores de outro trabalho) com os valores reais deste projeto.
  - Adicionar ao `Latex/ifacconf.bib` as entradas que faltam e já são referenciadas em `old.tex`:
    `braga2022controle` (Isadora Della-Sávia Braga, dissertação UFLA 2022 — ver acima),
    `marcello22019modelagem`, `delay`.
