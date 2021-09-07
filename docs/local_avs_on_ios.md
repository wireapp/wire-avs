
# Building ZClient-iOS with local AVS

## Requirements

Xcode7
Cocoa pods: sudo gem install cocoapods
Carthage: brew install carthage

## Set up xcode_link dir

Make a link to avsball:

```
mkdir xcode_link
cd xcode_link
ln -s ../avs/build/dist/ios/avsball ios
```

## Getting Zclient dependencies

Get pods and whatever

```
cd zclient-ios
pod init
carthage bootstrap
```

## Add getcomponents to Build Phases

Open xcworkspace file in Xcode
Add following line to "Check Pods manifest.lock" in build phases

```
${HOME}/Library/Python/2.7/bin/getcomponents --verbose --force --build-control ${SRCROOT}/BUILDCONTROL --platform ios
```

## Fix Buildconfig to get local AVS

Copy BUILDCONFIG to BUILDCONFIG.local, replace avs with local & add the paths section

```
[libraries]
avs: local

[paths]
avs: ../xcode_link
```

## Profit

