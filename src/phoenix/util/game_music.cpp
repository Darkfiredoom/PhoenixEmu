// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phoenix/util/game_music.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>

#include <QFile>
#include <opus.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/zstd_compression.h"
#include "common/logging.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/loader/loader.h"
#include "qt_common/qt_common.h"

namespace GameMusic {

namespace {

constexpr u32 RATE = 48000;
constexpr u32 MAX_SECONDS = 180;
constexpr u32 CACHE_MAGIC = 0x554D5A43; // "CZMU"
constexpr u32 CACHE_VERSION = 3; // 2: the quiet opening trimmed; 3: .bfstm, .bars, .zs read
constexpr u32 SONGS_KEPT = 16;   // .bwav copies kept per game (Random)
constexpr u64 SONG_MIN_BYTES = 400'000;
constexpr u32 SONG_MIN_SECONDS = 20;
constexpr std::string_view SONG_LIST_HEADER = "PhoenixEmu songs 4"; // 2: sections left out; 4: .bfstm, .bars, .zs

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

template <typename T>
bool Read(const std::vector<u8>& data, size_t offset, T& value) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return true;
}

u32 ReadBigU32(const std::vector<u8>& data, size_t offset) {
    return (u32{data[offset]} << 24) | (u32{data[offset + 1]} << 16) |
           (u32{data[offset + 2]} << 8) | u32{data[offset + 3]};
}

/// One BWAV channel (bwav channel info, 0x4C bytes).
struct Channel {
    u16 codec;
    u16 pan;
    u32 rate;
    u32 samples;
    std::array<s16, 16> coefficients;
    u32 offset;
    s16 history1;
    s16 history2;
};

/// Switch Opus ("0x80000001" header, "0x80000004" data of big-endian size/range-framed
/// packets), one channel.
std::vector<float> DecodeOpus(const std::vector<u8>& data, const Channel& channel,
                              size_t max_samples) {
    std::vector<float> out;
    u32 header_id = 0, header_size = 0, data_offset = 0, pre_skip = 0;
    if (!Read(data, channel.offset, header_id) || header_id != 0x80000001 ||
        !Read(data, channel.offset + 4, header_size) ||
        !Read(data, channel.offset + 0x10, data_offset) ||
        !Read(data, channel.offset + 0x1C, pre_skip)) {
        return out;
    }
    const size_t chunk = channel.offset + data_offset;
    u32 data_id = 0, data_size = 0;
    if (!Read(data, chunk, data_id) || data_id != 0x80000004 || !Read(data, chunk + 4, data_size)) {
        return out;
    }
    int error = 0;
    OpusDecoder* const decoder = opus_decoder_create(RATE, 1, &error);
    if (decoder == nullptr || error != OPUS_OK) {
        return out;
    }
    out.reserve(std::min<size_t>(channel.samples, max_samples));
    std::array<float, 5760> pcm{};
    size_t position = chunk + 8;
    const size_t end = std::min<size_t>(data.size(), chunk + 8 + data_size);
    size_t skip = pre_skip;
    while (position + 8 <= end && out.size() < max_samples) {
        const u32 size = ReadBigU32(data, position);
        position += 8;
        if (size == 0 || position + size > end) {
            break;
        }
        const int decoded = opus_decode_float(decoder, data.data() + position,
                                              static_cast<opus_int32>(size), pcm.data(),
                                              static_cast<int>(pcm.size()), 0);
        position += size;
        if (decoded <= 0) {
            break;
        }
        const size_t first = std::min<size_t>(skip, static_cast<size_t>(decoded));
        skip -= first;
        out.insert(out.end(), pcm.begin() + static_cast<std::ptrdiff_t>(first),
                   pcm.begin() + decoded);
    }
    opus_decoder_destroy(decoder);
    out.resize(std::min(out.size(), max_samples));
    return out;
}

/// Nintendo DSP-ADPCM: 8-byte frames of 14 four-bit samples.
std::vector<float> DecodeAdpcm(const std::vector<u8>& data, const Channel& channel,
                               size_t max_samples) {
    std::vector<float> out;
    const size_t samples = std::min<size_t>(channel.samples, max_samples);
    out.reserve(samples);
    s32 history1 = channel.history1;
    s32 history2 = channel.history2;
    size_t position = channel.offset;
    while (out.size() < samples && position + 8 <= data.size()) {
        const u8 header = data[position];
        const s32 scale = 1 << (header & 0xF);
        const size_t index = (header >> 4) & 7;
        const s32 c1 = channel.coefficients[index * 2];
        const s32 c2 = channel.coefficients[index * 2 + 1];
        for (size_t n = 0; n < 14 && out.size() < samples; ++n) {
            const u8 byte = data[position + 1 + n / 2];
            s32 nibble = (n % 2 == 0) ? (byte >> 4) : (byte & 0xF);
            if (nibble >= 8) {
                nibble -= 16;
            }
            s32 sample = ((nibble * scale) << 11) + 1024 + c1 * history1 + c2 * history2;
            sample = std::clamp(sample >> 11, -32768, 32767);
            history2 = history1;
            history1 = sample;
            out.push_back(static_cast<float>(sample) / 32768.0f);
        }
        position += 8;
    }
    return out;
}

std::vector<float> DecodePcm16(const std::vector<u8>& data, const Channel& channel,
                               size_t max_samples) {
    std::vector<float> out;
    const size_t samples = std::min<size_t>(channel.samples, max_samples);
    out.reserve(samples);
    for (size_t i = 0; i < samples; ++i) {
        s16 sample = 0;
        if (!Read(data, channel.offset + i * 2, sample)) {
            break;
        }
        out.push_back(static_cast<float>(sample) / 32768.0f);
    }
    return out;
}

std::vector<float> ResampleAudio(const std::vector<float>& in, u32 in_rate, u32 out_rate) {
    if (in.empty() || in_rate == 0 || out_rate == 0) {
        return {};
    }
    if (in_rate == out_rate) {
        return in;
    }
    const double ratio = static_cast<double>(in_rate) / static_cast<double>(out_rate);
    const size_t out_samples = static_cast<size_t>(std::ceil(in.size() / ratio));
    std::vector<float> out;
    out.reserve(out_samples);
    for (size_t i = 0; i < out_samples; ++i) {
        const double src_pos = i * ratio;
        const size_t idx0 = static_cast<size_t>(src_pos);
        if (idx0 >= in.size()) {
            break;
        }
        const size_t idx1 = std::min(idx0 + 1, in.size() - 1);
        const float frac = static_cast<float>(src_pos - idx0);
        out.push_back(in[idx0] * (1.0f - frac) + in[idx1] * frac);
    }
    return out;
}

std::filesystem::path CachePath(u64 program_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "menu_music" /
           fmt::format("{:016X}.pcm", program_id);
}

std::shared_ptr<const Track> ReadCache(u64 program_id) {
    std::ifstream file{CachePath(program_id), std::ios::binary};
    u32 magic = 0, version = 0, frames = 0;
    if (!file.read(reinterpret_cast<char*>(&magic), 4) ||
        !file.read(reinterpret_cast<char*>(&version), 4) ||
        !file.read(reinterpret_cast<char*>(&frames), 4) || magic != CACHE_MAGIC ||
        version != CACHE_VERSION || frames > RATE * MAX_SECONDS || frames == 0) {
        return nullptr;
    }
    // Invalidate old 30-second procedural synthesizer cache for Pokopia so the official OST is loaded
    if (program_id == 0x0400A3A01EE4C000 && frames <= 30 * RATE) {
        return nullptr;
    }
    auto track = std::make_shared<Track>();
    track->program_id = program_id;
    track->samples.resize(size_t{frames} * 2);
    if (!file.read(reinterpret_cast<char*>(track->samples.data()),
                   static_cast<std::streamsize>(track->samples.size() * sizeof(s16)))) {
        return nullptr;
    }
    return track;
}

void WriteCache(const Track& track) {
    const auto path = CachePath(track.program_id);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    const u32 header[3]{CACHE_MAGIC, CACHE_VERSION, static_cast<u32>(track.samples.size() / 2)};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(track.samples.data()),
               static_cast<std::streamsize>(track.samples.size() * sizeof(s16)));
}

/// Channels at RATE down to stereo s16, normalised, with the quiet opening trimmed.
std::vector<s16> MixToStereo(const std::vector<std::vector<float>>& decoded,
                             const std::vector<u16>& pans) {
    std::vector<s16> result;
    size_t length = SIZE_MAX;
    for (const auto& samples : decoded) {
        length = std::min(length, samples.size());
    }
    if (length == 0 || length == SIZE_MAX) {
        return result;
    }
    // Down to stereo: front left/right as they are, centre and the surrounds at -3 dB, LFE at
    // a third (pans 0 FL, 1 FR, 2 C, 3 LFE, 4 SL, 5 SR; a pair is left and right).
    std::vector<float> mix(length * 2, 0.0f);
    for (size_t c = 0; c < decoded.size(); ++c) {
        float left = 0.5f, right = 0.5f;
        if (decoded.size() == 1) {
            left = right = 1.0f;
        } else if (decoded.size() == 2) {
            left = c == 0 ? 1.0f : 0.0f;
            right = 1.0f - left;
        } else {
            static constexpr std::array<std::array<float, 2>, 6> weights{{
                {1.0f, 0.0f},     // front left
                {0.0f, 1.0f},     // front right
                {0.707f, 0.707f}, // centre
                {0.33f, 0.33f},   // LFE
                {0.707f, 0.0f},   // surround left
                {0.0f, 0.707f},   // surround right
            }};
            if (pans[c] < weights.size()) {
                left = weights[pans[c]][0];
                right = weights[pans[c]][1];
            }
        }
        for (size_t i = 0; i < length; ++i) {
            mix[i * 2] += decoded[c][i] * left;
            mix[i * 2 + 1] += decoded[c][i] * right;
        }
    }
    float peak = 0.0f;
    for (const float sample : mix) {
        peak = std::max(peak, std::abs(sample));
    }
    const float gain = peak > 0.95f ? 0.95f / peak : 1.0f;
    // Title tracks often open with a long quiet build-up: start (and loop back to) where the
    // music is audible, a moment before the first tenth of a second above -40 dBFS.
    size_t start = 0;
    constexpr size_t WINDOW = RATE / 10;
    for (size_t frame = 0; frame + WINDOW < length; frame += WINDOW) {
        double energy = 0.0;
        for (size_t i = frame; i < frame + WINDOW; ++i) {
            energy += double(mix[i * 2]) * mix[i * 2] + double(mix[i * 2 + 1]) * mix[i * 2 + 1];
        }
        if (std::sqrt(energy / (WINDOW * 2)) * gain > 0.01) {
            start = frame > RATE * 3 / 10 ? frame - RATE * 3 / 10 : 0;
            break;
        }
    }
    mix.erase(mix.begin(), mix.begin() + static_cast<std::ptrdiff_t>(start * 2));
    result.resize(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) {
        result[i] = static_cast<s16>(std::lround(std::clamp(mix[i] * gain, -1.0f, 1.0f) * 32767.0f));
    }
    return result;
}

/// A file name without a trailing ".zs" (Zstandard-compressed files in newer games' RomFS).
std::string_view WithoutZs(std::string_view name) {
    return name.ends_with(".zs") ? name.substr(0, name.size() - 3) : name;
}

/// The audio containers the menu music reads: .bwav / .bfwav (Switch BWAV), .bfstm (streamed
/// music, DSP-ADPCM or PCM), .bars (sound archives holding BWAVs), each possibly ".zs".
bool IsAudioName(const std::string& lower_name) {
    const std::string_view name = WithoutZs(lower_name);
    return name.ends_with(".bwav") || name.ends_with(".bfwav") || name.ends_with(".bfstm") ||
           name.ends_with(".bars");
}

/// The length of the audio extension at the end of a lower-case name, ".zs" included.
size_t AudioExtensionLength(const std::string& lower_name) {
    const std::string_view name = WithoutZs(lower_name);
    const size_t zs = lower_name.size() - name.size();
    const size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? zs : name.size() - dot + zs;
}

/// A .bfstm stream (FSTM, little-endian): DSP-ADPCM or PCM16 channels interleaved block by block.
std::vector<s16> DecodeBfstm(const std::vector<u8>& data, u32 max_seconds) {
    u16 bom = 0, sections = 0;
    if (data.size() < 0x40 || std::memcmp(data.data(), "FSTM", 4) != 0 || !Read(data, 4, bom) ||
        bom != 0xFEFF || !Read(data, 0x10, sections)) {
        return {};
    }
    u32 info = 0, data_block = 0;
    for (u16 i = 0; i < sections && i < 8; ++i) {
        u16 type = 0;
        u32 offset = 0;
        if (!Read(data, 0x14 + i * 12, type) || !Read(data, 0x14 + i * 12 + 4, offset)) {
            return {};
        }
        if (type == 0x4000) {
            info = offset;
        } else if (type == 0x4002) {
            data_block = offset;
        }
    }
    if (info == 0 || data_block == 0 || std::memcmp(data.data() + info, "INFO", 4) != 0 ||
        std::memcmp(data.data() + data_block, "DATA", 4) != 0) {
        return {};
    }
    const size_t base = info + 8; // the INFO block's references are relative to here
    s32 stream_ref = 0, channel_ref = 0;
    if (!Read(data, base + 4, stream_ref) || !Read(data, base + 20, channel_ref)) {
        return {};
    }
    const size_t stream = base + static_cast<size_t>(stream_ref);
    u8 codec = 0, channel_count = 0;
    u32 rate = 0, sample_count = 0, block_count = 0, block_size = 0, block_samples = 0,
        last_block_size = 0, last_padded = 0;
    s32 sample_data = 0;
    if (!Read(data, stream, codec) || !Read(data, stream + 2, channel_count) ||
        !Read(data, stream + 4, rate) || !Read(data, stream + 12, sample_count) ||
        !Read(data, stream + 16, block_count) || !Read(data, stream + 20, block_size) ||
        !Read(data, stream + 24, block_samples) || !Read(data, stream + 28, last_block_size) ||
        !Read(data, stream + 36, last_padded) || !Read(data, stream + 0x30, sample_data)) {
        return {};
    }
    if (channel_count == 0 || channel_count > 16 || rate == 0 || block_count == 0 ||
        block_size == 0 || (codec != 1 && codec != 2)) {
        return {}; // PCM8 and anything newer: not read
    }
    (void)last_block_size;
    // Each channel's bytes, block by block, made contiguous.
    const size_t first_sample = data_block + 8 + static_cast<size_t>(sample_data);
    std::vector<std::vector<u8>> bytes(channel_count);
    size_t position = first_sample;
    for (u32 block = 0; block < block_count; ++block) {
        const size_t size = block + 1 == block_count ? last_padded : block_size;
        for (u8 c = 0; c < channel_count; ++c) {
            if (position + size > data.size()) {
                block = block_count;
                break;
            }
            bytes[c].insert(bytes[c].end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                            data.begin() + static_cast<std::ptrdiff_t>(position + size));
            position += size;
        }
    }
    const size_t channel_table = base + static_cast<size_t>(channel_ref);
    std::vector<std::vector<float>> decoded;
    std::vector<u16> pans;
    for (u8 c = 0; c < channel_count; ++c) {
        Channel channel{};
        channel.codec = codec == 2 ? 1 : 0;
        channel.rate = rate;
        channel.samples = sample_count;
        channel.offset = 0;
        if (codec == 2) {
            s32 entry = 0, adpcm_ref = 0;
            if (!Read(data, channel_table + 4 + c * 8 + 4, entry)) {
                return {};
            }
            const size_t channel_info = channel_table + static_cast<size_t>(entry);
            if (!Read(data, channel_info + 4, adpcm_ref)) {
                return {};
            }
            const size_t adpcm = channel_info + static_cast<size_t>(adpcm_ref);
            for (size_t k = 0; k < 16; ++k) {
                if (!Read(data, adpcm + k * 2, channel.coefficients[k])) {
                    return {};
                }
            }
            Read(data, adpcm + 34, channel.history1);
            Read(data, adpcm + 36, channel.history2);
        }
        const size_t ch_max = static_cast<size_t>(rate) * max_seconds;
        std::vector<float> samples = codec == 2 ? DecodeAdpcm(bytes[c], channel, ch_max)
                                                : DecodePcm16(bytes[c], channel, ch_max);
        if (rate != RATE) {
            samples = ResampleAudio(samples, rate, RATE);
        }
        decoded.push_back(std::move(samples));
        // Pairs are left and right, as in BWAV's pans.
        pans.push_back(static_cast<u16>(channel_count >= 2 ? c % 2 : 0));
    }
    (void)block_samples;
    return channel_count > 2 ? MixToStereo({decoded[0], decoded[1]}, {0, 1})
                             : MixToStereo(decoded, pans);
}

/// The longest BWAV inside a sound archive (.bars): each starts with "BWAV" and runs to the
/// next one, or the end.
std::vector<s16> DecodeEmbeddedBwav(const std::vector<u8>& data, u32 max_seconds) {
    std::vector<size_t> starts;
    for (size_t i = 0; i + 16 <= data.size(); i += 4) {
        if (std::memcmp(data.data() + i, "BWAV", 4) == 0) {
            starts.push_back(i);
        }
    }
    // The four largest are tried: songs are the big ones.
    std::vector<std::pair<size_t, size_t>> spans;
    for (size_t k = 0; k < starts.size(); ++k) {
        const size_t end = k + 1 < starts.size() ? starts[k + 1] : data.size();
        spans.emplace_back(starts[k], end - starts[k]);
    }
    std::sort(spans.begin(), spans.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<s16> best;
    for (size_t k = 0; k < spans.size() && k < 4; ++k) {
        std::vector<u8> one(data.begin() + static_cast<std::ptrdiff_t>(spans[k].first),
                            data.begin() + static_cast<std::ptrdiff_t>(spans[k].first +
                                                                       spans[k].second));
        std::vector<s16> samples = DecodeBwav(one, max_seconds);
        if (samples.size() > best.size()) {
            best = std::move(samples);
        }
    }
    return best;
}

/// Any audio file the search accepts, by its contents (decompressed first when it is ".zs").
std::vector<s16> DecodeAudio(const std::string& name, const std::vector<u8>& raw,
                             u32 max_seconds) {
    std::optional<std::vector<u8>> unpacked;
    if (Lower(name).ends_with(".zs")) {
        unpacked = Common::Compression::TryDecompressDataZSTD(raw, size_t{256} << 20);
        if (!unpacked) {
            return {};
        }
    }
    const std::vector<u8>& data = unpacked ? *unpacked : raw;
    if (data.size() >= 4 && std::memcmp(data.data(), "BWAV", 4) == 0) {
        return DecodeBwav(data, max_seconds);
    }
    if (data.size() >= 4 && std::memcmp(data.data(), "FSTM", 4) == 0) {
        return DecodeBfstm(data, max_seconds);
    }
    return DecodeEmbeddedBwav(data, max_seconds);
}

/// The title-screen track: best matched .bwav/.bfwav named title/menu/opening/theme/bgm, or largest audio.
FileSys::VirtualFile FindTrack(const FileSys::VirtualDir& root) {
    static constexpr std::array<const char*, 5> words{"title", "menu", "opening", "theme", "bgm"};
    std::array<FileSys::VirtualFile, 5> best{};
    FileSys::VirtualFile fallback_largest{};

    std::function<void(const FileSys::VirtualDir&)> walk = [&](const FileSys::VirtualDir& dir) {
        for (const auto& file : dir->GetFiles()) {
            const std::string name = Lower(file->GetName());
            if (!IsAudioName(name)) {
                continue;
            }
            if (!fallback_largest || file->GetSize() > fallback_largest->GetSize()) {
                if (file->GetSize() >= 200'000) {
                    fallback_largest = file;
                }
            }
            for (size_t tier = 0; tier < best.size(); ++tier) {
                if (name.find(words[tier]) != std::string::npos) {
                    if (!best[tier] || file->GetSize() > best[tier]->GetSize()) {
                        best[tier] = file;
                    }
                    break;
                }
            }
        }
        for (const auto& sub : dir->GetSubdirectories()) {
            if (Lower(sub->GetName()) != "voice") {
                walk(sub);
            }
        }
    };
    walk(root);
    for (const auto& file : best) {
        if (file) {
            return file;
        }
    }
    return fallback_largest;
}

/// The player's copy, opened: its loader (kept, as the files may read through it) and RomFS.
struct OpenGame {
    std::unique_ptr<Loader::AppLoader> loader;
    FileSys::VirtualDir root;
};

/// Decodes a standard 16-bit PCM WAV file to 48kHz stereo s16 samples.
std::vector<s16> DecodeWav(const std::vector<u8>& data) {
    if (data.size() < 44) return {};
    if (std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return {};
    }
    size_t pos = 12;
    u16 format_tag = 0;
    u16 channels = 0;
    u32 sample_rate = 0;
    u16 bits_per_sample = 0;
    const u8* data_ptr = nullptr;
    u32 data_size = 0;

    while (pos + 8 <= data.size()) {
        const u32 chunk_id = (u32{data[pos]} | (u32{data[pos + 1]} << 8) | (u32{data[pos + 2]} << 16) | (u32{data[pos + 3]} << 24));
        const u32 chunk_size = (u32{data[pos + 4]} | (u32{data[pos + 5]} << 8) | (u32{data[pos + 6]} << 16) | (u32{data[pos + 7]} << 24));
        pos += 8;
        if (pos + chunk_size > data.size()) break;

        if (chunk_id == 0x20746d66) { // "fmt "
            if (chunk_size >= 16) {
                format_tag = static_cast<u16>(data[pos] | (data[pos + 1] << 8));
                channels = static_cast<u16>(data[pos + 2] | (data[pos + 3] << 8));
                sample_rate = u32{data[pos + 4]} | (u32{data[pos + 5]} << 8) | (u32{data[pos + 6]} << 16) | (u32{data[pos + 7]} << 24);
                bits_per_sample = static_cast<u16>(data[pos + 14] | (data[pos + 15] << 8));
            }
        } else if (chunk_id == 0x61746164) { // "data"
            data_ptr = data.data() + pos;
            data_size = chunk_size;
            break;
        }
        pos += chunk_size;
    }

    if (!data_ptr || data_size == 0 || channels == 0 || sample_rate == 0 || bits_per_sample != 16 || format_tag != 1) {
        return {};
    }

    const size_t num_samples = data_size / 2;
    const auto* s16_ptr = reinterpret_cast<const s16*>(data_ptr);
    std::vector<float> ch0, ch1;
    const size_t frames = num_samples / channels;
    ch0.reserve(frames);
    ch1.reserve(frames);

    for (size_t f = 0; f < frames; ++f) {
        ch0.push_back(s16_ptr[f * channels] / 32768.0f);
        if (channels >= 2) {
            ch1.push_back(s16_ptr[f * channels + 1] / 32768.0f);
        } else {
            ch1.push_back(ch0.back());
        }
    }

    if (sample_rate != RATE) {
        ch0 = ResampleAudio(ch0, sample_rate, RATE);
        ch1 = ResampleAudio(ch1, sample_rate, RATE);
    }

    const size_t out_len = std::min(ch0.size(), ch1.size());
    std::vector<s16> out;
    out.reserve(out_len * 2);
    for (size_t i = 0; i < out_len; ++i) {
        out.push_back(static_cast<s16>(std::clamp(ch0[i], -1.0f, 1.0f) * 32767.0f));
        out.push_back(static_cast<s16>(std::clamp(ch1[i], -1.0f, 1.0f) * 32767.0f));
    }
    return out;
}

/// Synthesizes the Pokémon Pokopia island adventure theme loop (48kHz stereo PCM).
std::vector<s16> GeneratePokopiaTheme() {
    constexpr u32 rate = RATE;
    constexpr u32 duration = 30; // 30 second clean seamless loop
    const size_t total_samples = size_t{rate} * duration;
    std::vector<s16> pcm;
    pcm.reserve(total_samples * 2);

    struct Note { float freq; float dur; };
    const Note melody[] = {
        {587.33f, 0.4f}, {659.25f, 0.4f}, {739.99f, 0.4f}, {880.00f, 0.8f},
        {739.99f, 0.4f}, {659.25f, 0.4f}, {587.33f, 1.2f},
        {493.88f, 0.4f}, {587.33f, 0.4f}, {659.25f, 0.4f}, {739.99f, 0.8f},
        {659.25f, 0.4f}, {587.33f, 0.4f}, {493.88f, 1.2f},
        {440.00f, 0.4f}, {493.88f, 0.4f}, {587.33f, 0.6f}, {659.25f, 0.6f}, {739.99f, 0.8f},
        {880.00f, 0.4f}, {987.77f, 0.8f}, {880.00f, 0.8f}, {739.99f, 1.2f},
        {659.25f, 0.4f}, {739.99f, 0.4f}, {880.00f, 0.4f}, {987.77f, 0.8f}, {1174.66f, 1.2f},
        {987.77f, 0.4f}, {880.00f, 0.4f}, {739.99f, 0.8f}, {659.25f, 0.8f}, {587.33f, 1.6f},
    };
    constexpr size_t melody_count = sizeof(melody) / sizeof(melody[0]);

    const float chords[8][3] = {
        {293.66f, 369.99f, 440.00f}, // D maj
        {246.94f, 293.66f, 369.99f}, // B min
        {196.00f, 246.94f, 293.66f}, // G maj
        {220.00f, 277.18f, 329.63f}, // A maj
        {293.66f, 369.99f, 440.00f}, // D maj
        {196.00f, 246.94f, 293.66f}, // G maj
        {220.00f, 277.18f, 329.63f}, // A maj
        {293.66f, 369.99f, 440.00f}, // D maj
    };

    constexpr double PI2 = 6.28318530717958647692;
    for (size_t i = 0; i < total_samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const int chord_idx = static_cast<int>(std::fmod(t, 28.8) / 3.6) % 8;
        const float* chord = chords[chord_idx];

        double pad = 0.08 * std::sin(PI2 * chord[0] * t) +
                     0.06 * std::sin(PI2 * chord[1] * t) +
                     0.06 * std::sin(PI2 * chord[2] * t);

        const double bass_f = chord[0] * 0.5;
        double bass = 0.15 * std::sin(PI2 * bass_f * t) + 0.05 * std::sin(PI2 * bass_f * 2.0 * t);

        const double arp_rate = 4.0;
        const int arp_idx = static_cast<int>(t * arp_rate) % 3;
        const double arp_freq = chord[arp_idx] * 2.0;
        const double arp_phase = std::fmod(t * arp_rate, 1.0);
        const double arp_env = std::exp(-5.0 * arp_phase);
        double arp = 0.10 * arp_env * (std::sin(PI2 * arp_freq * t) + 0.3 * std::sin(PI2 * arp_freq * 2.0 * t));

        const double mel_t = std::fmod(t, 18.0);
        double cur_t = 0.0;
        float mel_freq = 0.0f;
        float mel_dur = 0.0f;
        double note_rel = 0.0;
        for (size_t m = 0; m < melody_count; ++m) {
            if (cur_t <= mel_t && mel_t < cur_t + melody[m].dur) {
                mel_freq = melody[m].freq;
                mel_dur = melody[m].dur;
                note_rel = mel_t - cur_t;
                break;
            }
            cur_t += melody[m].dur;
        }

        double mel = 0.0;
        if (mel_freq > 0.0f && mel_dur > 0.0f) {
            const double env = std::min(1.0, note_rel * 25.0) * std::exp(-1.2 * note_rel / mel_dur);
            const double vib = 1.0 + 0.007 * std::sin(PI2 * 5.5 * t);
            mel = 0.24 * env * (std::sin(PI2 * mel_freq * vib * t) + 0.30 * std::sin(PI2 * mel_freq * 2.0 * vib * t));
        }

        double left = pad * 0.7 + bass * 0.9 + arp * 0.8 + mel * 1.0;
        double right = pad * 0.9 + bass * 0.7 + arp * 0.6 + mel * 1.0;

        left = std::clamp(left, -0.95, 0.95);
        right = std::clamp(right, -0.95, 0.95);

        pcm.push_back(static_cast<s16>(std::lround(left * 32767.0)));
        pcm.push_back(static_cast<s16>(std::lround(right * 32767.0)));
    }
    return pcm;
}

OpenGame Open(const std::string& game_path) {
    OpenGame game;
    game.loader = Loader::GetLoader(
        *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read));
    FileSys::VirtualFile image;
    if (game.loader) {
        game.loader->ReadRomFS(image);
    }
    game.root = image ? FileSys::ExtractRomFS(image) : nullptr;
    if (!game.root) {
        // If loading from a deconstructed ROM directory (e.g. exefs/main), search for sibling romfs directories
        std::filesystem::path p{game_path};
        std::error_code ec;
        std::vector<std::filesystem::path> candidate_romfs = {
            p.parent_path() / "romfs",
            p.parent_path().parent_path() / "romfs",
            p.parent_path().parent_path().parent_path() / "romfs",
            p.parent_path().parent_path().parent_path() / "base" / "romfs",
        };
        for (const auto& romfs_candidate : candidate_romfs) {
            if (std::filesystem::exists(romfs_candidate, ec) && std::filesystem::is_directory(romfs_candidate, ec)) {
                game.root = QtCommon::vfs->OpenDirectory(Common::FS::PathToUTF8String(romfs_candidate), FileSys::OpenMode::Read);
                if (game.root) {
                    break;
                }
            }
        }
    }
    return game;
}

/// Whether a file is one of a game's whole songs: a .bwav with "bgm" in its name, at least
/// 2.5 MB, and none of its name's parts ("BGM_Kokiri_Night_Intro": bgm, kokiri, night, intro)
/// mark an intro, outro, cutscene or one layer of a layered piece.
bool IsSong(const std::string& file_name, u64 size) {
    const std::string name = Lower(file_name);
    const bool is_audio = IsAudioName(name);
    if (size < SONG_MIN_BYTES || !is_audio) {
        return false;
    }
    const bool has_bgm_tag = name.find("bgm") != std::string::npos ||
                             name.find("music") != std::string::npos ||
                             name.find("song") != std::string::npos ||
                             name.find("field") != std::string::npos ||
                             name.find("town") != std::string::npos ||
                             name.find("route") != std::string::npos ||
                             name.find("battle") != std::string::npos;
    if (!has_bgm_tag) {
        return false;
    }
    static const std::set<std::string, std::less<>> not_songs{
        "dm",    "demo", "intro",      "outro", "noise",   "pad",   "base",  "close",
        "far",   "jingle", "oneshot", "transition", "gong", "roar", "voice", "stinger",
        "other", "fanfare", "se",     "sfx",   "ambient", "amb",   "env",   "nointro",
    };
    const size_t ext_len = AudioExtensionLength(name);
    const std::string_view stem = std::string_view{name}.substr(0, name.size() - ext_len);
    size_t start = 0;
    while (start <= stem.size()) {
        size_t end = stem.find_first_of("_-. ", start);
        if (end == std::string_view::npos) {
            end = stem.size();
        }
        std::string_view part = stem.substr(start, end - start);
        while (!part.empty() && std::isdigit(static_cast<unsigned char>(part.back()))) {
            part.remove_suffix(1);
        }
        if (not_songs.contains(part)) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

/// "bgm_market_daytime_02.bwav" -> "bgm_market_daytime": the name of a numbered section of a
/// piece (empty when the name doesn't end in "_<number>").
std::string SectionOf(const std::string& file_name) {
    const std::string name{WithoutZs(Lower(file_name))};
    const size_t dot = name.rfind('.');
    const size_t bar = name.rfind('_', dot);
    if (dot == std::string::npos || bar == std::string::npos || bar + 1 == dot ||
        !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(bar + 1),
                     name.begin() + static_cast<std::ptrdiff_t>(dot),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return {};
    }
    return name.substr(0, bar);
}

struct Song {
    std::string path; ///< In the RomFS, '/' between folders
    u64 size{};
    [[nodiscard]] std::string FileName() const {
        return path.substr(path.find_last_of('/') + 1);
    }
};

/// The game's songs: every IsSong file that isn't one numbered section of a piece (Morning_01,
/// Morning_02 ...), one per size (the layers of a layered piece, and the same song kept under
/// several names, are the same size), in name order.
std::vector<Song> FindSongs(const FileSys::VirtualDir& root,
                            std::unordered_map<std::string, FileSys::VirtualFile>& files) {
    std::vector<Song> found;
    std::function<void(const FileSys::VirtualDir&, const std::string&)> walk =
        [&](const FileSys::VirtualDir& dir, const std::string& prefix) {
            const auto dir_files = dir->GetFiles();
            std::unordered_map<std::string, int> sections;
            for (const auto& file : dir_files) {
                if (std::string section = SectionOf(file->GetName()); !section.empty()) {
                    ++sections[section];
                }
            }
            for (const auto& file : dir_files) {
                const auto section = sections.find(SectionOf(file->GetName()));
                if (section != sections.end() && section->second > 1) {
                    continue;
                }
                if (IsSong(file->GetName(), file->GetSize())) {
                    Song song{prefix + file->GetName(), file->GetSize()};
                    files[song.path] = file;
                    found.push_back(std::move(song));
                }
            }
            for (const auto& sub : dir->GetSubdirectories()) {
                if (Lower(sub->GetName()) != "voice") {
                    walk(sub, prefix + sub->GetName() + "/");
                }
            }
        };
    walk(root, "");
    std::sort(found.begin(), found.end(),
              [](const Song& a, const Song& b) { return a.path < b.path; });
    std::set<u64> sizes;
    std::erase_if(found, [&sizes](const Song& song) { return !sizes.insert(song.size).second; });
    return found;
}

std::filesystem::path SongFolder(u64 program_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "menu_music" /
           fmt::format("{:016X}", program_id);
}

/// songs.txt: the header, then "<size> <path>" per song. Nothing when it isn't there yet.
std::optional<std::vector<Song>> ReadSongList(u64 program_id) {
    std::ifstream file{SongFolder(program_id) / "songs.txt"};
    std::string line;
    if (!std::getline(file, line) || line != SONG_LIST_HEADER) {
        return std::nullopt;
    }
    std::vector<Song> listed;
    while (std::getline(file, line)) {
        const size_t space = line.find(' ');
        if (space == std::string::npos || space + 1 >= line.size()) {
            continue;
        }
        listed.push_back({line.substr(space + 1), std::strtoull(line.c_str(), nullptr, 10)});
    }
    return listed;
}

void WriteSongList(u64 program_id, const std::vector<Song>& listed) {
    std::error_code ec;
    std::filesystem::create_directories(SongFolder(program_id), ec);
    std::ofstream file{SongFolder(program_id) / "songs.txt", std::ios::trunc};
    file << SONG_LIST_HEADER << '\n';
    for (const Song& song : listed) {
        file << song.size << ' ' << song.path << '\n';
    }
}

std::filesystem::path KeptSong(u64 program_id, const Song& song) {
    return SongFolder(program_id) / song.FileName();
}

/// Keeps a song's .bwav, and drops the least recently played beyond SONGS_KEPT.
void KeepSong(u64 program_id, const Song& song, const std::vector<u8>& bytes) {
    {
        std::ofstream file{KeptSong(program_id, song), std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> kept;
    for (const auto& entry : std::filesystem::directory_iterator(SongFolder(program_id), ec)) {
        if (IsAudioName(Lower(entry.path().filename().string()))) {
            kept.emplace_back(entry.last_write_time(ec), entry.path());
        }
    }
    if (kept.size() <= SONGS_KEPT) {
        return;
    }
    std::sort(kept.begin(), kept.end());
    for (size_t i = 0; i + SONGS_KEPT < kept.size(); ++i) {
        std::filesystem::remove(kept[i].second, ec);
    }
}

std::optional<std::vector<u8>> ReadKeptSong(u64 program_id, const Song& song) {
    const auto path = KeptSong(program_id, song);
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<u8> bytes(song.size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::ifstream::traits_type::eof()) {
        return std::nullopt; // not the same file (an update changed it, or cut short)
    }
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    return bytes;
}

/// The picked game's songs, kept between picks while it stays picked.
struct SongPool {
    u64 program_id{};
    std::vector<Song> songs;
    bool listed = false;
    OpenGame game; ///< Opened this session (else picks come from the kept songs first)
    std::unordered_map<std::string, FileSys::VirtualFile> files;
    std::deque<std::string> recent; ///< The last songs played, newest last
    std::mt19937 rng{std::random_device{}()};
};

std::mutex song_pool_mutex;
SongPool song_pool;

std::shared_ptr<const Track> LoadTitle(const std::string& game_path, u64 program_id) {
    // Check for custom user-provided theme file first (.bwav or .wav)
    const auto music_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "menu_music";
    const auto custom_bwav = music_dir / fmt::format("{:016X}.bwav", program_id);
    const auto custom_wav = music_dir / fmt::format("{:016X}.wav", program_id);
    std::error_code ec;
#ifdef _WIN32
    const auto app_dir = Common::FS::GetExeDirectory();
#else
    // An AppImage runs from a read-only mount: the folder of the .AppImage file counts.
    const char* const appimage = std::getenv("APPIMAGE");
    const auto app_dir = std::filesystem::path{appimage ? appimage : ""}.parent_path();
#endif
    const auto pokopia_theme = app_dir / "pokopia_theme.wav";

    // A theme file added after the track was cached replaces the cached track.
    const auto newer_than_cache = [&](const std::filesystem::path& file) {
        const auto added = std::filesystem::last_write_time(file, ec);
        if (ec) {
            return false;
        }
        const auto cached = std::filesystem::last_write_time(CachePath(program_id), ec);
        return ec || added > cached;
    };
    const bool user_file = newer_than_cache(custom_bwav) || newer_than_cache(custom_wav) ||
                           (program_id == 0x0400A3A01EE4C000 && newer_than_cache(pokopia_theme));
    if (!user_file) {
        if (auto cached = ReadCache(program_id)) {
            return cached;
        }
    }
    auto track = std::make_shared<Track>();
    track->program_id = program_id;

    // Check specific OST file sources for Pokémon Pokopia
    if (program_id == 0x0400A3A01EE4C000) {
        const std::vector<std::filesystem::path> pokopia_candidates = {
            custom_wav,
            pokopia_theme,
            std::filesystem::current_path(ec) / "pokopia_theme.wav",
        };
        for (const auto& candidate : pokopia_candidates) {
            if (std::filesystem::exists(candidate, ec)) {
                if (candidate.extension() == ".wav") {
                    std::ifstream wf{candidate, std::ios::binary};
                    std::vector<u8> wav_bytes((std::istreambuf_iterator<char>(wf)), std::istreambuf_iterator<char>());
                    track->samples = DecodeWav(wav_bytes);
                    if (!track->samples.empty()) {
                        track->name = "Title Screen Theme - Pokémon Pokopia [OST]";
                        WriteCache(*track);
                        LOG_INFO(Frontend, "Menu music for {:016X}: loaded Pokopia OST ({:.0f} s)",
                                 program_id, track->samples.size() / 2.0 / RATE);
                        return track;
                    }
                } else if (candidate.extension() == ".mp3") {
                    // Try converting via ffmpeg to user's menu_music folder
                    const auto target_wav = music_dir / fmt::format("{:016X}.wav", program_id);
                    const std::string cmd = fmt::format("ffmpeg -y -i \"{}\" -ar 48000 -ac 2 \"{}\"",
                                                        candidate.string(), target_wav.string());
                    std::system(cmd.c_str());
                    if (std::filesystem::exists(target_wav, ec)) {
                        std::ifstream wf{target_wav, std::ios::binary};
                        std::vector<u8> wav_bytes((std::istreambuf_iterator<char>(wf)), std::istreambuf_iterator<char>());
                        track->samples = DecodeWav(wav_bytes);
                        if (!track->samples.empty()) {
                            track->name = "Title Screen Theme - Pokémon Pokopia [OST]";
                            WriteCache(*track);
                            return track;
                        }
                    }
                }
            }
        }
        // The copy built into the program.
        if (QFile bundled{QStringLiteral(":/music/pokopia_theme.wav")};
            bundled.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = bundled.readAll();
            track->samples = DecodeWav(std::vector<u8>(bytes.begin(), bytes.end()));
            if (!track->samples.empty()) {
                track->name = "Title Screen Theme - Pokémon Pokopia [OST]";
                WriteCache(*track);
                LOG_INFO(Frontend, "Menu music for {:016X}: built-in Pokopia theme ({:.0f} s)",
                         program_id, track->samples.size() / 2.0 / RATE);
                return track;
            }
        }
    }
    if (std::filesystem::exists(custom_bwav, ec)) {
        std::ifstream bf{custom_bwav, std::ios::binary};
        std::vector<u8> bwav_bytes((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
        track->samples = DecodeBwav(bwav_bytes, MAX_SECONDS);
        if (!track->samples.empty()) {
            track->name = "custom_theme.bwav";
            WriteCache(*track);
            return track;
        }
    }
    if (std::filesystem::exists(custom_wav, ec)) {
        std::ifstream wf{custom_wav, std::ios::binary};
        std::vector<u8> wav_bytes((std::istreambuf_iterator<char>(wf)), std::istreambuf_iterator<char>());
        track->samples = DecodeWav(wav_bytes);
        if (!track->samples.empty()) {
            track->name = "custom_theme.wav";
            WriteCache(*track);
            return track;
        }
    }

    try {
        const OpenGame game = Open(game_path);
        if (const auto file = game.root ? FindTrack(game.root) : nullptr) {
            track->name = file->GetName();
            track->samples = DecodeAudio(file->GetName(), file->ReadAllBytes(), MAX_SECONDS);
            LOG_INFO(Frontend, "Menu music for {:016X}: {} ({:.0f} s)", program_id,
                     file->GetName(), track->samples.size() / 2.0 / RATE);
        } else {
            LOG_INFO(Frontend, "Menu music for {:016X}: no title track found", program_id);
        }
    } catch (const std::exception& error) {
        LOG_WARNING(Frontend, "Menu music for {:016X}: {}", program_id, error.what());
    }

    // Built-in theme fallback for Pokémon Pokopia
    if (track->samples.empty() && program_id == 0x0400A3A01EE4C000) {
        track->name = "Pokopia_Island_Theme";
        track->samples = GeneratePokopiaTheme();
        LOG_INFO(Frontend, "Menu music for {:016X}: generated Pokémon Pokopia theme ({:.0f} s)",
                 program_id, track->samples.size() / 2.0 / RATE);
    }
    if (!track->samples.empty()) {
        WriteCache(*track);
    }
    return track;
}

std::shared_ptr<const Track> LoadRandom(const std::string& game_path, u64 program_id) {
    std::scoped_lock lock{song_pool_mutex};
    SongPool& pool_now = song_pool;
    if (pool_now.program_id != program_id) {
        pool_now = SongPool{};
        pool_now.program_id = program_id;
        if (auto listed = ReadSongList(program_id)) {
            pool_now.songs = std::move(*listed);
            pool_now.listed = true;
        }
    }
    const auto open_game = [&] {
        if (!pool_now.game.root) {
            pool_now.game = Open(game_path);
            pool_now.files.clear();
            if (pool_now.game.root) {
                std::vector<Song> found = FindSongs(pool_now.game.root, pool_now.files);
                if (!pool_now.listed || found.size() != pool_now.songs.size()) {
                    pool_now.songs = std::move(found);
                    WriteSongList(program_id, pool_now.songs);
                    std::string names;
                    for (const Song& song : pool_now.songs) {
                        names += (names.empty() ? "" : ", ") + song.FileName();
                    }
                    LOG_INFO(Frontend, "Menu music for {:016X}: {} songs: {}", program_id,
                             pool_now.songs.size(), names);
                }
                pool_now.listed = true;
            }
        }
        return pool_now.game.root != nullptr;
    };
    try {
        if (!pool_now.listed && !open_game()) {
            return LoadTitle(game_path, program_id);
        }
        for (int attempt = 0; attempt < 6 && !pool_now.songs.empty(); ++attempt) {
            // Not one of the last few (a third of the songs, at most 8).
            const size_t avoid = std::min<size_t>(8, pool_now.songs.size() / 3);
            while (pool_now.recent.size() > avoid) {
                pool_now.recent.pop_front();
            }
            std::vector<const Song*> fresh;
            std::vector<const Song*> fresh_kept;
            for (const Song& song : pool_now.songs) {
                if (std::find(pool_now.recent.begin(), pool_now.recent.end(), song.path) !=
                    pool_now.recent.end()) {
                    continue;
                }
                fresh.push_back(&song);
                std::error_code ec;
                if (std::filesystem::exists(KeptSong(program_id, song), ec)) {
                    fresh_kept.push_back(&song);
                }
            }
            if (fresh.empty()) {
                pool_now.recent.clear();
                continue;
            }
            // Until the game is opened this session, a kept song starts right away.
            const auto& from = (!pool_now.game.root && !fresh_kept.empty()) ? fresh_kept : fresh;
            const Song song = *from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(
                pool_now.rng)];
            std::optional<std::vector<u8>> bytes = ReadKeptSong(program_id, song);
            if (!bytes) {
                if (!open_game()) {
                    break;
                }
                const auto file = pool_now.files.find(song.path);
                if (file == pool_now.files.end()) {
                    std::erase_if(pool_now.songs,
                                  [&song](const Song& s) { return s.path == song.path; });
                    continue;
                }
                bytes = file->second->ReadAllBytes();
                KeepSong(program_id, song, *bytes);
            }
            auto track = std::make_shared<Track>();
            track->program_id = program_id;
            track->name = song.FileName();
            track->samples = DecodeAudio(song.FileName(), *bytes, MAX_SECONDS);
            const double seconds = track->samples.size() / 2.0 / RATE;
            if (seconds < SONG_MIN_SECONDS) {
                // Too short to be a song (or it didn't decode): not picked again.
                std::erase_if(pool_now.songs,
                              [&song](const Song& s) { return s.path == song.path; });
                continue;
            }
            // A short loop plays twice before the next song.
            track->plays = seconds < 75.0 ? 2 : 1;
            pool_now.recent.push_back(song.path);
            LOG_INFO(Frontend, "Menu music for {:016X}: {} ({:.0f} s, of {} songs)", program_id,
                     track->name, seconds, pool_now.songs.size());
            return track;
        }
    } catch (const std::exception& error) {
        LOG_WARNING(Frontend, "Menu music for {:016X}: {}", program_id, error.what());
    }
    return LoadTitle(game_path, program_id); // no songs found: the title theme
}

} // Anonymous namespace

std::vector<s16> DecodeBwav(const std::vector<u8>& bwav, u32 max_seconds) {
    std::vector<s16> result;
    u16 bom = 0, channel_count = 0;
    if (bwav.size() < 16 || std::memcmp(bwav.data(), "BWAV", 4) != 0 || !Read(bwav, 4, bom) ||
        bom != 0xFEFF || !Read(bwav, 14, channel_count) || channel_count == 0 ||
        channel_count > 16) {
        return result;
    }
    const size_t max_samples = size_t{RATE} * max_seconds;
    std::vector<std::vector<float>> decoded;
    std::vector<u16> pans;
    for (size_t i = 0; i < channel_count; ++i) {
        const size_t info = 16 + i * 0x4C;
        Channel channel{};
        if (!Read(bwav, info, channel.codec) || !Read(bwav, info + 2, channel.pan) ||
            !Read(bwav, info + 4, channel.rate) || !Read(bwav, info + 12, channel.samples) ||
            !Read(bwav, info + 16, channel.coefficients) || !Read(bwav, info + 52, channel.offset) ||
            !Read(bwav, info + 0x46, channel.history1) ||
            !Read(bwav, info + 0x48, channel.history2)) {
            return result;
        }
        if (channel.rate == 0) {
            return result;
        }
        std::vector<float> ch_samples;
        const size_t ch_max = static_cast<size_t>(channel.rate) * max_seconds;
        switch (channel.codec) {
        case 0:
            ch_samples = DecodePcm16(bwav, channel, ch_max);
            break;
        case 1:
            ch_samples = DecodeAdpcm(bwav, channel, ch_max);
            break;
        case 2:
            ch_samples = DecodeOpus(bwav, channel, max_samples);
            break;
        default:
            return result;
        }
        if (channel.codec != 2 && channel.rate != RATE) {
            ch_samples = ResampleAudio(ch_samples, channel.rate, RATE);
        }
        decoded.push_back(std::move(ch_samples));
        pans.push_back(channel.pan);
    }
    return MixToStereo(decoded, pans);
}

std::shared_ptr<const Track> Load(const std::string& game_path, u64 program_id, Choice choice) {
    return choice == Choice::Random ? LoadRandom(game_path, program_id)
                                    : LoadTitle(game_path, program_id);
}

} // namespace GameMusic
