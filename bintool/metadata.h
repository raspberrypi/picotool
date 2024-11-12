#pragma once

#include <memory>
#include <iterator>

#include "boot/picobin.h"
#include <map>

#include <cassert>

#include "elf_file.h"

#if ENABLE_DEBUG_LOG
#define DEBUG_LOG(...) printf(__VA_ARGS__)
#else
#define DEBUG_LOG(...) ((void)0)
#endif

struct item;

template<typename InputIterator> std::vector<uint32_t> lsb_bytes_to_words(InputIterator begin, InputIterator end) {
    using InputType = typename std::iterator_traits<InputIterator>::value_type;
    static_assert(sizeof(InputType) == 1, "");
    std::vector<uint32_t> rc;
    size_t size = end - begin;
    assert(!(size & 3));
    if (size) {
        rc.reserve(size / 4);
        for(auto it = begin; it < end; ) {
            uint32_t word = *it++;
            word |= (*it++) << 8;
            word |= (*it++) << 16;
            word |= (*it++) << 24;
            rc.push_back(word);
        }
    }
    return rc;
}

template<typename InputIterator, typename OutputType = std::uint8_t> std::vector<OutputType> words_to_lsb_bytes(InputIterator begin, InputIterator end) {
    static_assert(sizeof(OutputType) == 1, "");
    std::vector<OutputType> rc;
    size_t size = end - begin;
    if (size) {
        rc.reserve(size * 4);
        for(auto it = begin; it < end; ) {
            uint32_t word = *it++;
            rc.push_back(word & 0xff);
            rc.push_back((word >> 8) & 0xff);
            rc.push_back((word >> 16) & 0xff);
            rc.push_back((word >> 24) & 0xff);
        }
    }
    return rc;
}


struct item_writer_context {
    item_writer_context(uint32_t base_addr) : base_addr(base_addr), word_offset(0) {}
    std::map<std::shared_ptr<item>, uint32_t> item_word_offsets;
    uint32_t base_addr;
    uint32_t word_offset;
};

struct item {
    explicit item() {};
    virtual ~item() = default;
    virtual uint8_t type() const = 0;

    static uint32_t decode_size(uint32_t item_header) {
        uint32_t size;
        if (item_header & 0x80) {
            size = (uint16_t)(item_header >> 8);
        } else {
            size = (uint8_t)(item_header >> 8);
        }
        return size;
    }

    virtual std::vector<uint32_t> to_words(item_writer_context &ctx) const = 0;
    virtual uint32_t encode_type_and_size(unsigned int size) const {
        assert(size < PICOBIN_MAX_BLOCK_SIZE);
        if (size < 256) {
            return (size << 8u) | type();
        } else {
            // todo byte order
            return (size << 8u) | 0x80 | type();
        }
    }
};


// always use single byte size
struct single_byte_size_item : public item {
    virtual uint32_t encode_type_and_size(unsigned int size) const override {
        assert(size < PICOBIN_MAX_BLOCK_SIZE);
        assert(size < 128);
        assert(!(type() & 128));
        return (size << 8u) | type();
    }
};

// always use double byte size
struct double_byte_size_item : public item {
    virtual uint32_t encode_type_and_size(unsigned int size) const override {
        assert(size < PICOBIN_MAX_BLOCK_SIZE);
        assert(type() & 128);
        // todo byte order
        return (size << 8u) | 0x80 | type();
    }
};

struct ignored_item : public double_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_2BS_IGNORED; }
    ignored_item() = default;

    explicit ignored_item(uint32_t size, std::vector<uint32_t> data) : size(size), data(data) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        uint32_t size = item::decode_size(header);
        std::vector<uint32_t> data;
        for (unsigned int i=1; i < size; i++) {
            data.push_back(*it++);
        }
        return std::make_shared<ignored_item>(size, data);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        std::vector<uint32_t> ret;
        ret.push_back(encode_type_and_size(size));
        ret.insert(ret.end(), data.begin(), data.end());
        return ret;
    }

    uint32_t size;
    std::vector<uint32_t> data;
};

enum image_type_image_type {
    type_invalid=PICOBIN_IMAGE_TYPE_IMAGE_TYPE_INVALID,
    type_exe=PICOBIN_IMAGE_TYPE_IMAGE_TYPE_EXE,
    type_data=PICOBIN_IMAGE_TYPE_IMAGE_TYPE_DATA
};

enum image_type_exe_security {
    sec_unspecified=PICOBIN_IMAGE_TYPE_EXE_SECURITY_UNSPECIFIED,
    sec_ns=PICOBIN_IMAGE_TYPE_EXE_SECURITY_NS,
    sec_s=PICOBIN_IMAGE_TYPE_EXE_SECURITY_S
};

enum image_type_exe_cpu {
    cpu_arm=PICOBIN_IMAGE_TYPE_EXE_CPU_ARM,
    cpu_riscv=PICOBIN_IMAGE_TYPE_EXE_CPU_RISCV,
    cpu_varmulet=PICOBIN_IMAGE_TYPE_EXE_CPU_VARMULET
};

enum image_type_exe_chip {
    chip_rp2040=PICOBIN_IMAGE_TYPE_EXE_CHIP_RP2040,
    chip_rp2350=PICOBIN_IMAGE_TYPE_EXE_CHIP_RP2350
};

struct image_type_item : public single_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE; }
    image_type_item() = default;

    explicit image_type_item(uint16_t flags) : flags(flags) {}
    template <typename I> static std::shared_ptr<item> parse(I it, I end, uint32_t header) {
        return std::make_shared<image_type_item>(header >> 16);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        return {encode_type_and_size(1) | (flags << 16)};
    }

    image_type_image_type image_type() const { return static_cast<image_type_image_type>((flags & PICOBIN_IMAGE_TYPE_IMAGE_TYPE_BITS) >> PICOBIN_IMAGE_TYPE_IMAGE_TYPE_LSB); }
    image_type_exe_security security() const { return static_cast<image_type_exe_security>((flags & PICOBIN_IMAGE_TYPE_EXE_SECURITY_BITS) >> PICOBIN_IMAGE_TYPE_EXE_SECURITY_LSB); }
    image_type_exe_cpu cpu() const { return static_cast<image_type_exe_cpu>((flags & PICOBIN_IMAGE_TYPE_EXE_CPU_BITS) >> PICOBIN_IMAGE_TYPE_EXE_CPU_LSB); }
    image_type_exe_chip chip() const { return static_cast<image_type_exe_chip>((flags & PICOBIN_IMAGE_TYPE_EXE_CHIP_BITS) >> PICOBIN_IMAGE_TYPE_EXE_CHIP_LSB); }
    bool tbyb() const { return flags & PICOBIN_IMAGE_TYPE_EXE_TBYB_BITS; }

    uint16_t flags;
};

struct partition_table_item : public single_byte_size_item {
    struct partition {
        uint8_t permissions = 0;
        uint16_t first_sector = 0;
        uint16_t last_sector = 0;
        uint32_t flags = 0;
        uint64_t id;
        std::string name;
        std::vector<uint32_t> extra_families;

        std::vector<uint32_t> to_words() const {
            std::vector<uint32_t> ret = {
                ((permissions << PICOBIN_PARTITION_PERMISSIONS_LSB) & PICOBIN_PARTITION_PERMISSIONS_BITS)
                    | ((first_sector << PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB) & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                    | ((last_sector << PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB) & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS),
                ((permissions << PICOBIN_PARTITION_PERMISSIONS_LSB) & PICOBIN_PARTITION_PERMISSIONS_BITS)
                    | flags,
            };
            if (flags & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                ret.push_back((uint32_t)id);
                ret.push_back((uint32_t)(id >> 32));
            }
            if (extra_families.size() > 0) {
                assert(extra_families.size() == (flags & PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_BITS) >> PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB);
                ret.insert(ret.end(), extra_families.begin(), extra_families.end());
            }
            if (name.size() > 0) {
                char size = name.size();
                std::vector<char> name_vec = {size};
                for (char c : name) {
                    name_vec.push_back(c);
                }
                while (name_vec.size() % 4 != 0) name_vec.push_back(0);
                while (name_vec.size() > 0) {
                    ret.push_back(*(uint32_t*)name_vec.data());
                    name_vec.erase(name_vec.begin(), name_vec.begin() + 4);
                }
            }
            return ret;
        }
    };
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_PARTITION_TABLE; }
    partition_table_item() = default;

    explicit partition_table_item(uint32_t unpartitioned_flags, bool singleton) : unpartitioned_flags(unpartitioned_flags), singleton(singleton) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        uint32_t size = decode_size(header);
        uint8_t singleton_count = header >> 24;
        bool singleton = singleton_count & 0x80;
        uint8_t partition_count = singleton_count & 0x0f;
        uint32_t unpartitioned_flags = *it++;

        auto pt = std::make_shared<partition_table_item>(unpartitioned_flags, singleton);

        std::vector<uint32_t> data;
        for (unsigned int i=2; i < size; i++) {
            data.push_back(*it++);
        }
        int i=0;
        while (i < data.size()) {
            partition new_p;
            uint32_t permissions_locations = data[i++];
            new_p.permissions = (permissions_locations & PICOBIN_PARTITION_PERMISSIONS_BITS) >> PICOBIN_PARTITION_PERMISSIONS_LSB;
            new_p.first_sector = (permissions_locations & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
            new_p.last_sector = (permissions_locations & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;

            uint32_t permissions_flags = data[i++];
            uint8_t permissions2 = (permissions_flags & PICOBIN_PARTITION_PERMISSIONS_BITS) >> PICOBIN_PARTITION_PERMISSIONS_LSB;
            if (new_p.permissions != permissions2) {
                printf("Permissions mismatch %02x %02x\n", new_p.permissions, permissions2);
                assert(false);
            }
            new_p.flags = permissions_flags & (~PICOBIN_PARTITION_PERMISSIONS_BITS);

            if (new_p.flags & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
                new_p.id = (uint64_t)data[i++] | ((uint64_t)data[i++] << 32);
            }

            uint8_t num_extra_families = (new_p.flags & PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_BITS) >> PICOBIN_PARTITION_FLAGS_ACCEPTS_NUM_EXTRA_FAMILIES_LSB;
            for (int fam=0; fam < num_extra_families; fam++) {
                new_p.extra_families.push_back(data[i++]);
            }

            if (new_p.flags & PICOBIN_PARTITION_FLAGS_HAS_NAME_BITS) {
                auto bytes = words_to_lsb_bytes(data.begin() + i++, data.end());
                int name_size = bytes[0];
                // This works neatly - accounts for the size byte at the start
                i += name_size / 4;
                new_p.name = std::string((char*)(bytes.data() + 1), name_size);
            }

            pt->partitions.push_back(new_p);
        }
        return pt;
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        std::vector<uint32_t> partition_words;
        for (auto p : partitions) {
            auto words = p.to_words();
            partition_words.insert(partition_words.end(), words.begin(), words.end());
        }
        std::vector<uint32_t> ret = {
            encode_type_and_size(2 + partition_words.size()) | (singleton << 31) | ((uint8_t)partitions.size() << 24),
            unpartitioned_flags
        };
        ret.insert(ret.end(), partition_words.begin(), partition_words.end());
        return ret;
    }

    uint32_t unpartitioned_flags;
    bool singleton;
    std::vector<partition> partitions;
};

struct vector_table_item : public single_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_VECTOR_TABLE; }
    vector_table_item() = default;

    explicit vector_table_item(uint32_t addr) : addr(addr) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        if (decode_size(header) != 2) {
            printf("Bad block size %d for vector table item\n", decode_size(header));
            assert(false);
        }
        return std::make_shared<vector_table_item>(*it++);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        return {encode_type_and_size(2), addr};
    }

    uint32_t addr;
};

struct rolling_window_delta_item : public single_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_ROLLING_WINDOW_DELTA; }
    rolling_window_delta_item() = default;

    explicit rolling_window_delta_item(uint32_t addr) : addr(addr) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        if (decode_size(header) != 2) {
            printf("Bad block size %d for rolling window delta item\n", decode_size(header));
            assert(false);
        }
        return std::make_shared<rolling_window_delta_item>(*it++);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        return {encode_type_and_size(2), (uint32_t)addr};
    }

    int32_t addr;
};

struct entry_point_item : public single_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_ENTRY_POINT; }
    entry_point_item() = default;

    explicit entry_point_item(uint32_t ep, uint32_t sp) : ep(ep), sp(sp) {}
    explicit entry_point_item(uint32_t ep, uint32_t sp, uint32_t splim) : ep(ep), sp(sp), splim(splim), splim_set(true) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        uint32_t size = decode_size(header);
        uint32_t ep = *it++;
        uint32_t sp = *it++;
        if (size == 3) {
            return std::make_shared<entry_point_item>(ep, sp);
        } else if (size == 4) {
            uint32_t splim = *it++;
            return std::make_shared<entry_point_item>(ep, sp, splim);
        } else {
            printf("Bad block size %d for entry point item\n", size);
            assert(false);
            return nullptr;
        }
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        std::vector<uint32_t> ret = {encode_type_and_size(splim_set ? 4 : 3), ep, sp};
        if (splim_set) ret.push_back(splim);
        return ret;
    }

    uint32_t ep;
    uint32_t sp;
    uint32_t splim;
    bool splim_set = false;
};

struct load_map_item : public item {
    struct entry {
        uint32_t storage_address;
        uint32_t runtime_address;
        uint32_t size;
    };
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_LOAD_MAP; }
    load_map_item() = default;

    explicit load_map_item(bool absolute, std::vector<entry> entries) : absolute(absolute), entries(entries) {}
    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header, uint32_t current_addr) {
        uint8_t num_entries = (header >> 24) & 0x7f;
        bool absolute = (header >> 24) & 0x80;
        std::vector<entry> entries;
        for (int i=0; i < num_entries; i++) {
            uint32_t storage_address = (uint32_t)*it++;
            uint32_t runtime_address = (uint32_t)*it++;
            uint32_t size = (uint32_t)*it++;
            if (storage_address != 0) {
                if (absolute) {
                    size -= runtime_address;
                } else {
                    storage_address += current_addr;
                }
            }
            entries.push_back({
                storage_address,
                runtime_address,
                size
            });
        }

        return std::make_shared<load_map_item>(absolute, entries);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        assert(entries.size() < 256);
        std::vector<uint32_t> rc = {
                encode_type_and_size(1 + 3 * entries.size()) | (((uint8_t)entries.size())<<24) | (absolute ? 1 << 31 : 0)
        };
        for(const auto &entry : entries) {
            // todo byte order
            if (absolute) {
                rc.push_back(entry.storage_address);
                rc.push_back(entry.runtime_address);
                if (entry.storage_address != 0) {
                    rc.push_back(entry.runtime_address + entry.size);
                } else {
                    rc.push_back(entry.size);
                }
            } else {
                if (entry.storage_address != 0) {
                    rc.push_back(entry.storage_address - ctx.base_addr - ctx.word_offset * 4);
                } else {
                    rc.push_back(entry.storage_address);
                }
                rc.push_back(entry.runtime_address);
                rc.push_back(entry.size);
            }
        }
        return rc;
    }

    bool absolute;
    std::vector<entry> entries;
};

struct version_item : public item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_VERSION; }
    version_item() = default;
    explicit version_item(uint16_t major, uint16_t minor) : version_item(major, minor, 0, {}) {}
    version_item(uint16_t major, uint16_t minor, uint16_t rollback, std::vector<uint16_t> otp_rows) :
        rollback(rollback), otp_rows(std::move(otp_rows)) {
      // NOTE: A linux sysroot might define `major` and `minor` as macros so we
      // can't use regular initialization in the form `major(major)` since we woudn't
      // be able to tell if we're calling a macro or not.
      this->major = major;
      this->minor = minor;
    }

    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        // todo validate
        uint32_t major_minor = *it++;
        uint16_t major = major_minor >> 16;
        uint16_t minor = major_minor & 0xffff;
        unsigned int otp_row_count = header >> 24;
        uint16_t rollback = 0;
        std::vector<uint16_t> otp_rows;
        if (otp_row_count) {
            unsigned int pair = *it++;
            // todo endian
            rollback = (uint16_t) pair;
            for(unsigned int i=0;i<otp_row_count;i++) {
                if (i&1) pair = *it++;
                otp_rows.push_back((uint16_t)(pair >> ((1^(i&1))*16)));
            }
        }
        return std::make_shared<version_item>(major, minor, rollback, otp_rows);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        assert(otp_rows.size() < 256);
        unsigned int size = 2 + (!otp_rows.empty() + otp_rows.size() + 1) / 2;
        std::vector<uint32_t> rc = {
                (uint32_t)(encode_type_and_size(size) | (otp_rows.size() << 24))
        };
        uint32_t major_minor = (uint16_t)major << 16 | (uint16_t)minor;
        rc.push_back(major_minor);
        if (!otp_rows.empty()) {
            rc.push_back(rollback);
            for(unsigned int i=0;i<otp_rows.size();i++) {
                if (i&1) {
                    rc.push_back(otp_rows[i]);
                } else {
                    rc[rc.size()-1] |= otp_rows[i] << 16;
                }
            }
        }
        return rc;
    }

    uint16_t major;
    uint16_t minor;
    uint16_t rollback;
    std::vector<uint16_t> otp_rows;
};

struct hash_def_item : public single_byte_size_item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_1BS_HASH_DEF; }
    hash_def_item() = default;

    explicit hash_def_item(uint8_t hash_type) : hash_type(hash_type) {}
    explicit hash_def_item(uint8_t hash_type, uint16_t block_words_to_hash) : hash_type(hash_type), block_words_to_hash(block_words_to_hash) {}

    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        uint8_t hash_type = (header & 0xff000000) >> 24;
        uint16_t block_words_to_hash = *it++;
        return std::make_shared<hash_def_item>(hash_type, block_words_to_hash);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        return {
            encode_type_and_size(2) | (hash_type << 24),
            block_words_to_hash == 0 ? (ctx.word_offset + 2) : block_words_to_hash
        };
    }

    uint8_t hash_type;
    uint16_t block_words_to_hash = 0;
};

struct signature_item : public item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_SIGNATURE; }
    signature_item() = default;

    explicit signature_item(uint8_t sig_type) : sig_type(sig_type) {}
    explicit signature_item(uint8_t sig_type, std::vector<uint8_t> signature_bytes, std::vector<uint8_t> public_key_bytes) : sig_type(sig_type), signature_bytes(signature_bytes), public_key_bytes(public_key_bytes) {}

    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        auto sig_block_size = decode_size(header);
        uint8_t sig_type = (header & 0xff000000) >> 24;
        assert(sig_block_size == 0x21);

        std::vector<uint8_t> signature_bytes;
        std::vector<uint8_t> public_key_bytes;        

        {
            std::vector<uint32_t> words;
            for (int i=0; i < 16; i++) {
                words.push_back(*it++);
            }
            auto bytes = words_to_lsb_bytes(words.begin(), words.end());
            std::copy(bytes.begin(), bytes.end(), std::back_inserter(public_key_bytes));
        }
        {
            std::vector<uint32_t> words;
            for (int i=0; i < 16; i++) {
                words.push_back(*it++);
            }
            auto bytes = words_to_lsb_bytes(words.begin(), words.end());
            std::copy(bytes.begin(), bytes.end(), std::back_inserter(signature_bytes));
        }
        return std::make_shared<signature_item>(sig_type, signature_bytes, public_key_bytes);
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        assert(signature_bytes.size() % 4 == 0);
        assert(public_key_bytes.size() % 4 == 0);
        std::vector<uint32_t> rc = {
                encode_type_and_size(1 + public_key_bytes.size()/4 + signature_bytes.size()/4) | (sig_type << 24),
        };
        auto words = lsb_bytes_to_words(public_key_bytes.begin(), public_key_bytes.end());
        std::copy(words.begin(), words.end(), std::back_inserter(rc));
        words = lsb_bytes_to_words(signature_bytes.begin(), signature_bytes.end());
        std::copy(words.begin(), words.end(), std::back_inserter(rc));
        return rc;
    }

    uint8_t sig_type;
    std::vector<uint8_t> signature_bytes;
    std::vector<uint8_t> public_key_bytes;
};

struct hash_value_item : public item {
    uint8_t type() const override { return PICOBIN_BLOCK_ITEM_HASH_VALUE; }
    hash_value_item() = default;

    explicit hash_value_item(std::vector<uint8_t> hash_bytes) : hash_bytes(hash_bytes) {}

    template <typename I> static std::shared_ptr<item> parse(I& it, I end, uint32_t header) {
        auto hash_size = decode_size(header) - 1;
        std::vector<uint32_t> hash_words;
        for (unsigned int i=0; i < hash_size; i++) {
            hash_words.push_back(*it++);
        }
        return std::make_shared<hash_value_item>(words_to_lsb_bytes(hash_words.begin(), hash_words.end()));
    }

    std::vector<uint32_t> to_words(item_writer_context& ctx) const override {
        assert(hash_bytes.size() % 4 == 0);
        std::vector<uint32_t> rc = {
                encode_type_and_size(1 + hash_bytes.size()/4),
        };
        auto words = lsb_bytes_to_words(hash_bytes.begin(), hash_bytes.end());
        std::copy(words.begin(), words.end(), std::back_inserter(rc));
        return rc;
    }

    std::vector<uint8_t> hash_bytes;
};


struct block {
    explicit block(uint32_t physical_addr, uint32_t next_block_rel=0, uint32_t next_block_rel_index=0, std::vector<std::shared_ptr<item>> items = {})
        : physical_addr(physical_addr),
          next_block_rel(next_block_rel),
          next_block_rel_index(next_block_rel_index),
          items( std::move(items)) {}
    template <typename I> static std::unique_ptr<block> parse(uint32_t physical_addr, I next_block_rel_loc, I it, I end) {
        I block_base = it;
        uint32_t current_addr = physical_addr + 4; // for the ffffded3
        std::vector<std::shared_ptr<item>> items;
        while (it < end) {
            auto item_base = it;
            uint32_t header = *it++;
            uint32_t size = item::decode_size(header);
            std::shared_ptr<item> i;
            switch ((uint8_t)header) {
                case PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE:
                    i = image_type_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_PARTITION_TABLE:
                    i = partition_table_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_1BS_VECTOR_TABLE:
                    i = vector_table_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_1BS_ROLLING_WINDOW_DELTA:
                    i = rolling_window_delta_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_1BS_VERSION:
                    i = version_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_1BS_ENTRY_POINT:
                    i = entry_point_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_LOAD_MAP:
                    i = load_map_item::parse(it, end, header, current_addr);
                    break;
                case PICOBIN_BLOCK_ITEM_1BS_HASH_DEF:
                    i = hash_def_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_HASH_VALUE:
                    i = hash_value_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_SIGNATURE:
                    i = signature_item::parse(it, end, header);
                    break;
                case PICOBIN_BLOCK_ITEM_2BS_IGNORED:
                    i = ignored_item::parse(it, end, header);
                    break;
                default:
                    DEBUG_LOG("Ignoring block type: %02x\n", (uint8_t)header);
                    i = ignored_item::parse(it, end, header);
                    break;
            }
            if (i) {
                items.push_back(i);
            }
            current_addr += (size*4);
            if (it != item_base + size) {
                printf("WARNING: item at %08x had wrong size %x (expected %x)\n", (unsigned int)(physical_addr + (item_base - block_base) * 4), (int)(it - item_base), (int)size);
                it = item_base + size;
                assert(false);
            }
        }
        uint32_t next_block_rel = *next_block_rel_loc;
        uint32_t next_block_rel_index = next_block_rel_loc - block_base + 1;
        return std::make_unique<block>(physical_addr, next_block_rel, next_block_rel_index, items);
    }
    std::vector<uint32_t> to_words() {
        std::vector<uint32_t> words;
        words.push_back(PICOBIN_BLOCK_MARKER_START);
        item_writer_context ctx(physical_addr);
        for(const auto &item : items) {
            ctx.word_offset = words.size();
            ctx.item_word_offsets[item] = ctx.word_offset;
            const auto item_words = item->to_words(ctx);
            std::copy(item_words.begin(), item_words.end(), std::back_inserter(words));
        }
        // todo should use a real item struct

        assert(words.size() <= PICOBIN_MAX_BLOCK_SIZE - 3);
        // todo byte order
        words.push_back(PICOBIN_BLOCK_ITEM_2BS_LAST | (words.size() - 1) << 8);
        words.push_back(next_block_rel);
        words.push_back(PICOBIN_BLOCK_MARKER_END);
        return words;
    }

    template <typename I> std::shared_ptr<I> get_item() {
        I tmp = I();
        uint8_t type = tmp.type();
        auto it = std::find_if(items.begin(), items.end(), [type](std::shared_ptr<item> i) { return i->type() == type; });
        if (it != std::end(items)) {
            return std::dynamic_pointer_cast<I>(*it);
        } else {
            return nullptr;
        }
    }

    uint32_t physical_addr;
    uint32_t next_block_rel;
    uint32_t next_block_rel_index;
    std::vector<std::shared_ptr<item>> items;
};
