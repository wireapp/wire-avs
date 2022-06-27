#! /bin/bash

PRE=$(echo $1 | cut -c -4)
POST=$(echo $1 | cut -c 5-)
if [[ "$1" == "avs-master" ]]; then
	TAG="alpha"
elif [[ "$PRE" == "avs-" && "$POST" != "" ]]; then
	TAG=$POST
else
	echo "usage: upload_wasm.sh <build name>"
	echo "cowardly refusing to deploy for build $1"
	exit
fi

pushd build/dist/wasm >/dev/null

echo '//registry.npmjs.org/:_authToken=${NPM_TOKEN}' > .npmrc

echo "publishing $1 as $TAG"
npm publish --tag=$TAG

popd >/dev/null

