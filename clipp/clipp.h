/*****************************************************************************
 *  ___  _    _   ___ ___
 * |  _|| |  | | | _ \ _ \   CLIPP - command line interfaces for modern C++
 * | |_ | |_ | | |  _/  _/   version 1.2.3
 * |___||___||_| |_| |_|     https://github.com/muellan/clipp
 *
 * Licensed under the MIT License <http://opensource.org/licenses/MIT>.
 * Copyright (c) 2017-2018 André Müller <foss@andremueller-online.de>
 *
 * ---------------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/

/**
 * NOTE: this is just the formatting_ostream class from the original clipp header
 */
#ifndef AM_CLIPP_H__
#define AM_CLIPP_H__

#include <cstring>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <memory>
#include <vector>
#include <limits>
#include <stack>
#include <algorithm>
#include <sstream>
#include <utility>
#include <iterator>
#include <functional>


/*************************************************************************//**
 *
 * @brief primary namespace
 *
 *****************************************************************************/
namespace clipp {

/*****************************************************************************
 *
 * basic constants and datatype definitions
 *
 *****************************************************************************/
using arg_index = int;

using arg_string = std::string;
using doc_string = std::string;

using arg_list  = std::vector<arg_string>;

/*************************************************************************//**
 *
 * @brief stream decorator
 *        that applies formatting like line wrapping
 *
 *****************************************************************************/
    template<class OStream = std::ostream, class StringT = doc_string>
    class formatting_ostream
    {
    public:
        using string_type = StringT;
        using size_type   = typename string_type::size_type;
        using char_type   = typename string_type::value_type;

        formatting_ostream(OStream& os):
                os_(os),
                curCol_{0}, firstCol_{0}, lastCol_{100},
                hangingIndent_{0}, paragraphSpacing_{0}, paragraphSpacingThreshold_{2},
                curBlankLines_{0}, curParagraphLines_{1},
                totalNonBlankLines_{0},
                ignoreInputNls_{false}
        {}


        //---------------------------------------------------------------
        const OStream& base() const noexcept { return os_; }
        OStream& base()       noexcept { return os_; }

        bool good() const { return os_.good(); }


        //---------------------------------------------------------------
        /** @brief determines the leftmost border of the text body */
        formatting_ostream& first_column(int c) {
            firstCol_ = c < 0 ? 0 : c;
            return *this;
        }
        int first_column() const noexcept { return firstCol_; }

        /** @brief determines the rightmost border of the text body */
        formatting_ostream& last_column(int c) {
            lastCol_ = c < 0 ? 0 : c;
            return *this;
        }

        int last_column() const noexcept { return lastCol_; }

        int text_width() const noexcept {
            return lastCol_ - firstCol_;
        }

        /** @brief additional indentation for the 2nd, 3rd, ... line of
                   a paragraph (sequence of soft-wrapped lines) */
        formatting_ostream& hanging_indent(int amount) {
            hangingIndent_ = amount;
            return *this;
        }
        int hanging_indent() const noexcept {
            return hangingIndent_;
        }

        /** @brief amount of blank lines between paragraphs */
        formatting_ostream& paragraph_spacing(int lines) {
            paragraphSpacing_ = lines;
            return *this;
        }
        int paragraph_spacing() const noexcept {
            return paragraphSpacing_;
        }

        /** @brief insert paragraph spacing
                   if paragraph is at least 'lines' lines long */
        formatting_ostream& min_paragraph_lines_for_spacing(int lines) {
            paragraphSpacingThreshold_ = lines;
            return *this;
        }
        int min_paragraph_lines_for_spacing() const noexcept {
            return paragraphSpacingThreshold_;
        }

        /** @brief if set to true, newline characters will be ignored */
        formatting_ostream& ignore_newline_chars(bool yes) {
            ignoreInputNls_ = yes;
            return *this;
        }

        bool ignore_newline_chars() const noexcept {
            return ignoreInputNls_;
        }


        //---------------------------------------------------------------
        /* @brief insert 'n' spaces */
        void write_spaces(int n) {
            if(n < 1) return;
            os_ << string_type(size_type(n), ' ');
            curCol_ += n;
        }

        /* @brief go to new line, but continue current paragraph */
        void wrap_soft(int times = 1) {
            if(times < 1) return;
            if(times > 1) {
                os_ << string_type(size_type(times), '\n');
            } else {
                os_ << '\n';
            }
            curCol_ = 0;
            ++curParagraphLines_;
        }

        /* @brief go to new line, and start a new paragraph */
        void wrap_hard(int times = 1) {
            if(times < 1) return;

            if(paragraph_spacing() > 0 &&
               paragraph_lines() >= min_paragraph_lines_for_spacing())
            {
                times = paragraph_spacing() + 1;
            }

            if(times > 1) {
                os_ << string_type(size_type(times), '\n');
                curBlankLines_ += times - 1;
            } else {
                os_ << '\n';
            }
            if(at_begin_of_line()) {
                ++curBlankLines_;
            }
            curCol_ = 0;
            curParagraphLines_ = 1;
        }


        //---------------------------------------------------------------
        bool at_begin_of_line() const noexcept {
            return curCol_ <= current_line_begin();
        }
        int current_line_begin() const noexcept {
            return in_hanging_part_of_paragraph()
                   ? firstCol_ + hangingIndent_
                   : firstCol_;
        }

        int current_column() const noexcept {
            return curCol_;
        }

        int total_non_blank_lines() const noexcept {
            return totalNonBlankLines_;
        }
        int paragraph_lines() const noexcept {
            return curParagraphLines_;
        }
        int blank_lines_before_paragraph() const noexcept {
            return curBlankLines_;
        }


        //---------------------------------------------------------------
        template<class T>
        friend formatting_ostream&
        operator << (formatting_ostream& os, const T& x) {
            os.write(x);
            return os;
        }

        void flush() {
            os_.flush();
        }


    private:
        bool in_hanging_part_of_paragraph() const noexcept {
            return hanging_indent() > 0 && paragraph_lines() > 1;
        }
        bool current_line_empty() const noexcept {
            return curCol_ < 1;
        }
        bool left_of_text_area() const noexcept {
            return curCol_ < current_line_begin();
        }
        bool right_of_text_area() const noexcept {
            return curCol_ > lastCol_;
        }
        int columns_left_in_line() const noexcept {
            return lastCol_ - std::max(current_line_begin(), curCol_);
        }

        void fix_indent() {
            if(left_of_text_area()) {
                const auto fst = current_line_begin();
                write_spaces(fst - curCol_);
                curCol_ = fst;
            }
        }

        template<class Iter>
        bool only_whitespace(Iter first, Iter last) const {
            return last == std::find_if_not(first, last,
                                            [](char_type c) { return std::isspace(c); });
        }

        /** @brief write any object */
        template<class T>
        void write(const T& x) {
            std::ostringstream ss;
            ss << x;
            write(std::move(ss).str());
        }

        /** @brief write a stringstream */
        void write(const std::ostringstream& s) {
            write(s.str());
        }

        /** @brief write a string */
        void write(const string_type& s) {
            write(s.begin(), s.end());
        }

        /** @brief partition output into lines */
        template<class Iter>
        void write(Iter first, Iter last)
        {
            if(first == last) return;
            if(*first == '\n') {
                if(!ignore_newline_chars()) wrap_hard();
                ++first;
                if(first == last) return;
            }
            auto i = std::find(first, last, '\n');
            if(i != last) {
                if(ignore_newline_chars()) ++i;
                if(i != last) {
                    write_line(first, i);
                    write(i, last);
                }
            }
            else {
                write_line(first, last);
            }
        }

        /** @brief handle line wrapping due to column constraints */
        template<class Iter>
        void write_line(Iter first, Iter last)
        {
            if(first == last) return;
            if(only_whitespace(first, last)) return;

            if(right_of_text_area()) wrap_soft();

            if(at_begin_of_line()) {
                //discard whitespace, it we start a new line
                first = std::find_if(first, last,
                                     [](char_type c) { return !std::isspace(c); });
                if(first == last) return;
            }

            const auto n = int(std::distance(first,last));
            const auto m = columns_left_in_line();
            //if text to be printed is too long for one line -> wrap
            if(n > m) {
                //break before word, if break is mid-word
                auto breakat = first + m;
                while(breakat > first && !std::isspace(*breakat)) --breakat;
                //could not find whitespace before word -> try after the word
                if(!std::isspace(*breakat) && breakat == first) {
                    breakat = std::find_if(first+m, last,
                                           [](char_type c) { return std::isspace(c); });
                }
                if(breakat > first) {
                    if(curCol_ < 1) ++totalNonBlankLines_;
                    fix_indent();
                    std::copy(first, breakat, std::ostream_iterator<char_type>(os_));
                    curBlankLines_ = 0;
                }
                if(breakat < last) {
                    wrap_soft();
                    write_line(breakat, last);
                }
            }
            else {
                if(curCol_ < 1) ++totalNonBlankLines_;
                fix_indent();
                std::copy(first, last, std::ostream_iterator<char_type>(os_));
                curCol_ += n;
                curBlankLines_ = 0;
            }
        }

        /** @brief write a single character */
        void write(char_type c)
        {
            if(c == '\n') {
                if(!ignore_newline_chars()) wrap_hard();
            }
            else {
                if(at_begin_of_line()) ++totalNonBlankLines_;
                fix_indent();
                os_ << c;
                ++curCol_;
            }
        }

        OStream& os_;
        int curCol_;
        int firstCol_;
        int lastCol_;
        int hangingIndent_;
        int paragraphSpacing_;
        int paragraphSpacingThreshold_;
        int curBlankLines_;
        int curParagraphLines_;
        int totalNonBlankLines_;
        bool ignoreInputNls_;
    };

} //namespace clipp

#endif


