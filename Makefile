# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude -Ilib/argparse/include -lpthread -lboost_system -g -o0

# Directories
SRC_DIR = src
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client
OUTPUT_DIR = build
LIB_DIR = lib

# Source files
SERVER_SRC = $(wildcard $(SERVER_DIR)/*.cpp)
CLIENT_SRC = $(wildcard $(CLIENT_DIR)/*.cpp)

# Output binaries
SERVER_BIN = $(OUTPUT_DIR)/topic-server
CLIENT_BIN = $(OUTPUT_DIR)/topic-client

# Default target - build both
all: server client

# Compile server
server: $(SERVER_SRC)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $(SERVER_BIN)

# Compile client
client: $(CLIENT_SRC)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $(CLIENT_BIN)

# Run both
run: all
	@echo "Starting server..."
	@$(SERVER_BIN) &
	@echo "Starting client..."
	@$(CLIENT_BIN)

# Run only the server
run_server: server
	@echo "Starting server..."
	@$(SERVER_BIN)

# Run only the client
run_client: client
	@echo "Starting client..."
	@$(CLIENT_BIN)

# Clean build files
clean:
	rm -rf $(OUTPUT_DIR)

