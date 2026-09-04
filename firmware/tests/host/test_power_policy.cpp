#include "codex/power_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kDefaultAutoShutdownTimeoutSeconds == 7200);
  require(valid_auto_shutdown_timeout(0));
  require(valid_auto_shutdown_timeout(3600));
  require(valid_auto_shutdown_timeout(7200));
  require(valid_auto_shutdown_timeout(10800));
  require(valid_auto_shutdown_timeout(18000));
  require(!valid_auto_shutdown_timeout(4000));
  require(valid_auto_ultra_timeout(0));
  require(valid_auto_ultra_timeout(3600));
  require(valid_auto_ultra_timeout(10800));
  require(valid_auto_ultra_timeout(18000));
  require(valid_auto_ultra_timeout(28800));
  require(valid_auto_ultra_timeout(43200));
  require(!valid_auto_ultra_timeout(7200));
  UltraStandbyGuard guard{};
  require(update_ultra_standby_guard(guard, false, 10));
  require(!update_ultra_standby_guard(guard, true, 20));
  require(!update_ultra_standby_guard(guard, false, 30));
  require(!update_ultra_standby_guard(
      guard, false, 30 + kUltraStandbyGraceMs - 1));
  require(update_ultra_standby_guard(
      guard, false, 30 + kUltraStandbyGraceMs));
  require(!update_ultra_standby_guard(guard, true, 0xFFFFFFF0U));
  require(!update_ultra_standby_guard(guard, false, 0xFFFFFFF8U));
  require(update_ultra_standby_guard(
      guard, false, 0xFFFFFFF8U + kUltraStandbyGraceMs));
  require(!should_auto_shutdown({7200000, 7200000, 7199999, false, false}));
  require(should_auto_shutdown({7200000, 1, 7200000, false, false}));
  require(!should_auto_shutdown({7200000, 7200000, 7200000, true, true}));
  require(should_auto_shutdown({7200000, 7200000, 1, false, true}));
  require(evaluate_power_deadline({
              180000, 7200000, 179999, 8000000, false, false,
              true}) == PowerDeadline::None);
  require(evaluate_power_deadline({
              180000, 7200000, 180000, 8000000, false, false,
              true}) == PowerDeadline::Screensaver);
  require(evaluate_power_deadline({
              180000, 7200000, 7200001, 7200000, false, false,
              true}) == PowerDeadline::AutoShutdown);
  require(evaluate_power_deadline({
              180000, 7200000, 7200001, 7200000, true, false,
              true}) == PowerDeadline::Screensaver);
  require(evaluate_power_deadline({
              180000, 7200000, 7200001, 7200000, false, true,
              true}) == PowerDeadline::AutoShutdown);
  require(evaluate_power_deadline({
              180000, 7200000, 7200001, 7200000, false, false,
              false}) == PowerDeadline::AutoShutdown);
  require(evaluate_power_deadline({
              180000, 7200000, 3600000, 1, true, false, true,
              3600000, true, true}) == PowerDeadline::UltraStandby);
  require(evaluate_power_deadline({
              180000, 7200000, 3600000, 1, true, false, true,
              3600000, false, true}) == PowerDeadline::Screensaver);
  require(evaluate_power_deadline({
              180000, 7200000, 3600000, 1, true, false, true,
              3600000, true, false}) == PowerDeadline::Screensaver);
  require(evaluate_power_deadline({
              180000, 3600000, 7200000, 3600000, false, false, true,
              3600000, true, true}) == PowerDeadline::AutoShutdown);
  require(evaluate_power_deadline({
              180000, 7200000, 3600000, 1, true, false, false,
              3600000, true, true}) == PowerDeadline::UltraStandby);
  ExitProcess(failures);
}
