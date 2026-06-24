#import "VideoThumbnailPlugin.h"
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

@implementation VideoThumbnailPlugin
+ (void)registerWithRegistrar:(NSObject <FlutterPluginRegistrar> *)registrar {
    FlutterMethodChannel *channel = [FlutterMethodChannel
            methodChannelWithName:@"video_thumbnail"
                  binaryMessenger:[registrar messenger]];
    VideoThumbnailPlugin *instance = [[VideoThumbnailPlugin alloc] init];
    [registrar addMethodCallDelegate:instance channel:channel];
}

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result {

    NSDictionary *_args = call.arguments;

    NSString *file = _args[@"video"];

    NSMutableDictionary *headers = _args[@"headers"];

    NSString *path = _args[@"path"];
    int format = [[_args objectForKey:@"format"] intValue];
    int maxh = [[_args objectForKey:@"maxh"] intValue];
    int maxw = [[_args objectForKey:@"maxw"] intValue];
    int timeMs = [[_args objectForKey:@"timeMs"] intValue];
    int quality = [[_args objectForKey:@"quality"] intValue];
    _args = nil;
    bool isLocalFile = [file hasPrefix:@"file://"] || [file hasPrefix:@"/"];

    NSURL *url = [file hasPrefix:@"file://"] ? [NSURL fileURLWithPath:[file substringFromIndex:7]] :
                 ([file hasPrefix:@"/"] ? [NSURL fileURLWithPath:file]
                                        : [NSURL URLWithString:file]);

    if ([@"data" isEqualToString:call.method]) {

        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(void) {
            //Background Thread
            NSData *data = [VideoThumbnailPlugin generateThumbnail:url headers:headers format:format maxHeight:maxh maxWidth:maxw timeMs:timeMs quality:quality];
            dispatch_async(dispatch_get_main_queue(), ^(void) {
                result(data ? [FlutterStandardTypedData typedDataWithBytes:data] : nil);
            });
        });

    } else if ([@"file" isEqualToString:call.method]) {
        if ([path isEqual:[NSNull null]] && !isLocalFile) {
            path = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask,
                                                        YES) lastObject];
        }

        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(void) {
            //Background Thread

            NSData *data = [VideoThumbnailPlugin generateThumbnail:url headers:headers format:format maxHeight:maxh maxWidth:maxw timeMs:timeMs quality:quality];
            NSString *ext = ((format == 0) ? @"jpg" : (format == 1) ? @"png" : @"webp");
            NSURL *thumbnail = [[url URLByDeletingPathExtension] URLByAppendingPathExtension:ext];

            if (path && [path isKindOfClass:[NSString class]] && path.length > 0) {
                NSString *lastPart = [thumbnail lastPathComponent];
                thumbnail = [NSURL fileURLWithPath:path];
                if (![[thumbnail pathExtension] isEqualToString:ext]) {
                    thumbnail = [thumbnail URLByAppendingPathComponent:lastPart];
                }
            }

            NSError *error = nil;
            if (data == nil) {
                dispatch_async(dispatch_get_main_queue(), ^(void) {
                    result([FlutterError errorWithCode:@"Thumbnail Error" message:@"Failed to generate thumbnail" details:nil]);
                });
            } else if ([data writeToURL:thumbnail options:0 error:&error] != YES) {
                dispatch_async(dispatch_get_main_queue(), ^(void) {
                    if (error != nil) {
                        result([FlutterError errorWithCode:[NSString stringWithFormat:@"Error %ld", error.code]
                                                   message:error.domain
                                                   details:error.localizedDescription]);
                    } else
                        result([FlutterError errorWithCode:@"IO Error" message:@"Failed to write data to file" details:nil]);
                });
            } else {
                NSString *fullpath = [thumbnail absoluteString];
                if ([fullpath hasPrefix:@"file://"]) {
                    fullpath = [fullpath substringFromIndex:7];
                }
                dispatch_async(dispatch_get_main_queue(), ^(void) {
                    result(fullpath);
                });
            }
        });

    } else {
        result(FlutterMethodNotImplemented);
    }
}

+ (NSData *)generateThumbnail:(NSURL *)url headers:(NSMutableDictionary *)headers format:(int)format maxHeight:(int)maxh maxWidth:(int)maxw timeMs:(int)timeMs quality:(int)quality {

    AVURLAsset *asset = [[AVURLAsset alloc] initWithURL:url options:[headers isEqual:[NSNull null]]
                                                                    ? nil : @{
                                                                            @"AVURLAssetHTTPHeaderFieldsKey": headers}];
    AVAssetImageGenerator *imgGenerator = [[AVAssetImageGenerator alloc] initWithAsset:asset];

    imgGenerator.appliesPreferredTrackTransform = YES;
    imgGenerator.maximumSize = CGSizeMake((CGFloat) maxw, (CGFloat) maxh);
    imgGenerator.requestedTimeToleranceBefore = kCMTimeZero;
    imgGenerator.requestedTimeToleranceAfter = CMTimeMake(100, 1000);

    NSError *error = nil;
    CGImageRef cgImage = [imgGenerator copyCGImageAtTime:CMTimeMake(timeMs, 1000) actualTime:nil error:&error];

    if (error != nil || cgImage == NULL) {
        NSLog(@"couldn't generate thumbnail, error:%@", error);
        return nil;
    }

    // WEBP (format == 2) is intentionally unsupported on macOS to avoid a libwebp
    // dependency; video_trimmer only ever requests JPEG. PNG is supported too.
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
    CGImageRelease(cgImage);  // CGImageRef won't be released by ARC

    if (format == 0) {
        CGFloat fQuality = (CGFloat) (quality * 0.01);
        return [rep representationUsingType:NSBitmapImageFileTypeJPEG
                                 properties:@{NSImageCompressionFactor: @(fQuality)}];
    } else if (format == 1) {
        return [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    } else {
        NSLog(@"WEBP format is not supported on macOS");
        return nil;
    }
}

@end
