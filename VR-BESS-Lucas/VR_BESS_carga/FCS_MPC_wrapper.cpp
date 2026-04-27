
/*
 * Include Files
 *
 */
#if defined(MATLAB_MEX_FILE)
#include "tmwtypes.h"
#include "simstruc_types.h"
#else
#define SIMPLIFIED_RTWTYPES_COMPATIBILITY
#include "rtwtypes.h"
#undef SIMPLIFIED_RTWTYPES_COMPATIBILITY
#endif



/* %%%-SFUNWIZ_wrapper_includes_Changes_BEGIN --- EDIT HERE TO _END */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <Core>
#include <LU>
/* %%%-SFUNWIZ_wrapper_includes_Changes_END --- EDIT HERE TO _BEGIN */
#define u_width 1
#define u_1_width 1
#define u_2_width 1
#define u_3_width 1
#define u_4_width 1
#define u_5_width 1
#define y_width 1
#define y_1_width 1
#define y_2_width 1
#define y_3_width 1
#define y_4_width 1
#define y_5_width 1
#define y_6_width 1

/*
 * Create external references here.  
 *
 */
/* %%%-SFUNWIZ_wrapper_externs_Changes_BEGIN --- EDIT HERE TO _END */
/* extern double func(double a); */

int x_opt=0, x_old=0;
int i=0, c=0, f=0;
double g_opt= 0, g=0; 

// Conjunto de estados
int M[4][2] = {1,0, 1,1, 0,0, 0,1};

double X = 0, s1=0, s2=0, v=0;
double Ts = 1/100e+3;
double pi= 3.141592653589793; 

double Vo, Vom, Vok, Vok1, Vok2;
double ILs, ILsm, ILsk, ILsk1, ILsk2;
double ILb, ILbm, ILbk, ILbk1, ILbk2;


double Vin, Vink;
double Vcb, Vcbk;

// Valores dos componentes
double Ls = 3.5e-3;
double Lb = 3e-3;
double Rs = 150;
double Co = 400e-6;

double ILs_ref, ILb_ref, Vo_ref;
double dR = 5, Ro, r = dR/(Rs*(Rs+dR));

Eigen::Matrix4d Po{
    {1000, 1000, 1000, 1000},
    {1000, 1000, 1000, 1000},
    {1000, 1000, 1000, 1000},
    {1000, 1000, 1000, 1000}};

Eigen::Matrix4d Pk;

Eigen::Matrix4d R{
    {0.001, 0, 0, 0},
    {0, 0.001, 0, 0},
    {0, 0, 0.001, 0},
    {0, 0, 0, 0.001}};



Eigen::Matrix4d C{
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}};

Eigen::Matrix4d K;
Eigen::Matrix4d L;
Eigen::Matrix4d Z;
Eigen::Matrix<double, 4, 1> Xkp;
Eigen::Matrix<double, 4, 1> Xk;
Eigen::Matrix<double, 4, 1> Xm;
Eigen::Matrix<double, 4, 1> Y;
Eigen::Matrix<double, 2, 1> U;

Eigen::Matrix<double, 4, 1> Xa{
    {0},
    {0},
    {0},
    {0}};
/* %%%-SFUNWIZ_wrapper_externs_Changes_END --- EDIT HERE TO _BEGIN */

/*
 * Output function
 *
 */
void FCS_MPC_Outputs_wrapper(const real_T *Vin,
			const real_T *ILs,
			const real_T *Vo,
			const real_T *Vcb,
			const real_T *ILb,
			const real_T *Mod,
			real_T *S1,
			real_T *S2,
			real_T *Xo,
			real_T *Ref_Vo,
			real_T *Ref_ILb,
			real_T *Ref_ILs,
			real_T *y0)
{
/* %%%-SFUNWIZ_wrapper_Outputs_Changes_BEGIN --- EDIT HERE TO _END */
// Inicialização das Variáveis Globais
double g_opt = 1e10;

// Valores de medição no tempo K
double Vink = Vin[0]; 
double ILsm = ILs[0]; 
double ILbm = ILb[0];
double Vom = Vo[0]; 
double Vcbk = Vcb[0];


// Modo 1 e 3 Q123 = 2.45 Q4 = 0.001
// Modo 2 



Eigen::Matrix4d Q{
    {3.45, 0, 0, 0},
    {0, 3.45, 0, 0},
    {0, 0, 3.45, 0},
    {0, 0, 0, 0.001}};

y0[0] = Ro;    

s1 = M[x_old][0];
s2 = M[x_old][1];
v = s1*(1-s2) + X*(1-s1)*(1-s2);    

Eigen::Matrix4d A{
    {1, 0, v*Ts/Lb, 0},
    {0, 1, -Ts*(1-s1*s2)/Ls, 0},
    {-v*Ts/Co, Ts*(1-s1*s2)/Co, (1-Ts/(Rs*Co)), Vom*Ts/Co},
    {0, 0, 0, 1}};  

Eigen::Matrix<double, 4, 2> B{
    {0, -Ts/Lb},
    {Ts/Ls, 0},
    {0, 0},
    {0, 0}};

U(0,0) = Vink;
U(1,0) = Vcbk;

Y(0,0) = ILbm;
Y(1,0) = ILsm;    
Y(2,0) = Vom;
Y(3,0) = Xa(3,0);

// Estados preditos para tempo K
Xkp = A*Xa + B*U;

// Matriz de covariância
Pk = A*Po*A.transpose() + Q;

L = C*Pk*C.transpose() + R;
Z = L.inverse();

// Ganhos de Kalman
K = Pk*C.transpose()*Z;

// Valores com o filtro para K    
Xk = Xkp + K*(Y-C*Xkp);

ILbk = Xk(0,0);
ILsk = Xk(1,0);
Vok = Xk(2,0);
r = Xk(3,0);

Ro = Rs + r*Rs*Rs/(1-r*Rs);
double Vo_ref = 400;

// Identificação do modo de operação, definição de restrições e referências    
switch ((int)Mod[0]) {
    case 1:
        X = 0;
        c = 0;
        f = 2;
        ILb_ref = 1.08;
        ILs_ref = (Vcbk * ILb_ref + Vo_ref * Vo_ref / Ro) / Vink;
      
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
        ILs_ref = (Vcbk * ILb_ref + Vo_ref * Vo_ref / Ro) / Vink;
        break;
    case 4:
        c = 2;
        f = 3;
        X = 1;
        ILb_ref = -Vo_ref * Vo_ref / (Ro * Vcbk);
        ILs_ref = 0;
        break;
}


Ref_Vo[0] = Vo_ref;
Ref_ILb[0] = ILb_ref;
Ref_ILs[0] = ILs_ref;
Xo[0] = X;


// Correção do atraso do algoritmo de controle (1° Passo Predição) 

// MODO UNIFICADO

double ILbk1 = ILbk + Ts*(S1[0]*(1 - S2[0])+ Xo[0]*(1 - S1[0])*(1 - S2[0]))*Vok/Lb - Ts * Vcbk/Lb;

double ILsk1 = ILsk + Ts*(S1[0] * S2[0] - 1)*Vok/Ls + Ts*Vink/Ls;

double Vok1 = -Ts*(S1[0]*(1 - S2[0]) + Xo[0]*(1 - S1[0]) * (1 - S2[0]))*ILbk/Co + Ts*(1-S1[0]*S2[0])*ILsk/Co + (1-Ts/(Co*Ro))*Vok;

// Execução do MPC
int i = c; 
while (i <= f) {
    // 2° Passo de Predição
    double ILbk2 = ILbk1 + Ts * (M[i][0] * (1 - M[i][1]) + Xo[0] * (1 - M[i][0]) * (1 - M[i][1])) * Vok1 / Lb - Ts * Vcbk / Lb;
    
    double ILsk2 = ILsk1 + Ts * (M[i][0] * M[i][1] - 1) * Vok1 / Ls + Ts * Vink / Ls;

    //double Vok2 = -Ts * (M[i][0] * (1 - M[i][1]) + Xo[0] * (1 - M[i][0]) * (1 - M[i][1])) * ILbk1 / Co + Ts * (1 - (1 - M[i][0]) * M[i][1]) * ILsk1 / Co + (1 - Ts / (Co * Ro)) * Vok1;


    // Função Custo Corrente
    double g= (ILsk2 - ILs_ref) * (ILsk2 - ILs_ref) + (ILbk2 - ILb_ref) * (ILbk2 - ILb_ref);            

    // Seleção dos valores ótimos
    if (g < g_opt) {
        g_opt = g;
        x_opt = i;
    }

    // Incrementa i
    i = i + 1;
}
// Atualização dos valores    
x_old = x_opt;
Po = (C - K*C)*Pk;
Xa = Xk;

// Aplicação do chaveamento
S1[0] = M[x_old][0];
S2[0] = M[x_old][1];
/* %%%-SFUNWIZ_wrapper_Outputs_Changes_END --- EDIT HERE TO _BEGIN */
}


