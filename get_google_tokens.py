#!/usr/bin/env python3
"""
Google Calendar OAuth Token Generator

This script helps you get a refresh token for Google Calendar API.

Setup Instructions:
1. Go to https://console.cloud.google.com/
2. Create a new project (or select existing)
3. Enable Google Calendar API
4. Create OAuth consent screen (External, add your email)
5. Create OAuth credentials (Desktop app type)
6. Copy the Client ID and Client Secret

Usage:
  export GOOGLE_CLIENT_ID="your-client-id.apps.googleusercontent.com"
  export GOOGLE_CLIENT_SECRET="GOCSPX-your-client-secret"
  python3 get_google_tokens.py

The script will:
  - Open your browser for Google sign-in
  - Generate a refresh token
  - Display it for you to copy to .env file
"""
from google_auth_oauthlib.flow import Flow
import sys
import os

SCOPES = ['https://www.googleapis.com/auth/calendar.readonly']

# Get credentials from environment variables
CLIENT_ID = os.environ.get('GOOGLE_CLIENT_ID')
CLIENT_SECRET = os.environ.get('GOOGLE_CLIENT_SECRET')

if not CLIENT_ID or not CLIENT_SECRET:
    print("❌ Error: Missing Google OAuth credentials!")
    print("\nPlease set environment variables:")
    print("  export GOOGLE_CLIENT_ID='your-client-id.apps.googleusercontent.com'")
    print("  export GOOGLE_CLIENT_SECRET='GOCSPX-your-client-secret'")
    print("\nGet credentials from: https://console.cloud.google.com/apis/credentials")
    sys.exit(1)

print("=" * 70)
print("  Google Calendar OAuth Token Generator")
print("=" * 70)

client_config = {
    "installed": {
        "client_id": CLIENT_ID,
        "client_secret": CLIENT_SECRET,
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token",
        "redirect_uris": ["urn:ietf:wg:oauth:2.0:oob"]
    }
}

try:
    flow = Flow.from_client_config(client_config, scopes=SCOPES, redirect_uri='urn:ietf:wg:oauth:2.0:oob')
    auth_url, _ = flow.authorization_url(prompt='consent')

    print("\n" + "=" * 70)
    print("📋 COPY THIS URL AND OPEN IT IN YOUR BROWSER:")
    print("=" * 70)
    print(f"\n{auth_url}\n")
    print("=" * 70)

    print("\n1. Open the URL above in your browser")
    print("2. Sign in with your Google account")
    print("3. Click 'Allow' to grant calendar access")
    print("4. Copy the authorization code from the browser")
    print("\n" + "=" * 70)

    code = input("\nPaste the authorization code here: ").strip()

    print("\n🔐 Exchanging code for tokens...")
    flow.fetch_token(code=code)
    creds = flow.credentials

    print("\n✅ Authentication successful!")
    print("\n" + "=" * 70)
    print("📋 ADD THESE TO YOUR infrastructure/.env FILE:")
    print("=" * 70)
    print(f"\nGOOGLE_CLIENT_ID={CLIENT_ID}")
    print(f"GOOGLE_CLIENT_SECRET={CLIENT_SECRET}")
    print(f"GOOGLE_REFRESH_TOKEN={creds.refresh_token}")
    print("\n" + "=" * 70)
    print("\n🎉 Done! Copy the variables above to your .env file")

except Exception as e:
    print(f"\n❌ Error: {e}")
    sys.exit(1)
