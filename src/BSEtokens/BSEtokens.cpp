#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <set>
#include <thread>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <map>
#include <unordered_map>
#include <cctype>
#include <cstdio> // Include for std::remove
#include <filesystem> // Include for std::filesystem

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h> // Added for NI_MAXHOST, getnameinfo, and NI_NUMERICHOST
#endif

#ifdef __APPLE__
#include <net/if_dl.h> // Include for sockaddr_dl and LLADDR on macOS
#endif

using json = nlohmann::json;

// Define IFT_ETHER for macOS and Linux
#ifndef IFT_ETHER
#define IFT_ETHER 0x6 // Ethernet
#endif

// Function to write data received from cURL to a string
size_t WriteCallbackCurl(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

// Function to read a value from a file
std::string readValueFromFile(const std::string& filePath, const std::string& key) {
    std::ifstream file(filePath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(key) != std::string::npos) {
            return line.substr(line.find('=') + 1);
        }
    }
    return "";
}

// Function to get local IP address
std::string getLocalIP() {
#ifdef _WIN32
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct hostent* host = gethostbyname(hostname);
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    return inet_ntoa(addr);
#else
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                if (std::string(ifa->ifa_name) == "eth0" || std::string(ifa->ifa_name) == "en0") {
                    freeifaddrs(ifaddr);
                    return std::string(host);
                }
            }
        }
    }
    freeifaddrs(ifaddr);
    return "";
#endif
}

// Function to get public IP address
std::string getPublicIP() {
    std::ifstream ifs("http://api.ipify.org");
    std::string publicIP((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    return publicIP;
}

// Function to get MAC address for macOS/Linux
std::string getMACAddress() {
#ifdef __APPLE__
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return "";
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        if (ifa->ifa_addr->sa_family == AF_LINK) {
            struct sockaddr_dl* sdl = (struct sockaddr_dl*)ifa->ifa_addr;
            if (sdl->sdl_type == IFT_ETHER) {
                unsigned char* mac = (unsigned char*)LLADDR(sdl);
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                freeifaddrs(ifaddr);
                return std::string(macStr);
            }
        }
    }
    freeifaddrs(ifaddr);
    return "";
#else
    struct ifreq ifr;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return "";
    }

    // Dynamically choose the network interface name
    std::string interface_name;
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        close(sockfd);
        return "";
    }

    // Look for the first available non-loopback network interface
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_flags & IFF_LOOPBACK) continue; // Skip loopback interface

        interface_name = ifa->ifa_name;  // Use the first non-loopback interface
        break;
    }
    freeifaddrs(ifaddr);

    if (interface_name.empty()) {
        std::cerr << "No suitable network interface found." << std::endl;
        close(sockfd);
        return "";
    }

    std::cout << "Using interface: " << interface_name << std::endl;  // Debug: Print the interface being used

    strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ);

    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) == -1) {
        perror("ioctl");
        close(sockfd);
        return "";
    }
    close(sockfd);

    unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return std::string(macStr);
#endif
}

// Function to round off the number based on the symbol
int roundOff(double number, const std::string& symbol) {
    int roundedNumber;
    if (symbol == "BANKEX" || symbol == "SENSEX") {
        roundedNumber = std::ceil(number / 100.0) * 100;
    } else {
        roundedNumber = std::round(number);
    }
    return roundedNumber;
}

// Function to fetch historical data
void fetchHistoricalData(const std::string& D0_str, const std::vector<nlohmann::json>& amxidxInstruments, std::map<std::string, std::pair<int, int>>& referenceData) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    curl = curl_easy_init();

    if (curl) {
        std::string apiKey = readValueFromFile("config/Credentials.env", "API_KEY");
        std::string authToken = readValueFromFile("config/AuthTokens.ini", "AuthToken");
        std::string localIP = getLocalIP();
        std::string publicIP = getPublicIP();
        std::string macAddress = getMACAddress();

        for (const auto& item : amxidxInstruments) {
            std::string symbol = item["name"];
            std::string token = item["token"];

            if (token.empty()) {
                std::cerr << "Token not found for symbol: " << symbol << std::endl;
                continue;
            }

            curl_easy_setopt(curl, CURLOPT_URL, "https://apiconnect.angelone.in/rest/secure/angelbroking/historical/v1/getCandleData");
            std::string payload = "{ \"exchange\": \"BSE\", \"symboltoken\": \"" + token + "\", \"interval\": \"ONE_DAY\", \"fromdate\": \"" + D0_str + " 00:00\", \"todate\": \"" + D0_str + " 15:40\" }";
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());

            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, ("X-PrivateKey: " + apiKey).c_str());
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, ("X-MACAddress: " + macAddress).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, ("Authorization: Bearer " + authToken).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackCurl);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            res = curl_easy_perform(curl);

            if (res != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            } else {
                try {
                    json j = json::parse(readBuffer);
                    double ltp = j["data"][0][4];
                    int upperRange = roundOff(ltp * 1.10, symbol);
                    int lowerRange = roundOff(ltp * 0.90, symbol);

                    // Store the calculated ranges in the reference data map
                    referenceData[symbol] = std::make_pair(lowerRange, upperRange);

                    // Print the final calculated ranges and close price
                    std::cout << "Symbol: " << symbol << ", Close Price: " << ltp << ", Lower Range: " << lowerRange << ", Upper Range: " << upperRange << std::endl;

                } catch (const json::parse_error& e) {
                    std::cerr << "JSON Parse Error: " << e.what() << std::endl;
                }
            }

            readBuffer.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        curl_easy_cleanup(curl);
    }
}

void saveReferenceDataToCSV(const std::map<std::string, std::pair<int, int>>& referenceData) {
    // Ensure the reference_csv folder exists
    std::filesystem::path outputDir = "reference_csv";
    if (!std::filesystem::exists(outputDir)) {
        std::filesystem::create_directory(outputDir);
    }

    // Open the close.csv file for writing
    std::ofstream csvFile(outputDir / "close.csv");
    if (!csvFile.is_open()) {
        std::cerr << "Failed to open close.csv for writing." << std::endl;
        return;
    }

    // Write the headers
    csvFile << "symbol,lower_range,upper_range\n";

    // Iterate over the referenceData map and write each entry to the CSV file
    for (const auto& entry : referenceData) {
        const std::string& symbol = entry.first;
        const std::pair<int, int>& ranges = entry.second;
        csvFile << symbol << ","
                << ranges.first << ","
                << ranges.second << "\n";
    }

    // Close the CSV file
    csvFile.close();
}

// Function to write data to a string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        // Handle memory problem
        return 0;
    }
    return newLength;
}

// Function to download JSON data from a URL
std::string downloadJsonData(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if(res != CURLE_OK) {
            std::cerr << "Failed to download data: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }
    return readBuffer;
}

// Function to filter AMXIDX instruments
void filterAMXIDXInstruments(const nlohmann::json& jsonData, std::vector<nlohmann::json>& amxidxInstruments, const std::string& D0_str, std::map<std::string, std::pair<int, int>>& referenceData) {
    std::vector<std::string> indexNames = {"BANKEX", "SENSEX"};
    for (const auto& item : jsonData) {
        if (item.contains("instrumenttype") && item["instrumenttype"] == "AMXIDX" &&
            item.contains("name") && std::find(indexNames.begin(), indexNames.end(), item["name"]) != indexNames.end()) {
            // Store AMXIDX instrument
            amxidxInstruments.push_back(item);
        }
    }

    // Save AMXIDX instruments to AMXIDX_Tokens.csv
    std::filesystem::path outputDir = "SocketTokens";
    if (!std::filesystem::exists(outputDir)) {
        std::filesystem::create_directory(outputDir);
    }

    std::ofstream amxidxFile(outputDir / "AMXIDX_Tokens.csv");
    amxidxFile << "token,symbol,name,expiry,strike,lotsize,instrumenttype\n";

    for (const auto& item : amxidxInstruments) {
        amxidxFile << item["token"] << ","
                   << item["symbol"] << ","
                   << item["name"] << ","
                   << item["expiry"] << ","
                   << item["strike"] << ","
                   << item["lotsize"] << ","
                   << item["instrumenttype"] << "\n";
    }
    amxidxFile.close();

    // Fetch historical data for AMXIDX instruments
    fetchHistoricalData(D0_str, amxidxInstruments, referenceData);

    // Save the reference data to CSV
    saveReferenceDataToCSV(referenceData);
}

// Function to filter OPTIDX instruments
void filterOPTIDXInstruments(const nlohmann::json& jsonData, const std::string& D1_str, const std::string& D2_str, const std::string& sensexExpiryDateStr, std::vector<nlohmann::json>& optidxInstruments) {
    std::vector<std::string> indexNames = {"BANKEX", "SENSEX"};
    for (const auto& item : jsonData) {
        if (item.contains("instrumenttype") && item["instrumenttype"] == "OPTIDX" &&
            item.contains("exch_seg") && item["exch_seg"] == "BFO" &&
            item.contains("name") && std::find(indexNames.begin(), indexNames.end(), item["name"]) != indexNames.end()) {
            std::string expiry = item["expiry"];
            if ((item["name"] == "BANKEX" && (expiry == D1_str || expiry == D2_str)) ||
                (item["name"] == "SENSEX" && expiry == sensexExpiryDateStr)) {
                // Store OPTIDX instrument
                optidxInstruments.push_back(item);
            }
        }
    }
}

// Function to check strike prices against reference data and save to CSV
void checkAndSaveOPTIDXInstruments(const std::vector<nlohmann::json>& optidxInstruments, const std::map<std::string, std::pair<int, int>>& referenceData) {
    std::filesystem::path outputDir = "SocketTokens";
    if (!std::filesystem::exists(outputDir)) {
        std::filesystem::create_directory(outputDir);
    }

    // Sort OPTIDX instruments by expiry date (assuming expiry is in "YYYY-MM-DD" format)
    std::vector<nlohmann::json> sortedOptidxInstruments = optidxInstruments;
    std::sort(sortedOptidxInstruments.begin(), sortedOptidxInstruments.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a["expiry"] < b["expiry"];
    });

    // Save to CSV file
    std::ofstream csvFile(outputDir / "Tokens.csv");
    csvFile << "token,symbol,name,expiry,strike,lotsize,instrumenttype\n";

    bool found = false;
    for (const auto& item : sortedOptidxInstruments) {
        std::string name = item["name"];
        double strike = std::stod(item["strike"].get<std::string>());
        int adjustedStrike = static_cast<int>(strike / 100);

        auto range = referenceData.find(name);
        if (range != referenceData.end() && adjustedStrike >= range->second.first && adjustedStrike <= range->second.second) {
            // Save to CSV
            csvFile << item["token"] << ","
                    << item["symbol"] << ","
                    << item["name"] << ","
                    << item["expiry"] << ","
                    << adjustedStrike << ","
                    << item["lotsize"] << ","
                    << item["instrumenttype"] << "\n";
            found = true;
        }
    }
    csvFile.close();

    if (!found) {
        std::cout << "No OPTIDX instruments found." << std::endl;
    }
}

// New Date Handling Functions

struct Date {
    int day;
    int month;
    int year;
};

bool isWeekend(const Date& date) {
    std::tm timeinfo = {};
    timeinfo.tm_year = date.year - 1900;
    timeinfo.tm_mon = date.month - 1;
    timeinfo.tm_mday = date.day;
    std::mktime(&timeinfo);
    return (timeinfo.tm_wday == 0 || timeinfo.tm_wday == 6);
}

bool isHoliday(const Date& date, const std::vector<Date>& holidays) {
    for (const auto& holiday : holidays) {
        if (date.day == holiday.day && date.month == holiday.month && date.year == holiday.year) {
            return true;
        }
    }
    return false;
}

bool isValidTradingDay(const Date& date, const std::vector<Date>& holidays) {
    return !isWeekend(date) && !isHoliday(date, holidays);
}

Date getNextDate(const Date& date) {
    std::tm timeinfo = {};
    timeinfo.tm_year = date.year - 1900;
    timeinfo.tm_mon = date.month - 1;
    timeinfo.tm_mday = date.day + 1;
    std::mktime(&timeinfo);
    return {timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900};
}

Date getPreviousDate(const Date& date) {
    std::tm timeinfo = {};
    timeinfo.tm_year = date.year - 1900;
    timeinfo.tm_mon = date.month - 1;
    timeinfo.tm_mday = date.day - 1;
    std::mktime(&timeinfo);
    return {timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900};
}

std::vector<Date> readHolidays(const std::string& filename) {
    std::vector<Date> holidays;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return holidays;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        char equals, comma;
        Date holiday;

        if (iss >> key >> equals >> holiday.day >> comma >> holiday.month >> comma >> holiday.year) {
            if (key.find("holiday") != std::string::npos) {
                holidays.push_back(holiday);
            }
        }
    }

    return holidays;
}

std::string formatDateYYYYMMDD(const Date& date) {
    std::ostringstream oss;
    oss << date.year << '-'
        << std::setfill('0') << std::setw(2) << date.month << '-'
        << std::setfill('0') << std::setw(2) << date.day;
    return oss.str();
}

std::string formatDateDDMMMYYYY(const Date& date) {
    static const char* months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << date.day
        << months[date.month - 1]
        << date.year;
    return oss.str();
}

// Function to calculate the next Friday
Date getNextFriday(const Date& date) {
    std::tm timeinfo = {};
    timeinfo.tm_year = date.year - 1900;
    timeinfo.tm_mon = date.month - 1;
    timeinfo.tm_mday = date.day;
    std::mktime(&timeinfo);
    
    int daysToAdd = (5 - timeinfo.tm_wday + 7) % 7;
    timeinfo.tm_mday += daysToAdd;
    std::mktime(&timeinfo);
    
    return {timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900};
}

// Function to calculate expiry date for SENSEX
Date calculateExpiryDateForSENSEX(const Date& D1, const std::vector<Date>& holidays) {
    Date expiryDate;
    
    std::tm timeinfo = {};
    timeinfo.tm_year = D1.year - 1900;
    timeinfo.tm_mon = D1.month - 1;
    timeinfo.tm_mday = D1.day;
    std::mktime(&timeinfo);
    
    if (timeinfo.tm_wday == 5) { // If D1 is Friday
        expiryDate = D1;
    } else {
        expiryDate = getNextFriday(D1);
    }
    
    // Adjust backward if it's a holiday or weekend
    while (!isValidTradingDay(expiryDate, holidays)) {
        expiryDate = getPreviousDate(expiryDate);
    }
    
    return expiryDate;
}

// Function to adjust strike price in JSON
int adjustStrikePrice(const std::string& strikeStr) {
    // Remove decimal part
    std::string strikeWithoutDecimal = strikeStr.substr(0, strikeStr.find('.'));
    // Convert to integer and divide by 100
    return std::stoi(strikeWithoutDecimal) / 100;
}

// Function to generate sequence of values incrementing by 100
std::vector<int> generateStrikeSequence(int lowerRange, int upperRange) {
    std::vector<int> sequence;
    for (int strike = lowerRange; strike <= upperRange; strike += 100) {
        sequence.push_back(strike);
    }
    return sequence;
}

int main() {
    // Load holidays from config file
    std::vector<Date> holidays = readHolidays("config/settings/Holiday.ini");

    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    Date currentDate = {now->tm_mday, now->tm_mon + 1, now->tm_year + 1900};

    // Determine D1
    Date D1 = currentDate;
    while (!isValidTradingDay(D1, holidays)) {
        D1 = getNextDate(D1);
    }

    // Determine D0
    Date D0 = getPreviousDate(D1);
    while (!isValidTradingDay(D0, holidays)) {
        D0 = getPreviousDate(D0);
    }

    // Determine D2
    Date D2 = getNextDate(D1);
    while (!isValidTradingDay(D2, holidays)) {
        D2 = getNextDate(D2);
    }

    // Format dates
    std::string D0_str = formatDateYYYYMMDD(D0);
    std::string D1_str = formatDateDDMMMYYYY(D1);
    std::string D2_str = formatDateDDMMMYYYY(D2);

    std::cout << "D0: " << D0_str << "\nD1: " << D1_str << "\nD2: " << D2_str << std::endl;

    // Calculate expiry date for SENSEX
    Date sensexExpiryDate = calculateExpiryDateForSENSEX(D1, holidays);
    std::string sensexExpiryDateStr = formatDateDDMMMYYYY(sensexExpiryDate);
    std::cout << "SENSEX Expiry Date: " << sensexExpiryDateStr << std::endl;

    // Download JSON data from the provided URL
    std::string jsonData = downloadJsonData("https://margincalculator.angelbroking.com/OpenAPI_File/files/OpenAPIScripMaster.json");

    if (!jsonData.empty()) {
        // Parse the JSON data
        nlohmann::json jsonObj = nlohmann::json::parse(jsonData);

        // Vector to store filtered AMXIDX and OPTIDX instruments
        std::vector<nlohmann::json> amxidxInstruments;
        std::vector<nlohmann::json> optidxInstruments;

        // Map to store reference data
        std::map<std::string, std::pair<int, int>> referenceData;

        // Create threads
        std::thread amxidxThread(filterAMXIDXInstruments, std::ref(jsonObj), std::ref(amxidxInstruments), D0_str, std::ref(referenceData));
        std::thread optidxThread(filterOPTIDXInstruments, std::ref(jsonObj), D1_str, D2_str, sensexExpiryDateStr, std::ref(optidxInstruments));

        // Join threads
        amxidxThread.join();
        optidxThread.join();

        // Check and save OPTIDX instruments based on reference data
        checkAndSaveOPTIDXInstruments(optidxInstruments, referenceData);

        // Print the size of the filtered AMXIDX instruments
        std::cout << "Number of AMXIDX instruments: " << amxidxInstruments.size() << std::endl;
    }

    return 0;
}