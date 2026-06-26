#ifndef FLUTTER_PLUGIN_GET_THUMBNAIL_VIDEO_PLUGIN_H_
#define FLUTTER_PLUGIN_GET_THUMBNAIL_VIDEO_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>

namespace get_thumbnail_video {

class GetThumbnailVideoPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  GetThumbnailVideoPlugin();

  virtual ~GetThumbnailVideoPlugin();

  // Disallow copy and assign.
  GetThumbnailVideoPlugin(const GetThumbnailVideoPlugin &) = delete;
  GetThumbnailVideoPlugin &operator=(const GetThumbnailVideoPlugin &) = delete;

  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

}  // namespace get_thumbnail_video

#endif  // FLUTTER_PLUGIN_GET_THUMBNAIL_VIDEO_PLUGIN_H_
