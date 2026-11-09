/* Planta do VR-BESS para simulacao offline (ver sim_offline/rodar.sh).
   Painel: modelo de diodo do pv_array.tlib (17s x 2p, T = 25 C). Estados: iLs, iLb, Vpv, Vo;
   bateria = fonte ideal de 252 V; chaves pelo modelo gamma/m do FCS-MPC; iLb >= 0 quando
   gamma = 0 (diodo). Passo 0,2 us; controlador chamado a cada 10 us. I_pv passado ao
   controlador = corrente do PAINEL (igual ao sensor do VR-BESS.tse).
   Perfil: G = 650 / 1000 / 650 W/m2, 0,1 s cada; estatisticas da 2a metade de cada trecho. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
void step(void);
extern double V_pv,V_bat,V_o,I_pv,I_bat,Op_mode,S1,S2,dbg_ILs_ref,dbg_ILbat_ref,dbg_cost01,dbg_cost00;
static double ipanel(double V,double G){double k=1.3806e-23,q=1.6022e-19,a=1.2,T=298.15;
 double Ns=612,Isc=6.28,Voc=363.8;double Iph=Isc*G/1000;double Irs=Isc/(exp(q*Voc/(Ns*k*a*T))-1);
 double x=q*V/(Ns*k*a*T); if(x>80)x=80; double I=Iph-Irs*(exp(x)-1); return I<0?0:I;}
int main(int argc,char**argv){
 double Gs[3]={650,1000,650}; double G=Gs[0]; int meas_iL=0; double tseg=0.1, tend=0.3;
 double Ls=3.89e-3,Lb=4.23e-3,Co=400e-6,Cpv=220e-6,Ro=160,Vb=252,dt=2e-7;
 double iLs=0.0,iLb=0,Vpv=346,Vo=400; double s1=0,s2=0;
 long N=(long)(tend/dt), Nc=50; double sVpv=0,sIpv=0,sVo=0,sIb=0,sIbr=0,sIsr=0,n=0,vmin=1e9,vmax=0,rmin=1e9,rmax=-1e9,omin=1e9,omax=0; int seg=0;
 Op_mode=1;
 for(long k=0;k<N;k++){
  int sg=(int)(k*dt/tseg); if(sg>2)sg=2; if(sg!=seg){ printf("G=%4.0f | Vpv=%.1f [%.1f..%.1f] Ppv=%.0f Vo=%.1f [%.1f..%.1f] Ibat=%.3f ref=%.3f ILs_ref=[%.2f..%.2f]\n",G,sVpv/n,vmin,vmax,sVpv/n*sIpv/n,sVo/n,omin,omax,sIb/n,sIbr/n,rmin,rmax); seg=sg; G=Gs[sg]; sVpv=sIpv=sVo=sIb=sIbr=sIsr=n=0; vmin=rmin=omin=1e9; vmax=rmax=omax=-1e9;}
  double Ip=ipanel(Vpv,G);
  if(k%Nc==0){ V_pv=Vpv; I_pv=meas_iL?iLs:Ip; V_o=Vo; V_bat=Vb; I_bat=iLb; step(); s1=S1; s2=S2; }
  double gam=s1*(1-s2), m=1-s1*s2;
  double diLb=(gam*Vo-Vb)/Lb, diLs=(Vpv-m*Vo)/Ls;
  iLb+=dt*diLb; if(gam==0 && iLb<0) iLb=0;
  iLs+=dt*diLs; if(iLs<0) iLs=0;
  Vpv+=dt*(Ip-iLs)/Cpv; if(Vpv<0)Vpv=0;
  Vo+=dt*(m*iLs-gam*iLb-Vo/Ro)/Co;
  if(fmod(k*dt,tseg)>tseg/2){ if(dbg_ILs_ref<rmin)rmin=dbg_ILs_ref; if(dbg_ILs_ref>rmax)rmax=dbg_ILs_ref; if(Vo<omin)omin=Vo; if(Vo>omax)omax=Vo;sVpv+=Vpv;sIpv+=Ip;sVo+=Vo;sIb+=iLb;sIbr+=dbg_ILbat_ref;sIsr+=dbg_ILs_ref;n++; if(Vpv<vmin)vmin=Vpv; if(Vpv>vmax)vmax=Vpv;}
 }
 printf("G=%4.0f | Vpv=%.1f [%.1f..%.1f] Ppv=%.0f Vo=%.1f [%.1f..%.1f] Ibat=%.3f ref=%.3f ILs_ref=[%.2f..%.2f]\n",G,sVpv/n,vmin,vmax,sVpv/n*sIpv/n,sVo/n,omin,omax,sIb/n,sIbr/n,rmin,rmax);
}
