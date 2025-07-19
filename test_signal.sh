#!/bin/bash

echo "=== Testing Signal Handling ==="

# Test 1: Simple commands work
echo "Test 1: Basic functionality"
printf "ps\\nlogout\\n" | timeout 5s ./bin/pennos

echo -e "\n=== Test 1 completed ===\n"

# Test 2: Background command + ps  
echo "Test 2: Background commands"
printf "busy &\\nps\\nlogout\\n" | timeout 10s ./bin/pennos

echo -e "\n=== Test 2 completed ===\n"

echo "=== All tests completed ===" 