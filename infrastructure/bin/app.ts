#!/usr/bin/env node
import 'source-map-support/register';
import * as cdk from 'aws-cdk-lib';
import { CalendarIotStack } from '../lib/calendar-iot-stack';
import * as dotenv from 'dotenv';

// Load environment variables from .env file
dotenv.config();

const app = new cdk.App();

new CalendarIotStack(app, 'CalendarIotStack', {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION,
  },
  description: 'ESP32 E-Ink Calendar Infrastructure with IoT Core, Weather and Calendar Lambdas',
});

app.synth();
