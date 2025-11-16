# E-Ink Calendar with AWS IoT

ESP32-based e-ink display showing Google Calendar events and weather data via AWS IoT Core.

## 🏗️ Architecture

```
┌─────────────────┐      ┌──────────────────┐
│ Google Calendar │      │ OpenWeatherMap   │
└────────┬────────┘      └────────┬─────────┘
         │                        │
         │ OAuth                  │ API Key
         ▼                        ▼
    ┌────────────────────────────────┐
    │   AWS Lambda Functions         │
    │  ┌──────────┐   ┌───────────┐  │
    │  │ Calendar │   │  Weather  │  │
    │  │  Lambda  │   │  Lambda   │  │
    │  └────┬─────┘   └─────┬─────┘  │
    └───────┼───────────────┼────────┘
            │               │
            └───────┬───────┘
                    │ Publish to topics
                    ▼
         ┌─────────────────────┐
         │   AWS IoT Core      │
         │  (MQTT Broker)      │
         └──────────┬──────────┘
                    │ TLS certificates
                    ▼
            ┌──────────────┐
            │    ESP32     │
            │  (Subscribe) │
            └──────┬───────┘
                   │
                   ▼
          ┌────────────────┐
          │ E-Ink Display  │
          └────────────────┘
```

**How it works:**
1. **EventBridge** triggers two Lambda functions every 3 hours
2. **Calendar Lambda** fetches events from Google Calendar (OAuth)
3. **Weather Lambda** fetches current weather and forecast (API)
4. Both Lambdas publish data to **AWS IoT Core** MQTT topics
5. **ESP32** wakes from deep sleep, connects via TLS certificates
6. ESP32 subscribes to topics, receives calendar + weather data
7. **E-Ink display** updates and ESP32 returns to deep sleep

## 🔧 Hardware Components

### Required Parts:

| Component | Specification |
|-----------|--------------|
| **Microcontroller** | LOLIN32 Board<br>• ESP32 240MHz Dual-Core<br>• WLAN + Bluetooth/BLE<br>• 4MB Flash<br>• Built-in charging circuit<br>• Arduino IDE compatible |
| **Display** | 7.5" E-Ink Display HAT<br>• 800×480 resolution<br>• 3-color (red/black/white)<br>• Waveshare compatible<br>• For Raspberry Pi (works with ESP32) |
| **Battery** | Lithium Polymer (Li-Po)<br>• 3.7V 1200mAh<br>• JST connector |

### Pin Connections:

The display connects to the LOLIN32 via the 9-pin HAT connector:

| HAT Pin | LOLIN32 Pin | Function |
|---------|-------------|----------|
| VCC | 3.3V | Power (⚠️ **DO NOT use 5V**) |
| GND | GND | Ground |
| DIN | GPIO 23 | MOSI (SPI Data) |
| CLK | GPIO 18 | SCK (SPI Clock) |
| CS | GPIO 5 | Chip Select |
| DC | GPIO 17 | Data/Command |
| RST | GPIO 16 | Reset |
| BUSY | GPIO 4 | Busy Signal |
| PWR | GPIO 2 | HAT Power Control |

### Battery Life:

With deep sleep between updates (default: 30 minutes):
- **Expected runtime:** ~2-4 days on 1200mAh battery
- Wake time: ~10-15 seconds per update
- Sleep current: <100µA

## 📚 Required Libraries

The ESP32 code requires the following Arduino libraries (install via Arduino IDE Library Manager or `arduino-cli`):

- **GxEPD2** - E-Paper display driver
- **Adafruit GFX Library** - Graphics core library
- **ArduinoJson** - JSON parsing (v6 or later)
- **PubSubClient** - MQTT client for AWS IoT

Install all at once:
```bash
arduino-cli lib install "GxEPD2" "Adafruit GFX Library" "ArduinoJson" "PubSubClient"
```

Or use the Makefile:
```bash
cd eink-calendar
make install-deps
```

### Never commit:
- `eink-calendar/config.h` - Contains WiFi password
- `eink-calendar/certs/` - Contains AWS IoT certificates (private keys!)
- `infrastructure/.env` - Contains API keys

### First-time setup:

1. **ESP32 Configuration:**
   ```bash
   cd eink-calendar
   cp config.h.example config.h
   # Edit config.h with your WiFi and AWS IoT details
   ```

2. **Infrastructure Environment:**
   ```bash
   cd infrastructure
   cp .env.example .env
   # Edit .env with your OpenWeatherMap API key
   ```

3. **AWS IoT Certificates:**
   - Create certificates using AWS IoT console or CLI
   - Place in `eink-calendar/certs/`:
     - `root-ca.pem`
     - `certificate.pem.crt`
     - `private.pem.key`

## 📁 Project Structure

```
├── eink-calendar/          # ESP32 Arduino sketch
│   ├── config.h.example    # Configuration template
│   ├── config.h            # Your config (gitignored)
│   └── certs/              # AWS IoT certificates (gitignored)
│
├── infrastructure/         # AWS CDK infrastructure
│   ├── lib/                # CDK stack definitions
│   ├── lambda/             # Lambda functions (calendar & weather)
│   ├── .env.example        # Environment template
│   └── .env                # Your environment (gitignored)
│
└── extras/                 # Additional resources
```

## 🚀 Deployment

### 1. Get Google Calendar Credentials:

1. Go to https://console.cloud.google.com/
2. Create a new project
3. Enable **Google Calendar API** (APIs & Services → Library)
4. Create **OAuth consent screen**:
   - User Type: External
   - App name: "E-Ink Calendar"
   - Add your email address
5. Create **OAuth credentials** (APIs & Services → Credentials):
   - Click "Create Credentials" → "OAuth client ID"
   - Application type: **Desktop app**
   - Copy the Client ID and Client Secret

6. Generate refresh token:
   ```bash
   pip install google-auth-oauthlib
   export GOOGLE_CLIENT_ID="your-id.apps.googleusercontent.com"
   export GOOGLE_CLIENT_SECRET="GOCSPX-your-secret"
   python3 get_google_tokens.py
   ```

7. Copy the output credentials to `infrastructure/.env`

### 2. Deploy AWS Infrastructure:
```bash
cd infrastructure
npm install
cdk deploy
```

### 3. Upload Certificates to ESP32:

The certificates must be uploaded to the ESP32's LittleFS filesystem:

```bash
# Place your AWS IoT certificates in eink-calendar/data/certs/:
# - root-ca.pem
# - certificate.pem.crt
# - private.pem.key

# Upload certificates using the included script:
python3 upload_certificates.py /dev/ttyUSB0
```

### 4. Upload ESP32 Code:

Using Arduino IDE:
1. Open `eink-calendar/eink-calendar.ino`
2. Install required libraries (see above)
3. Select board: "LOLIN32"
4. Upload

Using Makefile:
```bash
cd eink-calendar
make install-deps  # Install libraries
make flash         # Compile and upload
make monitor       # View serial output
```

## 🔑 Environment Variables

### Infrastructure (.env):
- `WEATHER_API_KEY` - OpenWeatherMap API key (required)
- `WEATHER_CITY` - City for weather data (default: Vantaa,FI)
- `CALENDAR_ID` - Google Calendar ID (default: primary)

### ESP32 (config.h):
- `WIFI_SSID` - WiFi network name
- `WIFI_PASSWORD` - WiFi password
- `AWS_IOT_ENDPOINT` - AWS IoT endpoint
- `AWS_THING_NAME` - IoT thing name

## 💰 Cost Estimate

### Hardware (One-time)
- LOLIN32 ESP32 board: ~€8-12
- 7.5" E-Ink display (3-color): ~€50
- Li-Po battery 3.7V 1200mAh: ~€5-8
- **Total: €60-70**

### Cloud Services (Monthly)
- AWS IoT Core + Lambda + CloudWatch: **~€0.50-2/month**
- Google Calendar API: **FREE**
- OpenWeatherMap API: **FREE** (up to 1000 calls/day)

The project is designed to be cost-effective with minimal AWS usage.

## 📝 License

MIT License - see [LICENSE](LICENSE) file for details.
