#!/usr/bin/env bash
name="world"
echo "hello ${name}"
if [[ -n "$name" ]]; then printf "%s\n" "$name"; fi
