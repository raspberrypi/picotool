/**
 * Copyright (c) 2025 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rpi_connect_api.h"

#if HAS_MBEDTLS

#include <chrono>
#include <cstring>
#include <thread>

#include "errors.h"
#include "nlohmann/json.hpp"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

using json = nlohmann::json;
using std::string;

// ISRG Root X1 (RSA, valid until 2035-06-04) and ISRG Root X2 (ECC, valid until
// 2040-09-17), from the SDK's pico_rpi_connect: the Connect API's certificates
// chain to one of these, so the host's certificate store is not needed
static const char connect_ca_certs[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n"
"-----BEGIN CERTIFICATE-----\n"
"MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
"CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
"R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
"MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
"ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
"EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
"+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
"ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
"AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
"zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
"tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
"/q4AaOeMSQ+2b1tbFfLn\n"
"-----END CERTIFICATE-----\n"
;

namespace rpi_connect_api {

const char *const default_client_id = "96BE5607-EDF9-4403-8D49-31D17A99FAF0";

static const char *const api_host = "api.connect.raspberrypi.com";
static const uint32_t read_timeout_ms = 30000;

static string mbedtls_error(int rc) {
    char buf[128];
    mbedtls_strerror(rc, buf, sizeof(buf));
    return buf;
}

static string hex(const unsigned char *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    string s;
    for (size_t i = 0; i < len; i++) {
        s += digits[data[i] >> 4];
        s += digits[data[i] & 0xf];
    }
    return s;
}

static string sha256_hex(const string &data) {
    unsigned char hash[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const unsigned char *)data.data(), data.size(), hash);
    return hex(hash, sizeof(hash));
}

// X-Connect-Signature, as rpi_calculate_signature in the SDK computes it for a
// POST with only a Content-Type header: an HMAC-SHA256 of the method, URL,
// header and body hash, keyed with the hex SHA-256 of the serial number
static string signature(const string &url, const string &content_type_header, const string &body,
                        const string &serial_number) {
    string msg = "POST\n" + url + "\n" + content_type_header + "\n" + sha256_hex(body);
    string key = sha256_hex(serial_number);
    unsigned char mac[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const unsigned char *)key.data(), key.size(),
                    (const unsigned char *)msg.data(), msg.size(), mac);
    return hex(mac, sizeof(mac));
}

struct response {
    int status;
    json body;
};

// Decode a chunked transfer-encoded body
static string dechunk(const string &in) {
    string out;
    size_t pos = 0;
    while (pos < in.size()) {
        size_t eol = in.find("\r\n", pos);
        if (eol == string::npos) break;
        size_t len = std::stoul(in.substr(pos, eol - pos), nullptr, 16);
        if (len == 0) break;
        out += in.substr(eol + 2, len);
        pos = eol + 2 + len + 2;
    }
    return out;
}

// POST a JSON body over HTTPS, with the server verified against the pinned CAs
static response post_json(const string &endpoint, const json &body, const string &serial_number = "") {
    string data = body.dump();
    string content_type = "Content-Type: application/json";
    string headers = content_type + "\r\n";
    if (!serial_number.empty()) {
        string url = string("https://") + api_host + endpoint;
        headers += "X-Connect-Signature: " + signature(url, content_type, data, serial_number) + "\r\n";
    }
    string request = "POST " + endpoint + " HTTP/1.1\r\n"
                     "Host: " + api_host + "\r\n"
                     "Connection: close\r\n" +
                     headers +
                     "Content-Length: " + std::to_string(data.size()) + "\r\n"
                     "\r\n" + data;

    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&ca);

    string raw;
    string err;
    int rc;
    if ((rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0))) {
        err = "seeding random number generator";
    } else if ((rc = mbedtls_x509_crt_parse(&ca, (const unsigned char *)connect_ca_certs, sizeof(connect_ca_certs)))) {
        err = "parsing CA certificates";
    } else if ((rc = mbedtls_net_connect(&net, api_host, "443", MBEDTLS_NET_PROTO_TCP))) {
        err = string("connecting to ") + api_host;
    } else if ((rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                 MBEDTLS_SSL_PRESET_DEFAULT))) {
        err = "configuring TLS";
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &ca, nullptr);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        mbedtls_ssl_conf_read_timeout(&conf, read_timeout_ms);
        if ((rc = mbedtls_ssl_setup(&ssl, &conf)) || (rc = mbedtls_ssl_set_hostname(&ssl, api_host))) {
            err = "configuring TLS";
        } else {
            mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
            while ((rc = mbedtls_ssl_handshake(&ssl)) == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {}
            if (rc) {
                err = string("TLS handshake with ") + api_host;
            }
        }
    }
    for (size_t off = 0; !rc && off < request.size(); ) {
        rc = mbedtls_ssl_write(&ssl, (const unsigned char *)request.data() + off, request.size() - off);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            rc = 0;
        } else if (rc < 0) {
            err = "sending request";
        } else {
            off += rc;
            rc = 0;
        }
    }
    while (!rc) {
        unsigned char buf[1024];
        int n = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
        if (n > 0) {
            raw.append((const char *)buf, n);
        } else if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        } else {
            // The server closes the connection after the response
            if (n != 0 && n != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY && n != MBEDTLS_ERR_NET_CONN_RESET) {
                rc = n;
                err = "reading response";
            }
            break;
        }
    }
    if (!rc) mbedtls_ssl_close_notify(&ssl);

    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&ca);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);

    if (rc) {
        fail(ERROR_CONNECTION, "Raspberry Pi Connect API request failed: %s: %s", err.c_str(), mbedtls_error(rc).c_str());
    }

    size_t header_end = raw.find("\r\n\r\n");
    int status = 0;
    if (header_end == string::npos || sscanf(raw.c_str(), "HTTP/%*s %d", &status) != 1) {
        fail(ERROR_CONNECTION, "Invalid response from the Raspberry Pi Connect API");
    }
    string head = raw.substr(0, header_end);
    for (auto &c : head) c = (char)tolower((unsigned char)c);
    string body_text = raw.substr(header_end + 4);
    if (head.find("\r\ntransfer-encoding: chunked") != string::npos) {
        body_text = dechunk(body_text);
    }
    return {status, json::parse(body_text, nullptr, false)};
}

static string get_string(const json &j, const char *key) {
    if (j.is_object() && j.contains(key) && j[key].is_string()) return j[key];
    return "";
}

static string describe_error(const response &r) {
    string error = get_string(r.body, "error");
    if (error.empty()) return "HTTP status " + std::to_string(r.status);
    string description = get_string(r.body, "error_description");
    return description.empty() ? error : error + " - " + description;
}

string auth_key_exchange(const string &auth_key, const string &serial_number, const string &device_name,
                         const string &client_id) {
    auto r = post_json("/client/auth-key-exchange", {
        {"auth_key", auth_key},
        {"serial_number", serial_number},
        {"hostname", device_name},
        {"client_id", client_id},
    });
    string token = get_string(r.body, "access_token");
    if (token.empty()) {
        // An unknown key gets a 404 with no body
        fail(ERROR_NOT_POSSIBLE, "Auth key exchange failed: %s%s", describe_error(r).c_str(),
             r.status == 404 ? " - is the auth key valid?" : "");
    }
    return token;
}

string signin(const string &client_id, const string &serial_number,
              const std::function<void(const string &, const string &)> &show,
              const std::function<bool()> &cancelled) {
    auto r = post_json("/client/device", {
        {"client_id", client_id},
        {"login_hint", serial_number},
    });
    string device_code = get_string(r.body, "device_code");
    string user_code = get_string(r.body, "user_code");
    string url = get_string(r.body, "verification_uri_complete");
    if (r.status != 200 || device_code.empty()) {
        fail(ERROR_NOT_POSSIBLE, "Failed to start signin: %s", describe_error(r).c_str());
    }
    int interval_s = r.body.value("interval", 5);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(r.body.value("expires_in", 900));
    show(url, user_code);

    while (std::chrono::steady_clock::now() < expiry) {
        auto next_poll = std::chrono::steady_clock::now() + std::chrono::seconds(interval_s);
        while (std::chrono::steady_clock::now() < next_poll) {
            if (cancelled()) return "";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        r = post_json("/client/token", {
            {"client_id", client_id},
            {"device_code", device_code},
        }, serial_number);
        string token = get_string(r.body, "access_token");
        if (r.status == 200 && !token.empty()) {
            return token;
        }
        string error = get_string(r.body, "error");
        if (error == "slow_down") {
            // As RFC 8628 asks of device-code clients
            interval_s += 5;
        } else if (error != "authorization_pending") {
            fail(ERROR_NOT_POSSIBLE, "Signin failed: %s", describe_error(r).c_str());
        }
    }
    fail(ERROR_NOT_POSSIBLE, "Timed out waiting for signin");
    return "";
}

}

#endif
