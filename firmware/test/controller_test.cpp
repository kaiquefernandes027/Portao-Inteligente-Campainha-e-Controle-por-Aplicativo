#include <cassert>
#include <cstdint>
#include <iostream>
#include "../esp32_servo/GateController.h"

int main() {
  GateController gate(0, 90, 20, 300);
  assert(gate.state() == GateController::CLOSED && !gate.ledOn());
  assert(gate.start(false, 0) == GateController::ALREADY_THERE);
  assert(gate.start(true, 0) == GateController::STARTED);
  assert(gate.ledOn() && gate.moving());
  assert(gate.start(false, 5) == GateController::BUSY);
  int completions = 0;
  for (uint32_t t = 1; t <= 2099; ++t) completions += gate.tick(t);
  assert(gate.angle() == 90 && gate.moving() && completions == 0);
  assert(gate.tick(2100));
  assert(gate.state() == GateController::OPEN && gate.ledOn());
  assert(!gate.tick(5000));  // Nao emite uma segunda conclusao/beep.
  assert(gate.start(true, 5000) == GateController::ALREADY_THERE);
  assert(gate.start(false, 5000) == GateController::STARTED);
  for (uint32_t t = 5001; t < 7100; ++t) assert(!gate.tick(t));
  assert(gate.ledOn());  // Permanece aceso durante o fechamento.
  assert(gate.tick(7100));
  assert(gate.state() == GateController::CLOSED && !gate.ledOn());

  // millis() passa por zero a cada ~49 dias; movimento continua correto.
  GateController wrap(0, 1, 20, 300);
  wrap.start(true, UINT32_MAX - 10);
  assert(!wrap.tick(8));
  assert(!wrap.tick(9));
  assert(wrap.angle() == 1);
  assert(!wrap.tick(308));
  assert(wrap.tick(309));

  // Tambem permite calibrar um mecanismo cuja abertura reduz o angulo.
  GateController reversed(90, 0, 20, 0);
  reversed.start(true, 0);
  for (uint32_t t = 20; t <= 1800; t += 20) reversed.tick(t);
  assert(reversed.angle() == 0 && reversed.state() == GateController::OPEN);

  ButtonDebouncer button;
  assert(!button.update(true, 0));
  assert(!button.update(false, 10));
  assert(!button.update(true, 20));
  assert(!button.update(true, 59));
  assert(button.update(true, 60));
  assert(!button.update(true, 5000));  // Segurar nao repete a campainha.
  assert(!button.update(false, 5010));
  assert(!button.update(false, 5050));
  assert(!button.update(true, 5060));
  assert(button.update(true, 5100));

  PulseTimer pulse;
  assert(!pulse.active(0));
  pulse.start(UINT32_MAX - 249, 250);
  assert(pulse.active(UINT32_MAX));
  assert(!pulse.active(0));  // Vencimento exatamente no zero do millis().
  assert(!pulse.active(0x90000000));  // Nao volta a tocar dias depois.
  std::cout << "Controlador: abertura, fechamento, LED, debounce e temporizadores aprovados.\n";
}
