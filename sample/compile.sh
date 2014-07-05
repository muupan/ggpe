#!/bin/sh

rm -rf tmp/*
$CXX sample.cpp -g -std=c++11 -I../include -L../lib -lggpe -lboost_system-mt -lboost_timer-mt -lodbc -lgmp -lmysqlclient -lboost_filesystem-mt -lboost_regex-mt -lreadline -lYap -lglog
