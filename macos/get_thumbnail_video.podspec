#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint get_thumbnail_video.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'get_thumbnail_video'
  s.version          = '0.0.1'
  s.summary          = 'A flutter plugin for creating a thumbnail from a local video file or from a video URL.'
  s.description      = <<-DESC
A flutter plugin for creating a thumbnail from a local video file or from a video URL.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*'
  s.public_header_files = 'Classes/**/*.h'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.13'

  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
  s.swift_version = '5.0'
end
