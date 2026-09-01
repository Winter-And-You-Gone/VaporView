#include "serial_port.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace VaporView
{

#ifndef _WIN32
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
#ifdef B500000
  case 500000:
    return B500000;
#endif
  case 921600:
    return B921600;
  default:
    throw std::invalid_argument("Unsupported baudrate: " + std::to_string(baudrate));
  }
}
}
#endif

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
#ifdef _WIN32
    , handle_(other.handle_)
#endif
{
  other.fd_ = -1;
#ifdef _WIN32
  other.handle_ = nullptr;
#endif
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept
{
  if (this != &other)
  {
    close();
    fd_ = other.fd_;
    last_error_ = std::move(other.last_error_);
    other.fd_ = -1;
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#endif
  }
  return *this;
}

bool SerialPort::open(const std::string& port, const SerialConfig& config)
{
  close();
  if (config.baudrate <= 0)
  {
    last_error_ = "Baudrate must be a positive integer";
    return false;
  }

#ifdef _WIN32
  std::string winPort = port;
  if (winPort.rfind("\\\\.\\", 0) != 0)
  {
    winPort = "\\\\.\\" + winPort;
  }

  HANDLE h = CreateFileA(winPort.c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         nullptr,
                         OPEN_EXISTING,
                         0,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE)
  {
    last_error_ = "Failed to open port " + port + " (Win32 error " + std::to_string(GetLastError()) + ")";
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(h, &dcb))
  {
    last_error_ = "GetCommState failed (Win32 error " + std::to_string(GetLastError()) + ")";
    CloseHandle(h);
    return false;
  }

  dcb.BaudRate = static_cast<DWORD>(config.baudrate);
  dcb.ByteSize = static_cast<BYTE>(std::clamp(config.data_bits, 5, 8));
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = (config.parity != Parity::None);
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fInX = FALSE;
  dcb.fOutX = FALSE;

  switch (config.parity)
  {
  case Parity::Even:
    dcb.Parity = EVENPARITY;
    break;
  case Parity::Odd:
    dcb.Parity = ODDPARITY;
    break;
  case Parity::None:
  default:
    dcb.Parity = NOPARITY;
    break;
  }

  if (config.stop_bits == StopBits::Two)
  {
    dcb.StopBits = TWOSTOPBITS;
  }

  if (!SetCommState(h, &dcb))
  {
    last_error_ = "SetCommState failed (Win32 error " + std::to_string(GetLastError()) + ")";
    CloseHandle(h);
    return false;
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 100;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 100;
  timeouts.WriteTotalTimeoutMultiplier = 0;

  if (!SetCommTimeouts(h, &timeouts))
  {
    last_error_ = "SetCommTimeouts failed (Win32 error " + std::to_string(GetLastError()) + ")";
    CloseHandle(h);
    return false;
  }

  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  handle_ = h;
  fd_ = 0;
  last_error_.clear();
  return true;
#else
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
#endif
}

bool SerialPort::open(const std::string& port, int baudrate)
{
  return open(port, SerialConfig::N81(baudrate));
}

void SerialPort::close()
{
#ifdef _WIN32
  if (handle_ != nullptr)
  {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
  fd_ = -1;
#else
  if (fd_ >= 0)
  {
    ::close(fd_);
    fd_ = -1;
  }
#endif
}

bool SerialPort::isOpen() const
{
#ifdef _WIN32
  return handle_ != nullptr;
#else
  return fd_ >= 0;
#endif
}

ssize_t SerialPort::read(void* buffer, size_t size)
{
  if (!isOpen())
  {
    last_error_ = "Port not open";
    return -1;
  }

#ifdef _WIN32
  DWORD bytesRead = 0;
  if (!ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(size), &bytesRead, nullptr))
  {
    last_error_ = "Read error (Win32 error " + std::to_string(GetLastError()) + ")";
    return -1;
  }
  return static_cast<ssize_t>(bytesRead);
#else
  ssize_t n = ::read(fd_, buffer, size);
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
  {
    last_error_ = "Read error: " + std::string(std::strerror(errno));
  }
  return n;
#endif
}

ssize_t SerialPort::write(const void* buffer, size_t size)
{
  if (!isOpen())
  {
    last_error_ = "Port not open";
    return -1;
  }

#ifdef _WIN32
  const auto* ptr = static_cast<const uint8_t*>(buffer);
  size_t total = 0;
  while (total < size)
  {
    DWORD bytesWritten = 0;
    DWORD toWrite = static_cast<DWORD>(std::min<size_t>(size - total, 4096));
    if (!WriteFile(static_cast<HANDLE>(handle_), ptr + total, toWrite, &bytesWritten, nullptr))
    {
      last_error_ = "Write error (Win32 error " + std::to_string(GetLastError()) + ")";
      return total > 0 ? static_cast<ssize_t>(total) : -1;
    }
    if (bytesWritten == 0)
    {
      break;
    }
    total += static_cast<size_t>(bytesWritten);
  }
  return static_cast<ssize_t>(total);
#else
  const auto* ptr = static_cast<const uint8_t*>(buffer);
  size_t total = 0;
  while (total < size)
  {
    ssize_t n = ::write(fd_, ptr + total, size - total);
    if (n < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      last_error_ = "Write error: " + std::string(std::strerror(errno));
      return total > 0 ? static_cast<ssize_t>(total) : -1;
    }
    if (n == 0)
    {
      break;
    }
    total += static_cast<size_t>(n);
  }
  return static_cast<ssize_t>(total);
#endif
}

int SerialPort::readLine(std::string& line, char delimiter)
{
  if (!isOpen())
  {
    last_error_ = "Port not open";
    return -1;
  }

  line.clear();
  char ch;
  while (true)
  {
    ssize_t n = read(&ch, 1);
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
  if (!isOpen())
  {
    last_error_ = "Port not open";
    return -1;
  }

  data.clear();
  char buffer[1024];
  ssize_t n;
  while ((n = read(buffer, sizeof(buffer))) > 0)
  {
    data.append(buffer, static_cast<size_t>(n));
  }
  return static_cast<int>(data.size());
}

bool SerialPort::flush()
{
  if (!isOpen())
  {
    return false;
  }

#ifdef _WIN32
  return PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR | PURGE_TXCLEAR) != 0;
#else
  return tcflush(fd_, TCIOFLUSH) == 0;
#endif
}

bool SerialPort::setNonBlocking(bool non_blocking)
{
#ifdef _WIN32
  if (!isOpen())
  {
    return false;
  }

  COMMTIMEOUTS timeouts{};
  if (non_blocking)
  {
    // Return immediately with any bytes already buffered by the driver.
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
  }
  else
  {
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
  }
  timeouts.WriteTotalTimeoutConstant = 100;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  return SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts) != 0;
#else
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
#endif
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

