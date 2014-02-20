# Common macros
CXXFLAGS := -Wall -std=c++11 -I./include/ggpe -I./src
LIBS := -L/usr/local/lib/ -lYap -lreadline -ldl -lgmp -pthread -lboost_regex-mt -lmysqlclient
LIBS_OSX := -lodbc
LIBS_LINUX :=
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
OBJS := $(shell echo $(CXX_FILES) | perl -p -e 's/.(cpp|cc)/.o/g')
TARGET := lib/libggpe.a

# Macros for test
CXXFLAGS_TEST := $(CXXFLAGS_DEBUG) -I./test -DGTEST_USE_OWN_TR1_TUPLE
CXX_FILES_TEST := $(filter-out src/main.cpp, $(CXX_FILES)) $(shell find test \( -name \*.cpp -or -name \*.cc \) -print)
OBJS_TEST := $(shell echo $(CXX_FILES_TEST) | perl -p -e 's/.(cpp|cc)/.o/g')
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
all: $(OBJS)
	ar rcs $(TARGET) $(OBJS)

# "make test" means test
.PHONY: test
test: CXXFLAGS += $(CXXFLAGS_TEST)
test: $(OBJS_TEST)
	$(CXX) $(CXXFLAGS) -o $(TARGET_TEST) $(OBJS_TEST) $(LIBS)
	./$(TARGET_TEST)

.PHONY: clean
clean:
	rm -f $(OBJS) $(OBJS_TEST) $(TARGET) $(TARGET_TEST)

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $< -o $@

.cc.o:
	$(CXX) -c $(CXXFLAGS) $< -o $@
