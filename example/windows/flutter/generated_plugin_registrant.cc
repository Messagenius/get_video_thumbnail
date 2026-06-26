//
//  Generated file. Do not edit.
//

// clang-format off

#include "generated_plugin_registrant.h"

#include <file_selector_windows/file_selector_windows.h>
#include <get_thumbnail_video/get_thumbnail_video_plugin_c_api.h>

void RegisterPlugins(flutter::PluginRegistry* registry) {
  FileSelectorWindowsRegisterWithRegistrar(
      registry->GetRegistrarForPlugin("FileSelectorWindows"));
  GetThumbnailVideoPluginCApiRegisterWithRegistrar(
      registry->GetRegistrarForPlugin("GetThumbnailVideoPluginCApi"));
}
