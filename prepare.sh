#!/bin/sh

git submodule foreach git fetch
git submodule init
git submodule update
