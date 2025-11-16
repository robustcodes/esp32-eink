#!/usr/bin/env python3
"""
ESP32 Certificate Uploader for AWS IoT

This script uploads AWS IoT certificates to ESP32's LittleFS filesystem.

Usage:
  1. Download certificates from AWS IoT Console
  2. Place them in eink-calendar/certs/ directory
  3. Run: python3 upload_certificates.py /dev/ttyUSB0

Prerequisites:
  pip install esptool pyserial

  Download mklittlefs from:
  https://github.com/earlephilhower/mklittlefs/releases
  and add to PATH
"""

import os
import sys
import subprocess
import glob
import serial.tools.list_ports

def find_esp32_port():
    """Auto-detect ESP32 port"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "CP210" in port.description or "CH340" in port.description or "USB" in port.description:
            return port.device
    return None

def check_tools():
    """Check if required tools are installed"""
    tools = {
        "esptool.py": "pip install esptool",
        "mklittlefs": "Download from: https://github.com/earlephilhower/mklittlefs/releases"
    }

    missing = []
    for tool, install in tools.items():
        try:
            subprocess.run([tool, "--version"], capture_output=True, check=False)
        except FileNotFoundError:
            missing.append(f"{tool}: {install}")

    if missing:
        print("❌ Missing tools:")
        for m in missing:
            print(f"  - {m}")
        return False
    return True

def create_littlefs_image(data_dir="eink-calendar/data", output="littlefs.bin"):
    """Create LittleFS image from data directory"""
    # ESP32 4MB flash default partition (matches partition table at 0x290000)
    # Size is 0x160000 (1,441,792 bytes = 1.375 MB)
    fs_size = str(0x160000)  # Must match partition size exactly
    block_size = "4096"
    page_size = "256"

    cmd = [
        "mklittlefs",
        "-c", data_dir,
        "-b", block_size,
        "-p", page_size,
        "-s", fs_size,
        output
    ]

    print(f"\n📦 Creating LittleFS image from {data_dir}/...")
    print(f"   Size: {int(fs_size)/1024/1024:.2f} MB")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"❌ Error: {result.stderr}")
        return False

    print(f"✅ LittleFS image created: {output}")
    return True

def upload_littlefs(port, image="littlefs.bin"):
    """Upload LittleFS image to ESP32"""
    # LittleFS start address for 4MB ESP32 (same as SPIFFS partition)
    fs_address = "0x290000"

    cmd = [
        "esptool.py",
        "--chip", "esp32",
        "--port", port,
        "--baud", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        fs_address,
        image
    ]

    print(f"\n📤 Uploading LittleFS to {port}...")
    print(f"   Command: {' '.join(cmd)}\n")
    print("💡 If upload fails, hold BOOT button and press EN button on ESP32\n")

    result = subprocess.run(cmd)
    return result.returncode == 0

def organize_certificates(cert_dir):
    """Organize certificates in data directory"""
    data_dir = "eink-calendar/data/certs"
    os.makedirs(data_dir, exist_ok=True)

    # Expected files and their possible names
    cert_files = {
        "root-ca.pem": ["AmazonRootCA1.pem", "root-ca.pem", "root-ca.crt", "ca.pem"],
        "certificate.pem.crt": ["*-certificate.pem.crt", "certificate.pem.crt", "device.cert.pem", "cert.pem"],
        "private.pem.key": ["*-private.pem.key", "private.pem.key", "device.key", "private.key"]
    }

    found = {}

    # Find certificates
    for target, possible_names in cert_files.items():
        for name_pattern in possible_names:
            if "*" in name_pattern:
                # Handle wildcard patterns
                matches = glob.glob(os.path.join(cert_dir, name_pattern))
                if matches:
                    source = matches[0]
                    if os.path.exists(source):
                        found[target] = source
                        break
            else:
                source = os.path.join(cert_dir, name_pattern)
                if os.path.exists(source):
                    found[target] = source
                    break

    # Copy certificates to data directory
    for target, source in found.items():
        dest = os.path.join(data_dir, target)
        with open(source, 'r') as f:
            content = f.read()
        with open(dest, 'w') as f:
            f.write(content)
        print(f"✅ Copied {os.path.basename(source)} → {target}")

    # Check if all required certificates are present
    missing = []
    for required in cert_files.keys():
        if required not in found:
            missing.append(required)

    if missing:
        print("\n❌ Missing certificates:")
        for m in missing:
            print(f"   - {m}")
        return False

    return True

def main():
    print("=" * 50)
    print("  ESP32 Certificate Uploader for AWS IoT")
    print("=" * 50)

    # Check tools
    print("\n🔍 Checking required tools...")
    if not check_tools():
        print("\n❌ Please install missing tools first.")
        sys.exit(1)
    print("✅ All tools found!")

    # Get port
    port = sys.argv[1] if len(sys.argv) > 1 else find_esp32_port()

    if not port:
        print("\n❌ No ESP32 found! Please specify port:")
        print("   python3 upload_certificates.py /dev/ttyUSB0")
        sys.exit(1)

    print(f"✅ Using port: {port}")

    # Check for certificates
    cert_locations = [
        "eink-calendar/certs",
        "eink-calendar/certificates",
        "certs",
        "certificates",
        "."
    ]

    cert_dir = None

    for loc in cert_locations:
        if os.path.exists(loc):
            files = os.listdir(loc)
            if any(".pem" in f or ".crt" in f or ".key" in f for f in files):
                cert_dir = loc
                break

    if not cert_dir:
        print("\n❌ No certificates found!")
        print("\nPlease download certificates from AWS IoT Console and place them in:")
        print("   eink-calendar/certs/")
        print("\nRequired files:")
        print("   - AmazonRootCA1.pem (or root-ca.pem)")
        print("   - xxx-certificate.pem.crt")
        print("   - xxx-private.pem.key")
        sys.exit(1)

    print(f"\n📁 Found certificates in: {cert_dir}/")

    # Organize certificates
    print("\n📋 Organizing certificates...")
    if not organize_certificates(cert_dir):
        print("\n❌ Failed to organize certificates")
        sys.exit(1)

    # Create LittleFS image
    if not create_littlefs_image():
        print("\n❌ Failed to create LittleFS image")
        sys.exit(1)

    # Upload to ESP32
    if upload_littlefs(port):
        print("\n" + "=" * 50)
        print("✅ Certificates uploaded successfully!")
        print("=" * 50)
        print("\n🎉 Your ESP32 can now connect to AWS IoT Core!")

        # Cleanup
        if os.path.exists("littlefs.bin"):
            os.remove("littlefs.bin")
    else:
        print("\n❌ Upload failed!")
        print("💡 Check connection and try again")
        print("💡 Try holding BOOT button during upload")
        sys.exit(1)

if __name__ == "__main__":
    main()
