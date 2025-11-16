#!/bin/bash
set -e

echo "Building Google Calendar API Lambda Layer..."

# Clean previous build
rm -rf python/
mkdir -p python

# Install Python dependencies
pip install \
  google-auth \
  google-auth-oauthlib \
  google-api-python-client \
  -t python/ \
  --no-cache-dir

echo "✅ Lambda layer built successfully in lambda/layer/python/"
echo ""
echo "Layer size:"
du -sh python/
echo ""
echo "Next: Deploy with ./deploy.sh"
