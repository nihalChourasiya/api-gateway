#!/bin/bash
./build/mock_backend 9001 > /dev/null 2>&1 &
MB1=$!
./build/mock_backend 9002 > /dev/null 2>&1 &
MB2=$!
./build/mock_backend 9003 > /dev/null 2>&1 &
MB3=$!

# Start gateway
./build/api_gateway > scratch/gateway.log 2>&1 &
GW=$!

sleep 2
echo "Running wrk..."
wrk -t4 -c50 -d10s http://localhost:8080/users/1 > scratch/wrk_output.txt

kill $GW $MB1 $MB2 $MB3
echo "Done"
