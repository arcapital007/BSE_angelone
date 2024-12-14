#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp> // Include the nlohmann/json library
#include <unordered_map>
#include <queue>
#include <mutex>

using json = nlohmann::json;
namespace fs = std::filesystem;

typedef websocketpp::client<websocketpp::config::asio_tls_client> tls_client;

// Global map for token to symbol mapping
std::unordered_map<std::string, std::string> token_to_symbol_map;

// Function to load CSV data into the global map
void load_csv_data(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;

    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    // Read the header line
    std::getline(file, line);

    // Read the rest of the file line by line
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token, symbol, name, expiry, strike, lotsize, instrumenttype;

        auto extract_field = [](std::istringstream& ss) -> std::string {
            std::string field;
            if (ss.peek() == '"') {
                std::getline(ss, field, '"');  // Skip the opening quote
                std::getline(ss, field, '"');  // Extract the field
                ss.ignore(1, ',');  // Skip the closing quote and the comma
            } else {
                std::getline(ss, field, ',');
            }
            return field;
        };

        // Split the line into columns using the helper function
        token = extract_field(ss);
        symbol = extract_field(ss);
        name = extract_field(ss);
        expiry = extract_field(ss);
        strike = extract_field(ss);
        lotsize = extract_field(ss);
        instrumenttype = extract_field(ss);

        token_to_symbol_map[token] = symbol;  // Add the token-symbol pair to the map
    }
}

// Pre-process CSV data into the global map
void preprocess_csv_data() {
    load_csv_data("SocketTokens/AMXIDX_Tokens.csv");
    load_csv_data("SocketTokens/Tokens.csv");
}

class WebSocketClient {
public:
    WebSocketClient(const std::string& auth_token, const std::string& api_key, const std::string& client_code, const std::string& feed_token)
        : auth_token_(auth_token), api_key_(api_key), client_code_(client_code), feed_token_(feed_token), first_message_received_(false) {
    }

    void connect() {
        ws_client_.init_asio();

        ws_client_.set_tls_init_handler([this](websocketpp::connection_hdl) {
            return websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(websocketpp::lib::asio::ssl::context::sslv23);
        });

        // Set logging to be verbose
        ws_client_.set_access_channels(websocketpp::log::alevel::all);
        ws_client_.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Bind the handlers
        ws_client_.set_open_handler(std::bind(&WebSocketClient::on_open, this, std::placeholders::_1));
        ws_client_.set_message_handler(std::bind(&WebSocketClient::on_message, this, std::placeholders::_1, std::placeholders::_2));
        ws_client_.set_close_handler(std::bind(&WebSocketClient::on_close, this, std::placeholders::_1));
        ws_client_.set_fail_handler(std::bind(&WebSocketClient::on_error, this, std::placeholders::_1));
        ws_client_.set_pong_handler(std::bind(&WebSocketClient::on_pong, this, std::placeholders::_1, std::placeholders::_2));

        websocketpp::lib::error_code ec;
        tls_client::connection_ptr con = ws_client_.get_connection("wss://smartapisocket.angelone.in/smart-stream", ec);

        if (ec) {
            std::cout << "Could not create connection because: " << ec.message() << std::endl;
            return;
        }

        // Set headers
        con->replace_header("Authorization", auth_token_);
        con->replace_header("x-api-key", api_key_);
        con->replace_header("x-client-code", client_code_);
        con->replace_header("x-feed-token", feed_token_);

        connection_hdl_ = con->get_handle();
        ws_client_.connect(con);

        // Log that connection was made
        log_event("Sent connection message");

        std::thread asio_thread([&]() {
            ws_client_.run();
        });

        std::thread heartbeat_thread([&]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                send_ping();
            }
        });

        asio_thread.join();
        heartbeat_thread.join();
    }

    void send_request() {
        // First send AMXIDX_Tokens.csv tokens with exchangeType 3
        std::vector<std::string> amxidx_tokens = filter_tokens_from_csv("SocketTokens/AMXIDX_Tokens.csv");
        send_tokens_to_server(amxidx_tokens, 3);  // exchangeType 1 for AMXIDX_Tokens.csv
        log_event("tokens sent to server: AMXIDX_Tokens.csv");

        // Then send Tokens.csv tokens with exchangeType 4
        std::vector<std::string> tokens = filter_tokens_from_csv("SocketTokens/Tokens.csv");
        send_tokens_to_server(tokens, 4);  // exchangeType 2 for Tokens.csv
        log_event("tokens sent to server: Tokens.csv");
    }

private:
    tls_client ws_client_;
    websocketpp::connection_hdl connection_hdl_;  // Store the connection handle
    std::string auth_token_;
    std::string api_key_;
    std::string client_code_;
    std::string feed_token_;
    std::ofstream json_log_file_;
    bool first_message_received_;
    std::chrono::steady_clock::time_point first_message_time_;
    std::chrono::steady_clock::time_point last_logged_message_time_;

    std::queue<std::string> log_queue_;
    std::mutex log_mutex_;
    std::thread log_thread_;
    bool stop_logging_ = false;

    const int MAX_RETRY_ATTEMPT = 5;
    const int RETRY_DELAY = 10;
    const int RETRY_MULTIPLIER = 2;
    int current_retry_attempt = 0;
    bool retry_in_progress = false;

    struct SubscriptionData {
        int mode;
        std::vector<std::pair<int, std::vector<std::string>>> token_list;
    };
    
    std::map<std::string, SubscriptionData> subscription_state;

    const int HEARTBEAT_INTERVAL = 10;
    std::atomic<bool> heartbeat_active{false};
    std::thread heartbeat_thread;

    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "Connection opened." << std::endl;
        connection_hdl_ = hdl;

        // Create the "logs" folder and open the controller.json file
        std::filesystem::path log_dir = "logs";
        if (!std::filesystem::exists(log_dir)) {
            std::filesystem::create_directory(log_dir);
        }

        json_log_file_.open("logs/controller.json", std::ios::out | std::ios::app);
        if (!json_log_file_.is_open()) {
            std::cerr << "Error opening controller.json for logging" << std::endl;
        }

        send_request();  // Send the request when the connection is opened

        // Start the logging thread
        log_thread_ = std::thread(&WebSocketClient::log_worker, this);

        current_retry_attempt = 0;  // Reset retry counter on successful connection
        
        if (!subscription_state.empty()) {
            resubscribe();
        }
        
        // Start heartbeat monitoring
        start_heartbeat_monitor();
    }

    void on_message(websocketpp::connection_hdl hdl, tls_client::message_ptr msg) {
        // No action needed here since we are not saving raw data
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::cout << "Connection closed." << std::endl;
        if (json_log_file_.is_open()) {
            json_log_file_.close();
        }

        // Signal the logging thread to stop
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            stop_logging_ = true;
        }
        log_thread_.join();

        stop_heartbeat_monitor();
    }

    void on_error(websocketpp::connection_hdl hdl) {
        if (!retry_in_progress) {
            retry_in_progress = true;
            handle_reconnection();
            retry_in_progress = false;
        }
    }

    void on_pong(websocketpp::connection_hdl hdl, std::string payload) {
        std::cout << "Received pong: " << payload << std::endl;
        log_event("Heartbeat received.");
    }

    void send_ping() {
        websocketpp::lib::error_code ec;
        ws_client_.ping(connection_hdl_, "ping", ec);
        if (ec) {
            std::cout << "Ping error: " << ec.message() << std::endl;
        } else {
            log_event("Heartbeat sent.");
            std::cout << "Ping sent." << std::endl;
        }
    }

    void send_tokens_to_server(const std::vector<std::string>& tokens, int exchange_type) {
        const size_t chunk_size = 100;

        for (size_t i = 0; i < tokens.size(); i += chunk_size) {
            size_t end = std::min(i + chunk_size, tokens.size());
            std::vector<std::string> chunk(tokens.begin() + i, tokens.begin() + end);

            std::string unique_tokens = "";
            for (size_t j = 0; j < chunk.size(); ++j) {
                unique_tokens += "\"" + chunk[j] + "\"";
                if (j < chunk.size() - 1) {
                    unique_tokens += ", ";
                }
            }

            std::string request = R"({
                "correlationID": "abcde12345",
                "action": 1,
                "params": {
                    "mode": 3,
                    "tokenList": [
                        {
                            "exchangeType": )" + std::to_string(exchange_type) + R"(,
                            "tokens": [)" + unique_tokens + R"(]
                        }
                    ]
                }
            })";

            // Log the number of tokens in the chunk and the request format
            std::string log_message = "Number of tokens sent to server with exchangeType " + std::to_string(exchange_type) + ": " + std::to_string(chunk.size());
            log_event(log_message);

            websocketpp::lib::error_code ec;
            ws_client_.send(connection_hdl_, request, websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cout << "Send request error: " << ec.message() << std::endl;
            } else {
                std::cout << "Request sent." << std::endl;
            }
        }
    }

    std::vector<std::string> filter_tokens_from_csv(const std::string& filename) {
        std::vector<std::string> tokens;
        std::ifstream file(filename);
        std::string line;

        if (!file.is_open()) {
            std::cerr << "Error opening file: " << filename << std::endl;
            return tokens;
        }

        // Read the header line
        std::getline(file, line);

        // Read the rest of the file line by line
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string token, symbol, name, expiry, strike, lotsize, instrumenttype;

            auto extract_field = [](std::istringstream& ss) -> std::string {
                std::string field;
                if (ss.peek() == '"') {
                    std::getline(ss, field, '"');  // Skip the opening quote
                    std::getline(ss, field, '"');  // Extract the field
                    ss.ignore(1, ',');  // Skip the closing quote and the comma
                } else {
                    std::getline(ss, field, ',');
                }
                return field;
            };

            // Split the line into columns using the helper function
            token = extract_field(ss);
            symbol = extract_field(ss);
            name = extract_field(ss);
            expiry = extract_field(ss);
            strike = extract_field(ss);
            lotsize = extract_field(ss);
            instrumenttype = extract_field(ss);

            tokens.push_back(token);  // Add the token
        }

        // Log the total number of tokens read from the file
        log_event("Total tokens read from " + filename + ": " + std::to_string(tokens.size()));

        return tokens;
    }

    void log_event(const std::string& message) {
        std::time_t t = std::time(nullptr);
        std::tm tm = *std::localtime(&t);

        std::stringstream time_stream;
        time_stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream timestamp;
        timestamp << time_stream.str() << '.' << std::setfill('0') << std::setw(3) << now_ms.count();

        std::string log_entry = "{\n\t\"Source\" : \"AO\",\n\t\"message\" : \"" + message + "\",\n\t\"time\" : \"" + timestamp.str() + "\"\n}\n";

        // Add the log entry to the queue
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            log_queue_.push(log_entry);
        }
    }

    void log_worker() {
        while (true) {
            std::string log_entry;
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                if (log_queue_.empty()) {
                    if (stop_logging_) {
                        break;
                    }
                    continue;
                }
                log_entry = log_queue_.front();
                log_queue_.pop();
            }
            if (json_log_file_.is_open()) {
                json_log_file_ << log_entry;
                json_log_file_.flush();  // Ensure it is written to the file immediately
            }
        }
    }

    void handle_reconnection() {
        if (current_retry_attempt < MAX_RETRY_ATTEMPT) {
            current_retry_attempt++;
            
            // Calculate delay using exponential backoff
            int delay = RETRY_DELAY * std::pow(RETRY_MULTIPLIER, current_retry_attempt - 1);
            
            log_event("Attempting to reconnect. Attempt " + std::to_string(current_retry_attempt));
            
            // Sleep for the calculated delay
            std::this_thread::sleep_for(std::chrono::seconds(delay));
            
            // Close existing connection
            if (ws_client_.get_con_from_hdl(connection_hdl_)->get_state() 
                != websocketpp::session::state::closed) {
                ws_client_.close(connection_hdl_, 
                    websocketpp::close::status::normal, "Reconnecting");
            }
            
            // Attempt reconnection
            connect();
        } else {
            log_event("Max retry attempts reached. Connection closed.");
        }
    }

    void resubscribe() {
        for (const auto& [correlation_id, sub_data] : subscription_state) {
            json request;
            request["correlationID"] = correlation_id;
            request["action"] = 1;  // SUBSCRIBE_ACTION
            request["params"]["mode"] = sub_data.mode;
            
            json token_list = json::array();
            for (const auto& [exchange_type, tokens] : sub_data.token_list) {
                json exchange_data;
                exchange_data["exchangeType"] = exchange_type;
                exchange_data["tokens"] = tokens;
                token_list.push_back(exchange_data);
            }
            request["params"]["tokenList"] = token_list;

            websocketpp::lib::error_code ec;
            ws_client_.send(connection_hdl_, request.dump(), 
                websocketpp::frame::opcode::text, ec);
            
            if (ec) {
                log_event("Resubscription failed: " + ec.message());
            } else {
                log_event("Resubscribed to tokens for mode: " + 
                    std::to_string(sub_data.mode));
            }
        }
    }

    void start_heartbeat_monitor() {
        heartbeat_active = true;
        heartbeat_thread = std::thread([this]() {
            while (heartbeat_active) {
                websocketpp::lib::error_code ec;
                ws_client_.ping(connection_hdl_, "ping", ec);
                
                if (ec) {
                    log_event("Heartbeat failed: " + ec.message());
                    handle_reconnection();
                }
                
                std::this_thread::sleep_for(
                    std::chrono::seconds(HEARTBEAT_INTERVAL));
            }
        });
        heartbeat_thread.detach();
    }
    
    void stop_heartbeat_monitor() {
        heartbeat_active = false;
        if (heartbeat_thread.joinable()) {
            heartbeat_thread.join();
        }
    }
};

std::map<std::string, std::string> parse_ini_file(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);
            config[key] = value;
        }
    }
    return config;
}

std::map<std::string, std::string> parse_env_file(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);
            config[key] = value;
        }
    }
    return config;
}

int main() {
    // Pre-process CSV data into the global map
    preprocess_csv_data();

    // Read credentials from config files
    auto auth_config = parse_ini_file("config/AuthTokens.ini");
    auto env_config = parse_env_file("config/Credentials.env");

    std::string auth_token = auth_config["AuthToken"];
    std::string feed_token = auth_config["feedToken"];
    std::string client_code = env_config["clientcode"];
    std::string api_key = env_config["API_KEY"];

    // Initialize the WebSocket client
    WebSocketClient ws_client(auth_token, api_key, client_code, feed_token);

    // Connect to the server
    ws_client.connect();

    return 0;
}