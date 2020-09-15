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
#include <aws/cdi/CDIMonitoringRequest.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/memory/stl/AWSVector.h>
#include <aws/cdi/model/MetricGroup.h>
#include <utility>

namespace Aws
{
namespace CDIMonitoring
{
namespace Model
{

  /**
   * put metric groups request.<p><h3>See Also:</h3>   <a
   * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/PutMetricGroupsRequest">AWS
   * API Reference</a></p>
   */
  class AWS_CDIMONITORING_API PutMetricGroupsRequest : public CDIMonitoringRequest
  {
  public:
    PutMetricGroupsRequest();

    // Service request name is the Operation name which will send this request out,
    // each operation should has unique request name, so that we can get operation's name from this request.
    // Note: this is not true for response, multiple operations may have the same response name,
    // so we can not get operation's name from response.
    inline virtual const char* GetServiceRequestName() const override { return "PutMetricGroups"; }

    Aws::String SerializePayload() const override;


    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline const Aws::String& GetAvailabilityZone() const{ return m_availabilityZone; }

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline bool AvailabilityZoneHasBeenSet() const { return m_availabilityZoneHasBeenSet; }

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline void SetAvailabilityZone(const Aws::String& value) { m_availabilityZoneHasBeenSet = true; m_availabilityZone = value; }

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline void SetAvailabilityZone(Aws::String&& value) { m_availabilityZoneHasBeenSet = true; m_availabilityZone = std::move(value); }

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline void SetAvailabilityZone(const char* value) { m_availabilityZoneHasBeenSet = true; m_availabilityZone.assign(value); }

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline PutMetricGroupsRequest& WithAvailabilityZone(const Aws::String& value) { SetAvailabilityZone(value); return *this;}

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline PutMetricGroupsRequest& WithAvailabilityZone(Aws::String&& value) { SetAvailabilityZone(std::move(value)); return *this;}

    /**
     * The availability zone of the EC2 instance that the metrics are being sent from.
     * The format accepted for this is flexible. It is preferable to use the physical
     * ID, in the format: use1-az1, but the us-east-1a format is also accepted.
     */
    inline PutMetricGroupsRequest& WithAvailabilityZone(const char* value) { SetAvailabilityZone(value); return *this;}


    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline const Aws::String& GetCdiVersion() const{ return m_cdiVersion; }

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline bool CdiVersionHasBeenSet() const { return m_cdiVersionHasBeenSet; }

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline void SetCdiVersion(const Aws::String& value) { m_cdiVersionHasBeenSet = true; m_cdiVersion = value; }

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline void SetCdiVersion(Aws::String&& value) { m_cdiVersionHasBeenSet = true; m_cdiVersion = std::move(value); }

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline void SetCdiVersion(const char* value) { m_cdiVersionHasBeenSet = true; m_cdiVersion.assign(value); }

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithCdiVersion(const Aws::String& value) { SetCdiVersion(value); return *this;}

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithCdiVersion(Aws::String&& value) { SetCdiVersion(std::move(value)); return *this;}

    /**
     * The version of the AWS CDI SDK that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithCdiVersion(const char* value) { SetCdiVersion(value); return *this;}


    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline const Aws::String& GetDomainName() const{ return m_domainName; }

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline bool DomainNameHasBeenSet() const { return m_domainNameHasBeenSet; }

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline void SetDomainName(const Aws::String& value) { m_domainNameHasBeenSet = true; m_domainName = value; }

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline void SetDomainName(Aws::String&& value) { m_domainNameHasBeenSet = true; m_domainName = std::move(value); }

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline void SetDomainName(const char* value) { m_domainNameHasBeenSet = true; m_domainName.assign(value); }

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline PutMetricGroupsRequest& WithDomainName(const Aws::String& value) { SetDomainName(value); return *this;}

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline PutMetricGroupsRequest& WithDomainName(Aws::String&& value) { SetDomainName(std::move(value)); return *this;}

    /**
     * A unique identifier for this instance of the AWS CDI SDK.
     */
    inline PutMetricGroupsRequest& WithDomainName(const char* value) { SetDomainName(value); return *this;}


    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline const Aws::String& GetEc2InstanceId() const{ return m_ec2InstanceId; }

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline bool Ec2InstanceIdHasBeenSet() const { return m_ec2InstanceIdHasBeenSet; }

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline void SetEc2InstanceId(const Aws::String& value) { m_ec2InstanceIdHasBeenSet = true; m_ec2InstanceId = value; }

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline void SetEc2InstanceId(Aws::String&& value) { m_ec2InstanceIdHasBeenSet = true; m_ec2InstanceId = std::move(value); }

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline void SetEc2InstanceId(const char* value) { m_ec2InstanceIdHasBeenSet = true; m_ec2InstanceId.assign(value); }

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithEc2InstanceId(const Aws::String& value) { SetEc2InstanceId(value); return *this;}

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithEc2InstanceId(Aws::String&& value) { SetEc2InstanceId(std::move(value)); return *this;}

    /**
     * The ID of the EC2 instance that the metrics are being sent from.
     */
    inline PutMetricGroupsRequest& WithEc2InstanceId(const char* value) { SetEc2InstanceId(value); return *this;}


    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline const Aws::Vector<MetricGroup>& GetMetricGroups() const{ return m_metricGroups; }

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline bool MetricGroupsHasBeenSet() const { return m_metricGroupsHasBeenSet; }

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline void SetMetricGroups(const Aws::Vector<MetricGroup>& value) { m_metricGroupsHasBeenSet = true; m_metricGroups = value; }

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline void SetMetricGroups(Aws::Vector<MetricGroup>&& value) { m_metricGroupsHasBeenSet = true; m_metricGroups = std::move(value); }

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline PutMetricGroupsRequest& WithMetricGroups(const Aws::Vector<MetricGroup>& value) { SetMetricGroups(value); return *this;}

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline PutMetricGroupsRequest& WithMetricGroups(Aws::Vector<MetricGroup>&& value) { SetMetricGroups(std::move(value)); return *this;}

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline PutMetricGroupsRequest& AddMetricGroups(const MetricGroup& value) { m_metricGroupsHasBeenSet = true; m_metricGroups.push_back(value); return *this; }

    /**
     * The list of groups of metrics to publish. An empty array can be passed here to
     * use this as a discovery call to retrieve the endpoint to use.
     */
    inline PutMetricGroupsRequest& AddMetricGroups(MetricGroup&& value) { m_metricGroupsHasBeenSet = true; m_metricGroups.push_back(std::move(value)); return *this; }


    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline const Aws::String& GetSdkInstanceID() const{ return m_sdkInstanceID; }

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline bool SdkInstanceIDHasBeenSet() const { return m_sdkInstanceIDHasBeenSet; }

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline void SetSdkInstanceID(const Aws::String& value) { m_sdkInstanceIDHasBeenSet = true; m_sdkInstanceID = value; }

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline void SetSdkInstanceID(Aws::String&& value) { m_sdkInstanceIDHasBeenSet = true; m_sdkInstanceID = std::move(value); }

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline void SetSdkInstanceID(const char* value) { m_sdkInstanceIDHasBeenSet = true; m_sdkInstanceID.assign(value); }

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline PutMetricGroupsRequest& WithSdkInstanceID(const Aws::String& value) { SetSdkInstanceID(value); return *this;}

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline PutMetricGroupsRequest& WithSdkInstanceID(Aws::String&& value) { SetSdkInstanceID(std::move(value)); return *this;}

    /**
     * An ID that is unique for each new instance of the Krill SDK, such as a random
     * value created on each call to RmtCoreInitialize. At this time we don't prescribe
     * the exact format required.
     */
    inline PutMetricGroupsRequest& WithSdkInstanceID(const char* value) { SetSdkInstanceID(value); return *this;}

  private:

    Aws::String m_availabilityZone;
    bool m_availabilityZoneHasBeenSet;

    Aws::String m_cdiVersion;
    bool m_cdiVersionHasBeenSet;

    Aws::String m_domainName;
    bool m_domainNameHasBeenSet;

    Aws::String m_ec2InstanceId;
    bool m_ec2InstanceIdHasBeenSet;

    Aws::Vector<MetricGroup> m_metricGroups;
    bool m_metricGroupsHasBeenSet;

    Aws::String m_sdkInstanceID;
    bool m_sdkInstanceIDHasBeenSet;
  };

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
