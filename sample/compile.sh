#!/bin/sh

rm -rf tmp/*
if [ "`uname`" = "Linux" ]; then
  BOOST_SUFFIX=
else
  BOOST_SUFFIX=-mt
fi
$CXX sample.cpp -g -std=c++11 -I../include -L../lib -lggpe -lboost_system$BOOST_SUFFIX -lboost_timer$BOOST_SUFFIX -lboost_filesystem$BOOST_SUFFIX -lboost_regex$BOOST_SUFFIX -lgmp -lYap -lreadline -lglog -ldl
