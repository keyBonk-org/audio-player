SHELL := cmd.exe

# Compilation architecture
ARCH ?= 64

# Determine the architecture selection and assign variables.
ifeq ($(ARCH),32)
CXX      = i686-w64-mingw32-g++
ARCH_TEXT= x86
else
CXX      = g++
ARCH_TEXT= x64
endif

# compiler options
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -DUNICODE -D_UNICODE -I.\src

# Source code directory and the build result output directory.
SRC_DIR := .\src
BUILD_DIR := .\build

# Compilation library file.
$(BUILD_DIR)\audioPlayer_$(ARCH_TEXT).a : $(BUILD_DIR)\tmp_$(ARCH_TEXT).o
	@echo Creating static library $@
	@ar rcs $@ $^

# Compile object file.
$(BUILD_DIR)\tmp_$(ARCH_TEXT).o : $(SRC_DIR)\audioPlayer.cpp | $(BUILD_DIR)
	@echo Compiling $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# make build directory.
$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

# Copy all .hpp files from src to build (depends on library to ensure build exists)
copy_headers: $(BUILD_DIR)\audioPlayer_$(ARCH_TEXT).a
	@echo Copying headers...
	@copy $(SRC_DIR)\*.hpp $(BUILD_DIR)\ >nul

# Package headers and library into a zip archive inside build directory
package: copy_headers
	@echo Packaging into $(BUILD_DIR)\audio-player_$(ARCH_TEXT).zip...
	@powershell -Command "Set-Location '$(BUILD_DIR)'; Compress-Archive -Path '*.hpp','audioPlayer_$(ARCH_TEXT).a' -DestinationPath 'audio-player_$(ARCH_TEXT).zip' -Force"

# Default target: build and package
all: package

# Clean up build artifacts and generated zip
clean:
	@echo Cleaning build artifacts
	@if exist "$(BUILD_DIR)\tmp_x64.o" del /f /q "$(BUILD_DIR)\tmp_x64.o"
	@if exist "$(BUILD_DIR)\audioPlayer_x64.a" del /f /q "$(BUILD_DIR)\audioPlayer_x64.a"
	@if exist "$(BUILD_DIR)\tmp_x86.o" del /f /q "$(BUILD_DIR)\tmp_x86.o"
	@if exist "$(BUILD_DIR)\audioPlayer_x86.a" del /f /q "$(BUILD_DIR)\audioPlayer_x86.a"
	@if exist "$(BUILD_DIR)\*.hpp" del /f /q "$(BUILD_DIR)\*.hpp"
	@if exist "$(BUILD_DIR)\audio-player_x64.zip" del /f /q "$(BUILD_DIR)\audio-player_x64.zip"
	@if exist "$(BUILD_DIR)\audio-player_x86.zip" del /f /q "$(BUILD_DIR)\audio-player_x86.zip"

# Build 32-bit version
32:
	@$(MAKE) ARCH=32 all

# Build both 64-bit and 32-bit releases
release:
	@$(MAKE) ARCH=64 all
	@$(MAKE) ARCH=32 all

.PHONY: all clean 32 release copy_headers package
