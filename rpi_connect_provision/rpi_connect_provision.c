/**
 * Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* On-device provisioning for Raspberry Pi Connect: the provisioning subset of
 * the host tool's operations (main.c), run on the board itself so the OTP
 * identity key is never handled on a host.
 *
 * The operation and its inputs are binary info pointer variables in the
 * "provision" feature group, set with `picotool config` before the image is
 * loaded:
 *
 *   operation          what to do (default create_identity):
 *     create_identity    register the OTP identity key with an organisation
 *                        (host --create-device-identity), and clear any stored
 *                        access token so the new identity is used; needs
 *                        org_token
 *     identity_exchange  exchange the registered OTP identity for an access
 *                        token and cache it in FFS (host
 *                        --device-identity-exchange); checks the registration
 *     auth_key           exchange a provisioning auth key for an access token
 *                        and store it in FFS (host --auth); needs auth_key
 *     signin             device-code signin: prints a code to enter in a
 *                        browser, then stores the access token in FFS (host
 *                        --signin)
 *     store_token        store the given access token in FFS (host --token)
 *     clear              delete the stored token and any deployment state
 *     wifi               only store the WiFi credentials below
 *   org_token          organisation token, for create_identity
 *   auth_key           provisioning auth key, for auth_key
 *   token              access token, for store_token
 *   description        identity description, for create_identity
 *   device_name        device name (default pico-<board id>)
 *   client_id          Connect client UUID (default the library's)
 *   wifi_ssid          if set, stored in FFS along with wifi_password
 *   wifi_password        before the operation runs
 *
 * An operation fails if the secret it needs (org_token, auth_key or token) is
 * not set.
 *
 * Console output goes to USB and to the UART in the "uart_config" group:
 *   uart               UART instance, or -1 for no UART (default the board's)
 *   uart_tx, uart_rx   UART pins, or -1 for none (default the board's)
 *   uart_baud          UART baud rate (default 115200)
 *
 * The wireless chip's pins are in the "cyw43_config" group (default the
 * board's): wl_reg_on, wl_data_out, wl_data_in, wl_host_wake, wl_clock, wl_cs
 *
 * When done, the LED in the "led_config" group flashes - slowly on success,
 * quickly on failure - and then the board reboots to USB boot, ready for the
 * demo image to be flashed:
 *   led                GPIO of the LED, or -1 for the board's default LED
 *                      (on the wireless chip for W boards)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_util.h"

#include "pico/binary_info.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"
#include "pico/ffs.h"
#include "pico/rpi_connect.h"
#include "pico/rpi_connect_identity.h"
#include "pico/rpi_connect_ota.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

// Longest organisation token that survives being formatted into the API's
// "Authorization: Bearer %s" header (a 256-byte buffer in rpi_connect.c).
#define ORG_TOKEN_SIZE      234
// Access tokens are used in the same header
#define TOKEN_SIZE          ORG_TOKEN_SIZE
#define AUTH_KEY_SIZE       128
#define OPERATION_SIZE      32
#define DESCRIPTION_SIZE    64
#define DEVICE_NAME_SIZE    64
#define CLIENT_ID_SIZE      40
#define WIFI_SSID_SIZE      33
#define WIFI_PASSWORD_SIZE  65

// LED flashing when done: half-period for success and failure, and how long
#define LED_SUCCESS_HALF_PERIOD_MS  500
#define LED_FAILURE_HALF_PERIOD_MS  100
#define LED_FLASH_MS                5000

// Device-code signin: how often to poll for the token, and for how long
#define SIGNIN_POLL_MS      3000
// How often to repeat the code and URL, for a console connected after the start
#define SIGNIN_REPRINT_MS   15000
#define SIGNIN_TIMEOUT_MS   (15 * 60 * 1000)

bi_decl(bi_program_feature_group(0x1111, 0x2222, "provision"));
bi_decl(bi_ptr_string(0x1111, 0x2222, operation, "create_identity", OPERATION_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, org_token, "", ORG_TOKEN_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, auth_key, "", AUTH_KEY_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, token, "", TOKEN_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, description, "Pico 2 W OTA demo", DESCRIPTION_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, device_name, "", DEVICE_NAME_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, client_id, "", CLIENT_ID_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, wifi_ssid, "", WIFI_SSID_SIZE));
bi_decl(bi_ptr_string(0x1111, 0x2222, wifi_password, "", WIFI_PASSWORD_SIZE));

bi_decl(bi_program_feature_group(0x1234, 0x5679, "uart_config"));
bi_decl(bi_ptr_int32(0x1234, 0x5679, uart, PICO_DEFAULT_UART));
bi_decl(bi_ptr_int32(0x1234, 0x5679, uart_tx, PICO_DEFAULT_UART_TX_PIN));
bi_decl(bi_ptr_int32(0x1234, 0x5679, uart_rx, PICO_DEFAULT_UART_RX_PIN));
bi_decl(bi_ptr_int32(0x1234, 0x5679, uart_baud, PICO_DEFAULT_UART_BAUD_RATE));

bi_decl(bi_program_feature_group(0x1234, 0x567a, "cyw43_config"));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_reg_on, CYW43_DEFAULT_PIN_WL_REG_ON));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_data_out, CYW43_DEFAULT_PIN_WL_DATA_OUT));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_data_in, CYW43_DEFAULT_PIN_WL_DATA_IN));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_host_wake, CYW43_DEFAULT_PIN_WL_HOST_WAKE));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_clock, CYW43_DEFAULT_PIN_WL_CLOCK));
bi_decl(bi_ptr_int32(0x1234, 0x567a, wl_cs, CYW43_DEFAULT_PIN_WL_CS));

// Same group and key as xip_ram_perms, so picotool can set either the same way
bi_decl(bi_program_feature_group(0x1234, 0x5678, "led_config"));
bi_decl(bi_ptr_int32(0x1234, 0x5678, led, -1));

typedef enum {
    OP_CREATE_IDENTITY,
    OP_IDENTITY_EXCHANGE,
    OP_AUTH_KEY,
    OP_SIGNIN,
    OP_STORE_TOKEN,
    OP_CLEAR,
    OP_WIFI,
    OP_COUNT
} provision_op_t;

static const char *const op_names[OP_COUNT] = {
    [OP_CREATE_IDENTITY]   = "create_identity",
    [OP_IDENTITY_EXCHANGE] = "identity_exchange",
    [OP_AUTH_KEY]          = "auth_key",
    [OP_SIGNIN]            = "signin",
    [OP_STORE_TOKEN]       = "store_token",
    [OP_CLEAR]             = "clear",
    [OP_WIFI]              = "wifi",
};

static char serial_number[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
static bool cyw43_initialised;

async_context_t *rpi_connect_default_async_context(void) {
    return cyw43_arch_async_context();
}

// Secrets are only ever supplied via binary info
static int require_secret(const char *name, const char *value) {
    if (!*value) {
        printf("No %s given\n", name);
        return -1;
    }
    return 0;
}

static bool op_needs_network(provision_op_t op) {
    return op == OP_CREATE_IDENTITY || op == OP_IDENTITY_EXCHANGE ||
           op == OP_AUTH_KEY || op == OP_SIGNIN;
}

static int store_wifi_credentials(void) {
    int rc = ffs_update_string(FFS_WIFI_SSID_FILE_ID, wifi_ssid);
    if (rc == PICO_OK) {
        rc = ffs_update_string(FFS_WIFI_PASSWORD_FILE_ID, wifi_password);
    }
    if (rc != PICO_OK) {
        printf("Failed to store WiFi credentials (%d)\n", rc);
        return -1;
    }
    printf("Stored WiFi credentials for %s\n", wifi_ssid);
    return 0;
}

static int connect_wifi(void) {
    char *ssid = ffs_get_string(FFS_WIFI_SSID_FILE_ID);
    char *password = ffs_get_string(FFS_WIFI_PASSWORD_FILE_ID);
    int rc = -1;
    int wifi_rc;
    if (!ssid || !*ssid || !password) {
        printf("No WiFi credentials in ffs\n");
        goto end;
    }

    printf("Connecting to WiFi %s\n", ssid);
    cyw43_arch_enable_sta_mode();
    wifi_rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (wifi_rc) {
        printf("Failed to connect to WiFi (%d)\n", wifi_rc);
        goto end;
    }
    rpi_connect_set_async_context(rpi_connect_default_async_context());

    // Signed requests carry X-Connect-Timestamp when the time is known; it is
    // optional, so failure here is not fatal.
    if (rpi_connect_update_time() <= 0) {
        printf("Failed to get server time\n");
    }
    rc = 0;
end:
    free(ssid);
    free(password);
    return rc;
}

static int store_token(const char *new_token) {
    if (rpi_connect_ota_store_auth_token(new_token) != 0) {
        printf("Failed to store access token\n");
        return -1;
    }
    printf("Stored access token\n");
    return 0;
}

// Size of the raw P-256 private key written to OTP
#define IDENTITY_PRIVKEY_SIZE 32

// Make sure there is an identity key in OTP, generating one if create_if_empty is set.
// Generating the key is the only time the private key is handled here; afterwards it is
// only used via the rpi_connect_identity functions.
static int ensure_identity_key(bool create_if_empty) {
    if (rpi_connect_identity_key_available()) {
        return 0;
    }

    if (!create_if_empty) {
        printf("No identity key in OTP - exiting\n");
        return -1;
    }

    printf("No identity key in OTP - generating one\n");

    unsigned char privkey[IDENTITY_PRIVKEY_SIZE];
    mbedtls_ecp_keypair key;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    mbedtls_ecp_keypair_init(&key);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) {
        printf("Seeding entropy failed -  mbedtls_ctr_drbg_seed returned %d\n", ret);
        goto cleanup;
    }
    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &key, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        printf("Generating key failed - mbedtls_ecp_gen_key returned %d\n", ret);
        goto cleanup;
    }

    ret = mbedtls_ecp_write_key(&key, privkey, sizeof(privkey));
    if (ret != 0) {
        printf("Reading key failed - mbedtls_ecp_write_key returned %d\n", ret);
        goto cleanup;
    }

    printf("Private key generated - writing to OTP\n");
    otp_cmd_t cmd;
    cmd.flags = RPI_CONNECT_IDENTITY_OTP_ROW | OTP_CMD_ECC_BITS | OTP_CMD_WRITE_BITS;
    ret = rom_func_otp_access(privkey, sizeof(privkey), cmd);
    if (ret) {
        printf("ECC Write failed - rom_func_otp_access returned %d\n", ret);
    }

cleanup:
    mbedtls_platform_zeroize(privkey, sizeof(privkey));
    mbedtls_ecp_keypair_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (ret != 0) {
        return ret;
    }

    // Check the key can now be read back out of OTP
    if (!rpi_connect_identity_key_available()) {
        printf("Identity key not found in OTP after writing it\n");
        return -1;
    }
    return 0;
}

static int create_identity(void) {
    if (ensure_identity_key(true)) {
        return -1;
    }

    // This registers the public key of the OTP identity key, which makes this
    // identity, by construction, one the device can later prove it owns.
    char *id = rpi_connect_create_device_identity(org_token, description, device_name);
    if (!id) {
        printf("Failed to create device identity\n");
        return -1;
    }
    printf("Created device identity %s for %s\n", id, device_name);
    free(id);

    // A stored token takes priority over the identity in rpi_connect_ota_init,
    // so remove any old one for the new identity to be used
    int rc = ffs_delete(FFS_AUTH_TOKEN_FILE_ID);
    if (rc != PICO_OK && rc != PICO_ERROR_NOT_FOUND) {
        printf("Failed to clear stored access token (%d)\n", rc);
        return -1;
    }
    return 0;
}

static int identity_exchange(void) {
    if (ensure_identity_key(false)) {
        return -1;
    }

    char *device_id = NULL;
    char *new_token = rpi_connect_device_identity_exchange(
        client_id, device_name, serial_number, &device_id);
    if (!new_token) {
        printf("Device identity exchange failed\n");
        free(device_id);
        return -1;
    }
    printf("Device identity exchanged for device %s\n", device_id ? device_id : "(null)");
    int rc = store_token(new_token);
    free(new_token);
    free(device_id);
    return rc;
}

static int auth_key_exchange(void) {
    char *new_token = rpi_connect_handle_auth_key(auth_key, serial_number, device_name, client_id);
    if (!new_token) {
        printf("Auth key exchange failed\n");
        return -1;
    }
    int rc = store_token(new_token);
    free(new_token);
    return rc;
}

static int signin(void) {
    t_rpi_connect_signin *codes = rpi_connect_signin(client_id, serial_number);
    if (!codes) {
        printf("Failed to get signin information\n");
        return -1;
    }

    int rc = -1;
    absolute_time_t timeout = make_timeout_time_ms(SIGNIN_TIMEOUT_MS);
    absolute_time_t next_print = get_absolute_time();
    while (!time_reached(timeout)) {
        if (time_reached(next_print)) {
            printf("Visit %s in your browser and enter the user code %s (%lld minutes left)\n",
                   codes->verification_uri_complete, codes->user_code,
                   (absolute_time_diff_us(get_absolute_time(), timeout) / 60000000) + 1);
            stdio_flush();
            next_print = make_timeout_time_ms(SIGNIN_REPRINT_MS);
        }
        char *new_token = rpi_connect_retrieve_token_with_device_code(
            client_id, codes->device_code, serial_number);
        if (new_token) {
            printf("Access token received\n");
            rc = store_token(new_token);
            free(new_token);
            break;
        }
        sleep_ms(SIGNIN_POLL_MS);
    }
    if (rc && time_reached(timeout)) {
        printf("Timed out waiting for signin\n");
    }
    rpi_connect_signin_cleanup(codes);
    return rc;
}

static int clear_state(void) {
    // Deleting an absent file is not an error worth reporting
    static const uint8_t file_ids[] = {
        FFS_AUTH_TOKEN_FILE_ID,
        FFS_DEPLOYMENT_ID_FILE_ID,
        FFS_DEPLOYMENT_URI_FILE_ID,
        FFS_DEPLOYMENT_CHECKSUM_FILE_ID,
        FFS_DEPLOYMENT_STATUS_FILE_ID,
    };
    for (size_t i = 0; i < count_of(file_ids); i++) {
        ffs_delete(file_ids[i]);
    }
    printf("Cleared stored token and deployment state\n");
    return 0;
}

static int init_cyw43(void) {
    if (!cyw43_initialised) {
        // Needs CYW43_PIN_WL_DYNAMIC, and must be set before the driver starts
        uint pins[CYW43_PIN_INDEX_WL_COUNT] = {
            [CYW43_PIN_INDEX_WL_REG_ON]    = wl_reg_on,
            [CYW43_PIN_INDEX_WL_DATA_OUT]  = wl_data_out,
            [CYW43_PIN_INDEX_WL_DATA_IN]   = wl_data_in,
            [CYW43_PIN_INDEX_WL_HOST_WAKE] = wl_host_wake,
            [CYW43_PIN_INDEX_WL_CLOCK]     = wl_clock,
            [CYW43_PIN_INDEX_WL_CS]        = wl_cs,
        };
        int rc = cyw43_set_pins_wl(pins);
        if (rc != PICO_OK) {
            printf("Invalid cyw43 pins (%d)\n", rc);
            return -1;
        }
        rc = cyw43_arch_init();
        if (rc) {
            printf("Failed to initialise cyw43 (%d)\n", rc);
            return -1;
        }
        cyw43_initialised = true;
    }
    return 0;
}

static void init_stdio(void) {
    stdio_usb_init();
    if (uart >= 0 && uart < NUM_UARTS) {
        stdio_uart_init_full(uart_get_instance(uart), uart_baud, uart_tx, uart_rx);
    }
}

static bool led_is_wireless(void) {
#if defined(CYW43_WL_GPIO_LED_PIN)
    return led < 0;
#else
    return false;
#endif
}

static void led_put(bool on) {
#if defined(CYW43_WL_GPIO_LED_PIN)
    if (led_is_wireless()) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        return;
    }
#endif
    gpio_put(led, on);
}

// Flash the LED for a while to show the operation has finished
static void flash_led(bool success) {
#if !defined(CYW43_WL_GPIO_LED_PIN) && defined(PICO_DEFAULT_LED_PIN)
    if (led < 0) {
        led = PICO_DEFAULT_LED_PIN;
    }
#endif
    if (led_is_wireless()) {
        // The wireless chip may not have been needed for the operation
        if (init_cyw43()) {
            return;
        }
    } else if (led >= 0 && led < NUM_BANK0_GPIOS) {
        gpio_init(led);
        gpio_set_dir(led, GPIO_OUT);
    } else {
        return;
    }

    uint32_t half_period_ms = success ? LED_SUCCESS_HALF_PERIOD_MS : LED_FAILURE_HALF_PERIOD_MS;
    for (uint32_t ms = 0; ms < LED_FLASH_MS; ms += 2 * half_period_ms) {
        led_put(true);
        sleep_ms(half_period_ms);
        led_put(false);
        sleep_ms(half_period_ms);
    }
}

static int provision(void) {
    provision_op_t op;
    for (op = 0; op < OP_COUNT; op++) {
        if (strcmp(operation, op_names[op]) == 0) break;
    }
    if (op == OP_COUNT) {
        printf("Unknown operation '%s'\n", operation);
        return 1;
    }
    printf("Operation: %s\n", op_names[op]);

    pico_get_unique_board_id_string(serial_number, sizeof(serial_number));
    if (!*device_name) {
        snprintf(device_name, sizeof(device_name), "pico-%s", serial_number);
    }
    if (!*client_id) {
        snprintf(client_id, sizeof(client_id), "%s", rpi_connect_client_id());
    }

    int ffs_rc = ffs_initialise();
    if (ffs_rc != PICO_OK) {
        printf("Failed to initialise ffs (%d)\n", ffs_rc);
        return 1;
    }

    if (*wifi_ssid && store_wifi_credentials()) {
        return 1;
    } else if (op == OP_WIFI && !*wifi_ssid) {
        printf("No WiFi credentials given\n");
        return 1;
    }

    // Check for missing secrets before the slow network setup
    if ((op == OP_CREATE_IDENTITY && require_secret("org_token", org_token)) ||
        (op == OP_AUTH_KEY && require_secret("auth_key", auth_key)) ||
        (op == OP_STORE_TOKEN && require_secret("token", token))) {
        return 1;
    }

    if (op_needs_network(op)) {
        if (init_cyw43() || connect_wifi()) {
            return 1;
        }
    }

    int rc;
    switch (op) {
        case OP_CREATE_IDENTITY:   rc = create_identity(); break;
        case OP_IDENTITY_EXCHANGE: rc = identity_exchange(); break;
        case OP_AUTH_KEY:          rc = auth_key_exchange(); break;
        case OP_SIGNIN:            rc = signin(); break;
        case OP_STORE_TOKEN:       rc = store_token(token); break;
        case OP_CLEAR:             rc = clear_state(); break;
        case OP_WIFI:
        default:                   rc = 0; break;
    }
    return rc ? 1 : 0;
}

// Work area for the bootrom's partition table load (needs at least 3264 bytes)
#define PT_WORKAREA_SIZE 0x1000

int main() {
    init_stdio();
    rom_connect_internal_flash();
    rom_flash_exit_xip();
    rom_flash_enter_cmd_xip();

    // Only needed for the load, so freed straight after (malloc is 8-byte aligned)
    uint8_t *workarea = malloc(PT_WORKAREA_SIZE);
    int rc = workarea ? rom_load_partition_table(workarea, PT_WORKAREA_SIZE, false) : PICO_ERROR_INSUFFICIENT_RESOURCES;
    free(workarea);
    if (rc) printf("Failed to load partition table (%d)\n", rc);

    rc = provision();
    printf("Provisioning %s\n", rc ? "failed" : "succeeded");
    flash_led(rc == 0);

    // Hand the board back in BOOTSEL, ready for the demo image to be flashed.
    printf("Rebooting to USB boot\n");
    stdio_flush();

    reset_usb_boot(0, 0);
}
