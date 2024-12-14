# Project Name

🔥BSE AngelOne Project: Automated Stock Trading System🔥

## Overview

This project automates trading on the Bombay Stock Exchange (BSE) using the Angel One API. It handles authentication, fetches market data, generates trading parameters, and connects to a WebSocket for real-time data.

---

## Table of Contents

1. [Directory Structure](#directory-structure)
2. [Configuration Files](#configuration-files)
3. [Data Files](#data-files)
4. [Source Code Details](#source-code-details)
5. [How to Build and Run](#how-to-build-and-run)
6. [Important Features](#important-features)
7. [Error Handling and Logging](#error-handling-and-logging)
8. [Known Issues and Limitations](#known-issues-and-limitations)
9. [Future Improvements](#future-improvements)

---

## Directory Structure

The project has the following structure:

```plaintext
.
├── config
│   ├── settings
│   │   └── Holiday.ini
│   ├── AuthTokens.ini
│   └── Credentials.env
├── reference_csv
│   └── close.csv
├── SocketTokens
│   ├── AMXIDX_Tokens.csv
│   └── Tokens.csv
├── scripts
│   └── controller.sh
├── src
│   ├── Auth
│   │   └── auth.cpp
│   ├── BSEtokens
│   │   └── BSEtokens.cpp
│   └── Websocket
│       └── ws.cpp
├── logs
│   └── controller.json (auto-generated during runtime)
├── README.md
└── bin
    ├── auth (compiled binary)
    ├── BSEtokens (compiled binary)
    └── ws (compiled binary)
```

---

## Configuration Files

### 1. `config/settings/Holiday.ini`
Stores the list of holidays. The format for holidays is:
```ini
holiday1 = 25,12,2024
```

### 2. `config/AuthTokens.ini`
Stores authentication tokens after login.
```ini
feedToken=
AuthToken=
refreshToken=
```

### 3. `config/Credentials.env`
Stores credentials required for API authentication.
```plaintext
base32Secret=
clientcode=
password=
API_KEY=
```
**Note:** Ensure this file is added to `.gitignore` to avoid committing sensitive information.

---

## Data Files

### 1. `reference_csv/close.csv`
Contains calculated reference data (lower and upper ranges) for each symbol.
```csv
symbol,lower_range,upper_range
BANKEX,54700,66900
SENSEX,73400,89700
```

### 2. `SocketTokens/AMXIDX_Tokens.csv`
Contains token information for AMXIDX instruments.
```csv
token,symbol,name,expiry,strike,lotsize,instrumenttype
"99919000","SENSEX","SENSEX","","0.000000","1","AMXIDX"
"99919012","BANKEX","BANKEX","","0.000000","1","AMXIDX"
```

### 3. `SocketTokens/Tokens.csv`
Contains token information for OPTIDX instruments.
```csv
token,symbol,name,expiry,strike,lotsize,instrumenttype
"1164552","SENSEX24D1383200PE","SENSEX","13DEC2024",83200,"10","OPTIDX"
```

---

## Source Code Details

### 1. `src/Auth/auth.cpp`
This file handles:
- Authentication to AngelOne APIs.
- TOTP generation using the HMAC-SHA1 algorithm.
- Fetching and saving authentication tokens to `AuthTokens.ini`.

### 2. `src/BSEtokens/BSEtokens.cpp`
This file handles:
- Parsing holiday files to determine trading dates (D0, D1, D2).
- Fetching historical data for AMXIDX instruments.
- Calculating lower and upper ranges for BANKEX and SENSEX.
- Filtering and saving OPTIDX instruments to `Tokens.csv`.

### 3. `src/Websocket/ws.cpp`
This file handles:
- Connecting to AngelOne WebSocket for real-time data streaming.
- Sending tokens for AMXIDX and OPTIDX instruments.
- Logging messages to `logs/controller.json`.
- Robust error handling with exponential backoff for reconnections.
- Heartbeat mechanism to maintain WebSocket connection.

### 4. `scripts/controller.sh`
A shell script to automate the build and execution process. It:
- Compiles `auth.cpp`, `BSEtokens.cpp`, and `ws.cpp`.
- Runs the compiled binaries in sequence.
- Waits for CSV files to be generated before starting the WebSocket client.
- Logs all operations in JSON format to `logs/controller.json`.

---

## How to Build and Run

### Prerequisites
- **C++ Compiler**: `g++` with support for C++17.
- **Libraries**:
  - `libcurl`
  - `openssl`
  - `jsoncpp`
  - `websocketpp`
  - `boost_system`
  - `boost_thread`
  - `librdkafka++`

### Compilation
Run the following commands to compile the source code:
```bash
# Navigate to the project directory
cd /path/to/project

# Run the controller script
bash scripts/controller.sh
```

### Execution
The script `controller.sh` will:
1. Compile all source files.
2. Run the binaries in the following order:
   - `auth`
   - `BSEtokens`
   - `ws`
3. Wait for token files to be generated before running WebSocket streaming.

---

## Important Features

### 1. Dynamic Date Handling
- Calculates trading dates (D0, D1, D2) based on holidays and weekends.
- Adjusts SENSEX expiry dates to the nearest valid Friday.

### 2. Token Management
- Automatically filters AMXIDX and OPTIDX instruments.
- Saves filtered instruments to separate CSV files.

### 3. Real-Time Streaming
- Subscribes to tokens for real-time data via WebSocket.
- Handles large token lists by batching requests.
- Implements heartbeat and reconnection mechanisms.

### 4. Logging
- Logs all operations in JSON format for easier debugging.
- Includes timestamps for all events.

---

## Error Handling and Logging

### Error Handling
- **Holiday and Weekend Adjustments**: Ensures calculations exclude non-trading days.
- **Exponential Backoff**: Reconnection attempts after WebSocket failures.
- **Retry Limit**: Stops reconnection attempts after a maximum retry count.

### Logging
- Logs are stored in `logs/controller.json`.
- Includes events such as token generation, WebSocket connection status, and data streaming.

---

## Known Issues and Limitations

1. **Dependencies**: Ensure all required libraries are installed; otherwise, compilation will fail.
2. **Hardcoded Configurations**: Some API headers and parameters are hardcoded and may require updates for production.
3. **File Structure**: Changes to file paths or directory names require updates in the source code.

---

## Future Improvements

1. Add Docker support for easier deployment.
2. Implement unit tests for better reliability.
3. Use environment variables for sensitive configurations like API keys.

