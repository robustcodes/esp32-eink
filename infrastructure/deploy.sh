#!/bin/bash
set -e

# ESP32 E-Ink Calendar Infrastructure Deployment Script
# This script deploys the AWS CDK infrastructure for the calendar project

echo "======================================"
echo "ESP32 E-Ink Calendar AWS Infrastructure"
echo "======================================"
echo ""

# Check if AWS credentials are configured
if ! aws sts get-caller-identity > /dev/null 2>&1; then
    echo "❌ AWS credentials not configured!"
    echo "Please configure AWS CLI with 'aws configure'"
    exit 1
fi

# Get AWS account and region info
ACCOUNT=$(aws sts get-caller-identity --query Account --output text)
REGION=${AWS_REGION:-eu-central-1}

echo "📋 Deployment Configuration:"
echo "  Account: $ACCOUNT"
echo "  Region: $REGION"
echo ""

# Check if .env file exists
if [ -f .env ]; then
    echo "✓ Found .env file (will be loaded automatically by CDK)"
else
    echo "❌ .env file not found!"
    echo "   Create one from .env.example and add your credentials"
    exit 1
fi

echo ""
echo "🔨 Building TypeScript..."
npm run build

echo ""
echo "🔍 Synthesizing CloudFormation template..."
npx cdk synth

echo ""
echo "📦 Deploying infrastructure to AWS..."
echo "   This may take a few minutes..."
echo ""

npx cdk deploy --require-approval never

echo ""
echo "✅ Deployment complete!"
echo ""
echo "📝 Next Steps:"
echo "   1. Create and attach IoT certificate to your device:"
echo "      aws iot create-keys-and-certificate --set-as-active \\"
echo "        --certificate-pem-outfile certificate.pem.crt \\"
echo "        --public-key-outfile public.pem.key \\"
echo "        --private-key-outfile private.pem.key \\"
echo "        --region $REGION"
echo ""
echo "   2. Attach policy to certificate:"
echo "      aws iot attach-policy \\"
echo "        --policy-name eink-calendar-01-policy \\"
echo "        --target <certificate-arn> \\"
echo "        --region $REGION"
echo ""
echo "   3. Upload certificates to ESP32 device"
echo ""
echo "   4. Test the Lambdas:"
echo "      aws lambda invoke --function-name eink-calendar-01-weather-fetcher --region $REGION /tmp/weather-response.json"
echo "      aws lambda invoke --function-name eink-calendar-01-calendar-fetcher --region $REGION /tmp/calendar-response.json"
echo ""
