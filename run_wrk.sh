#!/bin/bash

# $1 is the # of threads
# $2 is the # of connections
# $3 is the duration of the test
#TODO
# look up flag for append mode


if [ $# -lt 3 ]
then
    echo "USAGE: run_wrk <threads> <connections> <duration>"
    exit
fi

wrk --latency -t$1 -c$2 -d$3s http://192.168.11.5:1234/index.html | tee wrk_$1_$2_$3.log

