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
#include <utility>

namespace Aws
{
template<typename RESULT_TYPE>
class AmazonWebServiceResult;

namespace Utils
{
namespace Json
{
  class JsonValue;
} // namespace Json
} // namespace Utils
namespace CDIMonitoring
{
namespace Model
{
  class AWS_CDIMONITORING_API PutMetricGroupsResult
  {
  public:
    PutMetricGroupsResult();
    PutMetricGroupsResult(const Aws::AmazonWebServiceResult<Aws::Utils::Json::JsonValue>& result);
    PutMetricGroupsResult& operator=(const Aws::AmazonWebServiceResult<Aws::Utils::Json::JsonValue>& result);


    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline const Aws::String& GetEndpoint() const{ return m_endpoint; }

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline void SetEndpoint(const Aws::String& value) { m_endpoint = value; }

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline void SetEndpoint(Aws::String&& value) { m_endpoint = std::move(value); }

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline void SetEndpoint(const char* value) { m_endpoint.assign(value); }

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline PutMetricGroupsResult& WithEndpoint(const Aws::String& value) { SetEndpoint(value); return *this;}

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline PutMetricGroupsResult& WithEndpoint(Aws::String&& value) { SetEndpoint(std::move(value)); return *this;}

    /**
     * The endpoint to use for the next call to /v1/put-metric-groups. If the endpoint
     * is the empty string or not present, then no endpoint should be specified for the
     * next call.
     */
    inline PutMetricGroupsResult& WithEndpoint(const char* value) { SetEndpoint(value); return *this;}

  private:

    Aws::String m_endpoint;
  };

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
