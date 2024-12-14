#include <iostream>
#include <string>
#include <fstream>
#include <curl/curl.h>
#include "nlohmann/json.hpp"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>
#include <map>

using json = nlohmann::json;

// Function declarations
std::string base32Decode(const std::string& encoded);
std::vector<unsigned char> intToBytes(uint64_t value);
std::string generateTOTP(const std::string& secret);
std::map<std::string, std::string> readConfig(const std::string& filename);
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

int main() {
    // Load the config
    std::map<std::string, std::string> config = readConfig("config/Credentials.env");
    if (config.empty()) {
        std::cerr << "Failed to read configuration from Credentials.env" << std::endl;
        return 1;
    }

    // Generate TOTP
    std::string secret = base32Decode(config["base32Secret"]);
    std::string totp = generateTOTP(secret);
    std::cout << "Generated TOTP: " << totp << std::endl;

    // Prepare JSON payload
    json payload = {
        {"clientcode", config["clientcode"]},
        {"password", config["password"]},
        {"totp", totp},
        {"state", "Prod"}       
    };
    std::string json_payload = payload.dump();

    // Set up and execute HTTP POST request
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "X-UserType: USER");
        headers = curl_slist_append(headers, "X-SourceID: WEB");
        headers = curl_slist_append(headers, "X-ClientLocalIP: CLIENT_LOCAL_IP");
        headers = curl_slist_append(headers, "X-ClientPublicIP: CLIENT_PUBLIC_IP");
        headers = curl_slist_append(headers, "X-MACAddress: MAC_ADDRESS");
        headers = curl_slist_append(headers, ("X-PrivateKey: " + config["API_KEY"]).c_str());

        curl_easy_setopt(curl.get(), CURLOPT_URL, "https://apiconnect.angelone.in/rest/auth/angelbroking/user/v1/loginByPassword");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_payload.c_str());

        std::string response_string;
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_string);

        CURLcode res = curl_easy_perform(curl.get());

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            // Parse and handle response
            try {
                json response_json = json::parse(response_string);
                std::cout << "Response JSON: " << response_json.dump(4) << std::endl;

                // Save tokens to AuthTokens.ini
                std::ofstream configFile("config/AuthTokens.ini");
                if (configFile.is_open()) {
                    configFile << "feedToken=" << response_json["data"]["feedToken"].get<std::string>() << std::endl;
                    configFile << "AuthToken=" << response_json["data"]["jwtToken"].get<std::string>() << std::endl;
                    configFile << "refreshToken=" << response_json["data"]["refreshToken"].get<std::string>() << std::endl;
                    configFile.close();
                } else {
                    std::cerr << "Failed to open config/AuthTokens.ini for writing" << std::endl;
                }
            } catch (json::parse_error& e) {
                std::cerr << "Failed to parse JSON: " << e.what() << std::endl;
            }
        }

        curl_slist_free_all(headers);
    }

    return 0;
}

// CURL write callback function
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Base32 decoding function
std::string base32Decode(const std::string& encoded) {
    std::string decoded;
    std::map<char, int> base32Lookup;
    static const std::string BASE32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    for (int i = 0; i < 32; ++i) {
        base32Lookup[BASE32_ALPHABET[i]] = i;
    }

    int buffer = 0, bitsLeft = 0;
    for (char c : encoded) {
        if (c == '=') break;
        buffer <<= 5;
        buffer |= base32Lookup[c];
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            decoded += static_cast<char>((buffer >> (bitsLeft - 8)) & 0xFF);
            bitsLeft -= 8;
        }
    }
    return decoded;
}

// Convert integer to big-endian 8-byte array
std::vector<unsigned char> intToBytes(uint64_t value) {
    std::vector<unsigned char> bytes(8);
    for (int i = 7; i >= 0; --i) {
        bytes[i] = value & 0xFF;
        value >>= 8;
    }
    return bytes;
}

// TOTP generation function
std::string generateTOTP(const std::string& secret) {
    uint64_t timeCounter = std::time(nullptr) / 30;
    std::vector<unsigned char> timeBytes = intToBytes(timeCounter);

    unsigned char* result = HMAC(EVP_sha1(), secret.c_str(), secret.length(), timeBytes.data(), timeBytes.size(), nullptr, nullptr);

    if (!result) {
        std::cerr << "Failed to generate HMAC" << std::endl;
        return "";
    }

    int offset = result[19] & 0xf;
    uint32_t binaryCode = (result[offset] & 0x7f) << 24 |
                          (result[offset + 1] & 0xff) << 16 |
                          (result[offset + 2] & 0xff) << 8 |
                          (result[offset + 3] & 0xff);

    uint32_t totp = binaryCode % static_cast<uint32_t>(pow(10, 6));
    std::ostringstream totpStream;
    totpStream << std::setw(6) << std::setfill('0') << totp;

    return totpStream.str();
}

// Function to read the config file
std::map<std::string, std::string> readConfig(const std::string& filename) {
    std::ifstream configFile(filename);
    std::map<std::string, std::string> configMap;
    if (!configFile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return configMap;
    }
    std::string line;
    while (std::getline(configFile, line)) {
        size_t delimPos = line.find('=');
        if (delimPos != std::string::npos) {
            std::string key = line.substr(0, delimPos);
            std::string value = line.substr(delimPos + 1);
            configMap[key] = value;
        }
    }
    return configMap;
}