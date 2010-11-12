#!/usr/bin/env sh
gcc -Wall -O4 -march=native -fno-strict-aliasing -DCONFIG_USE_FASTPOINT=1 main.c -lpthread
