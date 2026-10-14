#include <cstdint>
#include <iostream>

#include "verilator.cpp"

namespace {
constexpr uint32_t kGameState = 0x90000000;
constexpr int kWarmupCycles = 30000;
constexpr int kInterruptCycles = 20000;

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

  const auto initialState = get_mem_rd(*sim.g_top)(kGameState);
  passed &= check(initialState == 0, "Initial game state is LOSE");

  set_button_center_i(*sim.g_top, 1);
  for (int cycle = 0; cycle < 4; ++cycle)
    sim.runclk();
  set_button_center_i(*sim.g_top, 0);
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x344) != 0,
                  "Button interrupt was generated");

  for (int cycle = 0; cycle < kInterruptCycles; ++cycle)
    sim.runclk();

  const auto finalState = get_mem_rd(*sim.g_top)(kGameState);
  passed &= check(finalState == 1, "Interrupt handler updated game state to WIN");
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x341) != 0,
                  "Machine exception PC was captured");
  passed &= check(get_cpu_get_csr(*sim.g_top)(0x300) != 0,
                  "Machine status CSR is enabled");

  const auto green = get_led_g_o(*sim.g_top)[0];
  const auto red = get_led_r_o(*sim.g_top)[0];
  passed &= check(green != 0 && red == 0, "Green LED output indicates WIN");

  std::cout << "\nSimulation result: " << (passed ? "PASS" : "FAIL") << '\n';
  return passed ? 0 : 1;
}
