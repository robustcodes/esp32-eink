import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import * as lambda from 'aws-cdk-lib/aws-lambda';
import * as iot from 'aws-cdk-lib/aws-iot';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as events from 'aws-cdk-lib/aws-events';
import * as targets from 'aws-cdk-lib/aws-events-targets';
import * as logs from 'aws-cdk-lib/aws-logs';
import * as cloudwatch from 'aws-cdk-lib/aws-cloudwatch';
import * as cw_actions from 'aws-cdk-lib/aws-cloudwatch-actions';
import * as sqs from 'aws-cdk-lib/aws-sqs';
import * as sns from 'aws-cdk-lib/aws-sns';
import * as subscriptions from 'aws-cdk-lib/aws-sns-subscriptions';
import * as path from 'path';

export class CalendarIotStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    const thingName = process.env.THING_NAME || 'eink-calendar-01';
    const region = props?.env?.region || process.env.AWS_REGION || 'eu-central-1';
    const accountId = props?.env?.account || this.account;
    const iotEndpoint = process.env.IOT_ENDPOINT || '';

    // ==================== IoT Policy ====================

    const iotPolicy = new iot.CfnPolicy(this, 'CalendarIotPolicy', {
      policyName: `${thingName}-policy`,
      policyDocument: {
        Version: '2012-10-17',
        Statement: [
          {
            Effect: 'Allow',
            Action: 'iot:Connect',
            Resource: `arn:aws:iot:${region}:${accountId}:client/${thingName}*`,  // Allow wildcard for unique client IDs
          },
          {
            Effect: 'Allow',
            Action: 'iot:Subscribe',
            Resource: `arn:aws:iot:${region}:${accountId}:topicfilter/calendar/${thingName}/*`,
          },
          {
            Effect: 'Allow',
            Action: 'iot:Receive',
            Resource: `arn:aws:iot:${region}:${accountId}:topic/calendar/${thingName}/*`,
          },
          {
            Effect: 'Allow',
            Action: 'iot:Publish',
            Resource: [
              `arn:aws:iot:${region}:${accountId}:topic/calendar/${thingName}/status`,
              `arn:aws:iot:${region}:${accountId}:topic/$aws/things/${thingName}/shadow/*`,
            ],
          },
        ],
      },
    });

    // ==================== SNS Topic for Alarms ====================

    const alertTopic = new sns.Topic(this, 'AlertTopic', {
      topicName: `${thingName}-alerts`,
      displayName: 'E-Ink Calendar Alerts',
    });

    // Optional: Add email subscription (uncomment and set email)
    // alertTopic.addSubscription(new subscriptions.EmailSubscription('your-email@example.com'));

    // ==================== Dead Letter Queues ====================

    const weatherDLQ = new sqs.Queue(this, 'WeatherLambdaDLQ', {
      queueName: `${thingName}-weather-dlq`,
      retentionPeriod: cdk.Duration.days(14),
    });

    const calendarDLQ = new sqs.Queue(this, 'CalendarLambdaDLQ', {
      queueName: `${thingName}-calendar-dlq`,
      retentionPeriod: cdk.Duration.days(14),
    });

    // ==================== Lambda Layer for Google API ====================

    const googleApiLayer = new lambda.LayerVersion(this, 'GoogleApiLayer', {
      code: lambda.Code.fromAsset(path.join(__dirname, '../lambda/layer')),
      compatibleRuntimes: [lambda.Runtime.PYTHON_3_12],
      description: 'Google Calendar API Python libraries',
      layerVersionName: 'google-calendar-api',
    });

    // ==================== Weather Lambda ====================

    const weatherLambda = new lambda.Function(this, 'WeatherLambda', {
      functionName: `${thingName}-weather-fetcher`,
      runtime: lambda.Runtime.PYTHON_3_12,
      handler: 'index.handler',
      code: lambda.Code.fromAsset(path.join(__dirname, '../lambda/weather')),
      timeout: cdk.Duration.seconds(30),
      memorySize: 256,
      environment: {
        THING_NAME: thingName,
        WEATHER_API_KEY: process.env.WEATHER_API_KEY || '',
        WEATHER_CITY: process.env.WEATHER_CITY || '',
        IOT_ENDPOINT: iotEndpoint,
      },
      logRetention: logs.RetentionDays.ONE_WEEK,
      deadLetterQueue: weatherDLQ,
      deadLetterQueueEnabled: true,
    });

    // Grant Weather Lambda permission to publish to IoT and update Shadow
    weatherLambda.addToRolePolicy(new iam.PolicyStatement({
      effect: iam.Effect.ALLOW,
      actions: ['iot:Publish', 'iot:UpdateThingShadow', 'iot:GetThingShadow'],
      resources: [
        `arn:aws:iot:${region}:${accountId}:topic/calendar/${thingName}/weather`,
        `arn:aws:iot:${region}:${accountId}:thing/${thingName}`,
      ],
    }));

    // ==================== Calendar Lambda ====================

    const calendarLambda = new lambda.Function(this, 'CalendarLambda', {
      functionName: `${thingName}-calendar-fetcher`,
      runtime: lambda.Runtime.PYTHON_3_12,
      handler: 'index.handler',
      code: lambda.Code.fromAsset(path.join(__dirname, '../lambda/calendar')),
      timeout: cdk.Duration.seconds(30),
      memorySize: 512,
      layers: [googleApiLayer],
      environment: {
        THING_NAME: thingName,
        CALENDAR_ID: process.env.CALENDAR_ID || 'primary',
        IOT_ENDPOINT: iotEndpoint,
        // Google OAuth credentials from environment
        GOOGLE_CLIENT_ID: process.env.GOOGLE_CLIENT_ID || '',
        GOOGLE_CLIENT_SECRET: process.env.GOOGLE_CLIENT_SECRET || '',
        GOOGLE_REFRESH_TOKEN: process.env.GOOGLE_REFRESH_TOKEN || '',
      },
      logRetention: logs.RetentionDays.ONE_WEEK,
      deadLetterQueue: calendarDLQ,
      deadLetterQueueEnabled: true,
    });

    // Grant Calendar Lambda permission to publish to IoT and update Shadow
    calendarLambda.addToRolePolicy(new iam.PolicyStatement({
      effect: iam.Effect.ALLOW,
      actions: ['iot:Publish', 'iot:UpdateThingShadow', 'iot:GetThingShadow'],
      resources: [
        `arn:aws:iot:${region}:${accountId}:topic/calendar/${thingName}/events`,
        `arn:aws:iot:${region}:${accountId}:thing/${thingName}`,
      ],
    }));

    // ==================== EventBridge Schedules ====================

    // Weather schedule - every 3 hours
    const weatherSchedule = new events.Rule(this, 'WeatherSchedule', {
      ruleName: `${thingName}-weather-schedule`,
      description: 'Fetch weather data every 3 hours',
      schedule: events.Schedule.rate(cdk.Duration.hours(3)),
    });
    weatherSchedule.addTarget(new targets.LambdaFunction(weatherLambda));

    // Calendar schedule - every 3 hours
    const calendarSchedule = new events.Rule(this, 'CalendarSchedule', {
      ruleName: `${thingName}-calendar-schedule`,
      description: 'Fetch calendar events every 3 hours',
      schedule: events.Schedule.rate(cdk.Duration.hours(3)),
    });
    calendarSchedule.addTarget(new targets.LambdaFunction(calendarLambda));

    // ==================== IoT Rules (Trigger on Device Ready) ====================

    // Grant IoT Rules permission to invoke Lambdas
    weatherLambda.grantInvoke(new iam.ServicePrincipal('iot.amazonaws.com'));
    calendarLambda.grantInvoke(new iam.ServicePrincipal('iot.amazonaws.com'));

    // IoT Rule: Trigger Weather Lambda when ESP32 publishes "ready" status
    const weatherIotRule = new iot.CfnTopicRule(this, 'WeatherIotRule', {
      ruleName: `${thingName.replace(/-/g, '_')}_weather_trigger`,
      topicRulePayload: {
        sql: `SELECT * FROM 'calendar/${thingName}/status' WHERE status = 'ready'`,
        description: 'Trigger weather Lambda when device publishes ready status',
        actions: [
          {
            lambda: {
              functionArn: weatherLambda.functionArn,
            },
          },
        ],
        ruleDisabled: false,
      },
    });

    // IoT Rule: Trigger Calendar Lambda when ESP32 publishes "ready" status
    const calendarIotRule = new iot.CfnTopicRule(this, 'CalendarIotRule', {
      ruleName: `${thingName.replace(/-/g, '_')}_calendar_trigger`,
      topicRulePayload: {
        sql: `SELECT * FROM 'calendar/${thingName}/status' WHERE status = 'ready'`,
        description: 'Trigger calendar Lambda when device publishes ready status',
        actions: [
          {
            lambda: {
              functionArn: calendarLambda.functionArn,
            },
          },
        ],
        ruleDisabled: false,
      },
    });

    // ==================== CloudWatch Alarms ====================

    // Weather Lambda Error Alarm
    const weatherLambdaErrorAlarm = new cloudwatch.Alarm(this, 'WeatherLambdaErrorAlarm', {
      alarmName: `${thingName}-weather-lambda-errors`,
      alarmDescription: 'Alert when weather Lambda has errors',
      metric: weatherLambda.metricErrors({
        period: cdk.Duration.minutes(5),
      }),
      threshold: 2,
      evaluationPeriods: 1,
      comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_OR_EQUAL_TO_THRESHOLD,
      treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
    });
    weatherLambdaErrorAlarm.addAlarmAction(new cw_actions.SnsAction(alertTopic));

    // Calendar Lambda Error Alarm
    const calendarLambdaErrorAlarm = new cloudwatch.Alarm(this, 'CalendarLambdaErrorAlarm', {
      alarmName: `${thingName}-calendar-lambda-errors`,
      alarmDescription: 'Alert when calendar Lambda has errors',
      metric: calendarLambda.metricErrors({
        period: cdk.Duration.minutes(5),
      }),
      threshold: 2,
      evaluationPeriods: 1,
      comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_OR_EQUAL_TO_THRESHOLD,
      treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
    });
    calendarLambdaErrorAlarm.addAlarmAction(new cw_actions.SnsAction(alertTopic));

    // DLQ Message Count Alarm for Weather
    const weatherDLQAlarm = new cloudwatch.Alarm(this, 'WeatherDLQAlarm', {
      alarmName: `${thingName}-weather-dlq-messages`,
      alarmDescription: 'Alert when messages appear in Weather DLQ',
      metric: weatherDLQ.metricApproximateNumberOfMessagesVisible({
        period: cdk.Duration.minutes(5),
      }),
      threshold: 1,
      evaluationPeriods: 1,
      comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_OR_EQUAL_TO_THRESHOLD,
      treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
    });
    weatherDLQAlarm.addAlarmAction(new cw_actions.SnsAction(alertTopic));

    // DLQ Message Count Alarm for Calendar
    const calendarDLQAlarm = new cloudwatch.Alarm(this, 'CalendarDLQAlarm', {
      alarmName: `${thingName}-calendar-dlq-messages`,
      alarmDescription: 'Alert when messages appear in Calendar DLQ',
      metric: calendarDLQ.metricApproximateNumberOfMessagesVisible({
        period: cdk.Duration.minutes(5),
      }),
      threshold: 1,
      evaluationPeriods: 1,
      comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_OR_EQUAL_TO_THRESHOLD,
      treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
    });
    calendarDLQAlarm.addAlarmAction(new cw_actions.SnsAction(alertTopic));

    // ==================== Outputs ====================

    new cdk.CfnOutput(this, 'ThingName', {
      description: 'IoT Thing Name',
      value: thingName,
    });

    new cdk.CfnOutput(this, 'ThingArn', {
      description: 'IoT Thing ARN',
      value: `arn:aws:iot:${region}:${accountId}:thing/${thingName}`,
    });

    new cdk.CfnOutput(this, 'IotEndpoint', {
      description: 'IoT Core Endpoint (update with actual endpoint after deployment)',
      value: `${accountId}.iot.${region}.amazonaws.com`,
    });

    new cdk.CfnOutput(this, 'WeatherLambdaArn', {
      description: 'Weather Lambda Function ARN',
      value: weatherLambda.functionArn,
    });

    new cdk.CfnOutput(this, 'CalendarLambdaArn', {
      description: 'Calendar Lambda Function ARN',
      value: calendarLambda.functionArn,
    });

    new cdk.CfnOutput(this, 'GoogleOAuthSetup', {
      description: 'Google OAuth credentials configured via environment variables',
      value: 'Set GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET, and GOOGLE_REFRESH_TOKEN before deploying',
    });

    new cdk.CfnOutput(this, 'WeatherTopicName', {
      description: 'MQTT topic for weather data',
      value: `calendar/${thingName}/weather`,
    });

    new cdk.CfnOutput(this, 'CalendarTopicName', {
      description: 'MQTT topic for calendar events',
      value: `calendar/${thingName}/events`,
    });

    new cdk.CfnOutput(this, 'CertificateInstructions', {
      description: 'Instructions to create IoT certificate',
      value: 'Create certificate manually: aws iot create-keys-and-certificate --set-as-active',
    });
  }
}
