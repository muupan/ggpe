# Common macros
GGPE_PATH := $(shell pwd)
CXXFLAGS += -Wall -std=c++11 -I./include/ggpe -I./src -I. -D'GGPE_PATH="$(GGPE_PATH)"'
LIBS := -lYap -lreadline -ldl -lgmp -pthread -lmysqlclient -lglog
LIBS_OSX := -lodbc -lboost_regex-mt -lboost_system-mt -lboost_filesystem-mt
LIBS_LINUX := -lboost_regex -lboost_system -lboost_filesystem
OS := $(shell uname)
ifeq ($(OS), Darwin)
	LIBS += $(LIBS_OSX)
else
	LIBS += $(LIBS_LINUX)
endif

# Macros for build
CXXFLAGS_RELEASE := -O3 -march=native -flto -DNDEBUG
CXXFLAGS_DEBUG := -g -O0
CXX_FILES := $(shell find src \( -name \*.cpp -or -name \*.cc \) -print)
CXX_TEST_FILES := $(shell find src \( -name \*_test.cpp -or -name \*_test.cc \) -print)
CXX_NONTEST_FILES := $(filter-out $(CXX_TEST_FILES), $(CXX_FILES))
SRCS := $(CXX_NONTEST_FILES)
OBJS := $(shell echo $(SRCS) | perl -p -e 's/\.(cpp|cc)/\.o/g')
TARGET := lib/libggpe.a

# Macros for test
CXXFLAGS_TEST := $(CXXFLAGS_DEBUG) -I./test -DGTEST_USE_OWN_TR1_TUPLE
SRCS_TEST := gtest/gtest_main.cc gtest/gtest-all.cc $(filter-out src/main.cpp, $(CXX_FILES))
OBJS_TEST := $(shell echo $(SRCS_TEST) | perl -p -e 's/\.(cpp|cc)/\.o/g')
TARGET_TEST := bin/test

# "make release" or just "make" means release build
.PHONY: release
release: CXXFLAGS += $(CXXFLAGS_RELEASE)
release: all

# "make debug" means debug build
.PHONY: debug
debug: CXXFLAGS += $(CXXFLAGS_DEBUG)
debug: all

.PHONY: all
all: $(OBJS) interface.yap
	ar rcs $(TARGET) $(OBJS)

# "make test" means test
.PHONY: test
test: CXXFLAGS += $(CXXFLAGS_TEST)
test: $(OBJS_TEST) interface.yap
	rm -rf tmp/*
	$(CXX) $(CXXFLAGS) -o $(TARGET_TEST) $(OBJS_TEST) $(LDFLAGS) $(LIBS)
	./$(TARGET_TEST)

.PHONY: clean
clean:
	rm -f $(OBJS) $(OBJS_TEST) $(TARGET) $(TARGET_TEST)

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $< -o $@

.cc.o:
	$(CXX) -c $(CXXFLAGS) $< -o $@

interface.yap:
	yap -z "compile('interface.pl'), save_program('interface.yap'), halt"

