// -------------------------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// This file is part of the AWS CDI-SDK, licensed under the BSD 2-Clause "Simplified" License.
// License details at: https://github.com/aws/aws-cdi-sdk/blob/mainline/LICENSE
// -------------------------------------------------------------------------------------------

#ifndef CDI_LOG_ENUMS_H__
#define CDI_LOG_ENUMS_H__

/**
 * @file
 * @brief
 * This file declares the public enum data types that are part of the CDI log API.
 */

//*********************************************************************************************************************
//***************************************** START OF DEFINITIONS AND TYPES ********************************************
//*********************************************************************************************************************

/**
 * @brief This selector determines the log method to use for generating log messages within the SDK.
 * NOTE: Any changes made here MUST also be made to "log_method_key_array" in utilities_api.c.
 */
typedef enum {
    kLogMethodStdout,     ///< Send log messages directly to stdout.
    kLogMethodCallback,   ///< Send log messages to a user-registered callback function.
    kLogMethodFile,       ///< Write log messages directly to a file.
} CdiLogMethod;

/**
 * @brief This selector determines the SDK component type for logging. Logging for it can be enabled/disabled using
 *        CdiCoreLogComponentEnable().
 * NOTE: Any changes made here MUST also be made to "log_component_key_array" in utilities_api.c.
 */
typedef enum {
    kLogComponentGeneric,            ///< Generic component
    kLogComponentPayloadConfig,      ///< Payload configuration data component
    kLogComponentPerformanceMetrics, ///< Performance metrics component
    kLogComponentProbe,              ///< Probe component
    kLogComponentEndpointManager,    ///< Endpoint Manager component

    kLogComponentLast                ///< Must be last entry. Used for range checking. Do not remove.
} CdiLogComponent;

/**
 * @brief This selector determines the log level of messages generated using the CdiLogMessageCallback(). The log level
 *        can be set individually for log components using CdiCoreLogLevelSet().
 * NOTE: Any changes made here MUST also be made to "log_level_key_array" in utilities_api.c.
 */
typedef enum {
    kLogFatal,       ///< Fatal errors are not recoverable. Software needs to exit.
    kLogCritical,    ///< Critical errors are logged. Software may continue but something is very wrong.
    kLogError,       ///< Errors to the user.
    kLogWarning,     ///< Warnings to the user.
    kLogInfo,        ///< General information to the user.
    kLogVerbose,     ///< Additional verbose information to the user.
    kLogDebug,       ///< Debug information to the user.

    kLogLast,        ///< Must be last entry. Used for range checking. Do not remove.
} CdiLogLevel;

#endif // CDI_LOG_ENUMS_H__

