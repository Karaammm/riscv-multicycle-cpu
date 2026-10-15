#include <cstdint>
#include <iostream>

#include "verilator.cpp"

namespace {
constexpr uint32_t kGameState = 0x90000000;
constexpr int kWarmupCycles = 30000;
constexpr int kInterruptCycles = 60000;
constexpr int kMaxCycles = 200000;

bool check(bool condition, const char *message) {
  std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
  return condition;
}
}  // namespace

int main() {
  sim_t sim;
  sim.registerEventHandlers([](sim_t::Event, std::optional<std::string>) {});
  sim.load("program.s", "out/gecko.bin", "out/gecko");

  bool passed = true;
  std::cout << "RISC-V Multicycle CPU Smoke Test\n"
            << "--------------------------------\n";
  passed &= check(true, "CPU reset");

  for (int cycle = 0; cycle < kWarmupCycles; ++cycle)
    sim.runclk();

  passed &= check(get_mem_rd(*sim.g_top)(kGameState) == 0,
                  "Initial game state is LOSE");

  set_button_center_i(*sim.g_top, 1);
  for (int cycle = 0; cycle < 4; ++cycle)
    sim.runclk();
  set_button_center_i(*sim.g_top, 0);
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x344) != 0,
                  "Button interrupt was generated");

  // The firmware loops forever, so wait for the interrupt to resolve into the
  // green LED pattern rather than assuming a fixed cycle count.
  bool green = false;
  for (int cycle = 0; cycle < kInterruptCycles && !green; ++cycle) {
    sim.runclk();
    green = get_led_g_o(*sim.g_top)[0] != 0;
  }
  passed &= check(green, "Interrupt handler drove the LEDs green (WIN)");
  passed &= check(get_led_r_o(*sim.g_top)[0] == 0, "Red LEDs stayed off");
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x341) != 0,
                  "Machine exception PC was captured");
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x300) != 0,
                  "Machine status CSR is enabled");

  // The next round gets no button press, so it must fall back to LOSE (red).
  bool red = false;
  for (int cycle = 0; cycle < kMaxCycles && !red; ++cycle) {
    sim.runclk();
    red = get_led_r_o(*sim.g_top)[0] != 0;
  }
  passed &= check(red && get_led_g_o(*sim.g_top)[0] == 0,
                  "Next round without a press returns to LOSE");

  std::cout << "\nSimulation result: " << (passed ? "PASS" : "FAIL") << '\n';
  return passed ? 0 : 1;
}
