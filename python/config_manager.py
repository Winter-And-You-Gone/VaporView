"""
Configuration Manager for VaporView
Handles loading, saving, and managing application configuration.
"""

import json
import os
from pathlib import Path
from typing import Dict, Any, Optional
from dataclasses import dataclass, asdict
from datetime import datetime


@dataclass
class SerialPortConfig:
    port: str = ""
    baudrate: int = 115200
    parity: str = "N"
    databits: int = 8
    stopbits: int = 1


@dataclass
class AppConfig:
    gnss: SerialPortConfig = None
    imu: SerialPortConfig = None
    ptb: SerialPortConfig = None
    hmp: SerialPortConfig = None
    lidar: SerialPortConfig = None
    refresh_interval_ms: int = 100
    auto_connect: bool = False
    log_file: str = ""
    export_format: str = "csv"

    def __post_init__(self):
        is_windows = os.name == "nt"
        if self.gnss is None:
            self.gnss = SerialPortConfig(port="COM3" if is_windows else "/dev/ttyCOM3", baudrate=115200)
        if self.imu is None:
            self.imu = SerialPortConfig(port="COM4" if is_windows else "/dev/ttyIMU", baudrate=115200)
        if self.ptb is None:
            self.ptb = SerialPortConfig(
                port="COM5" if is_windows else "/dev/ttyBARO",
                baudrate=9600,
                parity="E",
                databits=7,
            )
        if self.hmp is None:
            self.hmp = SerialPortConfig(
                port="COM6" if is_windows else "/dev/ttyHMP",
                baudrate=19200,
                stopbits=2,
            )
        if self.lidar is None:
            self.lidar = SerialPortConfig(port="COM7" if is_windows else "/dev/ttyTF03", baudrate=115200)


class ConfigManager:
    DEFAULT_CONFIG_DIR = Path.home() / ".config" / "VaporView"
    DEFAULT_CONFIG_FILE = "config.json"

    def __init__(self, config_path: Optional[str] = None):
        if config_path:
            self.config_path = Path(config_path)
        else:
            self.config_path = self.DEFAULT_CONFIG_DIR / self.DEFAULT_CONFIG_FILE
        
        self.config = AppConfig()
        self._ensure_config_dir()
        self.load()

    def _ensure_config_dir(self):
        self.config_path.parent.mkdir(parents=True, exist_ok=True)

    def load(self) -> bool:
        if not self.config_path.exists():
            self.save()
            return True
        
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            self.config = AppConfig(
                gnss=SerialPortConfig(**data.get('gnss', {})),
                imu=SerialPortConfig(**data.get('imu', {})),
                ptb=SerialPortConfig(**data.get('ptb', {})),
                hmp=SerialPortConfig(**data.get('hmp', {})),
                lidar=SerialPortConfig(**data.get('lidar', {})),
                refresh_interval_ms=data.get('refresh_interval_ms', 100),
                auto_connect=data.get('auto_connect', False),
                log_file=data.get('log_file', ''),
                export_format=data.get('export_format', 'csv')
            )
            return True
        except Exception as e:
            print(f"Error loading config: {e}")
            self.config = AppConfig()
            return False

    def save(self) -> bool:
        try:
            data = {
                'gnss': asdict(self.config.gnss),
                'imu': asdict(self.config.imu),
                'ptb': asdict(self.config.ptb),
                'hmp': asdict(self.config.hmp),
                'lidar': asdict(self.config.lidar),
                'refresh_interval_ms': self.config.refresh_interval_ms,
                'auto_connect': self.config.auto_connect,
                'log_file': self.config.log_file,
                'export_format': self.config.export_format,
                'last_modified': datetime.now().isoformat()
            }
            
            with open(self.config_path, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            return True
        except Exception as e:
            print(f"Error saving config: {e}")
            return False

    def update_serial_config(self, device: str, port: str, baudrate: int, 
                             parity: str = "N", databits: int = 8, stopbits: int = 1):
        config_map = {
            'gnss': self.config.gnss,
            'imu': self.config.imu,
            'ptb': self.config.ptb,
            'hmp': self.config.hmp,
            'lidar': self.config.lidar,
        }
        
        if device in config_map:
            config_map[device].port = port
            config_map[device].baudrate = baudrate
            config_map[device].parity = parity
            config_map[device].databits = databits
            config_map[device].stopbits = stopbits

    def get_serial_config(self, device: str) -> Optional[SerialPortConfig]:
        config_map = {
            'gnss': self.config.gnss,
            'imu': self.config.imu,
            'ptb': self.config.ptb,
            'hmp': self.config.hmp,
            'lidar': self.config.lidar,
        }
        return config_map.get(device)


if __name__ == "__main__":
    manager = ConfigManager()
    print(f"Config path: {manager.config_path}")
    print(f"GNSS config: {manager.config.gnss}")
    print(f"IMU config: {manager.config.imu}")
    print(f"Lidar config: {manager.config.lidar}")

