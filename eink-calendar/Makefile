# Makefile for E-Ink Calendar ESP32 Project
# Uses arduino-cli for compilation and uploading

# Configuration
SKETCH := eink-calendar.ino
BOARD := esp32:esp32:lolin32
PORT := /dev/ttyUSB0
BAUD := 115200

# LittleFS configuration
LITTLEFS_BIN := littlefs.bin
LITTLEFS_ADDR := 0x290000
LITTLEFS_SIZE := 0x160000
DATA_DIR := data

# Colors for output
GREEN := \033[0;32m
YELLOW := \033[0;33m
RED := \033[0;31m
BLUE := \033[0;34m
NC := \033[0m # No Color

.PHONY: all compile upload monitor clean install-deps help port-detect certs littlefs build-littlefs flash-all flash-monitor run

# Default target
all: compile

help:
	@echo "$(GREEN)E-Ink Calendar - ESP32 Makefile$(NC)"
	@echo ""
	@echo "$(BLUE)Build & Upload:$(NC)"
	@echo "  $(YELLOW)make compile$(NC)       - Compile the sketch"
	@echo "  $(YELLOW)make upload$(NC)        - Upload sketch to ESP32"
	@echo "  $(YELLOW)make flash$(NC)         - Compile and upload sketch"
	@echo "  $(YELLOW)make monitor$(NC)       - Open serial monitor"
	@echo "  $(YELLOW)make run$(NC)           - Flash and monitor"
	@echo ""
	@echo "$(BLUE)Certificates & LittleFS:$(NC)"
	@echo "  $(YELLOW)make certs$(NC)         - Auto-create and upload certificates"
	@echo "  $(YELLOW)make littlefs$(NC)      - Upload existing littlefs.bin"
	@echo "  $(YELLOW)make build-littlefs$(NC)- Build littlefs.bin from data/"
	@echo "  $(YELLOW)make flash-all$(NC)     - Flash sketch + LittleFS"
	@echo "  $(YELLOW)make flash-monitor$(NC) - Flash all and monitor"
	@echo ""
	@echo "$(BLUE)Utilities:$(NC)"
	@echo "  $(YELLOW)make clean$(NC)         - Clean build files"
	@echo "  $(YELLOW)make install-deps$(NC)  - Install required libraries"
	@echo "  $(YELLOW)make port-detect$(NC)   - Auto-detect ESP32 port"
	@echo "  $(YELLOW)make config$(NC)        - Show current configuration"
	@echo ""
	@echo "Configuration:"
	@echo "  Board: $(BOARD)"
	@echo "  Port:  $(PORT)"
	@echo "  Baud:  $(BAUD)"
	@echo ""
	@echo "$(BLUE)Quick Start:$(NC)"
	@echo "  $(YELLOW)make littlefs$(NC)      # Upload certificates (first time)"
	@echo "  $(YELLOW)make flash-monitor$(NC) # Flash everything and monitor"

# Install required libraries
install-deps:
	@echo "$(GREEN)Installing required libraries...$(NC)"
	arduino-cli lib install "GxEPD2"
	arduino-cli lib install "Adafruit GFX Library"
	arduino-cli lib install "ArduinoJson"
	arduino-cli lib install "PubSubClient"
	@echo "$(GREEN)✓ Libraries installed$(NC)"

# Compile the sketch
compile:
	@echo "$(GREEN)Compiling $(SKETCH)...$(NC)"
	arduino-cli compile --fqbn $(BOARD) \
		--warnings all \
		$(SKETCH)
	@echo "$(GREEN)✓ Compilation successful$(NC)"

# Upload to ESP32
upload:
	@echo "$(GREEN)Uploading to ESP32 at $(PORT)...$(NC)"
	arduino-cli upload -p $(PORT) --fqbn $(BOARD) $(SKETCH)
	@echo "$(GREEN)✓ Upload complete$(NC)"

# Compile and upload in one step
flash: compile upload
	@echo "$(GREEN)✓ Flash complete$(NC)"

# Auto-detect ESP32 port
port-detect:
	@echo "$(GREEN)Detecting ESP32 port...$(NC)"
	@arduino-cli board list | grep -i "usb\|tty" || echo "$(RED)No boards detected$(NC)"

# Open serial monitor
monitor:
	@echo "$(GREEN)Opening serial monitor (Ctrl+C to exit)...$(NC)"
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# Clean build artifacts
clean:
	@echo "$(YELLOW)Cleaning build files...$(NC)"
	rm -rf build/
	@echo "$(GREEN)✓ Clean complete$(NC)"

# Show configuration
config:
	@echo "$(GREEN)Current Configuration:$(NC)"
	@echo "  Sketch:     $(SKETCH)"
	@echo "  Board:      $(BOARD)"
	@echo "  Port:       $(PORT)"
	@echo "  Baud Rate:  $(BAUD)"
	@echo ""
	@echo "$(YELLOW)Edit config.h to set WiFi credentials and AWS IoT details$(NC)"

# Quick flash and monitor
run: flash monitor

# ==================== Certificate & LittleFS Targets ====================

# Upload certificates to ESP32 (auto-creates LittleFS)
certs:
	@echo "$(BLUE)Uploading certificates to ESP32...$(NC)"
	@python3 ../upload_certificates.py $(PORT)
	@echo "$(GREEN)✓ Certificates uploaded!$(NC)"

# Upload existing LittleFS binary directly
littlefs:
	@if [ ! -f "$(LITTLEFS_BIN)" ]; then \
		echo "$(RED)Error: $(LITTLEFS_BIN) not found!$(NC)"; \
		echo "$(YELLOW)Run 'make build-littlefs' to create it first$(NC)"; \
		exit 1; \
	fi
	@echo "$(BLUE)Uploading LittleFS to ESP32 at $(PORT)...$(NC)"
	esptool.py --port $(PORT) write_flash $(LITTLEFS_ADDR) $(LITTLEFS_BIN)
	@echo "$(GREEN)✓ LittleFS uploaded!$(NC)"

# Build LittleFS image from data directory
build-littlefs:
	@if [ ! -d "$(DATA_DIR)" ]; then \
		echo "$(RED)Error: $(DATA_DIR) directory not found!$(NC)"; \
		exit 1; \
	fi
	@echo "$(BLUE)Building LittleFS image from $(DATA_DIR)/...$(NC)"
	mklittlefs -c $(DATA_DIR) -b 4096 -p 256 -s $(LITTLEFS_SIZE) $(LITTLEFS_BIN)
	@echo "$(GREEN)✓ LittleFS image created: $(LITTLEFS_BIN)$(NC)"

# Flash everything: compile sketch + upload sketch + upload LittleFS
flash-all: compile upload littlefs
	@echo "$(GREEN)✓ Complete flash (sketch + LittleFS) done!$(NC)"

# Flash and monitor in one step
flash-monitor: flash-all
	@sleep 2
	@$(MAKE) monitor
