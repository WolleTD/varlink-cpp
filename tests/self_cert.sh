#!/bin/bash
socket="certification-test.varlink"
rm -f $socket
./cert_server unix:$socket & server_pid=$!
sleep 0.1
./cert_client_async unix:$socket & client1_pid=$!
./cert_client unix:$socket & client2_pid=$!
wait $client1_pid
result1=$?
wait $client2_pid
result2=$?
kill $server_pid
wait $server_pid
exit $(( result1 + result2 + $? ))
