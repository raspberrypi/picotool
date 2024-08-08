/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _OTP_H
#define _OTP_H

#include <string>
#include <cassert>
#include <vector>
#include <cstdint>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

template <typename T> std::basic_string<T> lowercase(const std::basic_string<T>& s);
template <typename T> std::basic_string<T> uppercase(const std::basic_string<T>& s);

struct otp_field {
    otp_field() = default;
    otp_field(std::string name, uint32_t mask) : name(std::move(name)), mask(mask) {
        upper_name = uppercase(this->name);
    }
    otp_field(std::string name, uint32_t mask, std::string description) : otp_field(std::move(name), mask) {
        this->description = std::move(description);
    }
    std::string name;
    std::string upper_name;
    uint32_t mask;
    std::string description;

    friend void to_json(json& nlohmann_json_j, const otp_field& nlohmann_json_t) {
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, name, mask, description))
    }

    friend void from_json(const json& nlohmann_json_j, otp_field& nlohmann_json_t) {
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, name, mask, description))
        nlohmann_json_t.upper_name = uppercase(nlohmann_json_t.name);
    }
};

struct otp_reg {
    otp_reg() = default;
    explicit otp_reg(std::string name, uint32_t row, uint32_t mask) : name(std::move(name)), row(row), mask(mask) {
        upper_name = uppercase(this->name);
    }
    otp_reg& with_ecc() { assert(!redundancy); ecc = true; return *this; }
    otp_reg& with_crit() { assert(!ecc); crit = true; return *this; }
    otp_reg& with_redundancy(int r) { assert(!ecc); redundancy = r; return *this; }
    otp_reg& with_description(std::string d) { description = std::move(d); return *this; }
    otp_reg& with_field(const otp_field& field) { fields.push_back(field); return *this; }
    otp_reg& with_sequence(std::string prefix, int index, int length) { seq_prefix = std::move(prefix); seq_index = index; seq_length = length; return *this; }
    std::string name;
    std::string upper_name;
    std::string description;
    uint32_t row = 0xffffffff;
    uint32_t mask = 0;
    bool ecc = false;
    bool crit = false;
    unsigned int redundancy = 0;
    unsigned int seq_length = 0;
    unsigned int seq_index = 0;
    std::string seq_prefix;
    std::vector<otp_field> fields;

    friend void to_json(json& nlohmann_json_j, const otp_reg& nlohmann_json_t) {
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, name, description, ecc, crit, fields, mask, redundancy, row, seq_index, seq_length, seq_prefix))
    }

    friend void from_json(const json& nlohmann_json_j, otp_reg& nlohmann_json_t) {
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, name, description, row))
        if (nlohmann_json_j.contains("ecc")) nlohmann_json_j.at("ecc").get_to(nlohmann_json_t.ecc);
        if (nlohmann_json_j.contains("crit")) nlohmann_json_j.at("crit").get_to(nlohmann_json_t.crit);
        if (nlohmann_json_j.contains("redundancy")) nlohmann_json_j.at("redundancy").get_to(nlohmann_json_t.redundancy);

        if (nlohmann_json_j.contains("fields")) nlohmann_json_j.at("fields").get_to(nlohmann_json_t.fields);
        if (nlohmann_json_j.contains("mask")){
            nlohmann_json_j.at("mask").get_to(nlohmann_json_t.mask);
        } else {
            nlohmann_json_t.mask = nlohmann_json_t.ecc ? 0xffff : 0xffffff;
        }

        if (nlohmann_json_j.contains("seq_length") || nlohmann_json_j.contains("seq_index") || nlohmann_json_j.contains("seq_prefix")) {
            nlohmann_json_j.at("seq_length").get_to(nlohmann_json_t.seq_length);
            nlohmann_json_j.at("seq_index").get_to(nlohmann_json_t.seq_index);
            nlohmann_json_j.at("seq_prefix").get_to(nlohmann_json_t.seq_prefix);
        }
        nlohmann_json_t.upper_name = uppercase(nlohmann_json_t.name);
    }
};

void init_otp(std::map<uint32_t, otp_reg> &otp_regs, std::vector<std::string> extra_otp_files = {});

#endif
