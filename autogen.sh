#!/bin/sh
autoreconf --force -v --install || exit
./configure "$@"
