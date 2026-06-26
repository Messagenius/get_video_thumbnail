#include "get_thumbnail_video_plugin.h"

// This must be included before many other Windows headers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace get_thumbnail_video {

namespace {

std::wstring Utf8ToWide(const std::string &str) {
  if (str.empty()) return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                 static_cast<int>(str.size()), nullptr, 0);
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                      &result[0], size);
  return result;
}

std::string WideToUtf8(const std::wstring &str) {
  if (str.empty()) return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(),
                                 static_cast<int>(str.size()), nullptr, 0,
                                 nullptr, nullptr);
  std::string result(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                      &result[0], size, nullptr, nullptr);
  return result;
}

int GetInt(const flutter::EncodableMap &map, const char *key, int def) {
  auto it = map.find(flutter::EncodableValue(std::string(key)));
  if (it == map.end()) return def;
  if (const auto *v = std::get_if<int>(&it->second)) return *v;
  if (const auto *v = std::get_if<int64_t>(&it->second))
    return static_cast<int>(*v);
  return def;
}

std::string GetString(const flutter::EncodableMap &map, const char *key) {
  auto it = map.find(flutter::EncodableValue(std::string(key)));
  if (it == map.end()) return std::string();
  if (const auto *v = std::get_if<std::string>(&it->second)) return *v;
  return std::string();
}

std::map<std::string, std::string> GetStringMap(
    const flutter::EncodableMap &map, const char *key) {
  std::map<std::string, std::string> result;
  auto it = map.find(flutter::EncodableValue(std::string(key)));
  if (it == map.end()) return result;
  if (const auto *m = std::get_if<flutter::EncodableMap>(&it->second)) {
    for (const auto &kv : *m) {
      const auto *k = std::get_if<std::string>(&kv.first);
      const auto *v = std::get_if<std::string>(&kv.second);
      if (k && v) result[*k] = *v;
    }
  }
  return result;
}

// RAII wrapper that closes a WinHTTP handle when it goes out of scope.
struct HInternetCloser {
  void operator()(HINTERNET h) const {
    if (h) WinHttpCloseHandle(h);
  }
};
using ScopedHInternet =
    std::unique_ptr<std::remove_pointer<HINTERNET>::type, HInternetCloser>;

// Downloads the contents of `url` to `outFilePath` using WinHTTP. Custom
// request headers are applied. Media Foundation cannot reliably resolve
// remote (especially HTTPS) sources on desktop, so we fetch them ourselves
// first. Returns false and populates `error` on any failure.
bool DownloadUrlToFile(const std::wstring &url,
                       const std::map<std::string, std::string> &headers,
                       const std::wstring &outFilePath, std::string *error) {
  URL_COMPONENTSW comp = {};
  comp.dwStructSize = sizeof(comp);
  wchar_t host[256] = {};
  wchar_t path[4096] = {};
  comp.lpszHostName = host;
  comp.dwHostNameLength = ARRAYSIZE(host);
  comp.lpszUrlPath = path;
  comp.dwUrlPathLength = ARRAYSIZE(path);

  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &comp)) {
    if (error) *error = "Invalid URL";
    return false;
  }

  ScopedHInternet session(WinHttpOpen(
      L"get_thumbnail_video/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    if (error) *error = "WinHttpOpen failed";
    return false;
  }

  ScopedHInternet connect(
      WinHttpConnect(session.get(), host, comp.nPort, 0));
  if (!connect) {
    if (error) *error = "WinHttpConnect failed";
    return false;
  }

  const bool isHttps = comp.nScheme == INTERNET_SCHEME_HTTPS;
  ScopedHInternet request(WinHttpOpenRequest(
      connect.get(), L"GET", path, nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, isHttps ? WINHTTP_FLAG_SECURE : 0));
  if (!request) {
    if (error) *error = "WinHttpOpenRequest failed";
    return false;
  }

  // Follow redirects (common for CDN-hosted samples).
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                   &redirectPolicy, sizeof(redirectPolicy));

  for (const auto &kv : headers) {
    const std::wstring header = Utf8ToWide(kv.first + ": " + kv.second);
    WinHttpAddRequestHeaders(request.get(), header.c_str(),
                             static_cast<DWORD>(header.size()),
                             WINHTTP_ADDREQ_FLAG_ADD |
                                 WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    if (error) *error = "WinHttpSendRequest failed";
    return false;
  }
  if (!WinHttpReceiveResponse(request.get(), nullptr)) {
    if (error) *error = "WinHttpReceiveResponse failed";
    return false;
  }

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.get(),
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
          WINHTTP_NO_HEADER_INDEX) ||
      status < 200 || status >= 300) {
    if (error) *error = "HTTP status " + std::to_string(status);
    return false;
  }

  HANDLE file = CreateFileW(outFilePath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error) *error = "Failed to open temp file";
    return false;
  }

  std::vector<char> buffer(64 * 1024);
  bool ok = true;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      ok = false;
      if (error) *error = "WinHttpQueryDataAvailable failed";
      break;
    }
    if (available == 0) break;
    const DWORD toRead =
        (std::min)(static_cast<DWORD>(buffer.size()), available);
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), toRead, &read)) {
      ok = false;
      if (error) *error = "WinHttpReadData failed";
      break;
    }
    if (read == 0) break;
    DWORD written = 0;
    if (!WriteFile(file, buffer.data(), read, &written, nullptr) ||
        written != read) {
      ok = false;
      if (error) *error = "WriteFile failed";
      break;
    }
  }

  CloseHandle(file);
  return ok;
}

struct DecodedFrame {
  std::vector<uint8_t> pixels;  // 32bpp BGRA, top-down.
  UINT width = 0;
  UINT height = 0;
  UINT stride = 0;
};

std::string HrToString(HRESULT hr) {
  char buf[16];
  snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(hr));
  return buf;
}

// Decodes a single RGB32 frame at `timeMs` from `source` (a file path or URL)
// using Media Foundation. Returns false on any failure and, if `error` is
// non-null, populates it with a description of which step failed.
bool DecodeFrame(const std::wstring &source, int64_t timeMs, DecodedFrame *out,
                 std::string *error) {
  auto fail = [&](const char *step, HRESULT hr) {
    if (error) *error = std::string(step) + " failed (hr=" + HrToString(hr) + ")";
    return false;
  };

  // Enabling video processing lets the source reader insert MF's color
  // converter so we can request RGB32 even when the decoder outputs YUV
  // (NV12/YUY2/etc.). Without this most H.264 sources fail SetCurrentMediaType.
  ComPtr<IMFAttributes> readerAttrs;
  HRESULT hr = MFCreateAttributes(&readerAttrs, 1);
  if (FAILED(hr)) return fail("MFCreateAttributes", hr);
  readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

  ComPtr<IMFSourceReader> reader;
  hr = MFCreateSourceReaderFromURL(source.c_str(), readerAttrs.Get(), &reader);
  if (FAILED(hr)) return fail("MFCreateSourceReaderFromURL", hr);

  reader->SetStreamSelection(
      static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
  reader->SetStreamSelection(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

  // Ask the reader to decode to uncompressed RGB32 (BGRA byte order).
  ComPtr<IMFMediaType> outType;
  hr = MFCreateMediaType(&outType);
  if (FAILED(hr)) return fail("MFCreateMediaType", hr);
  outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  hr = reader->SetCurrentMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
      outType.Get());
  if (FAILED(hr)) return fail("SetCurrentMediaType(RGB32)", hr);

  ComPtr<IMFMediaType> currentType;
  hr = reader->GetCurrentMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType);
  if (FAILED(hr)) return fail("GetCurrentMediaType", hr);

  UINT32 width = 0, height = 0;
  hr = MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
  if (FAILED(hr) || width == 0 || height == 0)
    return fail("MFGetAttributeSize(FRAME_SIZE)", hr);

  LONG defaultStride = 0;
  if (FAILED(currentType->GetUINT32(
          MF_MT_DEFAULT_STRIDE,
          reinterpret_cast<UINT32 *>(&defaultStride))) ||
      defaultStride == 0) {
    defaultStride = static_cast<LONG>(width) * 4;
  }

  // Seek to the requested timestamp (100-ns units).
  if (timeMs > 0) {
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = timeMs * 10000;
    reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
  }

  // Read until we obtain a sample (decoders may return empty reads while
  // seeking/priming).
  ComPtr<IMFSample> sample;
  for (int i = 0; i < 60; ++i) {
    DWORD actualStream = 0;
    DWORD streamFlags = 0;
    LONGLONG ts = 0;
    sample.Reset();
    hr = reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
        &actualStream, &streamFlags, &ts, &sample);
    if (FAILED(hr)) return fail("ReadSample", hr);
    if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    if (sample) break;
  }
  if (!sample) {
    if (error) *error = "No video sample produced (end of stream before any frame)";
    return false;
  }

  ComPtr<IMFMediaBuffer> buffer;
  hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) return fail("ConvertToContiguousBuffer", hr);

  BYTE *data = nullptr;
  DWORD maxLen = 0, curLen = 0;
  hr = buffer->Lock(&data, &maxLen, &curLen);
  if (FAILED(hr)) return fail("IMFMediaBuffer::Lock", hr);

  const UINT outStride = width * 4;
  out->width = width;
  out->height = height;
  out->stride = outStride;
  out->pixels.resize(static_cast<size_t>(outStride) * height);

  // Normalize to a top-down buffer regardless of the source orientation.
  const LONG absStride = defaultStride < 0 ? -defaultStride : defaultStride;
  for (UINT row = 0; row < height; ++row) {
    const BYTE *srcRow =
        defaultStride >= 0
            ? data + static_cast<size_t>(absStride) * row
            : data + static_cast<size_t>(absStride) * (height - 1 - row);
    memcpy(out->pixels.data() + static_cast<size_t>(outStride) * row, srcRow,
           outStride);
  }

  buffer->Unlock();
  return true;
}

// Encodes a decoded frame to JPEG (format 0) or PNG (format 1) bytes using WIC,
// optionally downscaling to fit within maxw x maxh (preserving aspect ratio).
bool EncodeImage(const DecodedFrame &frame, int format, int quality, int maxw,
                 int maxh, std::vector<uint8_t> *out) {
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr =
      CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return false;

  ComPtr<IWICBitmap> bitmap;
  hr = factory->CreateBitmapFromMemory(
      frame.width, frame.height, GUID_WICPixelFormat32bppBGRA, frame.stride,
      static_cast<UINT>(frame.pixels.size()),
      const_cast<BYTE *>(frame.pixels.data()), &bitmap);
  if (FAILED(hr)) return false;

  ComPtr<IWICBitmapSource> sourceImage = bitmap;
  UINT targetW = frame.width;
  UINT targetH = frame.height;

  if (maxw > 0 || maxh > 0) {
    double scale = 1.0;
    if (maxw > 0)
      scale = (std::min)(scale, static_cast<double>(maxw) / frame.width);
    if (maxh > 0)
      scale = (std::min)(scale, static_cast<double>(maxh) / frame.height);
    if (scale < 1.0) {
      targetW = (std::max)(static_cast<UINT>(1), static_cast<UINT>(frame.width * scale));
      targetH = (std::max)(static_cast<UINT>(1), static_cast<UINT>(frame.height * scale));
      ComPtr<IWICBitmapScaler> scaler;
      if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
          SUCCEEDED(scaler->Initialize(bitmap.Get(), targetW, targetH,
                                       WICBitmapInterpolationModeFant))) {
        sourceImage = scaler;
      }
    }
  }

  ComPtr<IStream> stream;
  hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  if (FAILED(hr)) return false;

  const GUID container =
      (format == 1) ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg;
  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(container, nullptr, &encoder);
  if (FAILED(hr)) return false;
  if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
    return false;

  ComPtr<IWICBitmapFrameEncode> frameEncode;
  ComPtr<IPropertyBag2> props;
  if (FAILED(encoder->CreateNewFrame(&frameEncode, &props))) return false;

  if (format == 0) {
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = static_cast<float>(quality * 0.01);
    props->Write(1, &option, &value);
  }

  if (FAILED(frameEncode->Initialize(props.Get()))) return false;
  frameEncode->SetSize(targetW, targetH);
  WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
  frameEncode->SetPixelFormat(&pf);
  if (FAILED(frameEncode->WriteSource(sourceImage.Get(), nullptr))) return false;
  if (FAILED(frameEncode->Commit())) return false;
  if (FAILED(encoder->Commit())) return false;

  STATSTG stat = {};
  if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return false;
  const ULONG size = static_cast<ULONG>(stat.cbSize.QuadPart);
  out->resize(size);
  LARGE_INTEGER li = {};
  stream->Seek(li, STREAM_SEEK_SET, nullptr);
  ULONG read = 0;
  stream->Read(out->data(), size, &read);
  out->resize(read);
  return read > 0;
}

struct ThumbnailResult {
  std::optional<std::vector<uint8_t>> bytes;
  std::string error;
};

// Produces thumbnail bytes for the given arguments. On failure, returns an
// empty optional and sets `error`. WEBP (format 2) is unsupported on Windows.
ThumbnailResult GenerateThumbnail(const flutter::EncodableMap &args) {
  std::string video = GetString(args, "video");
  if (video.empty()) return {{}, "Video path is empty"};
  const int format = GetInt(args, "format", 0);
  const int maxh = GetInt(args, "maxh", 0);
  const int maxw = GetInt(args, "maxw", 0);
  const int timeMs = GetInt(args, "timeMs", 0);
  const int quality = GetInt(args, "quality", 100);

  if (format == 2) return {{}, "WEBP format is not supported on Windows"};

  std::wstring source = Utf8ToWide(video);
  // Strip file:// or file:/// URI prefix to a plain Windows path.
  if (source.rfind(L"file:///", 0) == 0)
    source = source.substr(8);
  else if (source.rfind(L"file://", 0) == 0)
    source = source.substr(7);

  // For http(s) URLs, fetch the bytes ourselves with WinHTTP and feed Media
  // Foundation a local file. The MF source resolver cannot reliably handle
  // HTTPS on desktop, and this also lets us honor the `headers` argument.
  std::wstring tempFile;
  const bool isHttp = source.rfind(L"http://", 0) == 0 ||
                      source.rfind(L"https://", 0) == 0;
  if (isHttp) {
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0)
      return {{}, "Failed to resolve temp directory"};
    wchar_t tempName[MAX_PATH] = {};
    if (GetTempFileNameW(tempDir, L"thm", 0, tempName) == 0)
      return {{}, "Failed to allocate temp file"};
    tempFile = tempName;

    std::string err;
    if (!DownloadUrlToFile(source, GetStringMap(args, "headers"), tempFile,
                           &err)) {
      DeleteFileW(tempFile.c_str());
      return {{}, "Failed to download " + video + ": " + err};
    }
    source = tempFile;
  }

  DecodedFrame frame;
  std::string decodeErr;
  const bool decoded = DecodeFrame(source, timeMs, &frame, &decodeErr);
  if (!tempFile.empty()) DeleteFileW(tempFile.c_str());

  if (!decoded)
    return {{}, "Failed to decode video frame at " + std::to_string(timeMs) +
                    "ms from: " + video +
                    (decodeErr.empty() ? "" : " (" + decodeErr + ")")};

  std::vector<uint8_t> bytes;
  if (!EncodeImage(frame, format, quality, maxw, maxh, &bytes))
    return {{}, "Failed to encode thumbnail image"};
  return {std::move(bytes), {}};
}

}  // namespace

// static
void GetThumbnailVideoPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "video_thumbnail",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<GetThumbnailVideoPlugin>();

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

GetThumbnailVideoPlugin::GetThumbnailVideoPlugin() {
  MFStartup(MF_VERSION);
}

GetThumbnailVideoPlugin::~GetThumbnailVideoPlugin() { MFShutdown(); }

void GetThumbnailVideoPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto *args =
      std::get_if<flutter::EncodableMap>(method_call.arguments());
  if (args == nullptr) {
    result->Error("bad_args", "Expected a map of arguments");
    return;
  }

  const std::string &method = method_call.method_name();

  if (method == "data") {
    auto tr = GenerateThumbnail(*args);
    if (!tr.bytes.has_value()) {
      result->Error("THUMBNAIL_ERROR", tr.error);
      return;
    }
    result->Success(flutter::EncodableValue(std::move(*tr.bytes)));
    return;
  }

  if (method == "file") {
    auto tr = GenerateThumbnail(*args);
    if (!tr.bytes.has_value()) {
      result->Error("THUMBNAIL_ERROR", tr.error);
      return;
    }

    const int format = GetInt(*args, "format", 0);
    const char *ext = (format == 1) ? "png" : "jpg";
    std::string video = GetString(*args, "video");
    std::string outPath = GetString(*args, "path");

    if (outPath.empty()) {
      // Derive a sibling file next to the source with the new extension.
      size_t dot = video.find_last_of('.');
      outPath = (dot == std::string::npos ? video : video.substr(0, dot)) +
                "." + ext;
    } else {
      // If a directory was passed, append the source file name.
      bool looksLikeDir =
          !outPath.empty() &&
          (outPath.back() == '\\' || outPath.back() == '/' ||
           outPath.find_last_of('.') == std::string::npos);
      if (looksLikeDir) {
        size_t slash = video.find_last_of("/\\");
        std::string name =
            slash == std::string::npos ? video : video.substr(slash + 1);
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (outPath.back() != '\\' && outPath.back() != '/') outPath += "\\";
        outPath += name + "." + ext;
      }
    }

    std::ofstream file(Utf8ToWide(outPath).c_str(),
                       std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      result->Error("IO_ERROR", "Failed to open output file: " + outPath);
      return;
    }
    file.write(reinterpret_cast<const char *>(tr.bytes->data()),
               static_cast<std::streamsize>(tr.bytes->size()));
    file.close();
    result->Success(flutter::EncodableValue(outPath));
    return;
  }

  result->NotImplemented();
}

}  // namespace get_thumbnail_video
