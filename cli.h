/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CLI_H
#define _CLI_H

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <numeric>
#include <sstream>
#include <limits>

/**
 * Note this is a hastily hacked together command line parser.
 *
 * I like the syntax of clipp; but it seemed really buggy, and writing something less functional
 * with similar syntax seemed like the quickest way to go.
 *
 * Ironically this is probably just as buggy off the happy path as clipp appeared to be, but
 * in this case the happy path is ours!
 */
namespace cli {

    typedef std::string string;
    template<typename T> using vector = std::vector<T>;
    template<typename A, typename B> using map = std::map<A, B>;
    template<typename A, typename B> using pair = std::pair<A, B>;
    template<typename T> using shared_ptr = std::shared_ptr<T>;

    auto join = [](const vector<string> &range, const string &separator) {
        if (range.empty()) return string();

        return accumulate(
                next(begin(range)), // there is at least 1 element, so OK.
                end(range),

                range[0], // the initial value

                [&separator](auto result, const auto &value) {
                    return result + separator + value;
                });
    };

    struct parse_error : public std::exception {
        explicit parse_error(string what) : _what(std::move(what)) {}

        const char *what() const noexcept override {
            return _what.c_str();
        }

    private:
        string _what;
    };

    struct group;
    template<typename T>
    struct matchable_derived;

    template <typename K, typename V> struct map_and_order {
        map<K,V> _map;
        vector<K> _order;

        V& operator[](const K& key) {
            auto i = _map.find(key);
            if (i == _map.end()) {
                _order.push_back(key);
            }
            return _map[key];
        }

        vector<K> ordered_keys() {
            return _order;
        }
    };

    struct option_map {
        typedef map_and_order<string, map_and_order<string, vector<pair<string, string>>>> container;

        void add(const string& major_group, const string& minor_group, const string& option, const string& description) {
            auto &v = contents[major_group][minor_group];
            // we don't want to repeat the same option
            if (std::find_if(v.begin(), v.end(), [&](const auto &x) { return x.first == option; }) == v.end()) {
                v.emplace_back(option, description);
            }
        }

        container contents;
    };

    struct matchable;

    enum struct match_type {
        not_yet,
        match,
        error,
        no_match,
    };

    struct opaque_settings {
        virtual shared_ptr<opaque_settings> copy() = 0;
        virtual void save_into() = 0;
        virtual void apply_from() = 0;
    };

    struct settings_holder {
        explicit settings_holder(shared_ptr<opaque_settings> settings) : settings(settings) {}
        settings_holder(const settings_holder &other) {
            settings = other.settings->copy();
        }
        void save_into() {
            settings->save_into();
        }
        void apply_from() {
            settings->apply_from();
        }
        shared_ptr<opaque_settings> settings;
    };

    struct match_state {
        vector<string> remaining_args;
        string error_message;
        int match_count = 0;
        int error_count = 0;
        // if we are an error for something mising; we should rather report on an up next
        // unsupported option than our error message
        bool prefer_unknown_option_message = false;
        std::map<const matchable *, int> matchable_counts;
        settings_holder settings;

        match_state(const settings_holder& settings) : settings(settings) {}

        void apply_settings_from() {
            settings.apply_from();
        }
        void save_settings_into() {
            settings.save_into();
        }

        match_type match_value(const matchable *matchable, std::function<bool(const string&)> filter);

        match_type check_min_max(const matchable *matchable);

        int get_match_count(const std::shared_ptr<matchable>& element) {
            return matchable_counts[element.get()];
        }

        match_type update_stats(match_type type, const matchable *matchable) {
            assert(type != match_type::not_yet);
            if (type == match_type::match) {
                match_count++;
                matchable_counts[matchable]++;
            } else if (type == match_type::error) {
                error_count++;
                matchable_counts[matchable]++;
            }
            return type;
        }

        match_type match_if_equal(const matchable *matchable, const string& s);
    };

    struct matcher {
    };

    struct matchable {
        matchable() = default;

        explicit matchable(string name) : _name(std::move(name)) {}

        std::function<string(string)> action = [](const string&) { return ""; };

        std::function<string()> missing;

        virtual match_type match(match_state& m) const { return match_type::no_match; }

        string name() const {
            return _name;
        }

        virtual std::vector<string> synopsys() const {
            return {_name};
        }

        virtual bool is_optional() const {
            return !_min;
        }

        bool doc_non_optional() const {
            return _doc_non_optional;
        }

        bool force_expand_help() const {
            return _force_expand_help;
        }

        string doc() const {
            return _doc;
        }

        virtual bool get_option_help(string major_group, string minor_group, option_map &options) const {
            return false;
        }

        int min() const {
            return _min;
        }

        int max() const {
            return _max;
        }

    protected:
        string _name;
        string _doc;
        int _min = 1;
        int _max = 1;
        bool _doc_non_optional = false;
        bool _force_expand_help = false;
    };

    template<typename D>
    struct matchable_derived : public matchable {
        matchable_derived() = default;
        explicit matchable_derived(string name) : matchable(std::move(name)) {}

        D &on_action(std::function<string(const string&)> action) {
            this->action = action;
            return *static_cast<D *>(this);
        }

        D &if_missing(std::function<string()> missing) {
            this->missing = missing;
            return *static_cast<D *>(this);
        }

        D &operator%(const string& doc) {
            _doc = doc;
            return *static_cast<D *>(this);
        }

        D &required() {
            _min = 1;
            _max = std::max(_min, _max);
            return *static_cast<D *>(this);
        }

        D &repeatable() {
            _max = std::numeric_limits<int>::max();
            return *static_cast<D *>(this);
        }

        D &min(int v) {
            _min = v;
            return *static_cast<D *>(this);
        }

        D &doc_non_optional(bool v) {
            _doc_non_optional = v;
            return *static_cast<D *>(this);
        }

        D &force_expand_help(bool v) {
            _force_expand_help = v;
            return *static_cast<D *>(this);
        }

        D &max(int v) {
            _max = v;
            return *static_cast<D *>(this);
        }
        std::shared_ptr<matchable> to_ptr() const {
            return std::shared_ptr<matchable>(new D(*static_cast<const D *>(this)));
        }

        template<typename T>
        group operator&(const matchable_derived<T> &m);
        template<typename T>
        group operator|(const matchable_derived<T> &m);
        template<typename T>
        group operator+(const matchable_derived<T> &m);
    };

    template<typename D>
    struct value_base : public matchable_derived<D> {
        std::function<bool(const string&)> exclusion_filter = [](const string &x){return false;};

        explicit value_base(string name) : matchable_derived<D>(std::move(name)) {
            this->_min = 1;
            this->_max = 1;
        }

        vector<string> synopsys() const override {
            string s = string("<") + this->_name + ">";
            if (this->_max > 1) s += "..";
            return {s};
        }

        bool get_option_help(string major_group, string minor_group, option_map &options) const override {
            if (this->doc().empty()) {
                return false;
            }
            options.add(major_group, minor_group, string("<") + this->_name + ">", this->doc());
            return true;
        }

        match_type match(match_state& ms) const override {
            match_type rc = ms.check_min_max(this);
            if (rc == match_type::not_yet) {
                rc = ms.match_value(this, exclusion_filter);
            }
            return rc;
        }

        D &with_exclusion_filter(std::function<bool(const string&)> exclusion_filter) {
            this->exclusion_filter = exclusion_filter;
            return *static_cast<D *>(this);
        }
    };

    struct option : public matchable_derived<option> {
        explicit option(char short_opt) : option(short_opt, "") {}

        explicit option(string _long_opt) : option(0, std::move(_long_opt)) {}

        option(char _short_opt, string _long_opt) {
            _min = 0;
            short_opt = _short_opt ? "-" + string(1, _short_opt) : "";
            long_opt = std::move(_long_opt);
            _name = short_opt.empty() ? long_opt : short_opt;
        }

        bool get_option_help(string major_group, string minor_group, option_map &options) const override {
            if (doc().empty()) return false;
            string label = short_opt.empty() ? "" : _name;
            if (!long_opt.empty()) {
                if (!label.empty()) label += ", ";
                label += long_opt;
            }
            options.add(major_group, minor_group, label, doc());
            return true;
        }

        template<typename T>
        option &set(T &t) {
            // note we cannot capture "this"
            on_action([&t](const string& value) {
                t = true;
                return "";
            });
            return *this;
        }

        template<typename T>
        option &clear(T &t) {
            // note we cannot capture "this"
            on_action([&t](const string& value) {
                t = false;
                return "";
            });
            return *this;
        }

        match_type match(match_state &ms) const override {
            match_type rc = ms.match_if_equal(this, short_opt);
            if (rc == match_type::no_match) {
                rc = ms.match_if_equal(this, long_opt);
            }
            return rc;
        }

    private:
        string short_opt;
        string long_opt;
    };

    struct value : public value_base<value> {
        explicit value(string name) : value_base(std::move(name)) {}

        template<typename T>
        value &set(T &t) {
            on_action([&](const string& value) {
                t = value;
                return "";
            });
            return *this;
        }
    };

    struct integer : public value_base<integer> {
        explicit integer(string name) : value_base(std::move(name)) {}

        template<typename T>
        integer &set(T &t) {
            int min = _min_value;
            int max = _max_value;
            string nm = "<" + name() + ">";
            // note we cannot capture "this"
            on_action([&t, min, max, nm](const string& value) {
                size_t pos = 0;
                long lvalue = std::numeric_limits<long>::max();
                try {
                    lvalue = std::stol(value, &pos);
                    if (pos != value.length()) {
                        return "Garbage after integer value: " + value.substr(pos);
                    }
                } catch (std::invalid_argument&) {
                    return value + " is not a valid integer";
                } catch (std::out_of_range&) {
                }
                if (lvalue != (int)lvalue) {
                    return value + " is too big";
                }
                t = (int)lvalue;
                if (t < min) {
                    return nm + " must be >= " + std::to_string(min);
                }
                if (t > max) {
                    return nm + " must be <= " + std::to_string(max);
                }
                return string("");
            });
            return *this;
        }

        integer& min_value(int v) {
            _min_value = v;
            return *this;
        }

        integer& max_value(int v) {
            _max_value = v;
            return *this;
        }

        int _min_value = 0;
        int _max_value = std::numeric_limits<int>::max();
    };

    struct hex : public value_base<hex> {
        explicit hex(string name) : value_base(std::move(name)) {}

        template<typename T>
        hex &set(T &t) {
            unsigned int min = _min_value;
            unsigned int max = _max_value;
            string nm = "<" + name() + ">";
            // note we cannot capture "this"
            on_action([&t, min, max, nm](string value) {
                auto ovalue = value;
                if (value.find("0x") == 0) value = value.substr(2);
                size_t pos = 0;
                long lvalue = std::numeric_limits<long>::max();
                try {
                    lvalue = std::stoul(value, &pos, 16);
                    if (pos != value.length()) {
                        return "Garbage after hex value: " + value.substr(pos);
                    }
                } catch (std::invalid_argument&) {
                    return ovalue + " is not a valid hex value";
                } catch (std::out_of_range&) {
                }
                if (lvalue != (unsigned int)lvalue) {
                    return value + " is not a valid 32 bit value";
                }
                t = (unsigned int)lvalue;
                if (t < min) {
                    std::stringstream ss;
                    ss << nm << " must be >= 0x" << std::hex << std::to_string(min);
                    return ss.str();
                }
                if (t > max) {
                    std::stringstream ss;
                    ss << nm << " must be M= 0x" << std::hex << std::to_string(min);
                    return ss.str();
                }
                return string("");
            });
            return *this;
        }

        hex& min_value(unsigned int v) {
            _min_value = v;
            return *this;
        }

        hex& max_value(unsigned int v) {
            _max_value = v;
            return *this;
        }

        unsigned int _min_value = 0;
        unsigned int _max_value = std::numeric_limits<unsigned int>::max();
    };

    struct group : public matchable_derived<group> {
        enum group_type {
            sequence,
            set,
            exclusive,
        };

    public:
        group() : type(set) {}

        template<typename T>
        explicit group(const T &t) : type(set), elements{t.to_ptr()} {}

        template<class Matchable, class... Matchables>
        group(Matchable m, Matchable ms...) : elements{m, ms}, type(set) {}

        group &set_type(group_type t) {
            type = t;
            return *this;
        }

        group &major_group(string g) {
            _major_group = std::move(g);
            return *this;
        }

        static string decorate(const matchable &e, string s) {
            if (e.is_optional() && !e.doc_non_optional()) {
                return string("[") + s + "]";
            } else {
                return s;
            }
        }

        vector<string> synopsys() const override {
            vector<string> rc;
            switch (type) {
                case set:
                case sequence: {
                    std::vector<std::vector<string>> tmp{{}};
                    for (auto &x : elements) {
                        auto xs = x->synopsys();
                        if (xs.size() == 1) {
                            for (auto &s : tmp) {
                                s.push_back(decorate(*x, xs[0]));
                            }
                        } else {
                            auto save = tmp;
                            tmp.clear();
                            for (auto &v : save) {
                                for (auto &s : xs) {
                                    auto nv = v;
                                    nv.push_back(decorate(*x, s));
                                    tmp.push_back(nv);
                                }
                            }
                        }
                    }
                    for (const auto &v : tmp) {
                        rc.push_back(join(v, " "));
                    }
                    break;
                }
                case exclusive:
                    for (auto &x : elements) {
                        auto xs = x->synopsys();
                        std::transform(xs.begin(), xs.end(), std::back_inserter(rc), [&](const auto &s) {
                            return decorate(*x, s);
                        });
                    }
                    break;
                default:
                    assert(false);
                    break;
            }
            return rc;
        }

        group operator|(const group &g) {
            return matchable_derived::operator|(g);
        }

        group operator&(const group &g) {
            return matchable_derived::operator&(g);
        }

        group operator+(const group &g) {
            return matchable_derived::operator+(g);
        }

        bool no_match_beats_error() const {
            return _no_match_beats_error;
        }

        group &no_match_beats_error(bool v) {
            _no_match_beats_error = v;
            return *this;
        }

        template<typename T>
        group operator&(const matchable_derived<T> &m) {
            if (type == sequence) {
                elements.push_back(m.to_ptr());
                return *this;
            }
            return matchable_derived::operator&(m);
        }

        template<typename T>
        group operator|(const matchable_derived<T> &m) {
            if (type == exclusive) {
                elements.push_back(m.to_ptr());
                return *this;
            }
            return matchable_derived::operator|(m);
        }

        template<typename T>
        group operator+(const matchable_derived<T> &m) {
            if (type == set) {
                elements.push_back(m.to_ptr());
                return *this;
            }
            return matchable_derived::operator+(m);
        }

        bool get_option_help(string major_group, string minor_group, option_map &options) const override {
            // todo beware.. this check is necessary as is, but I'm not sure what removing it breaks in terms of formatting  :-(
            if (is_optional() && !this->_doc_non_optional && !this->_force_expand_help) {
                options.add(major_group, minor_group, synopsys()[0], doc());
                return true;
            }
            if (!doc().empty()) {
                minor_group = doc();
            }
            if (!_major_group.empty()) {
                major_group = _major_group;
            }
            for (const auto &e : elements) {
                e->get_option_help(major_group, minor_group, options);
            }
            return true;
        }

        match_type match(match_state& ms) const override {
            match_type rc = ms.check_min_max(this);
            if (rc == match_type::no_match) return rc;
            assert(rc == match_type::not_yet);
            switch(type) {
                case sequence:
                    rc = match_sequence(ms);
                    break;
                case set:
                    rc = match_set(ms);
                    break;
                default:
                    rc = match_exclusive(ms);
                    break;
            }
            return ms.update_stats(rc, this);
        }

        match_type match_sequence(match_state& ms) const {
            match_type rc = match_type::no_match;
            for(const auto& e : elements) {
                rc = e->match(ms);
                assert(rc != match_type::not_yet);
                if (rc != match_type::match) {
                    break;
                }
            }
            return rc;
        }

        match_type match_set(match_state& ms) const {
            // because of repeatability, we keep matching until there is nothing left to match
//            vector<match_type> types(elements.size(), match_type::not_yet);
            bool had_any_matches = false;
            bool final_pass = false;
            do {
                bool matches_this_time = false;
                bool errors_this_time = false;
                bool not_min_this_time = false;
                for (size_t i=0;i<elements.size();i++) {
//                    if (types[i] == match_type::not_yet) {
                        auto ms_prime = ms;
                        ms_prime.apply_settings_from();
                        match_type t = elements[i]->match(ms_prime);
                        assert(t != match_type::not_yet);
                        if (t == match_type::match) {
                            // we got a match, so record in ms and try again
                            // (if the matchable isn't repeatable it will no match next time)
//                            types[i] = match_type::not_yet;
                            ms_prime.save_settings_into();
                            ms = ms_prime;
                            had_any_matches = true;
                            matches_this_time = true;
                        } else if (t == match_type::error) {
                            if (final_pass) {
                                ms_prime.save_settings_into();
                                ms = ms_prime;
                                return t;
                            }
                            errors_this_time = true;
                        } else {
                            if (ms.get_match_count(elements[i]) < elements[i]->min()) {
                                if (final_pass) {
                                    ms.error_message = elements[i]->missing ? elements[i]->missing() : "missing required argument";
                                    return match_type::error;
                                }
                                not_min_this_time = true;
                            }
                        }
//                    }
                }
                if (final_pass) break;
                if (!matches_this_time) {
                    if (errors_this_time || not_min_this_time) {
                        final_pass = true;
                    } else {
                        break;
                    }
                }
            } while (true);
            return had_any_matches ? match_type::match : match_type::no_match;
        }

        match_type match_exclusive(match_state& ms) const {
            vector<match_state> matches(elements.size(), ms);
            vector<match_type> types(elements.size(), match_type::no_match);
            int elements_with_errors = 0;
            int elements_with_no_match = 0;
            int error_at = -1;
            int error_match_count = -1;
            for (size_t i=0;i<elements.size();i++) {
                match_type t;
                matches[i].apply_settings_from();
                do {
                    t = elements[i]->match(matches[i]);
                    assert(t != match_type::not_yet);
                    if (t != match_type::no_match) {
                        types[i] = t;
                    }
                } while (t == match_type::match);
                matches[i].save_settings_into();
                if (types[i] == match_type::match) {
                    ms = matches[i];
                    return match_type::match;
                } else if (types[i] == match_type::error) {
                    if (matches[i].match_count > error_match_count) {
                        error_match_count = matches[i].match_count;
                        error_at = i;
                    }
                    elements_with_errors++;
                } else if (types[i] == match_type::no_match) {
                    elements_with_no_match++;
                }
            }
            if (elements_with_no_match && (!elements_with_errors || no_match_beats_error())) {
                return match_type::no_match;
            }
            if (elements_with_errors) {
                ms = matches[error_at];
                ms.apply_settings_from(); // todo perhaps want to apply the previous settings instead?
                return match_type::error;
            } else {
                // back out any modified settings
                ms.apply_settings_from();
                return match_type::no_match;
            }
        }

    private:
        string _major_group;
        vector<std::shared_ptr<matchable>> elements;
        group_type type;
        bool _no_match_beats_error = true;
    };

    template<typename D>
    template<typename T>
    group matchable_derived<D>::operator|(const matchable_derived<T> &m) {
        return group{this->to_ptr(), m.to_ptr()}.set_type(group::exclusive);
    }

    template<typename D>
    template<typename T>
    group matchable_derived<D>::operator&(const matchable_derived<T> &m) {
        int _min = matchable::min();
        int _max = matchable::max();
        min(1);
        max(1);
        return group{this->to_ptr(), m.to_ptr()}.set_type(group::sequence).min(_min).max(_max);
    }

    template<typename D>
    template<typename T>
    group matchable_derived<D>::operator+(const matchable_derived<T> &m) {
        return group{this->to_ptr(), m.to_ptr()};
    }

    vector<string> make_args(int argc, char **argv) {
        vector<string> args;
        for (int i = 1; i < argc; i++) {
            string arg(argv[i]);
            if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-') {
                // expand collapsed args (unconditionally for now)
                for (auto c = arg.begin() + 1; c != arg.end(); c++) {
                    args.push_back("-" + string(1, *c));
                }
            } else {
                args.push_back(arg);
            }
        }
        return args;
    }

    match_type match_state::check_min_max(const matchable *matchable) {
        if (matchable_counts[matchable] < matchable->min()) {
            return match_type::not_yet;
        }
        if (matchable_counts[matchable] >= matchable->max()) {
            return match_type::no_match;
        }
        return match_type::not_yet;
    }

    match_type match_state::match_if_equal(const matchable *matchable, const string& s) {
        if (remaining_args.empty()) return match_type::no_match;
        if (remaining_args[0] == s) {
            auto message = matchable->action(s);
            assert(message.empty());
            remaining_args.erase(remaining_args.begin());
            return update_stats(match_type::match, matchable);
        }
        return match_type::no_match;
    }

    match_type match_state::match_value(const matchable *matchable, std::function<bool(const string&)> exclusion_filter) {
        // treat an excluded value as missing
        bool empty = remaining_args.empty() || exclusion_filter(remaining_args[0]);
        if (empty) {
            if (matchable_counts[matchable] < matchable->min()) {
                prefer_unknown_option_message = !remaining_args.empty();
                error_message = matchable->missing ? matchable->missing() : "missing <" + matchable->name() +">";
                return update_stats(match_type::error, matchable);
            }
            return match_type::no_match;
        }
        auto message = matchable->action(remaining_args[0]);
        if (!message.empty()) {
            error_message = message;
            return update_stats(match_type::error, matchable);
        }
        remaining_args.erase(remaining_args.begin());
        return update_stats(match_type::match, matchable);
    }

    template<typename S> struct typed_settings : public opaque_settings {
        explicit typed_settings(S& settings) : root_settings(settings), settings(settings) {
        }

        shared_ptr<cli::opaque_settings> copy() override {
            auto c = std::make_shared<typed_settings<S>>(*this);
            c->settings = settings;
            return c;
        }

        void save_into() override {
            settings = root_settings;
        }

        void apply_from() override {
            root_settings = settings;
        }

        S& root_settings;
        S settings;
    };

    template<typename S> void match(S& settings, const group& g, std::vector<string> args) {
        auto holder = settings_holder(std::make_shared<typed_settings<S>>(settings));
        match_state ms(holder);
        ms.remaining_args = std::move(args);
        auto t = g.match(ms);
        if (!ms.prefer_unknown_option_message) {
            if (t == match_type::error) {
                throw parse_error(ms.error_message);
            }
        }
        if (!ms.remaining_args.empty()) {
            if (ms.remaining_args[0].find('-')==0) {
                throw parse_error("unexpected option: "+ms.remaining_args[0]);
            } else {
                throw parse_error("unexpected argument: "+ms.remaining_args[0]);
            }
        }
        if (ms.prefer_unknown_option_message) {
            if (t == match_type::error) {
                throw parse_error(ms.error_message);
            }
        }
    }
}

#endif
