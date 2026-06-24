#include "include/get_thumbnail_video/get_thumbnail_video_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "get_thumbnail_video_plugin.h"

void GetThumbnailVideoPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  get_thumbnail_video::GetThumbnailVideoPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
