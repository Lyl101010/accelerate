#ifndef GPS_H
#define GPS_H

#include <string>
#include <vector>

struct GpsInfo {
    std::string utc_time;
    double latitude;
    std::string latitude_dir;
    double longitude;
    std::string longitude_dir;
    int satellites;
    double altitude_m;
    double true_heading_deg;
    double magnetic_heading_deg;
    double speed_knots;
    double speed_kph;
    bool has_fix;
    bool has_satellites;
    bool has_altitude;
    bool has_true_heading;
    bool has_magnetic_heading;
    bool has_speed_knots;
    bool has_speed_kph;

    GpsInfo();
};

class Gps {
public:
    Gps();
    ~Gps();

    bool open(const std::string& device = "/dev/ttyUSB0", int baud_rate = 9600);
    void close();
    bool isOpen() const;

    bool readOnce(GpsInfo& info, int timeout_ms = 3000);
    const GpsInfo& latest() const;

    static void printInfo(const GpsInfo& info);

private:
    bool readLine(std::string& line, int timeout_ms);
    bool handleSentence(const std::string& line);
    bool parseGga(const std::vector<std::string>& fields);
    void parseVtg(const std::vector<std::string>& fields);

    int fd_;
    GpsInfo state_;
    std::string line_buffer_;
    std::vector<std::string> queued_lines_;
};

#endif  // GPS_H
