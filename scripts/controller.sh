#!/bin/bash

# Set the project directory (modify this to match your project location)
PROJECT_DIR="/home/ubuntu/BSE_angelone"
SRC_DIR="$PROJECT_DIR/src"
BIN_DIR="$PROJECT_DIR/bin"
LOG_DIR="$PROJECT_DIR/logs"

# Function to log messages in JSON format
log_json() {
    local message="$1"
    local timestamp=$(date +"%Y-%m-%d %H:%M:%S")
    local log_entry="{\"timestamp\":\"$timestamp\",\"message\":\"$message\"}"
    echo "$log_entry" >> "$LOG_DIR/controller.json"
}


# Clear the bin and logs directories
log_json "Clearing the bin and log directories..."
rm -rf "$BIN_DIR"/*
rm -rf "$LOG_DIR"/*
log_json "Bin and log directories cleared."

# Ensure the bin and logs directories exist
mkdir -p "$BIN_DIR"
mkdir -p "$LOG_DIR"

# Change to the project directory
cd "$PROJECT_DIR" || { echo "Project directory not found"; log_json "Project directory not found"; exit 1; }
echo "Current directory: $(pwd)"
log_json "Changed to project directory: $(pwd)"

# Function to count tokens in the CSV files
count_tokens() {
    if [ -f "SocketTokens/Tokens.csv" ]; then
        local token_count=$(tail -n +2 "SocketTokens/Tokens.csv" | wc -l)
        log_json "Total number of tokens in Tokens.csv: $token_count"
    else
        log_json "Tokens.csv not found in SocketTokens directory."
    fi

    if [ -f "SocketTokens/AMXIDX_Tokens.csv" ]; then
        local amxidx_token_count=$(tail -n +2 "SocketTokens/AMXIDX_Tokens.csv" | wc -l)
        log_json "Total number of tokens in AMXIDX_Tokens.csv: $amxidx_token_count"
    else
        log_json "AMXIDX_Tokens.csv not found in SocketTokens directory."
    fi
}

# Wait for CSV files to be created and written completely
wait_for_csv() {
    log_json "Waiting for AMXIDX_Tokens.csv and Tokens.csv to be written..."
    
    # Wait until both files exist and have non-zero size
    while [ ! -s "SocketTokens/Tokens.csv" ] || [ ! -s "SocketTokens/AMXIDX_Tokens.csv" ]; do
        log_json "Waiting for CSV files to be written..."
        sleep 2  # Wait for 2 seconds before checking again
    done

    log_json "CSV files are ready!"
}

# Compile auth.cpp
compile_auth() {
    log_json "Compiling auth.cpp..."
    g++ -I/usr/local/include -I/usr/local/include/json/single_include -o "$BIN_DIR/auth" "$SRC_DIR/Auth/auth.cpp" -lcurl -lssl -lcrypto -ljsoncpp -lpthread
    if [ $? -eq 0 ]; then
        log_json "auth.cpp compiled successfully."
    else
        log_json "Failed to compile auth.cpp, continuing with other files..."
        return 1
    fi
}

# Compile BSEtokens.cpp
compile_BSEtokens() {
    log_json "Compiling BSEtokens.cpp..."
    g++ -I/usr/local/include -I/usr/local/include/json/single_include -o "$BIN_DIR/BSEtokens" "$SRC_DIR/BSEtokens/BSEtokens.cpp" -lcurl -lpthread
    if [ $? -eq 0 ]; then
        log_json "BSEtokens.cpp compiled successfully."
    else
        log_json "Failed to compile BSEtokens.cpp, continuing with other files..."
        return 1
    fi
}

# Compile ws.cpp
compile_ws() {
    log_json "Compiling ws.cpp..."
    g++ -I/usr/local/include/websocketpp -I/usr/local/include -I/usr/include/librdkafka -o "$BIN_DIR/ws" "$SRC_DIR/Websocket/ws.cpp" -std=c++17 -lboost_system -lboost_thread -lssl -lcrypto -lpthread -lrdkafka++
    if [ $? -eq 0 ]; then
        log_json "ws.cpp compiled successfully."
    else
        log_json "Failed to compile ws.cpp."
        return 1
    fi
}

# Compile all source files
compile_all() {
    log_json "Starting compilation of all source files..."
    compile_auth
    log_json "Finished compiling auth.cpp"

    compile_BSEtokens
    log_json "Finished compiling BSEtokens.cpp"

    compile_ws
    log_json "Finished compiling ws.cpp"
}

# Run all compiled programs
run_all() {
    if [ -f "$BIN_DIR/auth" ]; then
        log_json "Running auth..."
        "$BIN_DIR/auth"
    else
        log_json "auth not found!"
    fi

    if [ -f "$BIN_DIR/BSEtokens" ]; then
        log_json "Running BSEtokens..."
        BSEtokens_OUTPUT=$("$BIN_DIR/BSEtokens")
        log_json "$BSEtokens_OUTPUT"
        
        if echo "$BSEtokens_OUTPUT" | grep -q "No OPTIDX instruments found"; then
            log_json "No websocket connection established as No OPTIDX instruments found."
        else
            wait_for_csv  # Wait for CSV files to be ready before running WebSocket code
            count_tokens
            
            if [ -f "$BIN_DIR/ws" ]; then
                log_json "Running ws..."
                "$BIN_DIR/ws"
            else
                log_json "ws not found!"
            fi
        fi
    else
        log_json "BSEtokens not found!"
    fi
}

# Make sure binaries are executable (will skip if not created)
if [ -f "$BIN_DIR/auth" ]; then
    chmod +x "$BIN_DIR/auth"
fi

if [ -f "$BIN_DIR/BSEtokens" ]; then
    chmod +x "$BIN_DIR/BSEtokens"
fi

if [ -f "$BIN_DIR/ws" ]; then
    chmod +x "$BIN_DIR/ws"
fi

# Main script logic
compile_all
run_all
