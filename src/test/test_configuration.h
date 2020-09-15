// ---------------------------------------------------------------------------
// Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
// ---------------------------------------------------------------------------

/**
 * @file
 * @brief
 * This header file contains definitions used to define the build configuration of the CDI SDK's test..
 */

#ifndef CDI_TEST_CONFIGURATION_H__
#define CDI_TEST_CONFIGURATION_H__

//*********************************************************************************************************************
//***************************************** IMPLEMENTATION VARIANTS AND OPTION ****************************************
//*********************************************************************************************************************

/// @brief Option to increase or decrease transmitter single payload timeout time.
#define TX_PAYLOAD_TIMEOUT_FACTOR       (100)

/// @brief Option to increase or decrease transmitter test timeout time.
/// This number must be large enough to compensate for payloads that may be in flight when test completes.
#define TX_ALL_DONE_TIMEOUT_FACTOR      (32)

/// @brief How often to refresh transfer stats on the console in seconds.
#define REFRESH_STATS_PERIOD_SECONDS    (1)

/// @brief If the Tx queue is full in CDI, this is the amount of time the rate_period_microseconds is divided by in
/// order to determine how long to wait before trying to resend the payload again.
#define TX_QUEUE_FULL_RATE_PERIOD_SLEEP_DIVISOR (100)

/// @brief How much time to wait for a connection to be made (use 10 minutes for now).
#define CONNECTION_WAIT_TIMEOUT_SECONDS (10*60)

/// @brief How much time to wait between tests if running in loop mode.
#define MAIN_TEST_LOOP_WAIT_TIMEOUT_MS  (1000*1)

/// @brief Enables a statistics gathering reconfiguration test that is defined in test_dynamic.c. When enabled, it uses
/// the configured statistics settings generated from command line options to dynamically make changes to the settings
/// and apply them.
//#define ENABLE_TEST_INTERNAL_CORE_STATS_RECONFIGURE

//*********************************************************************************************************************
//********************************************* FEATURES TO AID DEBUGGING *********************************************
//*********************************************************************************************************************

/// @brief Enable the line below to disable the rate timeout and use a sleep function to control the payload transfer
/// rate. Disabling the timeout allows the use of breakpoints to aid in debugging other parts of the system.
/// NOTE: This should normally NOT be enabled in GIT.
//#define DISABLE_RATE_TIMEOUT_FOR_DEBUG

//*********************************************************************************************************************
//******************************************* MAX SIZES FOR STATIC DATA/ARRAYS ****************************************
//*********************************************************************************************************************

/// @brief Maximum number SGL packets in test
#define TEST_TX_SGL_PACKET_ENTRIES      (10)

/// @brief Maximum number SGL packets buffer in test may be increased by.
#define TEST_TX_SGL_PACKET_ENTRIES_GROW (2)

/// @brief Number of times a memory pool may increase before an error occurs.
#define TEST_MAX_POOL_GROW_COUNT        (5)

#endif // CDI_TEST_CONFIGURATION_H__
