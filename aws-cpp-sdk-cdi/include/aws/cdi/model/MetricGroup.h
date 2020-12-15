/*
* Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#pragma once
#include <aws/cdi/CDIMonitoring_EXPORTS.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/cdi/model/StatisticSet.h>
#include <aws/core/utils/DateTime.h>
#include <utility>

namespace Aws
{
namespace Utils
{
namespace Json
{
  class JsonValue;
  class JsonView;
} // namespace Json
} // namespace Utils
namespace CDIMonitoring
{
namespace Model
{

  /**
   * The group of metrics for one connection.<p><h3>See Also:</h3>   <a
   * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/MetricGroup">AWS API
   * Reference</a></p>
   */
  class AWS_CDIMONITORING_API MetricGroup
  {
  public:
    MetricGroup();
    MetricGroup(Aws::Utils::Json::JsonView jsonValue);
    MetricGroup& operator=(Aws::Utils::Json::JsonView jsonValue);
    Aws::Utils::Json::JsonValue Jsonize() const;


    /**
     * The status of the connection: true indicates that it is connected, and false
     * indicates that it is disconnected.
     */
    inline bool GetConnected() const{ return m_connected; }

    /**
     * The status of the connection: true indicates that it is connected, and false
     * indicates that it is disconnected.
     */
    inline bool ConnectedHasBeenSet() const { return m_connectedHasBeenSet; }

    /**
     * The status of the connection: true indicates that it is connected, and false
     * indicates that it is disconnected.
     */
    inline void SetConnected(bool value) { m_connectedHasBeenSet = true; m_connected = value; }

    /**
     * The status of the connection: true indicates that it is connected, and false
     * indicates that it is disconnected.
     */
    inline MetricGroup& WithConnected(bool value) { SetConnected(value); return *this;}


    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline const Aws::String& GetConnectionName() const{ return m_connectionName; }

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline bool ConnectionNameHasBeenSet() const { return m_connectionNameHasBeenSet; }

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline void SetConnectionName(const Aws::String& value) { m_connectionNameHasBeenSet = true; m_connectionName = value; }

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline void SetConnectionName(Aws::String&& value) { m_connectionNameHasBeenSet = true; m_connectionName = std::move(value); }

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline void SetConnectionName(const char* value) { m_connectionNameHasBeenSet = true; m_connectionName.assign(value); }

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline MetricGroup& WithConnectionName(const Aws::String& value) { SetConnectionName(value); return *this;}

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline MetricGroup& WithConnectionName(Aws::String&& value) { SetConnectionName(std::move(value)); return *this;}

    /**
     * An identifier for this connection, which should be unique within this instance
     * (domain) of the AWS CDI SDK.
     */
    inline MetricGroup& WithConnectionName(const char* value) { SetConnectionName(value); return *this;}


    /**
     * The percentage of CPU that is being used by the adapter endpoint polling thread.
     */
    inline int GetCpuUtilization() const{ return m_cpuUtilization; }

    /**
     * The percentage of CPU that is being used by the adapter endpoint polling thread.
     */
    inline bool CpuUtilizationHasBeenSet() const { return m_cpuUtilizationHasBeenSet; }

    /**
     * The percentage of CPU that is being used by the adapter endpoint polling thread.
     */
    inline void SetCpuUtilization(int value) { m_cpuUtilizationHasBeenSet = true; m_cpuUtilization = value; }

    /**
     * The percentage of CPU that is being used by the adapter endpoint polling thread.
     */
    inline MetricGroup& WithCpuUtilization(int value) { SetCpuUtilization(value); return *this;}


    /**
     * The number of times that the status changed from connected to disconnected.
     */
    inline long long GetDisconnections() const{ return m_disconnections; }

    /**
     * The number of times that the status changed from connected to disconnected.
     */
    inline bool DisconnectionsHasBeenSet() const { return m_disconnectionsHasBeenSet; }

    /**
     * The number of times that the status changed from connected to disconnected.
     */
    inline void SetDisconnections(long long value) { m_disconnectionsHasBeenSet = true; m_disconnections = value; }

    /**
     * The number of times that the status changed from connected to disconnected.
     */
    inline MetricGroup& WithDisconnections(long long value) { SetDisconnections(value); return *this;}


    /**
     * The number of payloads that were lost during transit and were not recovered by
     * error correction.
     */
    inline long long GetDroppedPayloads() const{ return m_droppedPayloads; }

    /**
     * The number of payloads that were lost during transit and were not recovered by
     * error correction.
     */
    inline bool DroppedPayloadsHasBeenSet() const { return m_droppedPayloadsHasBeenSet; }

    /**
     * The number of payloads that were lost during transit and were not recovered by
     * error correction.
     */
    inline void SetDroppedPayloads(long long value) { m_droppedPayloadsHasBeenSet = true; m_droppedPayloads = value; }

    /**
     * The number of payloads that were lost during transit and were not recovered by
     * error correction.
     */
    inline MetricGroup& WithDroppedPayloads(long long value) { SetDroppedPayloads(value); return *this;}


    /**
     * The number of payloads that arrived late.
     */
    inline long long GetLatePayloads() const{ return m_latePayloads; }

    /**
     * The number of payloads that arrived late.
     */
    inline bool LatePayloadsHasBeenSet() const { return m_latePayloadsHasBeenSet; }

    /**
     * The number of payloads that arrived late.
     */
    inline void SetLatePayloads(long long value) { m_latePayloadsHasBeenSet = true; m_latePayloads = value; }

    /**
     * The number of payloads that arrived late.
     */
    inline MetricGroup& WithLatePayloads(long long value) { SetLatePayloads(value); return *this;}


    /**
     * The number of bytes transferred.
     */
    inline void SetBytesTransferred(long long value) { m_bytesTransferredHasBeenSet = true; m_bytesTransferred = value; }

    /**
     * The number of payloads that arrived late.
     */
    inline long long GetBytesTransferred() const{ return m_bytesTransferred; }

    /**
     * The number of payloads that arrived late.
     */
    inline bool BytesTransferredHasBeenSet() const { return m_bytesTransferredHasBeenSet; }

    /**
     * The number of payloads that arrived late.
     */
    inline MetricGroup& WithBytesTransferred(long long value) { SetBytesTransferred(value); return *this;}



    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline const StatisticSet& GetPayloadTime() const{ return m_payloadTime; }

    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline bool PayloadTimeHasBeenSet() const { return m_payloadTimeHasBeenSet; }

    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline void SetPayloadTime(const StatisticSet& value) { m_payloadTimeHasBeenSet = true; m_payloadTime = value; }

    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline void SetPayloadTime(StatisticSet&& value) { m_payloadTimeHasBeenSet = true; m_payloadTime = std::move(value); }

    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline MetricGroup& WithPayloadTime(const StatisticSet& value) { SetPayloadTime(value); return *this;}

    /**
     * A sample set of the time taken for each payload to be transferred in
     * microseconds.
     */
    inline MetricGroup& WithPayloadTime(StatisticSet&& value) { SetPayloadTime(std::move(value)); return *this;}


    /**
     * The P50 of the payload transfer times in microseconds.
     */
    inline long long GetPayloadTimeP50() const{ return m_payloadTimeP50; }

    /**
     * The P50 of the payload transfer times in microseconds.
     */
    inline bool PayloadTimeP50HasBeenSet() const { return m_payloadTimeP50HasBeenSet; }

    /**
     * The P50 of the payload transfer times in microseconds.
     */
    inline void SetPayloadTimeP50(long long value) { m_payloadTimeP50HasBeenSet = true; m_payloadTimeP50 = value; }

    /**
     * The P50 of the payload transfer times in microseconds.
     */
    inline MetricGroup& WithPayloadTimeP50(long long value) { SetPayloadTimeP50(value); return *this;}


    /**
     * The P90 of the payload transfer times in microseconds.
     */
    inline long long GetPayloadTimeP90() const{ return m_payloadTimeP90; }

    /**
     * The P90 of the payload transfer times in microseconds.
     */
    inline bool PayloadTimeP90HasBeenSet() const { return m_payloadTimeP90HasBeenSet; }

    /**
     * The P90 of the payload transfer times in microseconds.
     */
    inline void SetPayloadTimeP90(long long value) { m_payloadTimeP90HasBeenSet = true; m_payloadTimeP90 = value; }

    /**
     * The P90 of the payload transfer times in microseconds.
     */
    inline MetricGroup& WithPayloadTimeP90(long long value) { SetPayloadTimeP90(value); return *this;}


    /**
     * The P99 of the payload transfer times in microseconds.
     */
    inline long long GetPayloadTimeP99() const{ return m_payloadTimeP99; }

    /**
     * The P99 of the payload transfer times in microseconds.
     */
    inline bool PayloadTimeP99HasBeenSet() const { return m_payloadTimeP99HasBeenSet; }

    /**
     * The P99 of the payload transfer times in microseconds.
     */
    inline void SetPayloadTimeP99(long long value) { m_payloadTimeP99HasBeenSet = true; m_payloadTimeP99 = value; }

    /**
     * The P99 of the payload transfer times in microseconds.
     */
    inline MetricGroup& WithPayloadTimeP99(long long value) { SetPayloadTimeP99(value); return *this;}


    /**
     * The number of probe command retries due to dropped or lost control packets.
     */
    inline long long GetProbeRetries() const{ return m_probeRetries; }

    /**
     * The number of probe command retries due to dropped or lost control packets.
     */
    inline bool ProbeRetriesHasBeenSet() const { return m_probeRetriesHasBeenSet; }

    /**
     * The number of probe command retries due to dropped or lost control packets.
     */
    inline void SetProbeRetries(long long value) { m_probeRetriesHasBeenSet = true; m_probeRetries = value; }

    /**
     * The number of probe command retries due to dropped or lost control packets.
     */
    inline MetricGroup& WithProbeRetries(long long value) { SetProbeRetries(value); return *this;}


    /**
     * Whether the connection is being used for transmitting or receiving. This should
     * be true for receivers, and false for transmitters.
     */
    inline bool GetReceiver() const{ return m_receiver; }

    /**
     * Whether the connection is being used for transmitting or receiving. This should
     * be true for receivers, and false for transmitters.
     */
    inline bool ReceiverHasBeenSet() const { return m_receiverHasBeenSet; }

    /**
     * Whether the connection is being used for transmitting or receiving. This should
     * be true for receivers, and false for transmitters.
     */
    inline void SetReceiver(bool value) { m_receiverHasBeenSet = true; m_receiver = value; }

    /**
     * Whether the connection is being used for transmitting or receiving. This should
     * be true for receivers, and false for transmitters.
     */
    inline MetricGroup& WithReceiver(bool value) { SetReceiver(value); return *this;}


    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline const Aws::Utils::DateTime& GetTimestamp() const{ return m_timestamp; }

    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline bool TimestampHasBeenSet() const { return m_timestampHasBeenSet; }

    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline void SetTimestamp(const Aws::Utils::DateTime& value) { m_timestampHasBeenSet = true; m_timestamp = value; }

    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline void SetTimestamp(Aws::Utils::DateTime&& value) { m_timestampHasBeenSet = true; m_timestamp = std::move(value); }

    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline MetricGroup& WithTimestamp(const Aws::Utils::DateTime& value) { SetTimestamp(value); return *this;}

    /**
     * The time the metric group was sampled, as defined by date-time RFC3339 (subset
     * of ISO 8601). This will accept fractional second digits to nanosecond
     * resolution, although CloudWatch Metrics will use only down to the millisecond.
     */
    inline MetricGroup& WithTimestamp(Aws::Utils::DateTime&& value) { SetTimestamp(std::move(value)); return *this;}

  private:

    bool m_connected;
    bool m_connectedHasBeenSet;

    Aws::String m_connectionName;
    bool m_connectionNameHasBeenSet;

    int m_cpuUtilization;
    bool m_cpuUtilizationHasBeenSet;

    long long m_disconnections;
    bool m_disconnectionsHasBeenSet;

    long long m_droppedPayloads;
    bool m_droppedPayloadsHasBeenSet;

    long long m_latePayloads;
    bool m_latePayloadsHasBeenSet;

    long long m_bytesTransferred;
    bool m_bytesTransferredHasBeenSet;

    StatisticSet m_payloadTime;
    bool m_payloadTimeHasBeenSet;

    long long m_payloadTimeP50;
    bool m_payloadTimeP50HasBeenSet;

    long long m_payloadTimeP90;
    bool m_payloadTimeP90HasBeenSet;

    long long m_payloadTimeP99;
    bool m_payloadTimeP99HasBeenSet;

    long long m_probeRetries;
    bool m_probeRetriesHasBeenSet;

    bool m_receiver;
    bool m_receiverHasBeenSet;

    Aws::Utils::DateTime m_timestamp;
    bool m_timestampHasBeenSet;
  };

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
