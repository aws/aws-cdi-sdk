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

#include <aws/cdi/model/MetricGroup.h>
#include <aws/core/utils/json/JsonSerializer.h>

#include <utility>

using namespace Aws::Utils::Json;
using namespace Aws::Utils;

namespace Aws
{
namespace CDIMonitoring
{
namespace Model
{

MetricGroup::MetricGroup() :
    m_connected(false),
    m_connectedHasBeenSet(false),
    m_connectionNameHasBeenSet(false),
    m_cpuUtilization(0),
    m_cpuUtilizationHasBeenSet(false),
    m_disconnections(0),
    m_disconnectionsHasBeenSet(false),
    m_droppedPayloads(0),
    m_droppedPayloadsHasBeenSet(false),
    m_latePayloads(0),
    m_latePayloadsHasBeenSet(false),
    m_bytesTransferred(0),
    m_bytesTransferredHasBeenSet(false),
    m_payloadTimeHasBeenSet(false),
    m_payloadTimeP50(0),
    m_payloadTimeP50HasBeenSet(false),
    m_payloadTimeP90(0),
    m_payloadTimeP90HasBeenSet(false),
    m_payloadTimeP99(0),
    m_payloadTimeP99HasBeenSet(false),
    m_probeRetries(0),
    m_probeRetriesHasBeenSet(false),
    m_receiver(false),
    m_receiverHasBeenSet(false),
    m_timestampHasBeenSet(false)
{
}

MetricGroup::MetricGroup(JsonView jsonValue) :
    m_connected(false),
    m_connectedHasBeenSet(false),
    m_connectionNameHasBeenSet(false),
    m_cpuUtilization(0),
    m_cpuUtilizationHasBeenSet(false),
    m_disconnections(0),
    m_disconnectionsHasBeenSet(false),
    m_droppedPayloads(0),
    m_droppedPayloadsHasBeenSet(false),
    m_latePayloads(0),
    m_latePayloadsHasBeenSet(false),
    m_bytesTransferred(0),
    m_bytesTransferredHasBeenSet(false),
    m_payloadTimeHasBeenSet(false),
    m_payloadTimeP50(0),
    m_payloadTimeP50HasBeenSet(false),
    m_payloadTimeP90(0),
    m_payloadTimeP90HasBeenSet(false),
    m_payloadTimeP99(0),
    m_payloadTimeP99HasBeenSet(false),
    m_probeRetries(0),
    m_probeRetriesHasBeenSet(false),
    m_receiver(false),
    m_receiverHasBeenSet(false),
    m_timestampHasBeenSet(false)
{
  *this = jsonValue;
}

MetricGroup& MetricGroup::operator =(JsonView jsonValue)
{
  if(jsonValue.ValueExists("connected"))
  {
    m_connected = jsonValue.GetBool("connected");

    m_connectedHasBeenSet = true;
  }

  if(jsonValue.ValueExists("connectionName"))
  {
    m_connectionName = jsonValue.GetString("connectionName");

    m_connectionNameHasBeenSet = true;
  }

  if(jsonValue.ValueExists("cpuUtilization"))
  {
    m_cpuUtilization = jsonValue.GetInteger("cpuUtilization");

    m_cpuUtilizationHasBeenSet = true;
  }

  if(jsonValue.ValueExists("disconnections"))
  {
    m_disconnections = jsonValue.GetInt64("disconnections");

    m_disconnectionsHasBeenSet = true;
  }

  if(jsonValue.ValueExists("droppedPayloads"))
  {
    m_droppedPayloads = jsonValue.GetInt64("droppedPayloads");

    m_droppedPayloadsHasBeenSet = true;
  }

  if(jsonValue.ValueExists("latePayloads"))
  {
    m_latePayloads = jsonValue.GetInt64("latePayloads");

    m_latePayloadsHasBeenSet = true;
  }

  if(jsonValue.ValueExists("bytesTransferred"))
  {
    m_bytesTransferred = jsonValue.GetInt64("bytesTransferred");

    m_bytesTransferredHasBeenSet = true;
  }

  if(jsonValue.ValueExists("payloadTime"))
  {
    m_payloadTime = jsonValue.GetObject("payloadTime");

    m_payloadTimeHasBeenSet = true;
  }

  if(jsonValue.ValueExists("payloadTimeP50"))
  {
    m_payloadTimeP50 = jsonValue.GetInt64("payloadTimeP50");

    m_payloadTimeP50HasBeenSet = true;
  }

  if(jsonValue.ValueExists("payloadTimeP90"))
  {
    m_payloadTimeP90 = jsonValue.GetInt64("payloadTimeP90");

    m_payloadTimeP90HasBeenSet = true;
  }

  if(jsonValue.ValueExists("payloadTimeP99"))
  {
    m_payloadTimeP99 = jsonValue.GetInt64("payloadTimeP99");

    m_payloadTimeP99HasBeenSet = true;
  }

  if(jsonValue.ValueExists("probeRetries"))
  {
    m_probeRetries = jsonValue.GetInt64("probeRetries");

    m_probeRetriesHasBeenSet = true;
  }

  if(jsonValue.ValueExists("receiver"))
  {
    m_receiver = jsonValue.GetBool("receiver");

    m_receiverHasBeenSet = true;
  }

  if(jsonValue.ValueExists("timestamp"))
  {
    m_timestamp = jsonValue.GetString("timestamp");

    m_timestampHasBeenSet = true;
  }

  return *this;
}

JsonValue MetricGroup::Jsonize() const
{
  JsonValue payload;

  if(m_connectedHasBeenSet)
  {
   payload.WithBool("connected", m_connected);

  }

  if(m_connectionNameHasBeenSet)
  {
   payload.WithString("connectionName", m_connectionName);

  }

  if(m_cpuUtilizationHasBeenSet)
  {
   payload.WithInteger("cpuUtilization", m_cpuUtilization);

  }

  if(m_disconnectionsHasBeenSet)
  {
   payload.WithInt64("disconnections", m_disconnections);

  }

  if(m_droppedPayloadsHasBeenSet)
  {
   payload.WithInt64("droppedPayloads", m_droppedPayloads);

  }

  if(m_latePayloadsHasBeenSet)
  {
   payload.WithInt64("latePayloads", m_latePayloads);

  }

  if(m_bytesTransferredHasBeenSet)
  {
   payload.WithInt64("bytesTransferred", m_bytesTransferred);

  }

  if(m_payloadTimeHasBeenSet)
  {
   payload.WithObject("payloadTime", m_payloadTime.Jsonize());

  }

  if(m_payloadTimeP50HasBeenSet)
  {
   payload.WithInt64("payloadTimeP50", m_payloadTimeP50);

  }

  if(m_payloadTimeP90HasBeenSet)
  {
   payload.WithInt64("payloadTimeP90", m_payloadTimeP90);

  }

  if(m_payloadTimeP99HasBeenSet)
  {
   payload.WithInt64("payloadTimeP99", m_payloadTimeP99);

  }

  if(m_probeRetriesHasBeenSet)
  {
   payload.WithInt64("probeRetries", m_probeRetries);

  }

  if(m_receiverHasBeenSet)
  {
   payload.WithBool("receiver", m_receiver);

  }

  if(m_timestampHasBeenSet)
  {
   payload.WithString("timestamp", m_timestamp.ToGmtString(DateFormat::ISO_8601));
  }

  return payload;
}

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
