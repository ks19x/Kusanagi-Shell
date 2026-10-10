#pragma once

#include "shell/bar/bar_services.h"
#include "shell/bar/widget.h"

#include <memory>
#include <string>

struct Config;
class ConfigService;
class FileWatcher;
class CompositorPlatform;
class NotificationManager;
class HttpClient;
class IdleInhibitor;
class LockKeysService;
class MprisService;
class ModemManagerService;
class BluetoothService;
class BrightnessService;
class ClipboardService;
class EasyEffectsService;
class ExternalIpService;
class RenderContext;
class ScreenshotService;
class INetworkService;
class PipeWireService;
class PipeWireSpectrum;
class PowerProfilesService;
class TrayService;
class SystemMonitorService;
class UPowerService;
class WeatherService;
struct wl_output;
struct KusanagiModuleServices;
class GammaService;
namespace kusanagi::theme {
  class ThemeService;
}
namespace scripting {
  class ScriptApiContext;
}

class WidgetFactory {
public:
  explicit WidgetFactory(const BarServices& services);
  ~WidgetFactory();

  [[nodiscard]] std::unique_ptr<Widget> create(
      const std::string& name, wl_output* output, float contentScale = 1.0F, const std::string& barPosition = "top",
      const std::string& barName = "default", float widgetSpacing = 6.0F, bool enableScroll = true
  ) const;

  // The settings bar preview builds real bar widgets outside any bar. current() is the factory
  // the running bar uses (nullptr before the bar starts).
  [[nodiscard]] static const WidgetFactory* current() noexcept;
  [[nodiscard]] KusanagiModuleServices kusanagiServices() const;
  [[nodiscard]] CompositorPlatform& platform() const noexcept { return m_platform; }

private:
  CompositorPlatform& m_platform;
  ConfigService& m_configService;
  const Config& m_config;
  NotificationManager* m_notifications;
  TrayService* m_tray;
  PipeWireService* m_audio;
  EasyEffectsService* m_easyEffects;
  UPowerService* m_upower;
  SystemMonitorService* m_sysmon;
  PowerProfilesService* m_powerProfiles;
  INetworkService* m_network;
  ModemManagerService* m_modem;
  ExternalIpService* m_externalIp;
  IdleInhibitor* m_idleInhibitor;
  MprisService* m_mpris;
  PipeWireSpectrum* m_audioSpectrum;
  HttpClient* m_httpClient;
  WeatherService* m_weather;
  GammaService* m_nightLight;
  kusanagi::theme::ThemeService* m_themeService;
  BluetoothService* m_bluetooth;
  BrightnessService* m_brightness;
  LockKeysService* m_lockKeys;
  ClipboardService* m_clipboard;
  FileWatcher* m_fileWatcher;
  ScreenshotService* m_screenshots;
  RenderContext* m_renderContext = nullptr;
  scripting::ScriptApiContext* m_scriptApi = nullptr;
};
