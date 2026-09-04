#include "codex/smart_charge.h"
#include <cassert>
#include <cstdio>
int main() {
 using namespace codex;
 static_assert(charge_current_code(1200)==18);
 static_assert(charge_current_ma(18)==1200);
 for(unsigned int i=0;i<65536;i++) assert(charge_current_ma(charge_current_code(i))<=1200);
 ChargeSample s{true,true,true,false,3750,5000,1500,2,3}; SmartCharge p;
 assert(smart_charge_next(p,s,0)==600);
 assert(smart_charge_next(p,s,9999)==600);
 assert(smart_charge_next(p,s,10000)==800);
 assert(smart_charge_next(p,s,20000)==1000);
 assert(smart_charge_next(p,s,30000)==1200);
 // CV does not force an artificial taper; the PMIC regulates actual current.
 s.phase=3; assert(smart_charge_next(p,s,40000)==1200);
 s.constrained=true; assert(smart_charge_next(p,s,40001)==1000);
 s.constrained=false; assert(smart_charge_next(p,s,69999)==1000);
 assert(smart_charge_next(p,s,70001)==1200);
 s.battery_mv=2990; assert(smart_charge_next(p,s,70002)==200);
 s.battery_mv=3200; assert(smart_charge_next(p,s,80002)==400);
 s.battery_mv=3750;s.vbus_mv=4600;assert(smart_charge_next(p,s,80003)==200);
 s.vbus_mv=5000;s.input_limit_ma=900; SmartCharge limited;
 assert(smart_charge_next(limited,s,0)==600); assert(smart_charge_next(limited,s,10000)==600);
 s.input_limit_ma=1500;s.cv_code=4;SmartCharge wrong_cv;assert(smart_charge_next(wrong_cv,s,0)==200);
 for(unsigned phase=0;phase<8;phase++) {
   SmartCharge q; s.cv_code=3;s.phase=phase;
   auto ma=smart_charge_next(q,s,0);assert(ma<=1200);if(phase!=2&&phase!=3)assert(ma<=200);
 }
 unsigned seed=11;SmartCharge q;
 for(unsigned n=0;n<100000;n++) {
   seed=seed*1664525U+1013904223U;
   s.valid=(seed&1);s.present=(seed&2);s.vbus_good=(seed&4);s.constrained=(seed&8);
   s.battery_mv=2400+seed%2000;s.vbus_mv=4400+seed%1200;s.cv_code=seed%8;s.phase=(seed>>8)%8;
   s.input_limit_ma=(unsigned short[]){100,500,900,1000,1500,2000}[seed%6];
   assert(smart_charge_next(q,s,n*10000U)<=1200);
 }
 puts("PASS smart_charge: fast 1.2A, CC/CV, recovery, input and 100000 samples");
}
