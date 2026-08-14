#!/bin/bash
./build/mock_backend 9001 > /dev/null 2>&1 &
MB1=$!
./build/mock_backend 9002 > /dev/null 2>&1 &
MB2=$!
./build/mock_backend 9003 > /dev/null 2>&1 &
MB3=$!
./build/api_gateway > /dev/null 2>&1 &
GW=$!
sleep 2
wrk -t4 -c500 -d10s http://localhost:8080/users/1
kill $GW $MB1 $MB2 $MB3
