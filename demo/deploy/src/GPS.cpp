#include "GPS.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {

std::vector<std::string> splitCsv(const std::string& text)
{
    std::vector<std::string> fields;
    std::string field;
    std::stringstream ss(text);

    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }

    if (!text.empty() && text[text.size() - 1] == ',') {
        fields.push_back("");
    }

    return fields;
}

bool endsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parseDouble(const std::string& text, double& value)
{
    if (text.empty()) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }

    value = parsed;
    return true;
}

bool parseInt(const std::string& text, int& value)
{
    if (text.empty()) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    return -1;
}

bool checksumOk(const std::string& line)
{
    const std::string::size_type star = line.find('*');
    if (star == std::string::npos) {
        return true;
    }
    if (star + 2 >= line.size()) {
        return false;
    }

    const int hi = hexValue(line[star + 1]);
    const int lo = hexValue(line[star + 2]);
    if (hi < 0 || lo < 0) {
        return false;
    }

    unsigned char checksum = 0;
    const std::string::size_type begin = (!line.empty() && line[0] == '$') ? 1 : 0;
    for (std::string::size_type i = begin; i < star; ++i) {
        checksum ^= static_cast<unsigned char>(line[i]);
    }

    return checksum == static_cast<unsigned char>((hi << 4) | lo);
}

std::string sentencePayload(const std::string& line)
{
    const std::string::size_type begin = (!line.empty() && line[0] == '$') ? 1 : 0;
    const std::string::size_type star = line.find('*');
    const std::string::size_type end = (star == std::string::npos) ? line.size() : star;

    if (begin >= end) {
        return "";
    }

    return line.substr(begin, end - begin);
}

bool nmeaCoordinateToDegrees(const std::string& raw,
                             const std::string& direction,
                             double& degrees)
{
    double value = 0.0;
    if (!parseDouble(raw, value)) {
        return false;
    }

    const int whole_degrees = static_cast<int>(value / 100.0);
    const double minutes = value - whole_degrees * 100.0;
    double result = whole_degrees + minutes / 60.0;

    if (direction == "S" || direction == "W") {
        result = -result;
    } else if (direction != "N" && direction != "E") {
        return false;
    }

    degrees = result;
    return true;
}

std::string formatDouble(double value, int precision)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

speed_t baudRateToConstant(int baud_rate)
{
    switch (baud_rate) {
    case 1200:
        return B1200;
    case 2400:
        return B2400;
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return 0;
    }
}

}  // namespace

GpsInfo::GpsInfo()
    : latitude(0.0),
      longitude(0.0),
      satellites(0),
      altitude_m(0.0),
      true_heading_deg(0.0),
      magnetic_heading_deg(0.0),
      speed_knots(0.0),
      speed_kph(0.0),
      has_fix(false),
      has_satellites(false),
      has_altitude(false),
      has_true_heading(false),
      has_magnetic_heading(false),
      has_speed_knots(false),
      has_speed_kph(false)
{
}

Gps::Gps() : fd_(-1) {}

Gps::~Gps()
{
    close();
}

bool Gps::open(const std::string& device, int baud_rate)
{
    close();

    const speed_t speed = baudRateToConstant(baud_rate);
    if (speed == 0) {
        std::cerr << "Unsupported GPS baudrate: " << baud_rate << std::endl;
        return false;
    }

    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "GPS Serial Open Failed! device=" << device
                  << " error=" << std::strerror(errno) << std::endl;
        return false;
    }

    termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "tcgetattr failed: " << std::strerror(errno) << std::endl;
        close();
        return false;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "tcsetattr failed: " << std::strerror(errno) << std::endl;
        close();
        return false;
    }

    tcflush(fd_, TCIFLUSH);
    std::cout << "GPS Serial Opened! Device=" << device
              << " Baudrate=" << baud_rate << std::endl;
    return true;
}

void Gps::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    line_buffer_.clear();
    queued_lines_.clear();
}

bool Gps::isOpen() const
{
    return fd_ >= 0;
}

bool Gps::readOnce(GpsInfo& info, int timeout_ms)
{
    if (!isOpen()) {
        return false;
    }

    typedef std::chrono::steady_clock Clock;
    const Clock::time_point deadline =
        timeout_ms >= 0 ? Clock::now() + std::chrono::milliseconds(timeout_ms)
                        : Clock::time_point::max();
    bool first_try = true;

    while (timeout_ms < 0 || first_try || Clock::now() < deadline) {
        int step_timeout_ms = -1;
        if (timeout_ms >= 0) {
            if (timeout_ms == 0) {
                step_timeout_ms = 0;
            } else {
                const Clock::time_point now = Clock::now();
                if (now >= deadline) {
                    return false;
                }
                step_timeout_ms =
                    static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          deadline - now)
                                          .count());
                if (step_timeout_ms <= 0) {
                    step_timeout_ms = 1;
                }
            }
        }

        first_try = false;
        std::string line;
        if (!readLine(line, step_timeout_ms)) {
            return false;
        }

        if (handleSentence(line)) {
            info = state_;
            return true;
        }

        if (timeout_ms == 0) {
            return false;
        }
    }

    return false;
}

const GpsInfo& Gps::latest() const
{
    return state_;
}

void Gps::printInfo(const GpsInfo& info)
{
    std::cout << "*********************" << std::endl;
    std::cout << "UTC Time:" << info.utc_time << std::endl;
    std::cout << "Latitude:" << formatDouble(info.latitude, 8)
              << info.latitude_dir << std::endl;
    std::cout << "Longitude:" << formatDouble(info.longitude, 8)
              << info.longitude_dir << std::endl;
    std::cout << "Number of satellites:"
              << (info.has_satellites ? std::to_string(info.satellites) : "-")
              << std::endl;
    std::cout << "Altitude:"
              << (info.has_altitude ? formatDouble(info.altitude_m, 2) + "m" : "-")
              << std::endl;
    std::cout << "True north heading:"
              << (info.has_true_heading ? formatDouble(info.true_heading_deg, 2) + "deg" : "-")
              << std::endl;
    std::cout << "Magnetic north heading:"
              << (info.has_magnetic_heading ? formatDouble(info.magnetic_heading_deg, 2) + "deg" : "-")
              << std::endl;
    std::cout << "Ground speed:"
              << (info.has_speed_knots ? formatDouble(info.speed_knots, 2) + "Kn" : "-")
              << std::endl;
    std::cout << "Ground speed:"
              << (info.has_speed_kph ? formatDouble(info.speed_kph, 2) + "Km/h" : "-")
              << std::endl;
    std::cout << "*********************" << std::endl;
}

bool Gps::readLine(std::string& line, int timeout_ms)
{
    line.clear();
    if (!queued_lines_.empty()) {
        line = queued_lines_.front();
        queued_lines_.erase(queued_lines_.begin());
        return true;
    }

    typedef std::chrono::steady_clock Clock;
    const Clock::time_point deadline =
        timeout_ms >= 0 ? Clock::now() + std::chrono::milliseconds(timeout_ms)
                        : Clock::time_point::max();
    bool first_try = true;

    while (timeout_ms < 0 || first_try || Clock::now() < deadline) {
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            if (timeout_ms == 0) {
                wait_ms = 0;
            } else {
                const Clock::time_point now = Clock::now();
                if (now >= deadline) {
                    return false;
                }
                wait_ms =
                    static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          deadline - now)
                                          .count());
                if (wait_ms <= 0) {
                    wait_ms = 1;
                }
            }
        }

        first_try = false;

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd_, &read_set);

        timeval tv;
        timeval* tv_ptr = NULL;
        if (wait_ms >= 0) {
            tv.tv_sec = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;
            tv_ptr = &tv;
        }

        const int ready = select(fd_ + 1, &read_set, NULL, NULL, tv_ptr);
        if (ready == 0) {
            return false;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "GPS serial select failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        char buffer[128];
        const ssize_t n = ::read(fd_, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            std::cerr << "GPS serial read failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        for (ssize_t i = 0; i < n; ++i) {
            const char ch = buffer[i];
            if (ch == '\n') {
                if (!line_buffer_.empty()) {
                    queued_lines_.push_back(line_buffer_);
                }
                line_buffer_.clear();
                continue;
            }
            if (ch != '\r') {
                if (line_buffer_.size() < 256) {
                    line_buffer_.push_back(ch);
                } else {
                    line_buffer_.clear();
                }
            }
        }

        if (!queued_lines_.empty()) {
            line = queued_lines_.front();
            queued_lines_.erase(queued_lines_.begin());
            return true;
        }
    }

    return false;
}

bool Gps::handleSentence(const std::string& line)
{
    if (line.empty() || !checksumOk(line)) {
        return false;
    }

    const std::string payload = sentencePayload(line);
    const std::vector<std::string> fields = splitCsv(payload);
    if (fields.empty()) {
        return false;
    }

    const std::string& type = fields[0];
    if (endsWith(type, "GGA")) {
        return parseGga(fields);
    }
    if (endsWith(type, "VTG")) {
        parseVtg(fields);
    }

    return false;
}

bool Gps::parseGga(const std::vector<std::string>& fields)
{
    if (fields.size() < 10) {
        state_.has_fix = false;
        return false;
    }

    int fix_quality = 0;
    if (!parseInt(fields[6], fix_quality) || fix_quality <= 0) {
        state_.has_fix = false;
        return false;
    }

    double latitude = 0.0;
    double longitude = 0.0;
    if (!nmeaCoordinateToDegrees(fields[2], fields[3], latitude) ||
        !nmeaCoordinateToDegrees(fields[4], fields[5], longitude)) {
        state_.has_fix = false;
        return false;
    }

    state_.utc_time = fields[1];
    state_.latitude = latitude;
    state_.latitude_dir = fields[3];
    state_.longitude = longitude;
    state_.longitude_dir = fields[5];
    state_.has_fix = true;

    state_.has_satellites = parseInt(fields[7], state_.satellites);
    state_.has_altitude = parseDouble(fields[9], state_.altitude_m);

    return true;
}

void Gps::parseVtg(const std::vector<std::string>& fields)
{
    if (fields.size() < 9 || !state_.has_fix) {
        return;
    }

    state_.has_true_heading = parseDouble(fields[1], state_.true_heading_deg);
    state_.has_magnetic_heading = parseDouble(fields[3], state_.magnetic_heading_deg);
    state_.has_speed_knots = parseDouble(fields[5], state_.speed_knots);
    state_.has_speed_kph = parseDouble(fields[7], state_.speed_kph);
}
