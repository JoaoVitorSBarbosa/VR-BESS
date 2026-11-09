/* ====================================================================
   VR-BESS - FCS-MPC (orientadora) + MPPT P&O na tensao - ETAPA A v3
   C function1 do typhoonsim/VR-BESS.tse
   v2 (09/11/2026): Vpv filtrado (1 kHz) no PI e P&O congelado so
   quando a potencia media chega no limite (ripple congelava na v1).
   v3 (09/11/2026): residuo da bateria pela potencia MEDIDA do painel
   (filtrada) e PI mais lento (Kp=0,01, Ki=5) - o FCS-MPC regula a
   corrente do painel (I_pv), nao a do indutor, e o PI original oscilava.

   Este arquivo tem DUAS partes, uma para cada aba do editor do bloco:
     PARTE 1 -> aba arb_defs   (definicoes, constantes, variaveis static)
     PARTE 2 -> aba output_fnc (codigo executado a cada Ts = 10 us)
   Em cada aba: selecionar tudo, apagar e colar a parte correspondente
   (de "Begin code section" ate "End code section", inclusive).

   Antes de rodar:
     - Num_cel_par_pv1 (constante que alimenta Op_mode): 4 -> 1
     - execution_rate do C function1: 1e-5 (recomendado)

   Teste esperado (T = 25 C):
     G = 650 : V_pv ~ 302 V, P_pv ~ 1161 W, dbg_ILbat_ref ~ +0,7 A
     G = 1000: limitado pela demanda, V_pv ~ 347 V, dbg_ILbat_ref = 1,15
     V_out ~ 400 V nos dois. Nao testar abaixo de ~570 W/m² (etapa B).
   Debug: dbg_cost01 = Vpv_ref, dbg_cost00 = P media do periodo do P&O
   ==================================================================== */


/* ####################################################################
   PARTE 1 - aba arb_defs
   #################################################################### */

/*Begin code section*/
/* ---------- Parametros do sistema ---------- */

const double Ts = 1.0e-5;
const double Ls = 3.89e-3;
const double Lb = 4.23e-3;
const double Ro = 160.0;
const double Co = 400e-6;
const double Vo_ref = 400.0;

/* ---------- MPPT P&O + PI de Vpv (etapa A) ---------- */

const double N_mppt = 500.0;
const double dV_po = 5.0;
const double Vref_min = 200.0;
const double Vref_max = 380.0;
const double Kp_v = 0.01;
const double Ki_v = 5.0;
const double ILs_max = 6.5;
const double ILb_chg = 1.15;
const double ILb_dis = 4.6;
const double a_f = 0.0591;
const double lim_frac = 0.98;
const double a_p = 0.00624;

static double Vpv_ref = 300.0;
static double int_v = 0.0;
static double P_sum = 0.0;
static double P_old = 0.0;
static double dir_po = 1.0;
static double cnt_po = 0.0;
static double Vpv_f = -1.0;
static double Ppv_f = -1.0;

double Po, e_v, int_new, u_raw, u_pi;
double ILs_dem, ILs_top, P_avg, P_lim;

/* ---------- Estados persistentes ---------- */

static int initialized = 0;
static int x_old = 0;



/* ---------- Variaveis locais ---------- */

int M[4][2] = {
    {1, 0},
    {1, 1},
    {0, 0},
    {0, 1}
};

int i, j, k, p;
int c, f, x_opt;
int pivot_row;

double X;
double ILb_ref, ILs_ref;
double ILbk, ILsk, Vok, r;
double Vink, ILsm, ILbm, Vom, Vcbk;
double Vpv_safe;
double v, s1, s2;

double ILbk1, ILsk1, Vok1;
double ILbk2, ILsk2, Vok2;

double g, g_opt=33.0;
double best_cost;
double temp;
double denominator;
double Mod;
double aux=67;
/*End code section*/


/* ####################################################################
   PARTE 2 - aba output_fnc
   #################################################################### */

/*Begin code section*/
/* ============================================================
   FCS-MPC (orientadora) + MPPT P&O (etapa A)
   Adaptacao para Typhoon HIL - C Function
   Entradas escalares: Vin, ILs, ILb, Vo, Vcb, Mod
   Saidas escalares: S1, S2, Ro, Ref_Vo, Ref_ILb, Ref_ILs, Xo
   ============================================================ */



/* ---------- Leitura das entradas escalares ---------- */

Vink = V_pv;
ILsk = I_pv;
ILbk = I_bat;
Vok = V_o;
Vcbk = V_bat;
Mod = Op_mode;


/* ============================================================
   MPPT (etapa A): P&O na tensao + PI de Vpv -> ILs_ref
   Roda so no modo 1. ILs_ref e limitado pela demanda
   maxima (carga + bateria a ILb_chg); quando a potencia
   media chega nesse limite, o P&O fica congelado.
   Vpv_f: Vpv filtrado (passa-baixa 1 kHz) contra o ripple.
   Ppv_f: potencia medida do painel filtrada (100 Hz), usada
   no residuo da bateria (nao ILs_ref, que tem ripple do PI).
   ============================================================ */

Po = Vo_ref * Vo_ref / Ro;
Vpv_safe = Vink;
if (Vpv_safe < 50.0) Vpv_safe = 50.0;
u_pi = 0.0;
if (Vpv_f < 0.0) Vpv_f = Vink;
Vpv_f = Vpv_f + a_f * (Vink - Vpv_f);
if (Ppv_f < 0.0) Ppv_f = Vink * ILsk;
Ppv_f = Ppv_f + a_p * (Vink * ILsk - Ppv_f);

if ((int)Mod == 1) {
    P_lim = Po + Vcbk * ILb_chg;
    ILs_dem = P_lim / Vpv_safe;
    ILs_top = ILs_max;
    if (ILs_dem < ILs_top) ILs_top = ILs_dem;

    e_v = Vpv_f - Vpv_ref;
    int_new = int_v + Ki_v * Ts * e_v;
    u_raw = Kp_v * e_v + int_new;

    if (u_raw > ILs_top) {
        u_pi = ILs_top;
        if (e_v < 0.0) int_v = int_new;
    } else if (u_raw < 0.0) {
        u_pi = 0.0;
        if (e_v > 0.0) int_v = int_new;
    } else {
        u_pi = u_raw;
        int_v = int_new;
    }

    P_sum = P_sum + Vink * ILsk;
    cnt_po = cnt_po + 1.0;
    if (cnt_po >= N_mppt) {
        P_avg = P_sum / N_mppt;
        if (P_avg < lim_frac * P_lim) {
            if (P_avg < P_old) dir_po = -dir_po;
            Vpv_ref = Vpv_ref + dir_po * dV_po;
        }
        if (Vpv_ref > Vref_max) Vpv_ref = Vref_max;
        if (Vpv_ref < Vref_min) Vpv_ref = Vref_min;
        P_old = P_avg;
        P_sum = 0.0;
        cnt_po = 0.0;
    }
}


/* ---------- Estado de chaveamento anterior ---------- */


g_opt = 1e100;
S1 = M[x_old][0];
S2 = M[x_old][1];




/* ============================================================
   IDENTIFICACAO DO MODO DE OPERACAO
   ============================================================ */

c = 0;
f = 3;
X = 0.0;
ILb_ref = 0.0;
ILs_ref = 0.0;

switch ((int)Mod) {
    case 1:
        X = 0;
        c = 0;
        f = 2;
        ILs_ref = u_pi;
        ILb_ref = (Ppv_f - Po) / Vcbk;
        if (ILb_ref > ILb_chg) ILb_ref = ILb_chg;
        if (ILb_ref < -ILb_dis) ILb_ref = -ILb_dis;
        break;
    case 2:
        X = 0;
        c = 1;
        f = 2;
        ILb_ref = 0;
        ILs_ref = Vo_ref*Vo_ref/(Vink*Ro);      
        break;

    case 3:
        X = 1;
        c = 1;
        f = 3;
        ILb_ref = -1.08;
        ILs_ref = (Vcbk*ILb_ref + Vo_ref*Vo_ref/Ro)/Vink;
        break;
    case 4:
        c = 2;
        f = 3;
        X = 1;
        
        ILb_ref = -Vo_ref*Vo_ref/(Ro*Vcbk);
        ILs_ref = 0;
        
        break;
}


/* ============================================================
   FCS-MPC: PREDICAO DO PRIMEIRO PASSO
   ============================================================ */



ILbk1 = ILbk + Ts*(S1*(1.0 - S2)+ X*(1.0 - S1)*(1.0 - S2))*Vok/Lb - Ts * Vcbk/Lb;
ILsk1 = ILsk + Ts*(S1 * S2 - 1.0)*Vok/Ls + Ts*Vink/Ls;
Vok1 = -Ts*(S1*(1.0 - S2) + X*(1.0 - S1) * (1.0 - S2))*ILbk/Co + Ts*(1.0-S1*S2)*ILsk/Co + (1-Ts/(Co*Ro))*Vok;

/* ============================================================
   FCS-MPC: SELECAO DO ESTADO OTIMO
   ============================================================ */



int i = c; 

while (i <= f) {
    // 2° Passo de Predicão
    ILbk2 = ILbk1 + Ts * (M[i][0] * (1.0 - M[i][1]) + X * (1.0 - M[i][0]) * (1.0 - M[i][1])) * Vok1 / Lb - Ts * Vcbk / Lb;
    
    ILsk2 = ILsk1 + Ts * (M[i][0] * M[i][1] - 1.0) * Vok1 / Ls + Ts * Vink / Ls;

    //Vok2 = -Ts * (M[i][0] * (1 - M[i][1]) + X * (1 - M[i][0]) * (1 - M[i][1])) * ILbk1 / Co + Ts * (1 - (1 - M[i][0]) * M[i][1]) * ILsk1 / Co + (1 - Ts / (Co * Ro)) * Vok1;


    // Função Custo Corrente
    g= (ILsk2 - ILs_ref) * (ILsk2 - ILs_ref) + (ILbk2 - ILb_ref) * (ILbk2 - ILb_ref);            
    
    // Seleção dos valores ótimos
    if (g < g_opt) {
        g_opt = g;
        x_opt = i;
    }

    // Incrementa i
    i = i + 1;
    aux = 1;
}

/* ============================================================
   ATUALIZACAO DO ESTADO DE CHAVEAMENTO
   ============================================================ */

/* O estado selecionado sera aplicado na proxima chamada */

x_old = x_opt;

/* As saidas S1 e S2 continuam representando o estado aplicado
   nesta chamada; o estado selecionado fica armazenado em x_old. */

dbg_ILbat_ref =  ILb_ref ;
dbg_ILs_ref = ILs_ref ;
dbg_ILbat_m =  ILbk ;
dbg_ILs_m = ILsk;
dbg_cost11 = x_opt; 
dbg_cost10 = ILbk2; 
dbg_cost00 = P_old;
dbg_cost01 = Vpv_ref;
/*End code section*/
