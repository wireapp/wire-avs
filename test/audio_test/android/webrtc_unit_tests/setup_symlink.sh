# Setup Symlinks to fake the webrtc 3party structure
mkdir -p tmp/testing/gtest
cd tmp/testing/gtest
ln -s ../../../../../../../build/contrib/android/include
cd ../
mkdir gmock
cd gmock
ln -s ../../../../../src/gmock/include
cd ../../
