#!/bin/bash
./build/mock_backend 9001 > /dev/null 2>&1 &
MB1=$!
./build/mock_backend 9002 > /dev/null 2>&1 &
MB2=$!
./build/mock_backend 9003 > /dev/null 2>&1 &
MB3=$!

./build/api_gateway > scratch/gateway.log 2>&1 &
GW=$!

sleep 2
echo "Running wrk and perf..."
perf record -p $GW -F 99 -g -- sleep 10 &
PERF=$!

wrk -t4 -c50 -d10s http://localhost:8080/users/1 > scratch/wrk_perf.txt

wait $PERF
kill $GW $MB1 $MB2 $MB3
perf report --stdio --no-children -n > scratch/perf_report.txt
echo "Done"
