#include "interfaces.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <chrono>
#include <iomanip>

// List of MAC addresses to ignore (example: common APs, test devices)
const std::vector<std::string> Utils::IGNORED_MACS = {
    "00:00:00:00:00:00",
    "FF:FF:FF:FF:FF:FF",
    "01:00:5E:00:00:00", // IPv4 multicast
    "33:33:00:00:00:00", // IPv6 multicast
    "01:80:C2:00:00:00"  // Spanning tree
};

// Escape special characters for JSON
std::string Utils::escapeJSONString(const std::string &input)
{
    std::string output;
    output.reserve(input.length() * 2);

    for (char c : input)
    {
        switch (c)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (c >= 0x20 && c <= 0x7E)
            {
                output += c;
            }
            else
            {
                // Unicode escape for non-printable
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                output += buf;
            }
            break;
        }
    }

    return output;
}

// Convert date/time string to total minutes since epoch
long Utils::convertToTotalMinutes(const std::string &dateTimeStr)
{
    // Expected format: "YYYY-MM-DDTHH:MM:SS"
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);

    // Parse ISO8601 format
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail())
    {
        return -1; // Invalid format
    }

    // Convert to time_t
    std::time_t time = std::mktime(&tm);
    if (time == -1)
    {
        return -1; // Invalid time
    }

    // Convert to minutes
    return static_cast<long>(time / 60);
}

// Check if time difference is greater than specified minutes
bool Utils::isTimeDifferenceGreaterThan(const std::string &filename, int minutes)
{
    // Extract timestamp from filename
    // Expected format: "data_YYYY_MM_DD.csv" or similar

    // This is a simplified implementation
    // In a real system, you'd extract the date from the filename
    // and compare with current time

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);

    // For now, return true to allow processing
    return true;
}

// Split string by delimiter
std::vector<std::string> Utils::split(const std::string &str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter))
    {
        tokens.push_back(token);
    }

    return tokens;
}

// Trim whitespace from string
std::string Utils::trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
    {
        return "";
    }

    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Check if MAC address should be ignored
bool Utils::isIgnoredMAC(const std::string &mac)
{
    // Convert to uppercase for comparison
    std::string upperMac = mac;
    std::transform(upperMac.begin(), upperMac.end(), upperMac.begin(), ::toupper);

    // Check against ignore list
    for (const auto &ignored : IGNORED_MACS)
    {
        if (upperMac == ignored)
        {
            return true;
        }

        // Check if it's a multicast address (first byte odd)
        if (upperMac.length() >= 2)
        {
            char firstByte[3] = {upperMac[0], upperMac[1], '\0'};
            int value = std::strtol(firstByte, nullptr, 16);
            if (value & 0x01) // Multicast bit set
            {
                return true;
            }
        }
    }

    return false;
}

// Get current timestamp in ISO8601 format
std::string Utils::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&time_t);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}