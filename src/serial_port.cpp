#include "serial_port.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <system_error>
#include <algorithm>

namespace VaproView
{

namespace
{
speed_t baudToTermios(int baudrate)
{
  switch (baudrate)
  {
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
  case 230400:
    return B230400;
  case 460800:
    return B460800;
  case 921600:
    return B921600;
  default:
    throw std::invalid_argument("Unsupported baudrate: " + std::to_string(baudrate));
  }
}
}

SerialPort::SerialPort()
    : fd_(-1)
{
}

SerialPort::~SerialPort()
{
  close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept
    : fd_(other.fd_)
    , last_error_(std::move(other.last_error_))
{
  other.fd_ = -1;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept
{
  if (this != &other)
  {
    close();
    fd_ = other.fd_;
    last_error_ = std::move(other.last_error_);
    other.fd_ = -1;
  }
  return *this;
}

bool SerialPort::open(const std::string& port, const SerialConfig& config)
{
  close();

  fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0)
  {
    last_error_ = "Failed to open port " + port + ": " + std::strerror(errno);
    return false;
  }

  termios tty{};
  if (tcgetattr(fd_, &tty) != 0)
  {
    last_error_ = "tcgetattr failed: " + std::string(std::strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  cfmakeraw(&tty);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CRTSCTS;

  switch (config.parity)
  {
  case Parity::Even:
    tty.c_cflag |= PARENB;
    tty.c_cflag &= ~PARODD;
    break;
  case Parity::Odd:
    tty.c_cflag |= PARENB;
    tty.c_cflag |= PARODD;
    break;
  case Parity::None:
  default:
    tty.c_cflag &= ~PARENB;
    break;
  }

  tty.c_cflag &= ~CSIZE;
  switch (config.data_bits)
  {
  case 7:
    tty.c_cflag |= CS7;
    break;
  case 8:
  default:
    tty.c_cflag |= CS8;
    break;
  }

  switch (config.stop_bits)
  {
  case StopBits::Two:
    tty.c_cflag |= CSTOPB;
    break;
  case StopBits::One:
  default:
    tty.c_cflag &= ~CSTOPB;
    break;
  }

  try
  {
    const speed_t speed = baudToTermios(config.baudrate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
  }
  catch (const std::exception& e)
  {
    last_error_ = e.what();
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0)
  {
    last_error_ = "tcsetattr failed: " + std::string(std::strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  tcflush(fd_, TCIOFLUSH);
  last_error_.clear();
  return true;
}

bool SerialPort::open(const std::string& port, int baudrate)
{
  return open(port, SerialConfig::N81(baudrate));
}

void SerialPort::close()
{
  if (fd_ >= 0)
  {
    ::close(fd_);
    fd_ = -1;
  }
}

bool SerialPort::isOpen() const
{
  return fd_ >= 0;
}

ssize_t SerialPort::read(void* buffer, size_t size)
{
  if (fd_ < 0)
  {
    last_error_ = "Port not open";
    return -1;
  }

  ssize_t n = ::read(fd_, buffer, size);
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
  {
    last_error_ = "Read error: " + std::string(std::strerror(errno));
  }
  return n;
}

ssize_t SerialPort::write(const void* buffer, size_t size)
{
  if (fd_ < 0)
  {
    last_error_ = "Port not open";
    return -1;
  }

  ssize_t n = ::write(fd_, buffer, size);
  if (n < 0)
  {
    last_error_ = "Write error: " + std::string(std::strerror(errno));
  }
  return n;
}

int SerialPort::readLine(std::string& line, char delimiter)
{
  if (fd_ < 0)
  {
    last_error_ = "Port not open";
    return -1;
  }

  line.clear();
  char ch;
  while (true)
  {
    ssize_t n = ::read(fd_, &ch, 1);
    if (n <= 0)
    {
      return line.empty() ? -1 : static_cast<int>(line.size());
    }
    if (ch == delimiter)
    {
      return static_cast<int>(line.size());
    }
    line.push_back(ch);
  }
}

int SerialPort::readAll(std::string& data)
{
  if (fd_ < 0)
  {
    last_error_ = "Port not open";
    return -1;
  }

  data.clear();
  char buffer[1024];
  ssize_t n;
  while ((n = ::read(fd_, buffer, sizeof(buffer))) > 0)
  {
    data.append(buffer, static_cast<size_t>(n));
  }
  return static_cast<int>(data.size());
}

bool SerialPort::flush()
{
  if (fd_ < 0)
  {
    return false;
  }
  return tcflush(fd_, TCIOFLUSH) == 0;
}

bool SerialPort::setNonBlocking(bool non_blocking)
{
  if (fd_ < 0)
  {
    return false;
  }

  int flags = fcntl(fd_, F_GETFL, 0);
  if (flags < 0)
  {
    return false;
  }

  if (non_blocking)
  {
    flags |= O_NONBLOCK;
  }
  else
  {
    flags &= ~O_NONBLOCK;
  }

  return fcntl(fd_, F_SETFL, flags) == 0;
}

const std::string& SerialPort::lastError() const
{
  return last_error_;
}

int SerialPort::fileDescriptor() const
{
  return fd_;
}

}
