#! /bin/bash

DIR=$(dirname "${BASH_SOURCE[0]}")

PLAT=$(uname)
if [ "$PLAT" == "Darwin" ]
then
	$DIR/build_iosx.sh
elif [ "$PLAT" == "Linux" ]
then
	$DIR/build_linux.sh
else
	echo "Platform $PLAT not supported"
fi

