#!/bin/sh
str="$*"
make clean
git add . --ignore-removal
git commit -m "$str" 
git push origin master

