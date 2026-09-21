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
- Ls≈3,89 mH, Lbat≈4,23 mH, Co≈1,41 µF, Cbat≈0,109 µF
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

## Bug conhecido, não corrigido (`modelo_bateria`)

Com os parâmetros estimados de forma da curva LiFePO4 (E0_1=13,32, A_1=1,4), na carga cheia
(`it_c=0`) o modelo calcula `E = E0 + A = 14,72 V`, que excede `V_max=14,6 V`. Isso dispara o ramo
`elseif V > V_max: S=0`, que bloqueia carga **e** descarga — o ramo `SOC>=100 && I<0` (que deveria
bloquear só a carga) nunca é alcançado porque o `elseif` anterior já disparou. Correção pendente:
baixar `A_1` (ex.: ~1,0) ou garantir `E0_1 + A_1 < V_max_1` em corrente zero. Verificar os valores
atuais de `E0_1`/`A_1` antes de assumir que já foi corrigido.

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
    Precisam ser religados e re-testados antes de qualquer validação/uso sério do modelo.
  - `Cbat`/`Cvo`/`Cpv` têm ficado sem `initial_voltage` definido (partindo de 0V) mais de uma vez
    nesta sessão, aparentemente revertido sozinho por algum salvamento da UI do TySim — causa
    inrush de energização grande. Vale conferir se ainda está setado (`initial_voltage = "252"`
    em `Cbat`, `"400"` em `Cvo`, `"346"` em `Cpv`) antes de rodar simulações longas/sensíveis.
  - Bug do SOC=100% em `modelo_bateria` (ver seção acima) ainda não corrigido.
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
- Controlador FCS-MPC: ainda não implementado em nenhuma das duas plataformas — próxima etapa,
  necessária inclusive para resolver a diferença estrutural entre limitar corrente de carga e
  regular a tensão de saída simultaneamente (ver commits de recalibração para o porquê).
- KiCad: não iniciado.
