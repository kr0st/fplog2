#!/bin/bash

#check OS shell support for big numbers
((NUMBERS_CHECK=(2**63)-1));
if [ $NUMBERS_CHECK -ne 9223372036854775807 ]; then
    echo "ERROR: your OS does not support 64bit signed integers in shell!"
    exit 1
fi

#create named pipe if needed
if [ ! -p /tmp/fplog2_shared_sequence ]; then
    mkfifo /tmp/fplog2_shared_sequence
fi

shared_sequence=1

while true; do
    ((shared_sequence++))
    printf "0: %.16x" $shared_sequence | xxd -r -g0 -l 8 >> /tmp/fplog2_shared_sequence
done
