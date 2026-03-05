"""
File Logger for VaproView
Handles logging to files with rotation and formatting.
"""

import os
import csv
import json
from pathlib import Path
from datetime import datetime
from typing import Dict, Any, List, Optional
from dataclasses import asdict
import threading
import queue


class FileLogger:
    def __init__(self, log_dir: str = None, max_file_size_mb: int = 10, max_files: int = 5):
        if log_dir:
            self.log_dir = Path(log_dir)
        else:
            self.log_dir = Path.home() / ".local" / "share" / "VaproView" / "logs"
        
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.max_file_size = max_file_size_mb * 1024 * 1024
        self.max_files = max_files
        
        self.current_log_file = None
        self.log_queue = queue.Queue()
        self.running = False
        self.worker_thread = None
        
    def start(self):
        self.running = True
        self.worker_thread = threading.Thread(target=self._worker, daemon=True)
        self.worker_thread.start()
        
    def stop(self):
        self.running = False
        if self.worker_thread:
            self.worker_thread.join(timeout=2.0)
            
    def _get_log_file_path(self) -> Path:
        date_str = datetime.now().strftime("%Y%m%d")
        return self.log_dir / f"vaproview_{date_str}.log"
    
    def _rotate_if_needed(self):
        if self.current_log_file and self.current_log_file.exists():
            if self.current_log_file.stat().st_size >= self.max_file_size:
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                rotated = self.current_log_file.with_suffix(f".{timestamp}.log")
                self.current_log_file.rename(rotated)
                self._cleanup_old_files()
                
    def _cleanup_old_files(self):
        log_files = sorted(self.log_dir.glob("vaproview_*.log"), 
                          key=lambda p: p.stat().st_mtime, reverse=True)
        for old_file in log_files[self.max_files:]:
            old_file.unlink()
            
    def _worker(self):
        self.current_log_file = self._get_log_file_path()
        
        while self.running or not self.log_queue.empty():
            try:
                entry = self.log_queue.get(timeout=0.5)
                self._write_entry(entry)
            except queue.Empty:
                continue
                
    def _write_entry(self, entry: Dict[str, Any]):
        self._rotate_if_needed()
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        level = entry.get('level', 'INFO')
        source = entry.get('source', 'APP')
        message = entry.get('message', '')
        
        log_line = f"[{timestamp}] [{level}] [{source}] {message}\n"
        
        with open(self.current_log_file, 'a', encoding='utf-8') as f:
            f.write(log_line)
            
    def log(self, message: str, level: str = "INFO", source: str = "APP"):
        entry = {
            'message': message,
            'level': level,
            'source': source,
            'timestamp': datetime.now().isoformat()
        }
        self.log_queue.put(entry)
        
    def info(self, message: str, source: str = "APP"):
        self.log(message, "INFO", source)
        
    def warning(self, message: str, source: str = "APP"):
        self.log(message, "WARN", source)
        
    def error(self, message: str, source: str = "APP"):
        self.log(message, "ERROR", source)
        
    def debug(self, message: str, source: str = "APP"):
        self.log(message, "DEBUG", source)


class DataLogger:
    def __init__(self, data_dir: str = None):
        if data_dir:
            self.data_dir = Path(data_dir)
        else:
            self.data_dir = Path.home() / ".local" / "share" / "VaproView" / "data"
        
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.csv_files = {}
        self._lock = threading.Lock()
        
    def _get_csv_file(self, data_type: str) -> Path:
        date_str = datetime.now().strftime("%Y%m%d")
        return self.data_dir / f"{data_type}_{date_str}.csv"
        
    def log_gnss(self, data: Dict[str, Any]):
        with self._lock:
            file_path = self._get_csv_file("gnss")
            file_exists = file_path.exists()
            
            with open(file_path, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(['timestamp', 'latitude', 'longitude', 'altitude', 
                                   'vel_north', 'vel_east', 'heading', 'sats_used', 
                                   'sats_tracked', 'gdop', 'pdop', 'hdop', 'status'])
                
                writer.writerow([
                    datetime.now().isoformat(),
                    data.get('latitude', 0),
                    data.get('longitude', 0),
                    data.get('altitude', 0),
                    data.get('vel_north', 0),
                    data.get('vel_east', 0),
                    data.get('heading', 0),
                    data.get('num_satellites_used', 0),
                    data.get('num_satellites_tracked', 0),
                    data.get('gdop', 0),
                    data.get('pdop', 0),
                    data.get('hdop', 0),
                    data.get('position_status', '')
                ])
                
    def log_imu(self, data: Dict[str, Any]):
        with self._lock:
            file_path = self._get_csv_file("imu")
            file_exists = file_path.exists()
            
            with open(file_path, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(['timestamp', 'acc_x', 'acc_y', 'acc_z',
                                   'gyr_x', 'gyr_y', 'gyr_z',
                                   'roll', 'pitch', 'yaw',
                                   'quat_w', 'quat_x', 'quat_y', 'quat_z',
                                   'temperature', 'source'])
                
                writer.writerow([
                    datetime.now().isoformat(),
                    data.get('acceleration', [0,0,0])[0],
                    data.get('acceleration', [0,0,0])[1],
                    data.get('acceleration', [0,0,0])[2],
                    data.get('gyroscope', [0,0,0])[0],
                    data.get('gyroscope', [0,0,0])[1],
                    data.get('gyroscope', [0,0,0])[2],
                    data.get('rpy', [0,0,0])[0],
                    data.get('rpy', [0,0,0])[1],
                    data.get('rpy', [0,0,0])[2],
                    data.get('quaternion', [0,0,0,0])[0],
                    data.get('quaternion', [0,0,0,0])[1],
                    data.get('quaternion', [0,0,0,0])[2],
                    data.get('quaternion', [0,0,0,0])[3],
                    data.get('temperature', 0),
                    'HI83' if data.get('from_hi83') else 'HI91/HI81'
                ])
                
    def log_ptb(self, data: Dict[str, Any]):
        with self._lock:
            file_path = self._get_csv_file("ptb")
            file_exists = file_path.exists()
            
            with open(file_path, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(['timestamp', 'pressure_hpa'])
                
                writer.writerow([
                    datetime.now().isoformat(),
                    data.get('pressure_hpa', 0)
                ])
                
    def log_hmp(self, data: Dict[str, Any]):
        with self._lock:
            file_path = self._get_csv_file("hmp")
            file_exists = file_path.exists()
            
            with open(file_path, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(['timestamp', 'humidity', 'temperature'])
                
                writer.writerow([
                    datetime.now().isoformat(),
                    data.get('humidity', 0),
                    data.get('temperature', 0)
                ])


if __name__ == "__main__":
    logger = FileLogger()
    logger.start()
    
    logger.info("Test log message")
    logger.warning("Test warning")
    logger.error("Test error")
    
    import time
    time.sleep(1)
    logger.stop()
