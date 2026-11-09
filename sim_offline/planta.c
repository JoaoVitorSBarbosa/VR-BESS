/* Planta do VR-BESS para simulacao offline (ver sim_offline/rodar.sh).

   Painel: modelo de diodo do pv_array.tlib (17s x 2p, T = 25 C). Estados: iLs, iLb, Vpv, Vo;
   bateria = fonte ideal de 252 V. Convencao: iLb > 0 = bateria CARREGANDO (igual ao sensor
   I_bat do VR-BESS.tse). Chaves pelo modelo gamma/m do FCS-MPC:
     (1,0): gamma=1, m=1   (1,1): gamma=0, m=0   (0,1): gamma=0, m=1
     (0,0): so diodos conduzem -> m=1 e gamma=1 se iLb<0 (descarga pelo diodo), gamma=0 se
            iLb>0; nos dois casos iLb e levada a zero e nao cruza (conducao descontinua).
   Passo 0,2 us; controlador chamado a cada 10 us. I_pv passado ao controlador = corrente do
   PAINEL (igual ao sensor do VR-BESS.tse); I_bat = iLb.

   Uso: planta [G1,G2,...] [t_trecho_s] [Op_mode]
     padrao: 650,1000,650  0.1  1
   Imprime um resumo por trecho (estatisticas da 2a metade de cada trecho). "modo" e a media de
   dbg_cost10 (o controlador da etapa B escreve ali o modo efetivo); "trocas" conta as mudancas
   de modo no trecho inteiro. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void step(void);
extern double V_pv, V_bat, V_o, I_pv, I_bat, Op_mode, S1, S2;
extern double dbg_ILs_ref, dbg_ILbat_ref, dbg_cost10;

static double ipanel(double V, double G)
{
    double k = 1.3806e-23, q = 1.6022e-19, a = 1.2, T = 298.15;
    double Ns = 612, Isc = 6.28, Voc = 363.8;
    double Iph = Isc * G / 1000;
    double Irs = Isc / (exp(q * Voc / (Ns * k * a * T)) - 1);
    double x = q * V / (Ns * k * a * T);
    if (x > 80) x = 80;
    double I = Iph - Irs * (exp(x) - 1);
    return I < 0 ? 0 : I;
}

int main(int argc, char **argv)
{
    double Gs[32]; int nG = 0;
    char buf[512] = "650,1000,650";
    if (argc > 1) strncpy(buf, argv[1], sizeof buf - 1);
    for (char *t = strtok(buf, ","); t && nG < 32; t = strtok(NULL, ",")) Gs[nG++] = atof(t);
    double tseg = argc > 2 ? atof(argv[2]) : 0.1;
    Op_mode = argc > 3 ? atof(argv[3]) : 1.0;

    const double Ls = 3.89e-3, Lb = 4.23e-3, Co = 400e-6, Cpv = 220e-6, Ro = 160, Vb = 252;
    const double dt = 2e-7, tend = tseg * nG;
    const long N = (long)(tend / dt), Nc = 50;
    double iLs = 0.0, iLb = 0.0, Vpv = 346, Vo = 400, s1 = 0, s2 = 0;

    for (int sg = 0; sg < nG; sg++) {
        double G = Gs[sg];
        double sV = 0, sI = 0, sO = 0, sB = 0, sBr = 0, sM = 0, n = 0, modo_ant = -1;
        int trocas = 0;
        double vmin = 1e9, vmax = -1e9, omin = 1e9, omax = -1e9, rmin = 1e9, rmax = -1e9;
        long k0 = (long)(sg * tseg / dt), k1 = (long)((sg + 1) * tseg / dt);
        if (k1 > N) k1 = N;
        for (long k = k0; k < k1; k++) {
            double Ip = ipanel(Vpv, G);
            if (k % Nc == 0) {
                V_pv = Vpv; I_pv = Ip; V_o = Vo; V_bat = Vb; I_bat = iLb;
                step(); s1 = S1; s2 = S2;
                if (modo_ant >= 0 && dbg_cost10 != modo_ant) trocas++;
                modo_ant = dbg_cost10;
            }
            double m = 1 - s1 * s2, gam;
            if (s1 == 0 && s2 == 0) {
                gam = iLb < 0 ? 1.0 : 0.0;
                double nb = iLb + dt * (gam * Vo - Vb) / Lb;
                if ((iLb > 0 && nb < 0) || (iLb < 0 && nb > 0)) nb = 0;
                iLb = nb;
            } else {
                gam = s1 * (1 - s2);
                iLb += dt * (gam * Vo - Vb) / Lb;
            }
            iLs += dt * (Vpv - m * Vo) / Ls;
            if (iLs < 0) iLs = 0;
            Vpv += dt * (Ip - iLs) / Cpv;
            if (Vpv < 0) Vpv = 0;
            Vo += dt * (m * iLs - gam * iLb - Vo / Ro) / Co;
            if (k - k0 > (k1 - k0) / 2) {
                sV += Vpv; sI += Ip; sO += Vo; sB += iLb; sBr += dbg_ILbat_ref; sM += dbg_cost10; n++;
                if (Vpv < vmin) vmin = Vpv; if (Vpv > vmax) vmax = Vpv;
                if (Vo < omin) omin = Vo;   if (Vo > omax) omax = Vo;
                if (dbg_ILs_ref < rmin) rmin = dbg_ILs_ref;
                if (dbg_ILs_ref > rmax) rmax = dbg_ILs_ref;
            }
        }
        printf("G=%4.0f | Vpv=%.1f [%.1f..%.1f] Ppv=%.0f Vo=%.1f [%.1f..%.1f] Ibat=%.3f ref=%.3f "
               "ILs_ref=[%.2f..%.2f] modo=%.2f trocas=%d\n",
               G, sV / n, vmin, vmax, sV / n * sI / n, sO / n, omin, omax, sB / n, sBr / n,
               rmin, rmax, sM / n, trocas);
    }
    return 0;
}
