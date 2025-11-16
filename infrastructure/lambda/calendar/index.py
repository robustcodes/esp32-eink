"""
Calendar Lambda Function
Fetches Google Calendar events and publishes to IoT Core
"""

import json
import os
import boto3
from datetime import datetime, timedelta
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

# Environment variables
THING_NAME = os.environ['THING_NAME']
CALENDAR_ID = os.environ.get('CALENDAR_ID', 'primary')
IOT_ENDPOINT = os.environ.get('IOT_ENDPOINT', '')

# Google OAuth credentials from environment
GOOGLE_CLIENT_ID = os.environ['GOOGLE_CLIENT_ID']
GOOGLE_CLIENT_SECRET = os.environ['GOOGLE_CLIENT_SECRET']
GOOGLE_REFRESH_TOKEN = os.environ['GOOGLE_REFRESH_TOKEN']

# AWS clients
iot_client = boto3.client('iot-data', endpoint_url=f'https://{IOT_ENDPOINT}' if IOT_ENDPOINT else None)

def get_google_credentials():
    """Create Google OAuth credentials from environment variables"""
    try:
        # Validate credentials are present
        if not GOOGLE_CLIENT_ID or not GOOGLE_CLIENT_SECRET or not GOOGLE_REFRESH_TOKEN:
            raise ValueError("Google OAuth credentials not configured in environment variables")

        creds = Credentials(
            token=None,  # No access token stored, will be auto-refreshed
            refresh_token=GOOGLE_REFRESH_TOKEN,
            token_uri='https://oauth2.googleapis.com/token',
            client_id=GOOGLE_CLIENT_ID,
            client_secret=GOOGLE_CLIENT_SECRET,
            scopes=['https://www.googleapis.com/auth/calendar.readonly']
        )

        return creds

    except Exception as e:
        print(f"Error creating Google credentials: {e}")
        raise

def fetch_calendar_events():
    """Fetch upcoming calendar events from Google Calendar"""
    try:
        creds = get_google_credentials()

        # Build Calendar API service
        service = build('calendar', 'v3', credentials=creds)

        # Get events from now to 90 days in the future (3 months)
        now = datetime.utcnow()
        end_time = now + timedelta(days=90)

        print(f"Fetching events from {CALENDAR_ID}")
        print(f"Time range: {now.isoformat()}Z to {end_time.isoformat()}Z")

        events_result = service.events().list(
            calendarId=CALENDAR_ID,
            timeMin=now.isoformat() + 'Z',
            timeMax=end_time.isoformat() + 'Z',
            maxResults=50,  # Fetch many events, will trim to fit buffer
            singleEvents=True,
            orderBy='startTime'
        ).execute()

        events = events_result.get('items', [])
        print(f"Google Calendar API returned {len(events)} events")

        # Log each event for debugging
        for i, event in enumerate(events):
            print(f"Event {i+1}: {event.get('summary', 'No Title')} at {event['start']}")

        # Note: Access token is auto-refreshed by Google API library in memory
        # No need to persist it - will refresh on next cold start

        # Format events for ESP32
        event_list = []
        for event in events:
            start = event['start'].get('dateTime', event['start'].get('date'))
            end = event['end'].get('dateTime', event['end'].get('date'))

            # Parse datetime - Finnish translations
            day_names = ['Maanantai', 'Tiistai', 'Keskiviikko', 'Torstai', 'Perjantai', 'Lauantai', 'Sunnuntai']
            day_names_short = ['Ma', 'Ti', 'Ke', 'To', 'Pe', 'La', 'Su']

            if 'T' in start:  # DateTime event
                start_dt = datetime.fromisoformat(start.replace('Z', '+00:00'))
                end_dt = datetime.fromisoformat(end.replace('Z', '+00:00'))

                # Check if multi-day (different dates)
                if start_dt.date() != end_dt.date():
                    time_str = f"{day_names_short[start_dt.weekday()]} {start_dt.strftime('%d.%m')} - {day_names_short[end_dt.weekday()]} {end_dt.strftime('%d.%m')}"
                    is_multiday = True
                else:
                    time_str = f"{day_names_short[start_dt.weekday()]} {start_dt.strftime('%d.%m %H:%M')}"
                    is_multiday = False
            else:  # All-day event
                start_dt = datetime.fromisoformat(start)
                end_dt = datetime.fromisoformat(end)

                # Google Calendar end dates are exclusive (next day), so subtract 1 day
                end_dt = end_dt - timedelta(days=1)

                # Check if multi-day
                if start_dt.date() != end_dt.date():
                    time_str = f"{day_names_short[start_dt.weekday()]} {start_dt.strftime('%d.%m')} - {day_names_short[end_dt.weekday()]} {end_dt.strftime('%d.%m')}"
                    is_multiday = True
                else:
                    time_str = f"{day_names_short[start_dt.weekday()]} {start_dt.strftime('%d.%m')} Koko päivä"
                    is_multiday = False

            formatted_event = {
                'title': event.get('summary', 'No Title'),
                'time': time_str,
                'date': start,
                'location': event.get('location', ''),
                'description': event.get('description', '')[:100],  # Truncate description
                'all_day': 'T' not in start,
                'multiday': is_multiday,
            }

            event_list.append(formatted_event)

            # Log full event details
            print(f"  Formatted event: {formatted_event}")

        return event_list

    except HttpError as error:
        print(f"Google Calendar API error: {error}")
        raise
    except Exception as e:
        print(f"Error fetching calendar events: {e}")
        raise

def publish_to_iot(topic, payload):
    """Publish data to IoT Core"""
    try:
        response = iot_client.publish(
            topic=topic,
            qos=1,
            payload=json.dumps(payload)
        )
        print(f"Published to {topic}")
        return response
    except Exception as e:
        print(f"Error publishing to IoT: {e}")
        raise

def update_shadow(payload):
    """Update device shadow with calendar data"""
    try:
        shadow_update = {
            "state": {
                "desired": {
                    "calendar": payload,
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
    print(f"Calendar Lambda triggered for thing: {THING_NAME}")

    try:
        # Fetch calendar events
        events = fetch_calendar_events()

        # ESP32 MQTT buffer limit (leave margin for MQTT overhead)
        MAX_PAYLOAD_SIZE = 1900  # Buffer is 2048, use 1900 to be safe

        # Build payload and check size, trim if needed
        payload = {
            'events': events,
            'count': len(events),
            'timestamp': datetime.utcnow().isoformat(),
            'calendar_id': CALENDAR_ID,
        }

        # Check payload size and trim if necessary
        payload_json = json.dumps(payload)
        payload_size = len(payload_json)

        if payload_size > MAX_PAYLOAD_SIZE:
            print(f"⚠️  Payload too large ({payload_size} bytes), trimming to fit buffer...")

            # Strategy: Add events one by one, shortening descriptions if needed
            trimmed_events = []

            for i, evt in enumerate(events):
                # Try with full description first
                test_evt = evt.copy()
                test_payload = {
                    'events': trimmed_events + [test_evt],
                    'count': len(trimmed_events) + 1,
                    'timestamp': datetime.utcnow().isoformat(),
                    'calendar_id': CALENDAR_ID,
                }
                test_size = len(json.dumps(test_payload))

                if test_size <= MAX_PAYLOAD_SIZE:
                    # Fits with full description
                    trimmed_events.append(test_evt)
                    print(f"  ✓ Event {i+1}: '{evt['title']}' (full)")
                elif test_evt['description']:
                    # Try with shortened description (50% reduction)
                    test_evt['description'] = test_evt['description'][:50]
                    test_payload['events'][-1] = test_evt
                    test_size = len(json.dumps(test_payload))

                    if test_size <= MAX_PAYLOAD_SIZE:
                        trimmed_events.append(test_evt)
                        print(f"  ✓ Event {i+1}: '{evt['title']}' (shortened desc)")
                    else:
                        # Try with no description
                        test_evt['description'] = ''
                        test_payload['events'][-1] = test_evt
                        test_size = len(json.dumps(test_payload))

                        if test_size <= MAX_PAYLOAD_SIZE:
                            trimmed_events.append(test_evt)
                            print(f"  ✓ Event {i+1}: '{evt['title']}' (no desc)")
                        else:
                            print(f"  ✗ Event {i+1}: '{evt['title']}' (would exceed buffer)")
                            break
                else:
                    # No description to shorten, can't fit
                    print(f"  ✗ Event {i+1}: '{evt['title']}' (would exceed buffer)")
                    break

            payload = {
                'events': trimmed_events,
                'count': len(trimmed_events),
                'timestamp': datetime.utcnow().isoformat(),
                'calendar_id': CALENDAR_ID,
            }
            payload_json = json.dumps(payload)
            print(f"✅ Optimized to fit {len(trimmed_events)} events ({len(payload_json)} bytes)")

        # Publish to IoT topic (for immediate delivery if device is connected)
        topic = f"calendar/{THING_NAME}/events"
        print(f"Publishing {len(payload['events'])} events to {topic}")

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
                'message': 'Calendar events published successfully',
                'topic': topic,
                'event_count': len(payload['events']),
                'payload_size': len(payload_json),
            })
        }

    except Exception as e:
        print(f"Error in calendar lambda: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({
                'error': str(e)
            })
        }
