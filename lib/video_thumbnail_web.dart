import 'dart:async';
import 'dart:math' as math;
import 'package:web/web.dart' as web;
import 'dart:js_interop';

import 'package:cross_file/cross_file.dart';
import 'package:flutter/services.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';
import 'package:get_thumbnail_video/src/image_format.dart';
import 'package:get_thumbnail_video/src/video_thumbnail_platform.dart';

// An error code value to error name Map.
// See: https://developer.mozilla.org/en-US/docs/Web/API/MediaError/code
const Map<int, String> _kErrorValueToErrorName = <int, String>{
  1: 'MEDIA_ERR_ABORTED',
  2: 'MEDIA_ERR_NETWORK',
  3: 'MEDIA_ERR_DECODE',
  4: 'MEDIA_ERR_SRC_NOT_SUPPORTED',
};

// An error code value to description Map.
// See: https://developer.mozilla.org/en-US/docs/Web/API/MediaError/code
const Map<int, String> _kErrorValueToErrorDescription = <int, String>{
  1: 'The user canceled the fetching of the video.',
  2: 'A network error occurred while fetching the video, despite having previously been available.',
  3: 'An error occurred while trying to decode the video, despite having previously been determined to be usable.',
  4: 'The video has been found to be unsuitable (missing or in a format not supported by your browser).',
};

// The default error message, when the error is an empty string
// See: https://developer.mozilla.org/en-US/docs/Web/API/MediaError/message
const String _kDefaultErrorMessage =
    'No further diagnostic information can be determined or provided.';

/// A web implementation of the VideoThumbnailPlatform of the VideoThumbnail plugin.
class VideoThumbnailWeb extends VideoThumbnailPlatform {
  /// Constructs a VideoThumbnailWeb
  VideoThumbnailWeb();

  static void registerWith(Registrar registrar) {
    VideoThumbnailPlatform.instance = VideoThumbnailWeb();
  }

  @override
  Future<XFile> thumbnailFile({
    required String video,
    required Map<String, String>? headers,
    required String? thumbnailPath,
    required ImageFormat imageFormat,
    required int maxHeight,
    required int maxWidth,
    required int timeMs,
    required int quality,
  }) async {
    final blob = await _createThumbnail(
      videoSrc: video,
      headers: headers,
      imageFormat: imageFormat,
      maxHeight: maxHeight,
      maxWidth: maxWidth,
      timeMs: timeMs,
      quality: quality,
    );

    final url = web.URL.createObjectURL(blob);
    return XFile(url, mimeType: blob.type);
  }

  @override
  Future<Uint8List> thumbnailData({
    required String video,
    required Map<String, String>? headers,
    required ImageFormat imageFormat,
    required int maxHeight,
    required int maxWidth,
    required int timeMs,
    required int quality,
  }) async {
    final blob = await _createThumbnail(
      videoSrc: video,
      headers: headers,
      imageFormat: imageFormat,
      maxHeight: maxHeight,
      maxWidth: maxWidth,
      timeMs: timeMs,
      quality: quality,
    );
    
    final url = web.URL.createObjectURL(blob);
    final file = XFile(url, mimeType: blob.type);
    final bytes = await file.readAsBytes();
    
    web.URL.revokeObjectURL(url);

    return bytes;
  }

  Future<web.Blob> _createThumbnail({
    required String videoSrc,
    required Map<String, String>? headers,
    required ImageFormat imageFormat,
    required int maxHeight,
    required int maxWidth,
    required int timeMs,
    required int quality,
  }) async {
    final completer = Completer<web.Blob>();
    final video = web.document.createElement('video') as web.HTMLVideoElement;
    
    final timeSec = math.max(timeMs / 1000, 0);
    final fetchVideo = headers != null && headers.isNotEmpty;

    // Handle loadedmetadata event
    video.addEventListener('loadedmetadata', ((web.Event event) {
      video.currentTime = timeSec;

      if (fetchVideo) {
        final url = video.src;
        web.URL.revokeObjectURL(url);
      }
    }).toJS);

    // Handle seeked event
    video.addEventListener('seeked', ((web.Event event) {
      if (!completer.isCompleted) {
        final canvas = web.document.createElement('canvas') as web.HTMLCanvasElement;
        final ctx = canvas.getContext('2d') as web.CanvasRenderingContext2D;

        final videoWidth = video.videoWidth;
        final videoHeight = video.videoHeight;

        if (videoWidth == 0 || videoHeight == 0) {
          completer.completeError(
            PlatformException(
              code: 'VIDEO_DIMENSIONS_ERROR',
              message: 'Could not determine video dimensions '
                  '(${videoWidth}x$videoHeight)',
            ),
          );
          return;
        }

        if (maxWidth == 0 && maxHeight == 0) {
          canvas.width = videoWidth;
          canvas.height = videoHeight;
          ctx.drawImage(video, 0, 0);
        } else {
          final aspectRatio = videoWidth / videoHeight;
          int finalWidth = maxWidth;
          int finalHeight = maxHeight;
          
          if (maxWidth == 0) {
            finalWidth = (maxHeight * aspectRatio).round();
          } else if (maxHeight == 0) {
            finalHeight = (maxWidth / aspectRatio).round();
          }

          final inputAspectRatio = finalWidth / finalHeight;
          if (aspectRatio > inputAspectRatio) {
            finalHeight = (finalWidth / aspectRatio).round();
          } else {
            finalWidth = (finalHeight * aspectRatio).round();
          }

          canvas.width = finalWidth;
          canvas.height = finalHeight;
          ctx.drawImage(video, 0, 0, finalWidth, finalHeight);
        }

        try {
          final format = _imageFormatToCanvasFormat(imageFormat);
          final qualityValue = quality / 100;
          
          // Convert canvas to blob
          _canvasToBlob(canvas, format, qualityValue).then((blob) {
            completer.complete(blob);
          }).catchError((e, s) {
            completer.completeError(
              PlatformException(
                code: 'CANVAS_EXPORT_ERROR',
                details: e,
                stacktrace: s.toString(),
              ),
              s,
            );
          });
        } catch (e, s) {
          completer.completeError(
            PlatformException(
              code: 'CANVAS_EXPORT_ERROR',
              details: e,
              stacktrace: s.toString(),
            ),
            s,
          );
        }
      }
    }).toJS);

    // Handle error event
    video.addEventListener('error', ((web.Event event) {
      if (!completer.isCompleted) {
        final error = video.error;
        if (error != null) {
          final errorCode = error.code;
          final errorMessage = error.message;
          
          completer.completeError(
            PlatformException(
              code: _kErrorValueToErrorName[errorCode]!,
              message: errorMessage.isNotEmpty ? errorMessage : _kDefaultErrorMessage,
              details: _kErrorValueToErrorDescription[errorCode],
            ),
          );
        } else {
          completer.completeError(
            PlatformException(
              code: 'UNKNOWN_ERROR',
              message: 'An unknown error occurred',
            ),
          );
        }
      }
    }).toJS);

    if (fetchVideo) {
      try {
        final blob = await _fetchVideoByHeaders(
          videoSrc: videoSrc,
          headers: headers,
        );

        final url = web.URL.createObjectURL(blob);
        video.src = url;
      } catch (e, s) {
        completer.completeError(e, s);
      }
    } else {
      video.crossOrigin = 'Anonymous';
      video.src = videoSrc;
    }

    return completer.future;
  }

  Future<web.Blob> _canvasToBlob(web.HTMLCanvasElement canvas, String format, double quality) {
    final completer = Completer<web.Blob>();
    
    canvas.toBlob(((web.Blob? blob) {
      if (blob != null) {
        completer.complete(blob);
      } else {
        completer.completeError(
          PlatformException(
            code: 'CANVAS_TO_BLOB_ERROR',
            message: 'Failed to convert canvas to blob',
          ),
        );
      }
    }).toJS, format, quality.toJS);
    
    return completer.future;
  }

  /// Fetching video by [headers].
  ///
  /// To avoid reading the video's bytes into memory, set the
  /// XHR responseType to 'blob'. This allows the blob to be stored in
  /// the browser's disk or memory cache.
  Future<web.Blob> _fetchVideoByHeaders({
    required String videoSrc,
    required Map<String, String> headers,
  }) async {
    final completer = Completer<web.Blob>();
    final xhr = web.XMLHttpRequest();
    
    xhr.open('GET', videoSrc);
    xhr.responseType = 'blob';
    
    headers.forEach((key, value) {
      xhr.setRequestHeader(key, value);
    });

    xhr.addEventListener('load', ((web.Event event) {
      if (xhr.status >= 200 && xhr.status < 300) {
        completer.complete(xhr.response as web.Blob);
      } else {
        completer.completeError(
          PlatformException(
            code: 'VIDEO_FETCH_ERROR',
            message: 'Status: ${xhr.status} ${xhr.statusText}',
          ),
        );
      }
    }).toJS);

    xhr.addEventListener('error', ((web.Event event) {
      completer.completeError(
        PlatformException(
          code: 'VIDEO_FETCH_ERROR',
          message: 'Failed to fetch video',
        ),
      );
    }).toJS);

    xhr.send();

    return completer.future;
  }

  String _imageFormatToCanvasFormat(ImageFormat imageFormat) {
    switch (imageFormat) {
      case ImageFormat.JPEG:
        return 'image/jpeg';
      case ImageFormat.PNG:
        return 'image/png';
      case ImageFormat.WEBP:
        return 'image/webp';
    }
  }
}