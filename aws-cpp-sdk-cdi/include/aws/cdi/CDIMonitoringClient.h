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
#include <aws/cdi/CDIMonitoringErrors.h>
#include <aws/core/client/AWSError.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/AWSClient.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/json/JsonSerializer.h>
#include <aws/cdi/model/PutMetricGroupsResult.h>
#include <aws/core/client/AsyncCallerContext.h>
#include <aws/core/http/HttpTypes.h>
#include <future>
#include <functional>

namespace Aws
{

namespace Http
{
  class HttpClient;
  class HttpClientFactory;
} // namespace Http

namespace Utils
{
  template< typename R, typename E> class Outcome;
namespace Threading
{
  class Executor;
} // namespace Threading
} // namespace Utils

namespace Auth
{
  class AWSCredentials;
  class AWSCredentialsProvider;
} // namespace Auth

namespace Client
{
  class RetryStrategy;
} // namespace Client

namespace CDIMonitoring
{

namespace Model
{
        class PutMetricGroupsRequest;

        typedef Aws::Utils::Outcome<PutMetricGroupsResult, Aws::Client::AWSError<CDIMonitoringErrors>> PutMetricGroupsOutcome;

        typedef std::future<PutMetricGroupsOutcome> PutMetricGroupsOutcomeCallable;
} // namespace Model

  class CDIMonitoringClient;

    typedef std::function<void(const CDIMonitoringClient*, const Model::PutMetricGroupsRequest&, const Model::PutMetricGroupsOutcome&, const std::shared_ptr<const Aws::Client::AsyncCallerContext>&) > PutMetricGroupsResponseReceivedHandler;

  /**
   * API for AWS CDI Monitoring Service
   */
  class AWS_CDIMONITORING_API CDIMonitoringClient : public Aws::Client::AWSJsonClient
  {
    public:
      typedef Aws::Client::AWSJsonClient BASECLASS;

       /**
        * Initializes client to use DefaultCredentialProviderChain, with default http client factory, and optional client config. If client config
        * is not specified, it will be initialized to default values.
        */
        CDIMonitoringClient(const Aws::Client::ClientConfiguration& clientConfiguration = Aws::Client::ClientConfiguration());

       /**
        * Initializes client to use SimpleAWSCredentialsProvider, with default http client factory, and optional client config. If client config
        * is not specified, it will be initialized to default values.
        */
        CDIMonitoringClient(const Aws::Auth::AWSCredentials& credentials, const Aws::Client::ClientConfiguration& clientConfiguration = Aws::Client::ClientConfiguration());

       /**
        * Initializes client to use specified credentials provider with specified client config. If http client factory is not supplied,
        * the default http client factory will be used
        */
        CDIMonitoringClient(const std::shared_ptr<Aws::Auth::AWSCredentialsProvider>& credentialsProvider,
            const Aws::Client::ClientConfiguration& clientConfiguration = Aws::Client::ClientConfiguration());

        virtual ~CDIMonitoringClient();

        inline virtual const char* GetServiceClientName() const override { return "CDI Monitoring"; }


        /**
         * Publishes metric group data points to Amazon CloudWatch.<p><h3>See Also:</h3>  
         * <a
         * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/PutMetricGroups">AWS
         * API Reference</a></p>
         */
        virtual Model::PutMetricGroupsOutcome PutMetricGroups(const Model::PutMetricGroupsRequest& request) const;

        /**
         * Publishes metric group data points to Amazon CloudWatch.<p><h3>See Also:</h3>  
         * <a
         * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/PutMetricGroups">AWS
         * API Reference</a></p>
         *
         * returns a future to the operation so that it can be executed in parallel to other requests.
         */
        virtual Model::PutMetricGroupsOutcomeCallable PutMetricGroupsCallable(const Model::PutMetricGroupsRequest& request) const;

        /**
         * Publishes metric group data points to Amazon CloudWatch.<p><h3>See Also:</h3>  
         * <a
         * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/PutMetricGroups">AWS
         * API Reference</a></p>
         *
         * Queues the request into a thread executor and triggers associated callback when operation has finished.
         */
        virtual void PutMetricGroupsAsync(const Model::PutMetricGroupsRequest& request, const PutMetricGroupsResponseReceivedHandler& handler, const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context = nullptr) const;


      void OverrideEndpoint(const Aws::String& endpoint);
    private:
      void init(const Aws::Client::ClientConfiguration& clientConfiguration);
        void PutMetricGroupsAsyncHelper(const Model::PutMetricGroupsRequest& request, const PutMetricGroupsResponseReceivedHandler& handler, const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context) const;

      Aws::String m_uri;
      Aws::String m_configScheme;
      std::shared_ptr<Aws::Utils::Threading::Executor> m_executor;
  };

} // namespace CDIMonitoring
} // namespace Aws
