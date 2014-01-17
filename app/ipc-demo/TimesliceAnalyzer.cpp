// Copyright 2013 Jan de Cuveland <cmail@cuveland.de>

#include "TimesliceAnalyzer.hpp"
#include <iostream>
#include <sstream>

bool TimesliceAnalyzer::check_flesnet_pattern(const fles::MicrosliceDescriptor
                                              & descriptor,
                                              const uint64_t* content,
                                              size_t component)
{
    uint32_t crc = 0x00000000;
    for (size_t pos = 0; pos < descriptor.size / sizeof(uint64_t); ++pos) {
        uint64_t data_word = content[pos];
        crc ^= (data_word & 0xffffffff) ^ (data_word >> 32);
        uint64_t expected = (static_cast<uint64_t>(component) << 48)
                            | (pos * sizeof(uint64_t));
        if (data_word != expected)
            return false;
    }
    if (crc != descriptor.crc)
        return false;
    return true;
}

bool TimesliceAnalyzer::check_cbmnet_content(const uint16_t* content,
                                             size_t size)
{
    constexpr uint16_t source_address = 0;

    if (content[0] != source_address) {
        std::cerr << "unexpected source address: " << content[0] << std::endl;
        return false;
    }

    for (size_t i = 1; i < size - 1; ++i) {
        uint8_t low = content[i] & 0xff;
        uint8_t high = (content[i] >> 8) & 0xff;
        if (high != 0xbc || low != i - 1) {
            std::cerr << "unexpected cbmnet content word: " << content[i]
                      << std::endl;
            return false;
        }
    }

    return true;
}

bool TimesliceAnalyzer::check_cbmnet_frames(const uint16_t* content,
                                            size_t size)
{
    size_t frame_count = 0;
    uint8_t previous_frame_number = 0;
    size_t i = 0;
    while (i < size) {
        uint8_t frame_number = (content[i] >> 8) & 0xff;
        uint8_t word_count = (content[i] & 0xff)
                             + 1; // FIXME: Definition in hardware
        uint8_t padding_count = (4 - ((word_count + 1) & 0x3)) & 0x3;
        ++i;

        uint8_t expected_frame_number = previous_frame_number + 1;
        if (previous_frame_number != 0 && frame_number
                                          != expected_frame_number) {
            std::cerr << "unexpected cbmnet frame number" << std::endl;
            return false;
        }
        previous_frame_number = frame_number;

        if (word_count < 4 || word_count > 64 || i + word_count + padding_count
                                                 > size) {
            std::cerr << "invalid cbmnet frame word count: " << word_count
                      << std::endl;
            return false;
        }

        ++frame_count;
        if (check_cbmnet_content(&content[i], word_count) == false)
            return false;
        i += word_count + padding_count;
    }

    return true;
}

bool TimesliceAnalyzer::check_flib_pattern(const fles::MicrosliceDescriptor
                                           & descriptor,
                                           const uint64_t* content,
                                           size_t /* component */)
{
    if (content[0] != reinterpret_cast<const uint64_t*>(&descriptor)[0]
        || content[1] != reinterpret_cast<const uint64_t*>(&descriptor)[1]) {
        return false;
    }
    return check_cbmnet_frames(reinterpret_cast<const uint16_t*>(&content[2]),
                               (descriptor.size - 16) / sizeof(uint16_t));
}

bool TimesliceAnalyzer::check_microslice(const fles::MicrosliceDescriptor
                                         & descriptor,
                                         const uint64_t* content,
                                         size_t component, size_t microslice)
{
    if (descriptor.idx != microslice) {
        std::cerr << "microslice index " << descriptor.idx
                  << " found in descriptor " << microslice << std::endl;
        return false;
    }

    ++_microslice_count;
    _content_bytes += descriptor.size;

    switch (descriptor.sys_id) {
    case 0x01:
        return check_flesnet_pattern(descriptor, content, component);
    case 0xBC:
        return check_flib_pattern(descriptor, content, component);
    default:
        std::cerr << "unknown subsystem identifier" << std::endl;
        return false;
    }
    return true;
}

bool TimesliceAnalyzer::check_timeslice(const fles::Timeslice& ts)
{
    if (ts.num_components() == 0) {
        std::cerr << "no component in TS " << ts.index() << std::endl;
        return false;
    }

    ++_timeslice_count;

    for (size_t c = 0; c < ts.num_components(); ++c) {
        if (ts.num_microslices(c) == 0) {
            std::cerr << "no microslices in TS " << ts.index() << ", component "
                      << c << std::endl;
            return false;
        }
        for (size_t m = 0; m < ts.num_microslices(c); ++m) {
            bool success = check_microslice(
                ts.descriptor(c, m),
                reinterpret_cast<const uint64_t*>(ts.content(c, m)), c,
                ts.index() * ts.num_core_microslices() + m);
            if (!success) {
                std::cerr << "pattern error in TS " << ts.index() << ", MC "
                          << m << ", component " << c << std::endl;
                return false;
            }
        }
    }
    return true;
}

std::string TimesliceAnalyzer::statistics() const
{
    std::stringstream s;
    s << "timeslices checked: " << _timeslice_count << " (" << _content_bytes
      << " bytes in " << _microslice_count << " microslices, avg: "
      << static_cast<double>(_content_bytes) / _microslice_count << ")";
    return s.str();
}