#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint community_ar.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'community_ar'
  s.version          = '0.2.0'
  s.summary          = 'Open-source face AR for Flutter — beauty, hair, accessories.'
  s.description      = <<-DESC
Real-time face AR for Flutter: beauty filters, lip recolor, and perception
(landmarks, iris, segmentation, pose, skin tone) built on a C++/Metal core.
                       DESC
  s.homepage         = 'https://github.com/money-dey/community_ar'
  s.license          = { :type => 'Apache-2.0' }
  s.author           = { 'Community AR' => 'moneydey.ltd@gmail.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '13.0'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'

  # NATIVE C++ CORE (iOS device bring-up):
  # The Swift adapter in Classes/ bridges to the C++/Metal core under the
  # repo-root native/ tree (built via CMakeLists.txt as a static library on
  # Apple). Wiring that into this pod — adding the native sources / a
  # vendored_library, the Metal/CoreML frameworks, and the TFLite→CoreML
  # models — is part of iOS device bring-up and is intentionally not yet
  # configured here. Android builds the same core via android/build.gradle.kts.
end
