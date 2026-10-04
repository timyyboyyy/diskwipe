# diskwipe – Build, Tests, Auslieferung
CXX      ?= g++
WINCXX   ?= x86_64-w64-mingw32-g++-posix
WINDRES  ?= x86_64-w64-mingw32-windres
MAKENSIS ?= makensis

BUILD    := build
DIST     := dist
VERSION  := $(shell cat VERSION)
comma    := ,

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -I$(BUILD)
WINFLAGS := -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++

CORE_SRC     := $(filter-out %_win.cpp,$(wildcard src/*.cpp))
WIN_LIB_SRC  := $(filter-out src/gui_win.cpp,$(wildcard src/*_win.cpp))
TEST_SRC     := $(wildcard tests/test_*.cpp)
WIN_TEST_SRC := $(wildcard tests/win/test_*.cpp)
HEADERS      := $(wildcard src/*.h tests/*.h)

.PHONY: all test test-unit test-integration test-win windows dist clean
all: test windows

$(BUILD)/.dir:
	mkdir -p $(BUILD) tests/tmp
	touch $@

$(BUILD)/version.h: VERSION | $(BUILD)/.dir
	printf '#pragma once\n#define DW_VERSION_STR "%s"\n#define DW_VERSION_WSTR L"%s"\n#define DW_VERSION_NUM %s,0\n' \
	  "$(VERSION)" "$(VERSION)" "$(subst .,$(comma),$(VERSION))" > $@

# --- Linux ---
$(BUILD)/diskwipe_tests: $(CORE_SRC) $(TEST_SRC) $(HEADERS) | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -Itests -o $@ $(CORE_SRC) $(TEST_SRC)

$(BUILD)/wipe_image: $(CORE_SRC) tests/wipe_image.cpp $(HEADERS) | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -o $@ $(CORE_SRC) tests/wipe_image.cpp

test-unit: $(BUILD)/diskwipe_tests
	./$(BUILD)/diskwipe_tests

test-integration: $(BUILD)/wipe_image
	tests/integration.sh

test: test-unit test-integration

# --- Windows ---
$(BUILD)/diskwipe_tests.exe: $(CORE_SRC) $(WIN_LIB_SRC) $(TEST_SRC) $(WIN_TEST_SRC) $(HEADERS) | $(BUILD)/.dir
	$(WINCXX) $(CXXFLAGS) $(WINFLAGS) -Itests -o $@ $(CORE_SRC) $(WIN_LIB_SRC) $(TEST_SRC) $(WIN_TEST_SRC) -lbcrypt

test-win: $(BUILD)/diskwipe_tests.exe
	./$(BUILD)/diskwipe_tests.exe

$(BUILD)/resource.o: src/resource.rc src/resource.h src/diskwipe.manifest src/diskwipe.ico $(BUILD)/version.h
	$(WINDRES) -I src -I $(BUILD) -o $@ src/resource.rc

$(BUILD)/diskwipe.exe: $(CORE_SRC) $(WIN_LIB_SRC) src/gui_win.cpp $(BUILD)/resource.o $(BUILD)/version.h $(HEADERS)
	$(WINCXX) $(CXXFLAGS) $(WINFLAGS) -mwindows -municode -o $@ \
	  $(CORE_SRC) $(WIN_LIB_SRC) src/gui_win.cpp $(BUILD)/resource.o -lbcrypt -lcomctl32 -lole32 -luuid

windows: $(BUILD)/diskwipe.exe

dist: $(BUILD)/diskwipe.exe
	mkdir -p $(DIST)
	cp $(BUILD)/diskwipe.exe $(DIST)/diskwipe-$(VERSION)-portable.exe
	$(MAKENSIS) -V2 -DVERSION=$(VERSION) -DEXE=$(abspath $(BUILD)/diskwipe.exe) -DICON=$(abspath src/diskwipe.ico) \
	  -DOUTFILE=$(abspath $(DIST)/diskwipe-$(VERSION)-setup.exe) installer/diskwipe.nsi
	cd $(DIST) && sha256sum diskwipe-$(VERSION)-setup.exe diskwipe-$(VERSION)-portable.exe > SHA256SUMS.txt

clean:
	rm -rf $(BUILD) $(DIST) tests/tmp
