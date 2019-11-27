#! /bin/bash

srcfile=$1
dstfile=${srcfile/webrtc_checkout\/src/$2\/include}

dstpath=`dirname $dstfile`

mkdir -p $dstpath
cp $srcfile $dstfile

