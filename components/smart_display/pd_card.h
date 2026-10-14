#pragma once
// Live PD data lives only on PD tiles, like the Energy card's data block.
#include <array>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
namespace pd_card {
struct Battery {
  std::string name, connection, glow;
  std::array<float, 11> values{}; // soc, kWh, charge, cap, set, actual, and the three percentages
  uint8_t key = 0;
  bool eligible = false, online = false, solar = false;
};
struct Action {
  uint8_t key = 0;
  bool none = false;
  std::string entity, service;
  std::vector<std::pair<std::string, std::string>> data, templates;
};
struct Data {
  bool available = false, controller = false, burst = false;
  uint32_t color = 0;
  std::string mode, story, glance, footer;
  std::array<float, 3> power{}; // signed grid, target and allocated total
  std::vector<Battery> batteries;
  std::vector<Action> actions;
};
}  // namespace pd_card
