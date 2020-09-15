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

#include <aws/cdi/model/PutMetricGroupsRequest.h>
#include <aws/core/utils/json/JsonSerializer.h>

#include <utility>

using namespace Aws::CDIMonitoring::Model;
using namespace Aws::Utils::Json;
using namespace Aws::Utils;

PutMetricGroupsRequest::PutMetricGroupsRequest() : 
    m_availabilityZoneHasBeenSet(false),
    m_cdiVersionHasBeenSet(false),
    m_domainNameHasBeenSet(false),
    m_ec2InstanceIdHasBeenSet(false),
    m_metricGroupsHasBeenSet(false),
    m_sdkInstanceIDHasBeenSet(false)
{
}

Aws::String PutMetricGroupsRequest::SerializePayload() const
{
  JsonValue payload;

  if(m_availabilityZoneHasBeenSet)
  {
   payload.WithString("availabilityZone", m_availabilityZone);

  }

  if(m_cdiVersionHasBeenSet)
  {
   payload.WithString("cdiVersion", m_cdiVersion);

  }

  if(m_domainNameHasBeenSet)
  {
   payload.WithString("domainName", m_domainName);

  }

  if(m_ec2InstanceIdHasBeenSet)
  {
   payload.WithString("ec2InstanceId", m_ec2InstanceId);

  }

  if(m_metricGroupsHasBeenSet)
  {
   Array<JsonValue> metricGroupsJsonList(m_metricGroups.size());
   for(unsigned metricGroupsIndex = 0; metricGroupsIndex < metricGroupsJsonList.GetLength(); ++metricGroupsIndex)
   {
     metricGroupsJsonList[metricGroupsIndex].AsObject(m_metricGroups[metricGroupsIndex].Jsonize());
   }
   payload.WithArray("metricGroups", std::move(metricGroupsJsonList));

  }

  if(m_sdkInstanceIDHasBeenSet)
  {
   payload.WithString("sdkInstanceID", m_sdkInstanceID);

  }

  return payload.View().WriteReadable();
}




