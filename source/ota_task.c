/******************************************************************************
* File Name: ota_task.c
*
* Description: This file contains task and functions related to OTA operation.
*
*******************************************************************************
* (c) 2020, Cypress Semiconductor Corporation. All rights reserved.
*******************************************************************************
* This software, including source code, documentation and related materials
* ("Software"), is owned by Cypress Semiconductor Corporation or one of its
* subsidiaries ("Cypress") and is protected by and subject to worldwide patent
* protection (United States and foreign), United States copyright laws and
* international treaty provisions. Therefore, you may use this Software only
* as provided in the license agreement accompanying the software package from
* which you obtained this Software ("EULA").
*
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software source
* code solely for use in connection with Cypress's integrated circuit products.
* Any reproduction, modification, translation, compilation, or representation
* of this Software except as specified above is prohibited without the express
* written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer of such
* system or application assumes all risk of such use and in doing so agrees to
* indemnify Cypress against all liability.
*******************************************************************************/

/* Header file includes */
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"

/* IoT SDK, Secure Sockets, and MQTT initialization */
#include "iot_init.h"
#include "cy_iot_network_secured_socket.h"
#include "iot_mqtt.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

/* OTA API */
#include "cy_ota_api.h"

/* App specific configuration */
#include "ota_app_config.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* MAX connection retries to join WI-FI AP */
#define MAX_CONNECTION_RETRIES              (10u)

/* Wait between connection retries */
#define WIFI_CONN_RETRY_DELAY_MS            (500)

/*******************************************************************************
* Forward declaration
********************************************************************************/
cy_rslt_t connect_to_wifi_ap(void);
void ota_callback(cy_ota_cb_reason_t reason, uint32_t value, void *cb_arg );

/*******************************************************************************
* Global Variables
********************************************************************************/
/* OTA context */
cy_ota_context_ptr ota_context;

/* MQTT Credentials for OTA */
struct IotNetworkCredentials credentials =
{
    .pRootCa = ROOT_CA_CERTIFICATE,
    .rootCaSize = sizeof(ROOT_CA_CERTIFICATE),
    .pClientCert = CLIENT_CERTIFICATE,
    .clientCertSize = sizeof(CLIENT_CERTIFICATE),
    .pPrivateKey = CLIENT_KEY,
    .privateKeySize = sizeof(CLIENT_KEY),
};

/* Network parameters for OTA */
cy_ota_network_params_t ota_network_params =
{
    .server =
    {
        .pHostName = MQTT_BROKER_URL,
        .port = MQTT_SERVER_PORT
    },
    .transport = CY_OTA_TRANSPORT_MQTT,
    .u.mqtt =
    {
        .pTopicFilters = my_topics,
        .numTopicFilters = MQTT_TOPIC_FILTER_NUM,
        .pIdentifier = OTA_MQTT_ID,
        .awsIotMqttMode = AWS_IOT_MQTT_MODE
    },
#if (ENABLE_TLS == true)
    .credentials = &credentials
#else
    .credentials = NULL
#endif
};

/* Parameters for OTA agent */
cy_ota_agent_params_t ota_agent_params =
{
    .cb_func = ota_callback,
    .cb_arg = &ota_context,
    .reboot_upon_completion = 1,
};

/*******************************************************************************
 * Function Name: ota_task
 *******************************************************************************
 * Summary:
 *  Task to initialize required libraries and start OTA agent.
 *
 * Parameters:
 *  void *args : Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void ota_task(void *args)
{
    /* Connect to Wi-Fi AP */
    if( connect_to_wifi_ap() != CY_RSLT_SUCCESS )
    {
        printf("\n Failed to connect to Wi-FI AP.\n");
        CY_ASSERT(0);
    }

    /* Initialize the underlying support code that is needed for OTA and MQTT */
    if ( !IotSdk_Init() )
    {
        printf("\n IotSdk_Init Failed.\n");
        CY_ASSERT(0);
    }

    /* Call the Network Secured Sockets initialization function. */
    if( IotNetworkSecureSockets_Init() != IOT_NETWORK_SUCCESS )
    {
        printf("\n IotNetworkSecureSockets_Init Failed.\n");
        CY_ASSERT(0);
    }

    /* Initialize the MQTT subsystem */
    if( IotMqtt_Init() != IOT_MQTT_SUCCESS )
    {
        printf("\n IotMqtt_Init Failed.\n");
        CY_ASSERT(0);
    }

    /* Add the network interface to the OTA network parameters */
    ota_network_params.network_interface = (void *)IOT_NETWORK_INTERFACE_CY_SECURE_SOCKETS;

    /* Initialize and start the OTA agent */
    if( cy_ota_agent_start(&ota_network_params, &ota_agent_params, &ota_context) != CY_RSLT_SUCCESS )
    {
        printf("\n Initializing and starting the OTA agent failed.\n");
        CY_ASSERT(0);
    }

    vTaskSuspend( NULL );
 }

/*******************************************************************************
 * Function Name: connect_to_wifi_ap()
 *******************************************************************************
 * Summary:
 *  Connects to Wi-Fi AP using the user-configured credentials, retries up to a
 *  configured number of times until the connection succeeds.
 *
 *******************************************************************************/
cy_rslt_t connect_to_wifi_ap(void)
{
    cy_wcm_config_t wifi_config = { .interface = CY_WCM_INTERFACE_TYPE_STA};
    cy_wcm_connect_params_t wifi_conn_param;
    cy_wcm_ip_address_t ip_address;
    cy_rslt_t result;

    /* Variable to track the number of connection retries to the Wi-Fi AP specified
     * by WIFI_SSID macro. */
    uint32_t conn_retries = 0;

    /* Initialize Wi-Fi connection manager. */
    cy_wcm_init(&wifi_config);

     /* Set the Wi-Fi SSID, password and security type. */
    memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
    memcpy(wifi_conn_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_conn_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifi_conn_param.ap_credentials.security = WIFI_SECURITY;

    /* Connect to the Wi-Fi AP */
    for(conn_retries = 0; conn_retries < MAX_CONNECTION_RETRIES; conn_retries++)
    {
        result = cy_wcm_connect_ap( &wifi_conn_param, &ip_address );

        if (result == CY_RSLT_SUCCESS)
        {
            printf( "Successfully connected to Wi-Fi network '%s'.\n",
                    wifi_conn_param.ap_credentials.SSID);
            return result;
        }

        printf( "Connection to Wi-Fi network failed with error code %d."
                "Retrying in %d ms...\n", (int) result, WIFI_CONN_RETRY_DELAY_MS );
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_DELAY_MS));
    }

    printf( "Exceeded maximum Wi-Fi connection attempts\n" );

    return result;
}

/*******************************************************************************
 * Function Name: ota_callback()
 *******************************************************************************
 * Summary:
 *  Print the status of the OTA agent on every event. This callback is optional,
 *  but be aware that the OTA middleware will not print the status of OTA agent
 *  on its own.
 *
 *******************************************************************************/
void ota_callback(cy_ota_cb_reason_t reason, uint32_t value, void *cb_arg )
{
    cy_ota_agent_state_t ota_state;
    cy_ota_context_ptr ctx = *((cy_ota_context_ptr *)cb_arg);
    cy_ota_get_state(ctx, &ota_state);
    printf("Application OTA callback ctx:%p reason:%d %s value:%ld state:%d %s %s\n",
            ctx,
            reason,
            cy_ota_get_callback_reason_string(reason),
            value,
            ota_state,
            cy_ota_get_state_string(ota_state),
            cy_ota_get_error_string(cy_ota_last_error()));
}
