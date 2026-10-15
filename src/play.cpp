// Interactive terminal front-end for the RISC-V multicycle CPU.
//
//   make play    live board: raw keyboard input + colour LED / 7-segment render
//   make demo    scripted lose -> win walkthrough (no interaction required)
//
// This reuses the exact Verilator harness (sim_t) that the smoke test uses, so
// it drives the real, elaborated CPU + peripherals - it is not a mock-up.
//
// Board layout used by this front-end:
//   * capture:  0x90000000   0 = lose, 1 = win (written by program.s)
//   * buttons:  0x70000000   bit per push button / dip switch
//   * leds:     0x50000000   120-bit RGB panel (10 rows x 12 columns)
//   * 7-seg:    0x60000000   four 8-bit packed segment values

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "verilator.cpp"

namespace {

// ---------------------------------------------------------------------------
// ANSI helpers
// ---------------------------------------------------------------------------
constexpr const char *kReset = "\x1b[0m";
constexpr const char *kBold = "\x1b[1m";
constexpr const char *kDim = "\x1b[2m";
constexpr const char *kHome = "\x1b[H";
constexpr const char *kClearScreen = "\x1b[2J";
constexpr const char *kClearToEnd = "\x1b[J";
constexpr const char *kHideCursor = "\x1b[?25l";
constexpr const char *kShowCursor = "\x1b[?25h";
constexpr const char *kAltOn = "\x1b[?1049h";
constexpr const char *kAltOff = "\x1b[?1049l";

const std::string kBlock = "\xE2\x96\x88"; // U+2588 FULL BLOCK
const std::string kEmpty = " ";
const std::string kShade = "\xE2\x96\x91"; // U+2591 LIGHT SHADE

// program.s publishes the remaining countdown here each loop iteration.
// kCountdownTotal must match COUNTDOWN_CYCLES in program.s.
constexpr uint32_t kCountdownAddr = 0x90000004;
constexpr int kCountdownTotal = 5000;

bool g_color = true;

std::string fg(int r, int g, int b) {
  if (!g_color)
    return "";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
  return buf;
}

std::string bg(int r, int g, int b) {
  if (!g_color)
    return "";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
  return buf;
}

std::string hex(uint32_t v, int width) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%0*x", width, v);
  return buf;
}

std::string dec(uint32_t v) { return std::to_string(v); }

// Truncate to `maxw` visible columns, keeping ANSI sequences intact.
std::string clampVisible(const std::string &s, int maxw) {
  if (maxw <= 0)
    return "";
  std::string out;
  int w = 0;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      size_t j = i + 2;
      while (j < s.size() && !std::isalpha(static_cast<unsigned char>(s[j])))
        ++j;
      if (j < s.size())
        ++j;
      out.append(s, i, j - i);
      i = j;
      continue;
    }
    unsigned char c = static_cast<unsigned char>(s[i]);
    int len = (c & 0x80) == 0x00   ? 1
              : (c & 0xE0) == 0xC0 ? 2
              : (c & 0xF0) == 0xE0 ? 3
              : (c & 0xF8) == 0xF0 ? 4
                                   : 1;
    if (w + 1 > maxw) {
      out += kReset;
      return out;
    }
    out.append(s, i, len);
    i += len;
    ++w;
  }
  return out;
}

struct WinSize {
  int cols = 80;
  int rows = 24;
};

WinSize terminalSize() {
  WinSize ws;
  struct winsize w {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    if (w.ws_col > 0)
      ws.cols = w.ws_col;
    if (w.ws_row > 0)
      ws.rows = w.ws_row;
  }
  return ws;
}

// Split a frame into lines, clamp each to the terminal width and keep at most
// `rows` lines so the terminal never scrolls. Lines are joined with CRLF.
std::string fitFrame(const std::string &frame, const WinSize &ws) {
  std::vector<std::string> lines;
  std::string cur;
  for (char ch : frame) {
    if (ch == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty())
    lines.push_back(cur);
  const int w = std::max(1, ws.cols - 1);
  std::string out;
  const int limit = std::max(1, ws.rows);
  for (int i = 0; i < static_cast<int>(lines.size()) && i < limit; ++i) {
    if (i)
      out += "\n";
    out += clampVisible(lines[i], w);
  }
  return out;
}


// ---------------------------------------------------------------------------
// Raw terminal (ANSI alternate screen + non-blocking stdin)
// ---------------------------------------------------------------------------
class RawTerminal {
public:
  bool enable() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
      return false;
    if (tcgetattr(STDIN_FILENO, &orig_) != 0)
      return false;
    termios raw = orig_;
    cfmakeraw(&raw);
    // Keep output post-processing so '\n' still returns the cursor to column 0;
    // otherwise every rendered line drifts to the right.
    raw.c_oflag |= OPOST | ONLCR;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
      return false;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    active_ = true;
    std::fputs(kAltOn, stdout);
    std::fputs(kHideCursor, stdout);
    std::fputs(kClearScreen, stdout);
    std::fflush(stdout);
    return true;
  }

  void disable() {
    if (!active_)
      return;
    std::fputs(kReset, stdout);
    std::fputs(kShowCursor, stdout);
    std::fputs(kAltOff, stdout);
    std::fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
    active_ = false;
  }

  ~RawTerminal() { disable(); }

  // Returns raw bytes typed since the last call (may be empty).
  std::string poll() {
    std::string out;
    char buf[64];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
      out.append(buf, static_cast<size_t>(n));
    return out;
  }

private:
  termios orig_{};
  bool active_ = false;
};

RawTerminal *g_terminal = nullptr;
volatile std::sig_atomic_t g_signal = 0;

void onSignal(int sig) {
  g_signal = sig;
  if (g_terminal)
    g_terminal->disable();
  _exit(128 + sig);
}

// ---------------------------------------------------------------------------
// Board input model
// ---------------------------------------------------------------------------
// push_w bit order (see gecko.v):
//   0 joystick_pressed 1 joy_right 2 joy_left 3 joy_down 4 joy_up
//   5 center 6 button_right 7 button_left 8 button_bottom 9 button_top
enum Input {
  JoyPressed = 0,
  JoyRight = 1,
  JoyLeft = 2,
  JoyDown = 3,
  JoyUp = 4,
  Center = 5,
  ButtonRight = 6,
  ButtonLeft = 7,
  ButtonBottom = 8,
  ButtonTop = 9,
  kInputCount = 10,
};

const char *kInputName[kInputCount] = {"joy.press", "joy.right", "joy.left",
                                       "joy.down",  "joy.up",    "center",
                                       "right",     "left",      "bottom",
                                       "top"};

struct InputState {
  std::array<int, kInputCount> hold{}; // cycles left to keep the input high
  uint32_t switches = 0;               // 8 dip switches

  void press(int idx, int cycles) { hold[idx] = std::max(hold[idx], cycles); }
  bool down(int idx) const { return hold[idx] > 0; }
  void tick() {
    for (auto &h : hold)
      if (h > 0)
        --h;
  }
};

void applyInputs(sim_t &sim, const InputState &in) {
  set_joystick_pressed_i(*sim.g_top, in.down(JoyPressed));
  set_joystick_right_i(*sim.g_top, in.down(JoyRight));
  set_joystick_left_i(*sim.g_top, in.down(JoyLeft));
  set_joystick_down_i(*sim.g_top, in.down(JoyDown));
  set_joystick_up_i(*sim.g_top, in.down(JoyUp));
  set_button_center_i(*sim.g_top, in.down(Center));
  set_button_right_i(*sim.g_top, in.down(ButtonRight));
  set_button_left_i(*sim.g_top, in.down(ButtonLeft));
  set_button_bottom_i(*sim.g_top, in.down(ButtonBottom));
  set_button_top_i(*sim.g_top, in.down(ButtonTop));
  set_dip_switches_i(*sim.g_top, in.switches);
}

// ---------------------------------------------------------------------------
// Board readout
// ---------------------------------------------------------------------------
inline int bitOf(const VlWide<4> &w, int i) {
  return (w[i >> 5] >> (i & 31)) & 1;
}

bool ledsLit(sim_t &sim) {
  const VlWide<4> r = get_led_r_o(*sim.g_top);
  const VlWide<4> g = get_led_g_o(*sim.g_top);
  const VlWide<4> b = get_led_b_o(*sim.g_top);
  for (int i = 0; i < 120; ++i)
    if (bitOf(r, i) || bitOf(g, i) || bitOf(b, i))
      return true;
  return false;
}

// Short textual summary of the lit LEDs, so the piped/CI output is readable.
std::string ledSummary(sim_t &sim) {
  const VlWide<4> r = get_led_r_o(*sim.g_top);
  const VlWide<4> g = get_led_g_o(*sim.g_top);
  const VlWide<4> b = get_led_b_o(*sim.g_top);
  int nr = 0, ng = 0, nb = 0;
  for (int i = 0; i < 120; ++i) {
    nr += bitOf(r, i);
    ng += bitOf(g, i);
    nb += bitOf(b, i);
  }
  return "[" + std::to_string(nr) + "R " + std::to_string(ng) + "G " + std::to_string(nb) +
         "B]";
}

// Render the 120-LED panel as a 10-row x 12-column grid of coloured cells.
std::array<std::string, 10> renderLeds(sim_t &sim) {
  const VlWide<4> r = get_led_r_o(*sim.g_top);
  const VlWide<4> g = get_led_g_o(*sim.g_top);
  const VlWide<4> b = get_led_b_o(*sim.g_top);
  std::array<std::string, 10> rows;
  for (int row = 0; row < 10; ++row) {
    std::string line;
    for (int col = 0; col < 12; ++col) {
      const int i = row * 12 + col;
      const int rr = bitOf(r, i) ? 255 : 0;
      const int gg = bitOf(g, i) ? 255 : 0;
      const int bb = bitOf(b, i) ? 255 : 0;
      if (rr == 0 && gg == 0 && bb == 0) {
        line += bg(24, 26, 34) + "  " + kReset;
      } else {
        line += bg(rr, gg, bb) + "  " + kReset;
      }
    }
    rows[row] = line;
  }
  return rows;
}

// 3x5 glyph for one 7-segment digit (bits: a b c d e f g dp).
std::array<std::string, 5> segGlyph(uint8_t s) {
  const bool a = s & 0x01, b = s & 0x02, c = s & 0x04, d = s & 0x08;
  const bool e = s & 0x10, f = s & 0x20, g = s & 0x40;
  auto cell = [](bool on) -> std::string { return on ? kBlock : kEmpty; };
  return {
      cell(a) + cell(a) + cell(a),
      cell(f) + cell(g) + cell(b),
      cell(f) + cell(g) + cell(b),
      cell(e) + cell(g) + cell(c),
      cell(d) + cell(d) + cell(d),
  };
}

std::array<std::string, 5> renderSevenSeg(sim_t &sim) {
  const std::array<uint8_t, 4> digits = {
      static_cast<uint8_t>(get_sevensegment_1_o(*sim.g_top)),
      static_cast<uint8_t>(get_sevensegment_2_o(*sim.g_top)),
      static_cast<uint8_t>(get_sevensegment_3_o(*sim.g_top)),
      static_cast<uint8_t>(get_sevensegment_4_o(*sim.g_top)),
  };
  std::array<std::string, 5> out;
  for (int r = 0; r < 5; ++r) {
    std::string line;
    for (int d = 0; d < 4; ++d) {
      const auto glyph = segGlyph(digits[d]);
      line += glyph[r];
      line += (d < 3) ? " " : "";
    }
    out[r] = line;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Minimal instruction decoder (for the CPU panel)
// ---------------------------------------------------------------------------
const char *regName(uint32_t i) { return sim_t::regNames[i & 0x1f]; }

std::string csrName(uint32_t addr) {
  switch (addr) {
  case 0x300: return "mstatus";
  case 0x304: return "mie";
  case 0x305: return "mtvec";
  case 0x341: return "mepc";
  case 0x342: return "mcause";
  case 0x344: return "mip";
  default: return hex(addr, 3);
  }
}

int32_t sext(uint32_t v, int bits) {
  const uint32_t m = 1u << (bits - 1);
  return static_cast<int32_t>((v ^ m) - m);
}

std::string disasm(uint32_t in) {
  if (in == 0)
    return "unimp / blank";
  const uint32_t op = in & 0x7f;
  const uint32_t rd = (in >> 7) & 0x1f;
  const uint32_t f3 = (in >> 12) & 0x7;
  const uint32_t rs1 = (in >> 15) & 0x1f;
  const uint32_t rs2 = (in >> 20) & 0x1f;
  const uint32_t f7 = (in >> 25) & 0x7f;

  switch (op) {
  case 0x37: return "lui   " + std::string(regName(rd)) + ", " + hex(in >> 12, 5);
  case 0x17: return "auipc " + std::string(regName(rd)) + ", " + hex(in >> 12, 5);
  case 0x6f: {
    const int32_t imm = sext(((in >> 31) & 1) << 20 | ((in >> 12) & 0xff) << 12 |
                                 ((in >> 20) & 1) << 11 | ((in >> 21) & 0x3ff) << 1,
                             21);
    return "jal   " + std::string(regName(rd)) + ", " + std::to_string(imm);
  }
  case 0x67:
    return "jalr  " + std::string(regName(rd)) + ", " + std::to_string(sext(in >> 20, 12)) +
           "(" + regName(rs1) + ")";
  case 0x63: {
    const int32_t imm = sext(((in >> 31) & 1) << 12 | ((in >> 7) & 1) << 11 |
                                 ((in >> 25) & 0x3f) << 5 | ((in >> 8) & 0xf) << 1,
                             13);
    const char *mn = f3 == 0 ? "beq " : f3 == 1 ? "bne " : f3 == 4 ? "blt "
                      : f3 == 5 ? "bge " : f3 == 6 ? "bltu" : "bgeu";
    return std::string(mn) + "  " + regName(rs1) + ", " + regName(rs2) + ", " +
           std::to_string(imm);
  }
  case 0x03: {
    const char *mn = f3 == 2 ? "lw  " : f3 == 1 ? "lh  " : f3 == 0 ? "lb  " : "load";
    return std::string(mn) + "  " + regName(rd) + ", " + std::to_string(sext(in >> 20, 12)) +
           "(" + regName(rs1) + ")";
  }
  case 0x23: {
    const int32_t imm = sext(((in >> 25) & 0x7f) << 5 | ((in >> 7) & 0x1f), 12);
    return "sw    " + std::string(regName(rs2)) + ", " + std::to_string(imm) + "(" +
           regName(rs1) + ")";
  }
  case 0x13: {
    const int32_t imm = sext(in >> 20, 12);
    const char *mn = f3 == 0 ? "addi" : f3 == 7 ? "andi" : f3 == 6 ? "ori "
                      : f3 == 4 ? "xori" : f3 == 2 ? "slti" : f3 == 3 ? "sltiu"
                      : f3 == 1 ? "slli" : "op-imm";
    return std::string(mn) + "  " + regName(rd) + ", " + regName(rs1) + ", " +
           std::to_string(imm);
  }
  case 0x33: {
    const char *mn = "op";
    if (f3 == 0) mn = f7 == 0x20 ? "sub " : "add ";
    else if (f3 == 7) mn = "and ";
    else if (f3 == 6) mn = "or  ";
    else if (f3 == 4) mn = "xor ";
    else if (f3 == 2) mn = "slt ";
    else if (f3 == 3) mn = "sltu";
    return std::string(mn) + "  " + regName(rd) + ", " + regName(rs1) + ", " + regName(rs2);
  }
  case 0x73: {
    if (in == 0x00100073) return "ebreak";
    if (in == 0x30200073) return "mret";
    const char *mn = f3 == 1 ? "csrrw " : f3 == 2 ? "csrrs " : f3 == 3 ? "csrrc "
                      : f3 == 5 ? "csrrwi" : f3 == 6 ? "csrrsi" : "csrrci";
    const std::string csr = csrName(in >> 20);
    if (f3 >= 5)
      return std::string(mn) + " " + regName(rd) + ", " + csr + ", " + std::to_string(rs1);
    return std::string(mn) + " " + regName(rd) + ", " + csr + ", " + regName(rs1);
  }
  default:
    return "op 0x" + hex(op, 2).substr(2);
  }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct Options {
  bool demo = false;
  bool help = false;
  std::string program = "program.s";
  std::string bin = "out/gecko.bin";
  std::string exe = "out/gecko";
  double rate = 8000.0; // simulated cycles per real second
};

// CPU / board snapshot used by both the TUI and the demo renderer.
struct Snapshot {
  uint32_t pc = 0;
  uint32_t insn = 0;
  uint32_t cycles = 0;
  uint32_t game = 0;
  uint32_t countdown = 0;
  uint32_t mstatus = 0, mie = 0, mepc = 0, mcause = 0, mtvec = 0;
  uint32_t gpr[32]{};
  bool ledRed = false;
  bool ledGreen = false;
};

Snapshot capture(sim_t &sim, uint32_t cycles) {
  Snapshot s;
  s.cycles = cycles;
  s.pc = get_cpu_get_pc(*sim.g_top)();
  s.insn = get_mem_rd(*sim.g_top)(s.pc);
  s.game = get_mem_rd(*sim.g_top)(0x90000000);
  s.countdown = get_mem_rd(*sim.g_top)(kCountdownAddr);
  s.mstatus = get_cpu_get_csr(*sim.g_top)(0x300);
  s.mie = get_cpu_get_csr(*sim.g_top)(0x304);
  s.mepc = get_cpu_get_csr(*sim.g_top)(0x341);
  s.mcause = get_cpu_get_csr(*sim.g_top)(0x342);
  s.mtvec = get_cpu_get_csr(*sim.g_top)(0x305);
  for (int i = 0; i < 32; ++i)
    s.gpr[i] = get_cpu_get_gpr(*sim.g_top)(i);
  const VlWide<4> lr = get_led_r_o(*sim.g_top);
  const VlWide<4> lg = get_led_g_o(*sim.g_top);
  for (int i = 0; i < 120; ++i) {
    if (bitOf(lr, i))
      s.ledRed = true;
    if (bitOf(lg, i))
      s.ledGreen = true;
  }
  return s;
}

// ---------------------------------------------------------------------------
// Render a full frame
// ---------------------------------------------------------------------------
std::string buildFrame(sim_t &sim, const Options &opt, const InputState &in,
                       const Snapshot &s, bool running, bool paused,
                       const std::string &status) {
  const auto leds = renderLeds(sim);
  const auto seg = renderSevenSeg(sim);

  std::vector<std::string> right(11);
  right[0] = std::string(kBold) + "SEVEN-SEGMENT" + kReset;
  for (int i = 0; i < 5; ++i)
    right[i + 1] = "  " + seg[i];
  right[6] = "";
  right[7] = std::string(kBold) + "CPU" + kReset;
  right[8] = "  pc " + hex(s.pc, 8);
  right[9] = "  insn " + hex(s.insn, 8);

  std::string out;
  out += std::string(kBold) + " RISC-V multicycle CPU - interactive board" + kReset + "  ";
  const std::string state = paused ? std::string(fg(255, 196, 0)) + "PAUSED" + kReset
                            : running ? std::string(fg(120, 220, 120)) + "RUNNING" + kReset
                                      : std::string(fg(120, 220, 120)) + "DONE" + kReset;
  out += state + kDim + "   cycle " + dec(s.cycles) + kReset + "\n";
  out += kDim + std::string(78, '-') + kReset + "\n";

  out += std::string(kBold) + " LED PANEL (10x12)" + kReset + "            " + right[0] +
         "\n";
  for (int r = 0; r < 10; ++r) {
    out += " " + leds[r] + "   " + right[r + 1] + "\n";
  }

  // Inputs
  std::string inputs = std::string(kBold) + " INPUT" + kReset + "  center ";
  inputs += in.down(Center) ? std::string(fg(255, 210, 80)) + "o" + kReset
                            : std::string(kDim) + "." + kReset;
  inputs += "  top ";
  inputs += in.down(ButtonTop) ? std::string(fg(255, 210, 80)) + "o" + kReset
                               : std::string(kDim) + "." + kReset;
  inputs += "  bottom ";
  inputs += in.down(ButtonBottom) ? std::string(fg(255, 210, 80)) + "o" + kReset
                                  : std::string(kDim) + "." + kReset;
  inputs += "  left ";
  inputs += in.down(ButtonLeft) ? std::string(fg(255, 210, 80)) + "o" + kReset
                                : std::string(kDim) + "." + kReset;
  inputs += "  right ";
  inputs += in.down(ButtonRight) ? std::string(fg(255, 210, 80)) + "o" + kReset
                                 : std::string(kDim) + "." + kReset;
  out += " " + inputs + "\n";

  std::string joy = std::string(kBold) + " JOY  " + kReset + " up ";
  joy += in.down(JoyUp) ? std::string(fg(120, 200, 255)) + "o" + kReset
                        : std::string(kDim) + "." + kReset;
  joy += "  down ";
  joy += in.down(JoyDown) ? std::string(fg(120, 200, 255)) + "o" + kReset
                          : std::string(kDim) + "." + kReset;
  joy += "  left ";
  joy += in.down(JoyLeft) ? std::string(fg(120, 200, 255)) + "o" + kReset
                          : std::string(kDim) + "." + kReset;
  joy += "  right ";
  joy += in.down(JoyRight) ? std::string(fg(120, 200, 255)) + "o" + kReset
                           : std::string(kDim) + "." + kReset;
  joy += "  pressed ";
  joy += in.down(JoyPressed) ? std::string(fg(120, 200, 255)) + "o" + kReset
                             : std::string(kDim) + "." + kReset;
  out += " " + joy + "\n";

  // CPU / CSR / game state
  out += std::string(kBold) + " CPU  " + kReset + "x5/t0 " + hex(s.gpr[5], 8) +
         "  sp " + hex(s.gpr[2], 8) + "  ra " + hex(s.gpr[1], 8) + "  a0 " +
         hex(s.gpr[10], 8) + "\n";
  out += std::string(kBold) + " CSR  " + kReset + "mstatus " + hex(s.mstatus, 8) +
         "  mie " + hex(s.mie, 8) + "  mtvec " + hex(s.mtvec, 8) + "\n";
  out += std::string(kBold) + "      " + kReset + "mepc " + hex(s.mepc, 8) +
         "  mcause " + hex(s.mcause, 8) + "  last round: " +
         (s.ledGreen && !s.ledRed
              ? std::string(fg(120, 230, 120)) + "WIN" + kReset
          : s.ledRed ? std::string(fg(255, 90, 90)) + "LOSE" + kReset
                     : std::string(kDim) + "waiting..." + kReset) +
         "\n";

  // Countdown bar published by program.s each loop iteration (see COUNTDOWN_CYCLES).
  {
    constexpr int barWidth = 24;
    const uint32_t c = std::min(s.countdown, static_cast<uint32_t>(kCountdownTotal));
    const int filled =
        static_cast<int>(static_cast<uint64_t>(c) * barWidth / kCountdownTotal);
    const int pct = static_cast<int>(static_cast<uint64_t>(c) * 100 / kCountdownTotal);
    const std::string barColor = c > kCountdownTotal / 2   ? fg(120, 230, 120)
                                 : c > kCountdownTotal / 5 ? fg(255, 210, 80)
                                                           : fg(255, 90, 90);
    std::string bar = barColor;
    for (int i = 0; i < filled; ++i)
      bar += kBlock;
    bar += kReset;
    bar += kDim;
    for (int i = filled; i < barWidth; ++i)
      bar += kShade;
    bar += kReset;
    out += std::string(kBold) + " TIMER " + kReset + bar + "  " +
           (c > 0 ? std::to_string(pct) + "%  press [c] to win this round"
                  : std::string(kDim) + "next round..." + kReset) +
           "\n";
  }
  out += std::string(kDim) + " [space] run/pause   [.] step   [R] reset   [q] quit\n" +
         kReset;
  out += std::string(kDim) +
         " [c]enter [t]op [b]ottom [l]eft [r]ight   arrows = joystick   [p] press\n" +
         kReset;
  out += std::string(kDim) + " [1]..[8] toggle dip switches\n" + kReset;
  if (!status.empty())
    out += " " + status + "\n";
  return out;
}

// ---------------------------------------------------------------------------
// Simulation stepping wrapper
// ---------------------------------------------------------------------------
class Machine {
public:
  Machine(sim_t &sim, Options opt) : sim_(sim), opt_(std::move(opt)) {}

  void boot() {
    // Load the firmware only once: PcToLine::registerFile() calls exit() if it
    // is asked to load the same ELF twice, so a "reset" is a real hardware
    // reset (assert rst_ni for a few cycles) rather than a reload.
    if (!loaded_) {
      sim_.load(opt_.program, opt_.bin, opt_.exe);
      loaded_ = true;
    }
    sim_.g_ctx->time(0);
    cycles_ = 0;
    in_ = InputState{};
    for (int i = 0; i < 6; ++i)
      stepCycle(); // hold reset, then release it
  }

  // One clocked simulation cycle with the current input levels applied.
  void stepCycle() {
    applyInputs(sim_, in_);
    sim_.runclk();
    ++cycles_;
    in_.tick();
  }

  InputState &inputs() { return in_; }
  uint32_t cycles() const { return cycles_; }
  sim_t &sim() { return sim_; }

private:
  sim_t &sim_;
  Options opt_;
  InputState in_;
  uint32_t cycles_ = 0;
  bool loaded_ = false;
};

// ---------------------------------------------------------------------------
// Demo mode: scripted lose -> win walkthrough
// ---------------------------------------------------------------------------
void printBoard(sim_t &sim) {
  const auto leds = renderLeds(sim);
  for (int r = 0; r < 10; ++r)
    std::cout << "  " << leds[r] << "\n";
}

void runDemo(sim_t &sim, Options opt) {
  std::cout << kBold << "\n RISC-V multicycle CPU - live demo\n" << kReset;
  std::cout << kDim << " Driving the real elaborated CPU + peripherals "
            << "through the Verilator harness.\n" << kReset;

  const uint32_t kMaxCycles = 400000;

  // A single machine: the ELF can only be registered with libdw once, so the
  // two scenes share one Machine and use a hardware reset in between.
  Machine m(sim, opt);

  // --- Scene 1: nobody presses the button -> the program decides LOSE ---
  std::cout << "\n " << kBold << "[1/2] Wait for the button... but never press it"
            << kReset << "\n";
  {
    m.boot();
    while (!ledsLit(sim) && m.cycles() < kMaxCycles)
      m.stepCycle();
    const auto s = capture(sim, m.cycles());
    std::cout << "       countdown exhausted with no button press -> "
              << fg(255, 90, 90) << "LOSE (red)" << kReset << "\n";
    std::cout << kDim << "       LEDs: " << kReset << ledSummary(sim) << "\n";
    printBoard(sim);
  }

  // --- Scene 2: press the center button -> interrupt -> WIN ---
  std::cout << "\n " << kBold << "[2/2] Press the center button before the wait ends"
            << kReset << "\n";
  {
    m.boot();
    // Let the setup code run, then simulate a real button press.
    for (int i = 0; i < 2000; ++i)
      m.stepCycle();
    std::cout << "       injecting a center-button press (rising edge on "
              << "0x70000000)\n";
    m.inputs().press(Center, 64);
    uint32_t irqCycle = 0;
    while (!ledsLit(sim) && m.cycles() < kMaxCycles) {
      m.stepCycle();
      if (irqCycle == 0 && get_cpu_get_csr(*sim.g_top)(0x342) != 0)
        irqCycle = m.cycles();
    }
    const auto s = capture(sim, m.cycles());
    std::cout << "       trap taken, mcause = " << hex(s.mcause, 8) << ", mepc = "
              << hex(s.mepc, 8) << " at cycle " << irqCycle << "\n";
    std::cout << "       handler set the game state -> " << fg(120, 230, 120) << "WIN"
              << kReset << " (green)\n";
    std::cout << kDim << "       LEDs: " << kReset << ledSummary(sim) << "\n";
    printBoard(sim);
  }

  std::cout << "\n " << kBold << "Result:" << kReset << " the simulated CPU "
            << fg(120, 230, 120) << "responded to the interrupt" << kReset
            << " and drove the LEDs green.  The firmware now loops, so run " << kBold
            << "make play" << kReset << " to keep playing round after round.\n\n";
}

// ---------------------------------------------------------------------------
// Interactive mode
// ---------------------------------------------------------------------------
void runInteractive(sim_t &sim, Options opt) {
  RawTerminal term;
  if (!term.enable()) {
    std::cerr << "This mode needs an interactive terminal.\n"
              << "Try:  make demo\n"
              << "or within Docker:  docker run --rm -it -v \"$PWD:/workspace\" "
                 "-w /workspace riscv-multicycle-cpu play\n";
    return;
  }
  g_terminal = &term;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  Machine m(sim, opt);
  m.boot();

  bool paused = false;
  bool quit = false;
  int lastResult = 0; // 0 = waiting, 1 = win (green), 2 = lose (red)
  std::string status = "Press [c] or [Enter] to trigger the button interrupt.";
  std::string pending; // partial escape sequence carried between polls

  auto last = std::chrono::steady_clock::now();
  auto lastRender = last;
  double debt = 0.0;

  while (!quit) {
    if (g_signal)
      break;

    // --- keyboard ---
    std::string keys = pending + term.poll();
    pending.clear();
    for (size_t i = 0; i < keys.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(keys[i]);
      if (c == 0x1b) {
        // Escape sequence (arrow keys). Keep a partial sequence for the next poll.
        if (i + 2 >= keys.size()) {
          pending = keys.substr(i);
          break;
        }
        if (keys[i + 1] == '[') {
          const char code = keys[i + 2];
          if (code == 'A') m.inputs().press(JoyUp, 64);
          else if (code == 'B') m.inputs().press(JoyDown, 64);
          else if (code == 'C') m.inputs().press(JoyRight, 64);
          else if (code == 'D') m.inputs().press(JoyLeft, 64);
          i += 2;
        }
        continue;
      }
      switch (c) {
      case 'q':
      case 3: // Ctrl-C
        quit = true;
        break;
      case ' ':
        paused = !paused;
        status = paused ? "Paused. [.] to single-step, [space] to resume."
                        : "Running.";
        break;
      case '.':
      case 'n':
        m.stepCycle();
        status = "Single-stepped " + dec(m.cycles()) + " cycles.";
        break;
      case 'R':
        m.boot();
        paused = false;
        lastResult = 0;
        status = "Reset. Press [c] or [Enter] to trigger the button interrupt.";
        break;
      case 'c':
      case '\r':
      case '\n':
        m.inputs().press(Center, 64);
        status = "Center button pressed - watch mcause and the LEDs.";
        break;
      case 't': m.inputs().press(ButtonTop, 64); break;
      case 'b': m.inputs().press(ButtonBottom, 64); break;
      case 'l': m.inputs().press(ButtonLeft, 64); break;
      case 'r': m.inputs().press(ButtonRight, 64); break;
      case 'p': m.inputs().press(JoyPressed, 64); break;
      default:
        if (c >= '1' && c <= '8') {
          const uint32_t bit = 1u << (c - '1');
          m.inputs().switches ^= bit;
        }
        break;
      }
    }

    // --- advance simulation in real time (endless) ---
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    if (!paused) {
      debt += opt.rate * dt;
      uint64_t toRun = static_cast<uint64_t>(debt);
      debt -= static_cast<double>(toRun);
      for (uint64_t i = 0; i < toRun; ++i)
        m.stepCycle();

      // Announce each round result when the LED colour changes.
      const bool green = get_led_g_o(*sim.g_top)[0] != 0;
      const bool red = get_led_r_o(*sim.g_top)[0] != 0;
      const int result = green && !red ? 1 : red ? 2 : 0;
      if (result != lastResult) {
        lastResult = result;
        if (result == 1)
          status = std::string(fg(120, 230, 120)) +
                   "YOU WIN - LEDs are green. Keep pressing for the next round." + kReset;
        else if (result == 2)
          status = std::string(fg(255, 90, 90)) +
                   "GAME OVER - LEDs are red. Press [c] before the countdown ends." + kReset;
      }
    } else {
      debt = 0.0;
    }

    // --- render at ~30 fps ---
    if (std::chrono::duration<double>(now - lastRender).count() > 1.0 / 30.0) {
      const auto snap = capture(sim, m.cycles());
      const WinSize ws = terminalSize();
      std::string shown = status;
      if (ws.cols < 60 || ws.rows < 16)
        shown = "Terminal too small - please enlarge the window.";
      std::string frame = std::string(kHome) +
                          fitFrame(buildFrame(sim, opt, m.inputs(), snap, !paused,
                                              paused, shown),
                                   ws) +
                          kClearToEnd;
      std::fwrite(frame.data(), 1, frame.size(), stdout);
      std::fflush(stdout);
      lastRender = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  term.disable();
  std::cout << "Goodbye.\n";
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
Options parseArgs(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--demo" || a == "-d")
      opt.demo = true;
    else if (a == "--help" || a == "-h")
      opt.help = true;
    else if (a == "--rate" && i + 1 < argc)
      opt.rate = std::atof(argv[++i]);
    else if (a == "--program" && i + 1 < argc)
      opt.program = argv[++i];
    else if (a == "--bin" && i + 1 < argc)
      opt.bin = argv[++i];
    else if (a == "--exe" && i + 1 < argc)
      opt.exe = argv[++i];
    else if (a == "--no-color")
      g_color = false;
  }
  return opt;
}

void printHelp() {
  std::cout <<
      "RISC-V multicycle CPU - interactive board\n\n"
      "Usage: Vplay [options]\n\n"
      "  --demo, -d        run the scripted lose -> win walkthrough\n"
      "  --rate <hz>       simulated cycles per real second (default 8000)\n"
      "  --program <s>     assembly source used for source locations\n"
      "  --bin <path>      firmware image (default out/gecko.bin)\n"
      "  --exe <path>      ELF used for debug line info (default out/gecko)\n"
      "  --no-color        disable ANSI colour\n"
      "  --help, -h        show this help\n\n"
      "Interactive keys:\n"
      "  c / Enter   press center button (fires the interrupt in program.s)\n"
      "  t b l r     top / bottom / left / right buttons\n"
      "  arrows / p  joystick direction / pressed\n"
      "  1..8        toggle dip switches\n"
      "  space       run / pause          . step once\n"
      "  R           reset the simulation q quit\n";
}

} // namespace

int main(int argc, char **argv) {
  const Options opt = parseArgs(argc, argv);
  if (opt.help) {
    printHelp();
    return 0;
  }

  if (!isatty(STDOUT_FILENO))
    g_color = false;

  sim_t sim;
  sim.registerEventHandlers([](sim_t::Event, std::optional<std::string>) {});
  if (!std::filesystem::exists(opt.bin)) {
    std::cerr << "Firmware image not found: " << opt.bin
              << "\nBuild it first (e.g. `make build_gecko`) or run via `make play` / "
                 "`make demo`.\n";
    return 1;
  }

  if (opt.demo || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    runDemo(sim, opt);
  } else {
    runInteractive(sim, opt);
  }
  return 0;
}
