#!/bin/bash
socket="certification-test.varlink"
rm -f $socket
./cert_server unix:$socket & server_pid=$!
sleep 0.01
./cert_client unix:$socket
result=$?
kill $server_pid
wait $server_pid
exit $(( result + $? ))
