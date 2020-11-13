// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This file contains internal definitions and implementation used with the SDK that is not part of the API. It is used
 * as a C interface to the CloudWatch SDK, which uses a C++ interface.
 */

// This file must be included first since it defines CLOUDWATCH_METRICS_ENABLED.
#include "configuration.h"

#ifdef CLOUDWATCH_METRICS_ENABLED
#include "cloudwatch_sdk_metrics.h"

#include <aws/core/Aws.h>
#include <aws/core/internal/AWSHttpResourceClient.h>
#include <aws/core/utils/logging/FormattedLogSystem.h>
#include <aws/core/utils/UUID.h>
#include <aws/monitoring/CloudWatchClient.h>
#include <aws/monitoring/model/PutMetricDataRequest.h>

#ifdef METRICS_GATHERING_SERVICE_ENABLED
#include <aws/cdi/CDIMonitoringClient.h>
#include <aws/cdi/model/PutMetricGroupsRequest.h>
#endif  // METRICS_GATHERING_SERVICE_ENABLED

#include "cdi_logger_api.h"
#undef GetMessage                   // workaround for AWSError method GetMessage()

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * This class is intended to have a single static instance. It will initialize the AWS SDK during program
 * initialization and shut it down when the program ends.
 */
class AwsSdkState
{
public:
    /**
     * Constructor which initizlizes the AWS SDK.
     */
    AwsSdkState();

    /**
     * Destructor, shuts down the AWS SDK.
     */
    ~AwsSdkState();

public:
    /**
     * Returns the value of the random UUID that was set when this object was instantiated.
     *
     * @return A reference to the UUID as a string.
     */
    const Aws::String& GetUuid() const { return m_uuid; }

private:
    Aws::SDKOptions m_options; ///< The AWS SDK options whose lifetime is managed by this object.
    Aws::String m_uuid;        ///< A UUID string generated at construction.
};

/**
 * @brief Class used to redirect AWS SDK logging to CDI logging methods.
 */
class CdiCloudWatchLogging : public Aws::Utils::Logging::FormattedLogSystem
{
public:

    /**
     * Constructor. This just creates an instance of the object.
     *
     * @param log_level The AWS Log level used to construct the base class instance.
     */
    CdiCloudWatchLogging(Aws::Utils::Logging::LogLevel log_level) :
        FormattedLogSystem(log_level)
    {}

    virtual ~CdiCloudWatchLogging() {}

    /**
     * Flushes buffered messages.
     */
    void Flush() override {};

protected:
    /**
     * Override this function of the FormattedLogSystem class to write to CDI SDK logs instead of AWS SDK logs.
     */
    virtual void ProcessFormattedStatement(Aws::String&& statement) override;
};

/**
 * @brief Abstract base class used to interface C functions to the C++ AWS-SDK for CloudWatch metrics and for the CDI
 *        metrics gathering service.
 */
class MetricsClass {
public:
    /**
     * Destructor.
     */
    virtual ~MetricsClass() {}

    /**
     * Sends the statistics set to the object's destination.
     *
     * @param stats_ptr Address of the statistics structure to publish.
     *
     * @return CdiReturnStatus kCdiStatusOk if the statistics were successfully published,
     *         kCdiStatusCloudWatchThrottling if the service requires the publication rate to be reduced, or possibly
     *         some other value from CdiReturnStatus.
     */
    virtual CdiReturnStatus Send(const CloudWatchTransferStats* stats_ptr) = 0;

protected:
    /// This static instance automatically initializes and shuts down the AWS SDK.
    static AwsSdkState s_aws_sdk_state;
};

/**
 * Concrete class for publishing CDI SDK metrics to the CloudWatch metrics service.
 */
class UserMetrics : public MetricsClass
{
public:
    /**
     * Default constructor which is unavailable.
     */
    UserMetrics() = delete;

    /**
     * Constructor. This just creates an instance of the object.
     *
     * @param config_ptr Pointer to configuration data.
     */
    UserMetrics(const CloudWatchConfigData* config_ptr);

    /**
     * Send the specified set of statistics to CloudWatch.
     *
     * @param stats_ptr Pointer to statistics to send.
     *
     * @return CdiReturnStatus kCdiStatusOk if the statistics were successfully published,
     *         kCdiStatusCloudWatchThrottling if the service requires the publication rate to be reduced, or possibly
     *         some other value from CdiReturnStatus.
     */
    CdiReturnStatus Send(const CloudWatchTransferStats* stats_ptr) override;

private:
    /**
     * Sets the common parts in a datum.
     *
     * @param datum The object to modify.
     * @param connection_name_str Reference to name of the connection.
     * @param direction_str Either "Tx" or "Rx" to indicate the direction of the connection.
     * @param high_resolution If true, use 1-second metric storage resolution, otherwise use AWS-SDK default (1-minute
     *                        resolution).
     * @param timestamp Timestamp of the metric.
     * @param metric_name_str Reference to name of the metric.
     */
    void SetDatumBoilerplate(Aws::CloudWatch::Model::MetricDatum& datum, const Aws::String& connection_name_str,
                             const Aws::String& direction_str, bool high_resolution,
                             const Aws::Utils::DateTime timestamp, const Aws::String& metric_name_str);

    /**
     * Create a new metric datum object using a standard data point value and add it to the specified request.
     *
     * @param request Reference to request object.
     * @param connection_name_str Reference to name of the connection.
     * @param direction_str Either "Tx" or "Rx" to indicate the direction of the connection.
     * @param high_resolution If true, use 1-second metric storage resolution, otherwise use AWS-SDK default (1-minute
     *                        resolution).
     * @param timestamp Timestamp of the metric.
     * @param metric_name_str Reference to name of the metric.
     * @param data_point Value of the metric to add.
     *
     * @return true if successful, otherwise false.
     */
    bool AddDatum(Aws::CloudWatch::Model::PutMetricDataRequest& request, const Aws::String& connection_name_str,
                  const Aws::String& direction_str, bool high_resolution, const Aws::Utils::DateTime timestamp,
                  const Aws::String& metric_name_str, int data_point);

    /**
     * Create a new metric datum object using a statistic set of values and add it to the specified request.
     *
     * @param request Reference to request object.
     * @param connection_name_str Reference to name of the connection.
     * @param direction_str Either "Tx" or "Rx" to indicate the direction of the connection.
     * @param high_resolution If true, use 1-second metric storage resolution, otherwise use AWS-SDK default (1-minute
     *                        resolution).
     * @param timestamp Timestamp of the metric.
     * @param metric_name Reference to name of the metric.
     * @param sample_count Number of samples in the set.
     * @param min Minimum value in the set.
     * @param max Maximum value in the set.
     * @param sum Sum of the values in the set.Minimum value in the set.
     *
     * @return true if successful, otherwise false.
     */
    bool AddDatum(Aws::CloudWatch::Model::PutMetricDataRequest& request, const Aws::String& connection_name_str,
                  const Aws::String& direction_str, bool high_resolution, const Aws::Utils::DateTime timestamp,
                  const Aws::String& metric_name, int sample_count, int min, int max, int sum);

    // Private data members.
    Aws::String m_region_str;               ///< Region string.
    Aws::String m_namespace_str;            ///< Namespace string.
    Aws::String m_dimension_domain_str;     ///< Dimension domain string.
    Aws::String m_dimension_connection_str; ///< Dimension connection string.
};

#ifdef METRICS_GATHERING_SERVICE_ENABLED
/**
 * Concrete class for publishing CDI SDK metrics to the CDI metrics gathering service.
 */
class MetricsGatherer : public MetricsClass
{
public:
    /**
     * Default constructor which is unavailable.
     */
    MetricsGatherer() = delete;

    /**
     * Constructor.
     *
     * @param config_ptr Pointer to a configuration structure containing the information required for initialization.
     */
    MetricsGatherer(const MetricsGathererConfigData* config_ptr);

    /**
     * Sends the statistics set to the CDI metrics gathering service.
     *
     * @param stats_ptr Address of the statistics structure to publish.
     *
     * @return CdiReturnStatus kCdiStatusOk if the statistics were successfully published,
     *         kCdiStatusCloudWatchThrottling if the service requires the publication rate to be reduced, or possibly
     *         some other value from CdiReturnStatus.
     */
    CdiReturnStatus Send(const CloudWatchTransferStats* stats_ptr) override;

private:
    Aws::Client::ClientConfiguration m_client_config;            ///< SDK client configuration structure.
    Aws::CDIMonitoring::Model::PutMetricGroupsRequest m_request; ///< Request object reused for every Send.
    Aws::CDIMonitoring::Model::MetricGroup m_group;              ///< Metrics group object reused for every Send.
    Aws::String m_last_endpoint_str;                             ///< Endpoint returned from sending metrics data.
};
#endif  // METRICS_GATHERING_SERVICE_ENABLED

//*********************************************************************************************************************
//****************************************** START OF CLASS DEFINITIONS ***********************************************
//*********************************************************************************************************************

AwsSdkState::AwsSdkState()
{
    // Call CdiCloudWatchLogging() to redirect AWS SDK logging into CDI log files. Set logging level.
    m_options.loggingOptions.logger_create_fn =
                           [] { return std::make_shared<CdiCloudWatchLogging>(Aws::Utils::Logging::LogLevel::Error); };
    m_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Error;

    // From AWS-SDK documentation: Install a global SIGPIPE handler that logs the error and prevents it from terminating
    // the current process. This can be used on operating systems on which CURL is being used. In some situations CURL
    // cannot avoid triggering a SIGPIPE. For more information see: https://curl.haxx.se/libcurl/c/CURLOPT_NOSIGNAL.html
    m_options.httpOptions.installSigPipeHandler = true;
    // Initialize the CloudWatch SDK.
    Aws::InitAPI(m_options);

    // Generate a UUID to send with metrics. This must be done after the API is initialized.
    m_uuid = Aws::Utils::UUID::RandomUUID();
}

AwsSdkState::~AwsSdkState()
{
    // Shut down.
    Aws::ShutdownAPI(m_options);
}

void CdiCloudWatchLogging::ProcessFormattedStatement(Aws::String&& statement)
{
    // Remove the newline and line feed (if there is one... looking at you, Windows) from 'statement' because our logger
    // adds a newline.
    Aws::String str(statement);
    str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
    str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());
    // Write to the CDI logger, but add a prefix to identify this message as being from AWS SDK.
    CDI_LOG_THREAD(kLogInfo, "AWS-SDK: [%s].", str.c_str());
}

UserMetrics::UserMetrics(const CloudWatchConfigData* config_ptr) :
    m_namespace_str(config_ptr->namespace_str),
    m_dimension_domain_str(config_ptr->dimension_domain_str)
{
    // If region string is not empty then use it, otherwise get current region from the CloudWatch SDK.
    if (config_ptr->region_str && '\0' != config_ptr->region_str[0]) {
        m_region_str = config_ptr->region_str;
    } else {
        Aws::Internal::EC2MetadataClient client;
        m_region_str = client.GetCurrentRegion();
    }
}

/// This static instance automatically initializes and shuts down the AWS SDK.
AwsSdkState MetricsClass::s_aws_sdk_state;

CdiReturnStatus UserMetrics::Send(const CloudWatchTransferStats* stats_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Create CloudWatchClient dynamically, otherwise AWS-SDK can generate broken pipe exceptions.
    Aws::Client::ClientConfiguration client_config;
    // Set region and create an instance of the CloudWatchClient.
    client_config.region = m_region_str;
    Aws::CloudWatch::CloudWatchClient cw(client_config);

    Aws::CloudWatch::Model::PutMetricDataRequest request;
    request.SetNamespace(m_namespace_str);

    const Aws::Utils::DateTime timestamp(static_cast<int64_t>(stats_ptr->timestamp_in_ms_since_epoch));
    const char* direction_str = stats_ptr->is_receiver ? "Rx" : "Tx";
    const char* connection_str = stats_ptr->dimension_connection_str;
    const bool high_resolution = stats_ptr->high_resolution;

    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "DroppedPayloads",
             stats_ptr->count_based_delta_stats.delta_num_payloads_dropped);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "LatePayloads",
            stats_ptr->count_based_delta_stats.delta_num_payloads_late);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "Disconnections",
            stats_ptr->count_based_delta_stats.delta_dropped_connection_count);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "Connected",
             stats_ptr->connected);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "CpuUtilization",
             stats_ptr->cpu_utilization / 100);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "ProbeRetries",
            stats_ptr->count_based_delta_stats.delta_probe_command_retry_count);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "PayloadTimeP50",
            stats_ptr->payload_time_interval_stats.transfer_time_P50);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "PayloadTimeP90",
            stats_ptr->payload_time_interval_stats.transfer_time_P90);
    AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "PayloadTimeP99",
            stats_ptr->payload_time_interval_stats.transfer_time_P99);

    if (0 != stats_ptr->payload_time_interval_stats.transfer_count) {
        AddDatum(request, connection_str, direction_str, high_resolution, timestamp, "PayloadTime",
                 stats_ptr->payload_time_interval_stats.transfer_count,
                 stats_ptr->payload_time_interval_stats.transfer_time_min,
                 stats_ptr->payload_time_interval_stats.transfer_time_max,
                 stats_ptr->payload_time_interval_stats.transfer_time_sum);
    }

    Aws::CloudWatch::Model::PutMetricDataOutcome outcome;
    try {
        outcome = cw.PutMetricData(request);
    } catch (...) {
        // Should never get here, but just to be safe catch all exceptions.
        CDI_LOG_THREAD(kLogError, "PutMetricData() failed. Caught an unexpected exception.");
        rs = kCdiStatusCloudWatchThrottling;
    }

    if (kCdiStatusOk == rs && !outcome.IsSuccess()) {
        auto error = outcome.GetError();
        Aws::CloudWatch::CloudWatchErrors err_type = error.GetErrorType();
        if (err_type == Aws::CloudWatch::CloudWatchErrors::THROTTLING) {
            // NOTE: Default limits for PutMetricData() are: 40 KB for HTTP POST requests. PutMetricData can handle
            // 150 transactions per second (TPS), which is the maximum number of operation requests you can make per
            // second without being throttled. You can request a quota increase through AWS.
            CDI_LOG_THREAD(kLogInfo, "PutMetricData() is being throttling by AWS-SDK. Message[%s].",
                           outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchThrottling;
        } else if (err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_CLIENT_TOKEN_ID ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_ACCESS_KEY_ID ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::SIGNATURE_DOES_NOT_MATCH ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::MISSING_AUTHENTICATION_TOKEN) {
            // In testing, if the access key is wrong, INVALID_CLIENT_TOKEN_ID is returned. If the secret key is wrong,
            // SIGNATURE_DOES_NOT_MATCH is returned. Added INVALID_ACCESS_KEY_ID to this list too.
            CDI_LOG_THREAD(kLogError, "PutMetricData() failed. Check credentials. ErrorType[%d] Message[%s].",
                           err_type, outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchInvalidCredentials;
        } else if (err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_PARAMETER_COMBINATION ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_QUERY_PARAMETER ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_PARAMETER_VALUE ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::VALIDATION ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::MISSING_PARAMETER ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::MISSING_REQUIRED_PARAMETER ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::MALFORMED_QUERY_STRING ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::MISSING_ACTION ||
                   err_type == Aws::CloudWatch::CloudWatchErrors::INVALID_ACTION) {
            CDI_LOG_THREAD(kLogError, "PutMetricData() failed. ErrorType[%d] Message[%s].", err_type,
                        outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusFatal;
        } else {
            CDI_LOG_THREAD(kLogError,
                           "PutMetricData() failed. Throttling due to unexpected error. ErrorType[%d] Message[%s].",
                           err_type,
                        outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchThrottling;
        }
    }

    return rs;
}

void UserMetrics::SetDatumBoilerplate(Aws::CloudWatch::Model::MetricDatum& datum,
                                      const Aws::String& connection_name_str, const Aws::String& direction_str,
                                      bool high_resolution, const Aws::Utils::DateTime timestamp,
                                      const Aws::String& metric_name_str)
{
    datum.SetTimestamp(timestamp);
    datum.SetMetricName(metric_name_str);
    datum.SetUnit(Aws::CloudWatch::Model::StandardUnit::Count);

    if (high_resolution) {
        datum.SetStorageResolution(1); // Valid values are 1 or 60.
    }

    Aws::CloudWatch::Model::Dimension dimension;
    dimension.SetName("Domain");
    dimension.SetValue(m_dimension_domain_str);
    datum.AddDimensions(dimension);

    dimension.SetName("Connection");
    dimension.SetValue(connection_name_str);
    datum.AddDimensions(dimension);

    dimension.SetName("Direction");
    dimension.SetValue(direction_str);
    datum.AddDimensions(dimension);
}

bool UserMetrics::AddDatum(Aws::CloudWatch::Model::PutMetricDataRequest& request,
                           const Aws::String& connection_name_str, const Aws::String& direction_str,
                           bool high_resolution, const Aws::Utils::DateTime timestamp,
                           const Aws::String& metric_name_str, int data_point)
{
    Aws::CloudWatch::Model::MetricDatum datum;
    SetDatumBoilerplate(datum, connection_name_str, direction_str, high_resolution, timestamp, metric_name_str);
    datum.SetValue(data_point);
    request.AddMetricData(datum);
    return true;
}

bool UserMetrics::AddDatum(Aws::CloudWatch::Model::PutMetricDataRequest& request,
                           const Aws::String& connection_name_str, const Aws::String& direction_str,
                           bool high_resolution, const Aws::Utils::DateTime timestamp,
                           const Aws::String& metric_name_str, int sample_count, int min, int max, int sum)
{
    Aws::CloudWatch::Model::StatisticSet stat_set;
    stat_set.SetSampleCount(sample_count);
    stat_set.SetMinimum(min);
    stat_set.SetMaximum(max);
    stat_set.SetSum(sum);

    Aws::CloudWatch::Model::MetricDatum datum;
    SetDatumBoilerplate(datum, connection_name_str, direction_str, high_resolution, timestamp, metric_name_str);
    datum.SetUnit(Aws::CloudWatch::Model::StandardUnit::Microseconds);
    datum.SetStatisticValues(stat_set);
    request.AddMetricData(datum);

    return true;
}

#ifdef METRICS_GATHERING_SERVICE_ENABLED
MetricsGatherer::MetricsGatherer(const MetricsGathererConfigData* config_ptr)
{
    Aws::StringStream version;
    version << CDI_SDK_VERSION << "." << CDI_SDK_MAJOR_VERSION << "." << CDI_SDK_MINOR_VERSION;
    m_request.SetCdiVersion(version.str());

    m_request.SetDomainName(config_ptr->dimension_domain_str);

    // Instantiate an object that can query the EC2 metadata endpoint.
    Aws::Internal::EC2MetadataClient client;

    // Set the region of the client config object for clients to use.
    m_client_config.region = client.GetCurrentRegion();

    // Set static items in the request that will get published along with other metrics data.
    Aws::String az{client.GetResource("/latest/meta-data/placement/availability-zone-id")};
    if (az.empty()) {
        CDI_LOG_THREAD(kLogWarning, "Retrieval of availability-zone-id failed, falling back to availability-zone");
        az = client.GetResource("/latest/meta-data/placement/availability-zone");
    }
    m_request.SetAvailabilityZone(az);
    m_request.SetEc2InstanceId(client.GetResource("/latest/meta-data/instance-id"));
    m_request.SetSdkInstanceID(s_aws_sdk_state.GetUuid());

    Aws::CDIMonitoring::Model::PutMetricGroupsOutcome outcome;
    try
    {
        // Try to send an empty group of metrics and check the outcome for permissions errors.
        Aws::Vector<Aws::CDIMonitoring::Model::MetricGroup> groups;
        Aws::CDIMonitoring::Model::PutMetricGroupsRequest request = m_request;
        request.SetMetricGroups(groups);
        Aws::CDIMonitoring::CDIMonitoringClient client{m_client_config};
        outcome = client.PutMetricGroups(request);
    } catch (...)
    {
        // Should never get here, but just to be safe catch all exceptions.
        CDI_LOG_THREAD(kLogError, "PutMetricGroups() failed. Caught an unexpected exception.");
        throw kCdiStatusFatal;
    }
    if (!outcome.IsSuccess()) {
        auto err_type = outcome.GetError().GetErrorType();
        if (err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_CLIENT_TOKEN_ID ||
                err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_ACCESS_KEY_ID ||
                err_type == Aws::CDIMonitoring::CDIMonitoringErrors::SIGNATURE_DOES_NOT_MATCH ||
                err_type == Aws::CDIMonitoring::CDIMonitoringErrors::MISSING_AUTHENTICATION_TOKEN ||
                err_type == Aws::CDIMonitoring::CDIMonitoringErrors::ACCESS_DENIED) {
            // In testing, if the access key is wrong, INVALID_CLIENT_TOKEN_ID is returned. If the secret key is wrong,
            // SIGNATURE_DOES_NOT_MATCH is returned. Added INVALID_ACCESS_KEY_ID to this list too.
            CDI_LOG_THREAD(kLogError, "PutMetricGroups() failed. Check credentials. ErrorType[%d] Message[%s].",
                           err_type, outcome.GetError().GetMessage().c_str());
            throw kCdiStatusCloudWatchInvalidCredentials;
        }
    }
}

CdiReturnStatus MetricsGatherer::Send(const CloudWatchTransferStats* stats_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    // Create CloudWatchClient dynamically, otherwise AWS-SDK can generate broken pipe exceptions.
    Aws::CDIMonitoring::CDIMonitoringClient client{m_client_config};
    if (!m_last_endpoint_str.empty()) {
        client.OverrideEndpoint(m_last_endpoint_str);
    }

    // Create and fill in a statistics set with its component values.
    Aws::CDIMonitoring::Model::StatisticSet statistics_set;
    statistics_set.SetMaximum(stats_ptr->payload_time_interval_stats.transfer_time_max);
    statistics_set.SetMinimum(stats_ptr->payload_time_interval_stats.transfer_time_min);
    statistics_set.SetSampleCount(stats_ptr->payload_time_interval_stats.transfer_count);
    statistics_set.SetSum(stats_ptr->payload_time_interval_stats.transfer_time_sum);

    // Fill in all the other details of the statistics group.
    m_group.SetConnected(stats_ptr->connected);
    m_group.SetConnectionName(stats_ptr->dimension_connection_str);
    m_group.SetCpuUtilization(stats_ptr->cpu_utilization / 100);
    m_group.SetDisconnections(stats_ptr->count_based_delta_stats.delta_dropped_connection_count);
    m_group.SetDroppedPayloads(stats_ptr->count_based_delta_stats.delta_num_payloads_dropped);
    m_group.SetLatePayloads(stats_ptr->count_based_delta_stats.delta_num_payloads_late);
    m_group.SetPayloadTime(statistics_set);
    m_group.SetPayloadTimeP50(stats_ptr->payload_time_interval_stats.transfer_time_P50);
    m_group.SetPayloadTimeP90(stats_ptr->payload_time_interval_stats.transfer_time_P90);
    m_group.SetPayloadTimeP99(stats_ptr->payload_time_interval_stats.transfer_time_P99);
    m_group.SetProbeRetries(stats_ptr->count_based_delta_stats.delta_dropped_connection_count);
    m_group.SetReceiver(stats_ptr->is_receiver);
    m_group.SetTimestamp(Aws::Utils::DateTime{static_cast<int64_t>(stats_ptr->timestamp_in_ms_since_epoch)});

    // Create a vector to put the group into so it can be set in the request and sent.
    Aws::Vector<Aws::CDIMonitoring::Model::MetricGroup> groups;
    groups.push_back(m_group);
    m_request.SetMetricGroups(groups);

    Aws::CDIMonitoring::Model::PutMetricGroupsOutcome outcome;
    try
    {
        outcome = client.PutMetricGroups(m_request);
    } catch (...)
    {
        // Should never get here, but just to be safe catch all exceptions.
        CDI_LOG_THREAD(kLogError, "PutMetricGroups() failed. Caught an unexpected exception.");
        rs = kCdiStatusCloudWatchThrottling;
    }

    if (outcome.IsSuccess()) {
        // Save the endpoint returned by the service for subsequent calls.
        m_last_endpoint_str = outcome.GetResult().GetEndpoint();
    } else if (kCdiStatusOk == rs) {
        auto error = outcome.GetError();
        Aws::CDIMonitoring::CDIMonitoringErrors err_type = error.GetErrorType();
        if (err_type == Aws::CDIMonitoring::CDIMonitoringErrors::THROTTLING) {
            CDI_LOG_THREAD(kLogInfo, "PutMetricGroups() is being throttling by AWS-SDK. Message[%s].",
                           outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchThrottling;
        } else if (err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_CLIENT_TOKEN_ID ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_ACCESS_KEY_ID ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::SIGNATURE_DOES_NOT_MATCH ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::MISSING_AUTHENTICATION_TOKEN) {
            CDI_LOG_THREAD(kLogError, "PutMetricGroups() failed. Check credentials. ErrorType[%d] Message[%s].",
                           err_type, outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchInvalidCredentials;
        } else if (err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_PARAMETER_COMBINATION ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::BAD_REQUEST ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_QUERY_PARAMETER ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_PARAMETER_VALUE ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::VALIDATION ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::MISSING_PARAMETER ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::MALFORMED_QUERY_STRING ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::MISSING_ACTION ||
                   err_type == Aws::CDIMonitoring::CDIMonitoringErrors::INVALID_ACTION) {
            CDI_LOG_THREAD(kLogError, "PutMetricGroups() failed. ErrorType[%d] Message[%s].", err_type,
                        outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusFatal;
        } else {
            CDI_LOG_THREAD(kLogError,
                           "PutMetricData() failed. Throttling due to unexpected error. ErrorType[%d] Message[%s].",
                           err_type,
                        outcome.GetError().GetMessage().c_str());
            rs = kCdiStatusCloudWatchThrottling;
        }
    }

    return rs;
}
#endif  // METRICS_GATHERING_SERVICE_ENABLED

//*********************************************************************************************************************
//****************************************** START OF PUBLIC C FUNCTIONS **********************************************
//*********************************************************************************************************************

CdiReturnStatus CloudWatchSdkMetricsCreate(const CloudWatchConfigData* config_ptr,
                                           CloudWatchSdkMetricsHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    MetricsClass* class_ptr = new UserMetrics(config_ptr);
    if (nullptr == class_ptr) {
        rs = kCdiStatusNotEnoughMemory;
    }

    *ret_handle_ptr = reinterpret_cast<CloudWatchSdkMetricsHandle>(class_ptr);

    return rs;
}

CdiReturnStatus CloudWatchSdkMetricsDestroy(CloudWatchSdkMetricsHandle handle)
{
    MetricsClass* class_ptr = reinterpret_cast<MetricsClass*>(handle);

    delete class_ptr; // Can safely call delete directly, even on NULL pointers.

    return kCdiStatusOk;
}

#ifdef METRICS_GATHERING_SERVICE_ENABLED
CdiReturnStatus MetricsGathererCreate(const MetricsGathererConfigData* config_ptr,
                                      CloudWatchSdkMetricsHandle* ret_handle_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    MetricsClass* class_ptr = nullptr;
    try
    {
        class_ptr = new MetricsGatherer(config_ptr);
        if (nullptr == class_ptr) {
            rs = kCdiStatusNotEnoughMemory;
        }
    } catch (CdiReturnStatus exception_value)
    {
        // The effect of any failure is the same: this service cannot be used.
        rs = exception_value;
    }

    *ret_handle_ptr = reinterpret_cast<CloudWatchSdkMetricsHandle>(class_ptr);

    return rs;
}

CdiReturnStatus MetricsGathererDestroy(CloudWatchSdkMetricsHandle handle)
{
    // These two functions do exactly the same thing.
    return CloudWatchSdkMetricsDestroy(handle);
}
#endif  // METRICS_GATHERING_SERVICE_ENABLED

CdiReturnStatus MetricsSend(CloudWatchSdkMetricsHandle handle, const CloudWatchTransferStats* transfer_stats_ptr)
{
    CdiReturnStatus rs = kCdiStatusOk;

    MetricsClass* class_ptr = reinterpret_cast<MetricsClass*>(handle);
    if (nullptr == class_ptr) {
        rs = kCdiStatusInvalidHandle;
    } else {
        rs = class_ptr->Send(transfer_stats_ptr);
    }

    return rs;
}

#else // CLOUDWATCH_METRICS_ENABLED
#pragma message("Building with CloudWatch disabled.")
#endif // CLOUDWATCH_METRICS_ENABLED
