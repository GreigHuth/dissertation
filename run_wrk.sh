#!/bin/bash

# $1 is the # of threads
# $2 is the # of connections
# $3 is the duration of the test
#TODO



if [ $# -lt 2 ]
then
    echo "USAGE: run_wrk <threads> <duration>"
    exit
fi

test_params=(4 8 50 100 250 500 1000)

for i in {0..6}
do
    for j in {1..3}
    do
        echo "running test ${test_params[$i]}. Run: $j"
        wrk --latency -t$1 -c${test_params[$i]} -d$2s http://192.168.11.5:1234/index.html | tee -a wrk_$1_${test_params[$i]}_$2.log

    done
done
