"""
Weather Lambda Function
Fetches weather data and forecast from OpenWeatherMap and publishes to IoT Core
"""

import json
import os
import boto3
import urllib.request
import urllib.error
from datetime import datetime, timezone, timedelta

# Environment variables
THING_NAME = os.environ['THING_NAME']
WEATHER_API_KEY = os.environ['WEATHER_API_KEY']
WEATHER_CITY = os.environ['WEATHER_CITY']
IOT_ENDPOINT = os.environ.get('IOT_ENDPOINT', '')

# AWS clients
iot_client = boto3.client('iot-data', endpoint_url=f'https://{IOT_ENDPOINT}' if IOT_ENDPOINT else None)

def fetch_weather():
    """Fetch current weather data from OpenWeatherMap API"""
    base_url = "http://api.openweathermap.org/data/2.5/weather"
    params = f"?q={WEATHER_CITY}&appid={WEATHER_API_KEY}&units=metric"
    url = base_url + params

    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())

        weather_data = {
            'temp': int(data['main']['temp']),
            'feels_like': int(data['main']['feels_like']),
            'humidity': data['main']['humidity'],
            'wind': int(data['wind']['speed']),
            'description': data['weather'][0]['description'],
            'icon': data['weather'][0]['icon'],
            'condition': data['weather'][0]['main'],
            'pressure': data['main']['pressure'],
            'visibility': data.get('visibility', 0) // 1000,  # Convert to km
        }

        return weather_data

    except urllib.error.URLError as e:
        print(f"Error fetching weather: {e}")
        raise
    except Exception as e:
        print(f"Error parsing weather data: {e}")
        raise

def fetch_forecast():
    """Fetch forecast for next 24 hours from OpenWeatherMap (3-hour intervals)"""
    base_url = "http://api.openweathermap.org/data/2.5/forecast"
    params = f"?q={WEATHER_CITY}&appid={WEATHER_API_KEY}&units=metric"
    url = base_url + params

    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())

        # Get current time
        now = datetime.now(tz=timezone.utc)

        # Extract forecasts for next 24 hours at 6-hour intervals
        forecast_data = []

        # We want 4 data points over 24 hours: now, +6h, +12h, +18h
        target_hours_offset = [0, 6, 12, 18]

        for offset in target_hours_offset:
            target_time = now + timedelta(hours=offset)

            # Find the closest forecast entry to this target time
            closest_item = None
            min_diff = float('inf')

            for item in data['list']:
                item_time = datetime.fromtimestamp(item['dt'], tz=timezone.utc)
                time_diff = abs((item_time - target_time).total_seconds())

                if time_diff < min_diff:
                    min_diff = time_diff
                    closest_item = item

            if closest_item:
                item_time = datetime.fromtimestamp(closest_item['dt'], tz=timezone.utc)
                forecast_data.append({
                    'time': item_time.strftime('%H:%M'),
                    'hour': item_time.hour,
                    'temp': int(closest_item['main']['temp']),
                    'icon': closest_item['weather'][0]['icon'],
                    'description': closest_item['weather'][0]['description'],
                })

        print(f"Found {len(forecast_data)} forecast intervals for next 24h")
        for fc in forecast_data:
            print(f"  {fc['time']}: {fc['temp']}°C, {fc['description']}")

        return forecast_data

    except urllib.error.URLError as e:
        print(f"Error fetching forecast: {e}")
        return []
    except Exception as e:
        print(f"Error parsing forecast data: {e}")
        return []

def publish_to_iot(topic, payload):
    """Publish data to IoT Core"""
    try:
        response = iot_client.publish(
            topic=topic,
            qos=1,
            payload=json.dumps(payload)
        )
        print(f"Published to {topic}: {json.dumps(payload)}")
        return response
    except Exception as e:
        print(f"Error publishing to IoT: {e}")
        raise

def update_shadow(payload):
    """Update device shadow with weather data"""
    try:
        shadow_update = {
            "state": {
                "desired": {
                    "weather": payload,
                    "lastUpdated": datetime.utcnow().isoformat()
                }
            }
        }

        response = iot_client.update_thing_shadow(
            thingName=THING_NAME,
            payload=json.dumps(shadow_update)
        )
        print(f"Updated shadow for {THING_NAME}")
        return response
    except Exception as e:
        print(f"Error updating shadow: {e}")
        raise

def handler(event, context):
    """Lambda handler function"""
    print(f"Weather Lambda triggered for thing: {THING_NAME}")

    try:
        # Fetch current weather and forecast
        weather = fetch_weather()
        forecast = fetch_forecast()

        # Get current date/time
        now = datetime.utcnow()

        # Finnish date formatting
        day_names_fi = ['Maanantai', 'Tiistai', 'Keskiviikko', 'Torstai', 'Perjantai', 'Lauantai', 'Sunnuntai']
        month_names_fi = ['', 'Tammikuu', 'Helmikuu', 'Maaliskuu', 'Huhtikuu', 'Toukokuu',
                          'Kesäkuu', 'Heinäkuu', 'Elokuu', 'Syyskuu', 'Lokakuu', 'Marraskuu', 'Joulukuu']

        day_name = day_names_fi[now.weekday()]
        month_name = month_names_fi[now.month]
        date_str = f"{day_name} {now.day} {month_name}"

        # Add metadata
        payload = {
            'current': weather,
            'forecast': forecast,
            'date': date_str,  # e.g., "Perjantai 14 Marraskuu"
            'timestamp': now.isoformat(),
            'source': 'openweathermap',
            'city': WEATHER_CITY,
        }

        # Publish to IoT topic (for immediate delivery if device is connected)
        topic = f"calendar/{THING_NAME}/weather"

        # Log full payload
        payload_json = json.dumps(payload, indent=2)
        print(f"Full payload to ESP32:\n{payload_json}")
        print(f"Payload size: {len(payload_json)} bytes")

        # Publish to MQTT topic
        publish_to_iot(topic, payload)

        # Also update Device Shadow (persists data for sleeping device)
        update_shadow(payload)

        return {
            'statusCode': 200,
            'body': json.dumps({
                'message': 'Weather data published successfully',
                'topic': topic,
                'temperature': weather['temp'],
                'forecast_count': len(forecast),
            })
        }

    except Exception as e:
        print(f"Error in weather lambda: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({
                'error': str(e)
            })
        }
