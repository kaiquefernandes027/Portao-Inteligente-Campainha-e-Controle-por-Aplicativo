#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include "../esp32_servo/GateController.h"
#include "../esp32_servo/WireTypes.h"
#include "../esp32_painel/PanelIndicator.h"
#include "../esp32_painel/ChiptunePlayer.h"
#include "../esp32_painel/InputTimers.h"

void chiptune(uint32_t start){
  ChiptunePlayer p;p.start(start);std::vector<uint16_t> notes{p.frequency()};unsigned pauses=0;uint32_t ended=0;
  for(uint32_t i=1;i<5000;i++){
    if(p.update(start+i)){if(p.frequency())notes.push_back(p.frequency());else if(p.playing())pauses++;}
    if(!p.playing()){ended=i;break;}
  }
  assert(notes.size()==24&&pauses==23&&ended==4346&&ended==ChiptunePlayer::durationMs());
  assert(notes[0]==1568&&notes[1]==1760&&notes[2]==1976&&notes[3]==2093&&notes.back()==2093);
  assert(!p.playing()&&p.frequency()==0&&!p.update(start+6000));
}
int main(){
  chiptune(0);chiptune(UINT32_MAX-200);
  ChiptunePlayer p;p.start(0);p.update(120);p.start(130);assert(p.frequency()==1568);p.stop();assert(p.frequency()==0&&!p.playing());
  PanelIndicator panel;assert(!panel.known()&&!panel.ledOn());
  PanelPacket state={};state.link=true;strcpy(state.state,"closed");state.receivedAt=1;
  assert(!panel.apply(state)&&!panel.ledOn());
  strcpy(state.state,"opening");state.receivedAt=150;assert(!panel.apply(state)&&panel.ledOn());
  strcpy(state.state,"open");strcpy(state.completionId,"command-0001");state.receivedAt=2100;
  assert(panel.apply(state)&&panel.ledOn());state.receivedAt=2200;assert(!panel.apply(state));
  assert(!panel.live(6000)&&panel.ledOn()); // Nao inventa um fechamento quando o link cai.
  strcpy(state.completionId,"command-0002");state.receivedAt=6000;assert(!panel.apply(state)); // Sem bip atrasado ao reconectar.
  state.link=false;panel.apply(state);assert(!panel.live(6001)&&panel.ledOn());
  state.link=true;strcpy(state.state,"closed");state.receivedAt=6100;assert(!panel.apply(state)&&!panel.ledOn());

  CommandClock clock;assert(clock.remaining(10000,0)<0);clock.sync(100000,100,40);
  assert(clock.remaining(105000,100)==4960);assert(clock.remaining(105000,5060)==0);
  clock.sync(100000,UINT32_MAX-10,20);assert(clock.remaining(101000,9)==960);

  GateController gate(0,90,20,300);ChiptunePlayer simultaneous;gate.start(true,0);simultaneous.start(0);bool finished=false;
  for(uint32_t t=1;t<=ChiptunePlayer::durationMs();t++){simultaneous.update(t);if(gate.tick(t)){assert(t==2100&&simultaneous.playing());finished=true;}}
  assert(finished&&gate.state()==GateController::OPEN&&!simultaneous.playing());
  panelio::ButtonDebouncer button;assert(!button.update(true,0));assert(button.update(true,40));assert(!button.update(true,4000));
  panelio::PulseTimer pulse;pulse.start(UINT32_MAX-10,20);assert(pulse.active(8));assert(!pulse.active(9));
  std::cout<<"Automacao v2: 24 notas, painel remoto/LED/bip, reconexao, prazo e servo simultaneo aprovados.\n";
}
