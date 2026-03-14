#ifndef VaporView_SERIAL_PORT_H
#define VaporView_SERIAL_PORT_H

#include <cstdint>
#include <cstddef>
#include <string>

#ifdef _MSC_VER
#include <BaseTsd.h>
using ssize_t = SSIZE_T;
#endif

namespace VaporView
{

enum class Parity
{
  None,
  Even,
  Odd
};

enum class StopBits
{
  One,
  Two
};

struct SerialConfig
{
  int baudrate = 115200;
  int data_bits = 8;
  Parity parity = Parity::None;
  StopBits stop_bits = StopBits::One;

  static SerialConfig N81(int baud = 115200)
  {
    return {baud, 8, Parity::None, StopBits::One};
  }

  static SerialConfig N82(int baud = 115200)
  {
    return {baud, 8, Parity::None, StopBits::Two};
  }

  static SerialConfig E71(int baud = 9600)
  {
    return {baud, 7, Parity::Even, StopBits::One};
  }

  static SerialConfig O71(int baud = 9600)
  {
    return {baud, 7, Parity::Odd, StopBits::One};
  }
};

class SerialPort
{
public:
  SerialPort();
  ~SerialPort();

  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;
  SerialPort(SerialPort&& other) noexcept;
  SerialPort& operator=(SerialPort&& other) noexcept;

  bool open(const std::string& port, const SerialConfig& config);
  bool open(const std::string& port, int baudrate);
  void close();
  bool isOpen() const;

  ssize_t read(void* buffer, size_t size);
  ssize_t write(const void* buffer, size_t size);

  int readLine(std::string& line, char delimiter = '\n');
  int readAll(std::string& data);

  bool flush();
  bool setNonBlocking(bool non_blocking);

  const std::string& lastError() const;
  int fileDescriptor() const;

private:
  int fd_;
  std::string last_error_;
#ifdef _WIN32
  void* handle_ = nullptr;
#endif
};

}

#endif

