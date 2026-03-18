"""
Data Exporter for VaporView
Handles exporting sensor data to various formats (CSV, JSON, KML).
"""

import csv
import json
from pathlib import Path
from datetime import datetime
from typing import Dict, Any, List, Optional
from dataclasses import asdict
import xml.etree.ElementTree as ET
from xml.dom import minidom


class DataExporter:
    def __init__(self, export_dir: str = None):
        if export_dir:
            self.export_dir = Path(export_dir)
        else:
            self.export_dir = Path.home() / "Documents" / "nav_exports"
        
        self.export_dir.mkdir(parents=True, exist_ok=True)
        
    def _generate_filename(self, prefix: str, format: str) -> Path:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        return self.export_dir / f"{prefix}_{timestamp}.{format}"
        
    def export_to_csv(self, data: Dict[str, Any], filename: str = None) -> str:
        if filename:
            file_path = Path(filename)
        else:
            file_path = self._generate_filename("nav_data", "csv")
            
        with open(file_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            
            writer.writerow(['Category', 'Parameter', 'Value', 'Unit', 'Valid', 'Timestamp'])
            
            gnss = data.get('gnss', {})
            if gnss:
                ts = gnss.get('timestamp', '')
                writer.writerow(['GNSS', 'Position Status', gnss.get('position_status', ''), '', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'Latitude', gnss.get('latitude', 0), 'deg', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'Longitude', gnss.get('longitude', 0), 'deg', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'Altitude', gnss.get('altitude', 0), 'm', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'North Velocity', gnss.get('vel_north', 0), 'm/s', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'East Velocity', gnss.get('vel_east', 0), 'm/s', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'Heading', gnss.get('heading', 0), 'deg', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'Satellites Used', gnss.get('num_satellites_used', 0), '', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'GDOP', gnss.get('gdop', 0), '', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'PDOP', gnss.get('pdop', 0), '', gnss.get('valid', False), ts])
                writer.writerow(['GNSS', 'HDOP', gnss.get('hdop', 0), '', gnss.get('valid', False), ts])
                
            imu = data.get('imu', {})
            if imu:
                ts = imu.get('timestamp', '')
                acc = imu.get('acceleration', [0, 0, 0])
                gyr = imu.get('gyroscope', [0, 0, 0])
                rpy = imu.get('rpy', [0, 0, 0])
                quat = imu.get('quaternion', [0, 0, 0, 0])
                
                writer.writerow(['IMU', 'Acceleration X', acc[0], 'm/s²', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Acceleration Y', acc[1], 'm/s²', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Acceleration Z', acc[2], 'm/s²', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Gyroscope X', gyr[0], 'dps', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Gyroscope Y', gyr[1], 'dps', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Gyroscope Z', gyr[2], 'dps', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Roll', rpy[0], 'deg', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Pitch', rpy[1], 'deg', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Yaw', rpy[2], 'deg', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Quaternion W', quat[0], '', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Quaternion X', quat[1], '', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Quaternion Y', quat[2], '', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Quaternion Z', quat[3], '', imu.get('valid', False), ts])
                writer.writerow(['IMU', 'Temperature', imu.get('temperature', 0), '°C', imu.get('valid', False), ts])
                
            ptb = data.get('ptb', {})
            if ptb:
                ts = ptb.get('timestamp', '')
                writer.writerow(['PTB210', 'Pressure', ptb.get('pressure_hpa', 0), 'hPa', ptb.get('valid', False), ts])
                
            hmp = data.get('hmp', {})
            if hmp:
                ts = hmp.get('timestamp', '')
                writer.writerow(['HMP3', 'Humidity', hmp.get('humidity', 0), '%RH', hmp.get('valid', False), ts])
                writer.writerow(['HMP3', 'Temperature', hmp.get('temperature', 0), '°C', hmp.get('valid', False), ts])

            lidar = data.get('lidar', {})
            if lidar:
                ts = lidar.get('timestamp', '')
                writer.writerow(['TF03', 'Distance', lidar.get('distance_m', 0), 'm', lidar.get('valid', False), ts])
                writer.writerow(['TF03', 'Signal Strength', lidar.get('signal_strength', 0), '', lidar.get('valid', False), ts])
                
        return str(file_path)
        
    def export_to_json(self, data: Dict[str, Any], filename: str = None) -> str:
        if filename:
            file_path = Path(filename)
        else:
            file_path = self._generate_filename("nav_data", "json")
            
        export_data = {
            'export_time': datetime.now().isoformat(),
            'gnss': data.get('gnss', {}),
            'imu': data.get('imu', {}),
            'ptb': data.get('ptb', {}),
            'hmp': data.get('hmp', {}),
            'lidar': data.get('lidar', {}),
        }
        
        with open(file_path, 'w', encoding='utf-8') as f:
            json.dump(export_data, f, indent=2, ensure_ascii=False, default=str)
            
        return str(file_path)
        
    def export_to_kml(self, data: Dict[str, Any], filename: str = None) -> str:
        if filename:
            file_path = Path(filename)
        else:
            file_path = self._generate_filename("nav_data", "kml")
            
        gnss = data.get('gnss', {})
        if not gnss or not gnss.get('valid'):
            raise ValueError("No valid GNSS data to export")
            
        kml = ET.Element('kml', xmlns="http://www.opengis.net/kml/2.2")
        document = ET.SubElement(kml, 'Document')
        
        name = ET.SubElement(document, 'name')
        name.text = "VaporView Position"
        
        placemark = ET.SubElement(document, 'Placemark')
        
        pm_name = ET.SubElement(placemark, 'name')
        pm_name.text = f"Position at {gnss.get('timestamp', 'unknown')}"
        
        description = ET.SubElement(placemark, 'description')
        desc_text = f"""
        Position Status: {gnss.get('position_status', 'N/A')}
        Altitude: {gnss.get('altitude', 0):.3f} m
        Satellites: {gnss.get('num_satellites_used', 0)}/{gnss.get('num_satellites_tracked', 0)}
        HDOP: {gnss.get('hdop', 0):.2f}
        """
        description.text = desc_text
        
        point = ET.SubElement(placemark, 'Point')
        coordinates = ET.SubElement(point, 'coordinates')
        coordinates.text = f"{gnss.get('longitude', 0)},{gnss.get('latitude', 0)},{gnss.get('altitude', 0)}"
        
        xml_str = ET.tostring(kml, encoding='unicode')
        pretty_xml = minidom.parseString(xml_str).toprettyxml(indent="  ")
        
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(pretty_xml)
            
        return str(file_path)
        
    def export_trajectory_to_kml(self, trajectory: List[Dict[str, Any]], filename: str = None) -> str:
        if filename:
            file_path = Path(filename)
        else:
            file_path = self._generate_filename("nav_trajectory", "kml")
            
        kml = ET.Element('kml', xmlns="http://www.opengis.net/kml/2.2")
        document = ET.SubElement(kml, 'Document')
        
        name = ET.SubElement(document, 'name')
        name.text = "VaporView Trajectory"
        
        placemark = ET.SubElement(document, 'Placemark')
        
        pm_name = ET.SubElement(placemark, 'name')
        pm_name.text = "Trajectory Path"
        
        linestring = ET.SubElement(placemark, 'LineString')
        tessellate = ET.SubElement(linestring, 'tessellate')
        tessellate.text = "1"
        
        coordinates = ET.SubElement(linestring, 'coordinates')
        coord_list = []
        for point in trajectory:
            if point.get('valid'):
                coord_list.append(f"{point.get('longitude', 0)},{point.get('latitude', 0)},{point.get('altitude', 0)}")
        coordinates.text = " ".join(coord_list)
        
        for i, point in enumerate(trajectory):
            if not point.get('valid'):
                continue
            placemark = ET.SubElement(document, 'Placemark')
            
            pm_name = ET.SubElement(placemark, 'name')
            pm_name.text = f"Point {i+1}"
            
            timestamp = ET.SubElement(placemark, 'TimeStamp')
            when = ET.SubElement(timestamp, 'when')
            when.text = point.get('timestamp', '')
            
            pt = ET.SubElement(placemark, 'Point')
            coords = ET.SubElement(pt, 'coordinates')
            coords.text = f"{point.get('longitude', 0)},{point.get('latitude', 0)},{point.get('altitude', 0)}"
        
        xml_str = ET.tostring(kml, encoding='unicode')
        pretty_xml = minidom.parseString(xml_str).toprettyxml(indent="  ")
        
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(pretty_xml)
            
        return str(file_path)


if __name__ == "__main__":
    exporter = DataExporter()
    
    test_data = {
        'gnss': {
            'latitude': 39.9042,
            'longitude': 116.4074,
            'altitude': 50.0,
            'vel_north': 10.5,
            'vel_east': 5.2,
            'heading': 45.0,
            'num_satellites_used': 12,
            'num_satellites_tracked': 15,
            'gdop': 1.2,
            'pdop': 1.5,
            'hdop': 0.9,
            'position_status': 'RTK_FIXED',
            'valid': True,
            'timestamp': datetime.now().isoformat()
        },
        'imu': {
            'acceleration': [0.1, 0.2, 9.8],
            'gyroscope': [0.01, 0.02, 0.03],
            'rpy': [1.0, 2.0, 45.0],
            'quaternion': [0.9, 0.1, 0.1, 0.4],
            'temperature': 25.5,
            'valid': True,
            'timestamp': datetime.now().isoformat()
        },
        'ptb': {
            'pressure_hpa': 1013.25,
            'valid': True,
            'timestamp': datetime.now().isoformat()
        },
        'hmp': {
            'humidity': 65.0,
            'temperature': 22.5,
            'valid': True,
            'timestamp': datetime.now().isoformat()
        },
        'lidar': {
            'distance_m': 12.34,
            'signal_strength': 128,
            'valid': True,
            'timestamp': datetime.now().isoformat()
        }
    }
    
    print(f"CSV: {exporter.export_to_csv(test_data)}")
    print(f"JSON: {exporter.export_to_json(test_data)}")
    print(f"KML: {exporter.export_to_kml(test_data)}")

