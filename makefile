SHELL := cmd.exe

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -DUNICODE -D_UNICODE -I.\src

SRC_DIR := .\src
BUILD_DIR := .\build

$(BUILD_DIR)\audioPlayer.a : $(BUILD_DIR)\tmp.o
	@echo Creating static library $@
	@ar rcs $@ $^

$(BUILD_DIR)\tmp.o : $(SRC_DIR)\audioPlayer.cpp | $(BUILD_DIR)
	@echo Compiling $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

.PHONY: all clean 32 release
all: $(BUILD_DIR)\audioPlayer.a
clean:
	@echo Cleaning build artifacts
	@if exist "$(BUILD_DIR)\tmp.o" del /f /q "$(BUILD_DIR)\tmp.o"
	@if exist "$(BUILD_DIR)\audioPlayer.a" del /f /q "$(BUILD_DIR)\audioPlayer.a"
#32位编译和release(32+64)先不写
